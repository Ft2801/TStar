// =============================================================================
// JSUI.cpp
//
// Implementation of the TStar scriptable UI framework.
//
// Threading model:
//   - Script objects (JSDialog, JSLabel, …) live on the script worker thread.
//   - All Qt widgets live on the main GUI thread.
//   - Property writes dispatch to the GUI thread via:
//       Qt::QueuedConnection          — async, fire-and-forget (property sets)
//       Qt::BlockingQueuedConnection  — sync, caller blocks  (init / execute)
//   - Signal callbacks (onClick, onValueChanged, …) are invoked on the script
//     thread by re-posting via QMetaObject::invokeMethod with Qt::QueuedConnection
//     targeting the proxy object itself.
// =============================================================================

#include "JSUI.h"
#include "MainWindow.h"
#include "widgets/CustomMdiSubWindow.h"

#include <QWidget>
#include <QBoxLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QFrame>
#include <QScrollArea>
#include <QApplication>
#include <QThread>
#include <QDebug>

namespace Scripting {

// =============================================================================
// Helpers
// =============================================================================

namespace {

/** @brief Returns true if the caller is running on the GUI (main) thread. */
inline bool isGuiThread()
{
    return QThread::currentThread() == QCoreApplication::instance()->thread();
}

/**
 * @brief Invoke a callable on the GUI thread.
 *
 * If already on the GUI thread, executes immediately (direct call).
 * Otherwise posts via BlockingQueuedConnection so the caller waits for
 * completion — safe for initialization sequences.
 */
template <typename Fn>
void runOnGuiThreadSync(Fn&& fn)
{
    if (isGuiThread()) {
        fn();
    } else {
        // We need a QObject receiver living on the GUI thread to post to.
        // QCoreApplication::instance() is always on the main thread.
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            std::forward<Fn>(fn),
            Qt::BlockingQueuedConnection
        );
    }
}

/**
 * @brief Post a callable to the GUI thread without waiting (fire-and-forget).
 */
template <typename Fn>
void postToGuiThread(Fn&& fn)
{
    if (isGuiThread()) {
        fn();
    } else {
        QMetaObject::invokeMethod(
            QCoreApplication::instance(),
            std::forward<Fn>(fn),
            Qt::QueuedConnection
        );
    }
}

} // anonymous namespace

// =============================================================================
// JSUIControl
// =============================================================================

JSUIControl::JSUIControl(QObject* parent)
    : QObject(parent)
{
}

QWidget* JSUIControl::widget() const
{
    return m_widget.data();
}

void JSUIControl::setEnabled(bool enabled)
{
    m_enabled = enabled;
    if (m_widget) {
        postToGuiThread([w = m_widget, enabled]() {
            if (w) w->setEnabled(enabled);
        });
    }
}

void JSUIControl::setVisible(bool visible)
{
    m_visible = visible;
    if (m_widget) {
        postToGuiThread([w = m_widget, visible]() {
            if (w) w->setVisible(visible);
        });
    }
}

void JSUIControl::setMinWidth(int width)
{
    m_minWidth = width;
    if (m_widget) {
        postToGuiThread([w = m_widget, width, mh = m_minHeight]() {
            if (w) w->setMinimumSize(width, mh);
        });
    }
}

void JSUIControl::setMinHeight(int height)
{
    m_minHeight = height;
    if (m_widget) {
        postToGuiThread([w = m_widget, mw = m_minWidth, height]() {
            if (w) w->setMinimumSize(mw, height);
        });
    }
}

void JSUIControl::setMaxWidth(int width)
{
    m_maxWidth = width;
    if (m_widget) {
        postToGuiThread([w = m_widget, width, mh = m_maxHeight]() {
            if (w) w->setMaximumSize(width, mh);
        });
    }
}

void JSUIControl::setMaxHeight(int height)
{
    m_maxHeight = height;
    if (m_widget) {
        postToGuiThread([w = m_widget, mw = m_maxWidth, height]() {
            if (w) w->setMaximumSize(mw, height);
        });
    }
}

void JSUIControl::setToolTip(const QString& tip)
{
    m_toolTip = tip;
    if (m_widget) {
        postToGuiThread([w = m_widget, tip]() {
            if (w) w->setToolTip(tip);
        });
    }
}

// =============================================================================
// JSLayout
// =============================================================================

JSLayout::JSLayout(QObject* parent)
    : JSUIControl(parent)
{
}

void JSLayout::initLayout(Qt::Orientation orientation)
{
    runOnGuiThreadSync([this, orientation]() {
        // Create a container widget that owns the layout
        auto* container = new QWidget();
        container->setContentsMargins(m_margin, m_margin, m_margin, m_margin);

        QBoxLayout* layout = (orientation == Qt::Vertical)
            ? static_cast<QBoxLayout*>(new QVBoxLayout(container))
            : static_cast<QBoxLayout*>(new QHBoxLayout(container));

        layout->setContentsMargins(m_margin, m_margin, m_margin, m_margin);
        layout->setSpacing(m_spacing);

        m_widget = container;
        m_layout = layout;
    });
}

QBoxLayout* JSLayout::boxLayout() const
{
    return qobject_cast<QBoxLayout*>(m_layout.data());
}

void JSLayout::setSpacing(int px)
{
    m_spacing = px;
    if (m_layout) {
        postToGuiThread([l = m_layout, px]() {
            if (auto* bl = qobject_cast<QBoxLayout*>(l.data()))
                bl->setSpacing(px);
        });
    }
}

void JSLayout::setMargin(int px)
{
    m_margin = px;
    if (m_layout) {
        postToGuiThread([l = m_layout, px]() {
            if (l) l->setContentsMargins(px, px, px, px);
        });
    }
}

void JSLayout::add(QObject* control, int stretch)
{
    if (!control) {
        qWarning() << "JSLayout::add() called with null control";
        return;
    }

    auto* jsControl = qobject_cast<JSUIControl*>(control);
    if (!jsControl) {
        qWarning() << "JSLayout::add() called with non-JSUIControl object";
        return;
    }

    // Keep a reference so the child isn't garbage-collected by JS/QObject
    m_children.append(jsControl);

    // Reparent the proxy so its lifetime is tied to this layout's lifetime
    if (jsControl->parent() == nullptr || jsControl->parent() == this->parent()) {
        jsControl->setParent(this);
    }

    // For nested layouts, the child's widget IS the layout's container widget.
    // For leaf controls, it's the actual widget.
    QPointer<QWidget> childWidget = jsControl->widget();

    if (childWidget) {
        postToGuiThread([l = m_layout, childWidget, stretch]() {
            auto* bl = qobject_cast<QBoxLayout*>(l.data());
            if (bl && childWidget) {
                bl->addWidget(childWidget, stretch);
            }
        });
    } else {
        qWarning() << "JSLayout::add() — child control has no widget yet:"
                   << control->metaObject()->className();
    }
}

void JSLayout::addStretch(int stretch)
{
    postToGuiThread([l = m_layout, stretch]() {
        auto* bl = qobject_cast<QBoxLayout*>(l.data());
        if (bl) bl->addStretch(stretch);
    });
}

void JSLayout::addSpacing(int pixels)
{
    postToGuiThread([l = m_layout, pixels]() {
        auto* bl = qobject_cast<QBoxLayout*>(l.data());
        if (bl) bl->addSpacing(pixels);
    });
}

// =============================================================================
// JSVerticalSizer
// =============================================================================

JSVerticalSizer::JSVerticalSizer(QObject* parent)
    : JSLayout(parent)
{
    initLayout(Qt::Vertical);
}

// =============================================================================
// JSHorizontalSizer
// =============================================================================

JSHorizontalSizer::JSHorizontalSizer(QObject* parent)
    : JSLayout(parent)
{
    initLayout(Qt::Horizontal);
}

// =============================================================================
// JSLabel
// =============================================================================

JSLabel::JSLabel(QObject* parent)
    : JSUIControl(parent)
{
    runOnGuiThreadSync([this]() {
        auto* lbl = new QLabel();
        lbl->setAlignment(static_cast<Qt::Alignment>(m_alignment));
        lbl->setWordWrap(m_wordWrap);
        m_widget = lbl;
    });
}

void JSLabel::setText(const QString& text)
{
    m_text = text;
    postToGuiThread([w = m_widget, text]() {
        if (auto* lbl = qobject_cast<QLabel*>(w.data()))
            lbl->setText(text);
    });
}

void JSLabel::setWordWrap(bool wrap)
{
    m_wordWrap = wrap;
    postToGuiThread([w = m_widget, wrap]() {
        if (auto* lbl = qobject_cast<QLabel*>(w.data()))
            lbl->setWordWrap(wrap);
    });
}

void JSLabel::setAlignment(int align)
{
    m_alignment = align;
    postToGuiThread([w = m_widget, align]() {
        if (auto* lbl = qobject_cast<QLabel*>(w.data()))
            lbl->setAlignment(static_cast<Qt::Alignment>(align));
    });
}

// =============================================================================
// JSPushButton
// =============================================================================

JSPushButton::JSPushButton(QObject* parent)
    : JSUIControl(parent)
{
    runOnGuiThreadSync([this]() {
        auto* btn = new QPushButton();

        // The signal is connected in the GUI thread.
        // The slot (handleClicked) lives on the proxy (script thread).
        // Qt::QueuedConnection ensures it crosses the thread boundary safely.
        connect(btn, &QPushButton::clicked,
                this, &JSPushButton::handleClicked,
                Qt::QueuedConnection);

        m_widget = btn;
    });
}

void JSPushButton::setText(const QString& text)
{
    m_text = text;
    postToGuiThread([w = m_widget, text]() {
        if (auto* btn = qobject_cast<QPushButton*>(w.data()))
            btn->setText(text);
    });
}

void JSPushButton::setCheckable(bool checkable)
{
    m_checkable = checkable;
    postToGuiThread([w = m_widget, checkable]() {
        if (auto* btn = qobject_cast<QPushButton*>(w.data()))
            btn->setCheckable(checkable);
    });
}

void JSPushButton::setChecked(bool checked)
{
    m_checked = checked;
    postToGuiThread([w = m_widget, checked]() {
        if (auto* btn = qobject_cast<QPushButton*>(w.data()))
            btn->setChecked(checked);
    });
}

void JSPushButton::setOnClick(const QJSValue& callback)
{
    m_onClick = callback;
}

void JSPushButton::handleClicked(bool checked)
{
    // This slot is called on the script thread (QueuedConnection from GUI).
    m_checked = checked;

    if (m_onClick.isCallable()) {
        QJSValue result = m_onClick.call();
        if (result.isError()) {
            qWarning() << "JSPushButton onClick error:"
                       << result.property("lineNumber").toInt()
                       << result.toString();
        }
    }
}

// =============================================================================
// JSSlider
// =============================================================================

JSSlider::JSSlider(QObject* parent)
    : JSUIControl(parent)
{
    runOnGuiThreadSync([this]() {
        auto* sl = new QSlider(Qt::Horizontal);
        sl->setMinimum(m_min);
        sl->setMaximum(m_max);
        sl->setValue(m_value);
        sl->setSingleStep(m_step);

        connect(sl, &QSlider::valueChanged,
                this, &JSSlider::handleValueChanged,
                Qt::QueuedConnection);

        connect(sl, &QSlider::sliderPressed,
                this, &JSSlider::handleSliderPressed,
                Qt::QueuedConnection);

        connect(sl, &QSlider::sliderReleased,
                this, &JSSlider::handleSliderReleased,
                Qt::QueuedConnection);

        m_widget = sl;
    });
}

void JSSlider::setMin(int v)
{
    m_min = v;
    postToGuiThread([w = m_widget, v]() {
        if (auto* sl = qobject_cast<QSlider*>(w.data()))
            sl->setMinimum(v);
    });
}

void JSSlider::setMax(int v)
{
    m_max = v;
    postToGuiThread([w = m_widget, v]() {
        if (auto* sl = qobject_cast<QSlider*>(w.data()))
            sl->setMaximum(v);
    });
}

void JSSlider::setValue(int v)
{
    m_value = v;
    postToGuiThread([w = m_widget, v]() {
        if (auto* sl = qobject_cast<QSlider*>(w.data())) {
            sl->blockSignals(true);
            sl->setValue(v);
            sl->blockSignals(false);
        }
    });
}

void JSSlider::setStep(int v)
{
    m_step = v;
    postToGuiThread([w = m_widget, v]() {
        if (auto* sl = qobject_cast<QSlider*>(w.data()))
            sl->setSingleStep(v);
    });
}

void JSSlider::setOnValueChanged(const QJSValue& callback)
{
    m_onValueChanged = callback;
}

void JSSlider::setOnSliderPressed(const QJSValue& callback)
{
    m_onSliderPressed = callback;
}

void JSSlider::setOnSliderReleased(const QJSValue& callback)
{
    m_onSliderReleased = callback;
}

void JSSlider::handleValueChanged(int newValue)
{
    // Sync cached state from the GUI thread back to the proxy
    m_value = newValue;

    if (m_onValueChanged.isCallable()) {
        QJSValue result = m_onValueChanged.call({newValue});
        if (result.isError()) {
            qWarning() << "JSSlider onValueChanged error:"
                       << result.property("lineNumber").toInt()
                       << result.toString();
        }
    }
}

void JSSlider::handleSliderPressed()
{
    if (m_onSliderPressed.isCallable()) {
        m_onSliderPressed.call();
    }
}

void JSSlider::handleSliderReleased()
{
    if (m_onSliderReleased.isCallable()) {
        m_onSliderReleased.call();
    }
}

// =============================================================================
// JSSpinBox
// =============================================================================

JSSpinBox::JSSpinBox(QObject* parent)
    : JSUIControl(parent)
{
    runOnGuiThreadSync([this]() {
        auto* sp = new QDoubleSpinBox();
        sp->setMinimum(m_min);
        sp->setMaximum(m_max);
        sp->setValue(m_value);
        sp->setSingleStep(m_step);
        sp->setDecimals(m_precision);
        if (!m_suffix.isEmpty()) sp->setSuffix(m_suffix);

        connect(sp, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                this, &JSSpinBox::handleValueChanged,
                Qt::QueuedConnection);

        m_widget = sp;
    });
}

void JSSpinBox::setMin(double v)
{
    m_min = v;
    postToGuiThread([w = m_widget, v]() {
        if (auto* sp = qobject_cast<QDoubleSpinBox*>(w.data()))
            sp->setMinimum(v);
    });
}

void JSSpinBox::setMax(double v)
{
    m_max = v;
    postToGuiThread([w = m_widget, v]() {
        if (auto* sp = qobject_cast<QDoubleSpinBox*>(w.data()))
            sp->setMaximum(v);
    });
}

void JSSpinBox::setValue(double v)
{
    m_value = v;
    postToGuiThread([w = m_widget, v]() {
        if (auto* sp = qobject_cast<QDoubleSpinBox*>(w.data())) {
            sp->blockSignals(true);
            sp->setValue(v);
            sp->blockSignals(false);
        }
    });
}

void JSSpinBox::setStep(double v)
{
    m_step = v;
    postToGuiThread([w = m_widget, v]() {
        if (auto* sp = qobject_cast<QDoubleSpinBox*>(w.data()))
            sp->setSingleStep(v);
    });
}

void JSSpinBox::setPrecision(int p)
{
    m_precision = p;
    postToGuiThread([w = m_widget, p]() {
        if (auto* sp = qobject_cast<QDoubleSpinBox*>(w.data()))
            sp->setDecimals(p);
    });
}

void JSSpinBox::setSuffix(const QString& s)
{
    m_suffix = s;
    postToGuiThread([w = m_widget, s]() {
        if (auto* sp = qobject_cast<QDoubleSpinBox*>(w.data()))
            sp->setSuffix(s);
    });
}

void JSSpinBox::setOnValueChanged(const QJSValue& callback)
{
    m_onValueChanged = callback;
}

void JSSpinBox::handleValueChanged(double newValue)
{
    m_value = newValue;

    if (m_onValueChanged.isCallable()) {
        QJSValue result = m_onValueChanged.call({newValue});
        if (result.isError()) {
            qWarning() << "JSSpinBox onValueChanged error:"
                       << result.property("lineNumber").toInt()
                       << result.toString();
        }
    }
}

// =============================================================================
// JSCheckBox
// =============================================================================

JSCheckBox::JSCheckBox(QObject* parent)
    : JSUIControl(parent)
{
    runOnGuiThreadSync([this]() {
        auto* cb = new QCheckBox();
        cb->setChecked(m_checked);

        connect(cb, &QCheckBox::toggled,
                this, &JSCheckBox::handleToggled,
                Qt::QueuedConnection);

        m_widget = cb;
    });
}

void JSCheckBox::setText(const QString& text)
{
    m_text = text;
    postToGuiThread([w = m_widget, text]() {
        if (auto* cb = qobject_cast<QCheckBox*>(w.data()))
            cb->setText(text);
    });
}

void JSCheckBox::setChecked(bool checked)
{
    m_checked = checked;
    postToGuiThread([w = m_widget, checked]() {
        if (auto* cb = qobject_cast<QCheckBox*>(w.data()))
            cb->setChecked(checked);
    });
}

void JSCheckBox::setOnToggled(const QJSValue& callback)
{
    m_onToggled = callback;
}

void JSCheckBox::handleToggled(bool checked)
{
    m_checked = checked;

    if (m_onToggled.isCallable()) {
        QJSValue result = m_onToggled.call({checked});
        if (result.isError()) {
            qWarning() << "JSCheckBox onToggled error:"
                       << result.property("lineNumber").toInt()
                       << result.toString();
        }
    }
}

// =============================================================================
// JSComboBox
// =============================================================================

JSComboBox::JSComboBox(QObject* parent)
    : JSUIControl(parent)
{
    runOnGuiThreadSync([this]() {
        auto* cb = new QComboBox();

        connect(cb, QOverload<int>::of(&QComboBox::currentIndexChanged),
                this, &JSComboBox::handleIndexChanged,
                Qt::QueuedConnection);

        m_widget = cb;
    });
}

QString JSComboBox::currentText() const
{
    if (m_selectedIndex >= 0 && m_selectedIndex < m_items.size())
        return m_items.at(m_selectedIndex);
    return QString();
}

void JSComboBox::setSelectedIndex(int index)
{
    m_selectedIndex = index;
    postToGuiThread([w = m_widget, index]() {
        if (auto* cb = qobject_cast<QComboBox*>(w.data()))
            cb->setCurrentIndex(index);
    });
}

void JSComboBox::setOnIndexChanged(const QJSValue& callback)
{
    m_onIndexChanged = callback;
}

void JSComboBox::addItem(const QString& text)
{
    m_items.append(text);
    postToGuiThread([w = m_widget, text]() {
        if (auto* cb = qobject_cast<QComboBox*>(w.data()))
            cb->addItem(text);
    });
}

void JSComboBox::clearItems()
{
    m_items.clear();
    m_selectedIndex = 0;
    postToGuiThread([w = m_widget]() {
        if (auto* cb = qobject_cast<QComboBox*>(w.data()))
            cb->clear();
    });
}

void JSComboBox::handleIndexChanged(int newIndex)
{
    m_selectedIndex = newIndex;

    if (m_onIndexChanged.isCallable()) {
        QJSValue result = m_onIndexChanged.call({newIndex});
        if (result.isError()) {
            qWarning() << "JSComboBox onIndexChanged error:"
                       << result.property("lineNumber").toInt()
                       << result.toString();
        }
    }
}

// =============================================================================
// JSDialog
// =============================================================================

JSDialog::JSDialog(MainWindow* mainWindow, QObject* parent)
    : JSUIControl(parent)
    , m_mainWindow(mainWindow)
{
    // The actual subwindow is NOT created here — we defer until execute()
    // so that all properties (title, size, sizer) can be set first.
}

JSDialog::~JSDialog()
{
    // If the script object is destroyed while the dialog is still open
    // (e.g. engine teardown), close it cleanly.
    if (m_subWindow) {
        tearDownOnGuiThread();
    }
}

void JSDialog::setWindowTitle(const QString& title)
{
    m_windowTitle = title;
    if (m_subWindow) {
        postToGuiThread([sub = m_subWindow, title]() {
            if (sub) sub->setSubWindowTitle(title);
        });
    }
}

void JSDialog::setDialogWidth(int w)
{
    m_dialogWidth = w;
}

void JSDialog::setDialogHeight(int h)
{
    m_dialogHeight = h;
}

void JSDialog::setSizer(QObject* sizer)
{
    auto* layout = qobject_cast<JSLayout*>(sizer);
    if (!layout) {
        qWarning() << "JSDialog::setSizer() — argument is not a JSLayout";
        return;
    }
    m_sizer = layout;
    // Reparent so the sizer survives until the dialog is destroyed
    m_sizer->setParent(this);
}

int JSDialog::execute()
{
    m_result = 0;

    // Build the subwindow synchronously on the GUI thread
    runOnGuiThreadSync([this]() { buildOnGuiThread(); });

    if (!m_subWindow) {
        qWarning() << "JSDialog::execute() — failed to create subwindow";
        return 0;
    }

    // Show the window (posted asynchronously — it just needs to happen)
    postToGuiThread([sub = m_subWindow]() {
        if (sub) {
            sub->show();
            sub->raise();
            sub->activateWindow();
        }
    });

    // Run a local event loop on the script thread.
    // The loop is quit by ok(), cancel(), or onSubWindowClosed().
    m_eventLoop = new QEventLoop(this);
    connect(this, &JSDialog::dialogClosed, m_eventLoop, &QEventLoop::quit,
            Qt::QueuedConnection);
    m_eventLoop->exec();
    delete m_eventLoop;
    m_eventLoop = nullptr;

    return m_result;
}

void JSDialog::ok()
{
    m_result = 1;
    tearDownOnGuiThread();
    emit dialogClosed(1);
}

void JSDialog::cancel()
{
    m_result = 0;
    tearDownOnGuiThread();
    emit dialogClosed(0);
}

void JSDialog::onSubWindowClosed()
{
    // Called when the user closes the window via the title-bar × button.
    // The subwindow is already closing, so we just quit the event loop.
    m_subWindow = nullptr;
    m_contentWidget = nullptr;
    if (m_eventLoop && m_eventLoop->isRunning()) {
        emit dialogClosed(m_result);
    }
}

void JSDialog::buildOnGuiThread()
{
    // Must be called from the GUI thread.
    Q_ASSERT(isGuiThread());

    if (!m_mainWindow) return;

    // ---- Create the content widget -----------------------------------------

    auto* content = new QWidget();
    content->setMinimumSize(m_dialogWidth, m_dialogHeight);

    // Apply the root sizer if one was provided
    if (m_sizer && m_sizer->widget()) {
        auto* outerLayout = new QVBoxLayout(content);
        outerLayout->setContentsMargins(8, 8, 8, 8);
        outerLayout->setSpacing(0);
        outerLayout->addWidget(m_sizer->widget());
    } else {
        // Empty placeholder layout
        auto* emptyLayout = new QVBoxLayout(content);
        emptyLayout->setContentsMargins(8, 8, 8, 8);
        Q_UNUSED(emptyLayout);
    }

    m_contentWidget = content;

    // ---- Wrap in a tool subwindow via MainWindow ----------------------------
    //
    // setupToolSubwindow creates a CustomMdiSubWindow with setToolWindow(true)
    // which hides the sidebar strips (NameStrip, LinkStrip, AdaptStrip) that
    // are only relevant for image viewer windows.

    CustomMdiSubWindow* sub =
        m_mainWindow->setupToolSubwindow(nullptr, content, m_windowTitle);

    if (!sub) {
        qWarning() << "JSDialog::buildOnGuiThread() — setupToolSubwindow returned null";
        delete content;
        m_contentWidget = nullptr;
        return;
    }

    sub->resize(m_dialogWidth + 24, m_dialogHeight + 48); // +padding for chrome

    m_subWindow = sub;
    m_widget    = content;

    // Connect the subwindow close event back to our slot.
    // CustomMdiSubWindow emits destroyed when it is closed/deleted.
    connect(sub, &QObject::destroyed,
            this, &JSDialog::onSubWindowClosed,
            Qt::QueuedConnection);

    // Center in the MDI area
    m_mainWindow->centerToolWindow(sub);
}

void JSDialog::tearDownOnGuiThread()
{
    if (!m_subWindow) return;

    QPointer<CustomMdiSubWindow> sub = m_subWindow;
    m_subWindow    = nullptr;
    m_contentWidget = nullptr;
    m_widget       = nullptr;

    postToGuiThread([sub]() {
        if (sub) {
            sub->setSkipCloseAnimation(true);
            sub->close();
        }
    });
}

} // namespace Scripting