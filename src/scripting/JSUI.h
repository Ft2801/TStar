// =============================================================================
// JSUI.h
//
// Scriptable UI framework for the TStar JavaScript scripting engine.
//
// Provides a PixInsight-style dialog system that allows scripts to create
// professional-grade tool dialogs using the native TStar MDI subwindow
// infrastructure and dark theme.
//
// Architecture: Proxy Pattern
//   Every JS-facing object (JSDialog, JSLabel, etc.) is a QObject proxy
//   living on the script's worker thread. The actual Qt widgets are created
//   and owned on the main GUI thread. All cross-thread interactions use
//   QMetaObject::invokeMethod with the appropriate connection type:
//     - Qt::BlockingQueuedConnection  for synchronous operations (init, execute)
//     - Qt::QueuedConnection          for fire-and-forget updates (property sets)
//
// Usage from JavaScript:
//   var d = new Dialog();
//   d.windowTitle = "My Script Tool";
//   var sizer = new VerticalSizer(d);
//   var lbl   = new Label(d);
//   lbl.text  = "Smoothing:";
//   sizer.add(lbl);
//   var sl = new Slider(d);
//   sl.min = 0; sl.max = 100; sl.value = 50;
//   sizer.add(sl);
//   d.setSizer(sizer);
//   d.execute();
// =============================================================================

#ifndef JSUI_H
#define JSUI_H

#include <QObject>
#include <QPointer>
#include <QJSValue>
#include <QVariant>
#include <QEventLoop>

// Forward declarations — avoids pulling in heavy headers into every TU
class QWidget;
class QLayout;
class QBoxLayout;
class QLabel;
class QPushButton;
class QSlider;
class QDoubleSpinBox;
class QCheckBox;
class QComboBox;
class CustomMdiSubWindow;
class MainWindow;

namespace Scripting {

// =============================================================================
// JSUIControl — Abstract base for all scriptable UI elements
//
// Holds a QPointer to the real Qt widget on the GUI thread and exposes the
// common properties (enabled, visible, sizing constraints, tooltip) shared
// by every control.
// =============================================================================

class JSUIControl : public QObject {
    Q_OBJECT

    // Common cross-thread properties
    Q_PROPERTY(bool    enabled   READ isEnabled  WRITE setEnabled )
    Q_PROPERTY(bool    visible   READ isVisible  WRITE setVisible )
    Q_PROPERTY(int     minWidth  READ minWidth   WRITE setMinWidth )
    Q_PROPERTY(int     minHeight READ minHeight  WRITE setMinHeight)
    Q_PROPERTY(int     maxWidth  READ maxWidth   WRITE setMaxWidth )
    Q_PROPERTY(int     maxHeight READ maxHeight  WRITE setMaxHeight)
    Q_PROPERTY(QString toolTip   READ toolTip    WRITE setToolTip  )

public:
    explicit JSUIControl(QObject* parent = nullptr);
    ~JSUIControl() override = default;

    // -- Accessors (thread-safe reads from cached state) ----------------------

    bool    isEnabled()  const { return m_enabled;   }
    bool    isVisible()  const { return m_visible;   }
    int     minWidth()   const { return m_minWidth;  }
    int     minHeight()  const { return m_minHeight; }
    int     maxWidth()   const { return m_maxWidth;  }
    int     maxHeight()  const { return m_maxHeight; }
    QString toolTip()    const { return m_toolTip;   }

    // -- Setters (dispatch to GUI thread via QueuedConnection) ----------------

    Q_INVOKABLE void setEnabled  (bool enabled);
    Q_INVOKABLE void setVisible  (bool visible);
    Q_INVOKABLE void setMinWidth (int w);
    Q_INVOKABLE void setMinHeight(int h);
    Q_INVOKABLE void setMaxWidth (int w);
    Q_INVOKABLE void setMaxHeight(int h);
    Q_INVOKABLE void setToolTip  (const QString& tip);

    // -- C++ internal API -----------------------------------------------------

    /** @brief The underlying Qt widget. Only valid on the GUI thread. */
    QWidget* widget() const;

protected:
    QPointer<QWidget> m_widget;   ///< Actual widget on the GUI thread.
    QPointer<QLayout> m_layout;   ///< If this control wraps a sizer.

private:
    // Cached state — written from any thread, applied to widget on GUI thread
    bool    m_enabled   = true;
    bool    m_visible   = true;
    int     m_minWidth  = 0;
    int     m_minHeight = 0;
    int     m_maxWidth  = 16777215;   // QWIDGETSIZE_MAX
    int     m_maxHeight = 16777215;
    QString m_toolTip;
};

// =============================================================================
// JSLayout — Base class for sizer containers (VerticalSizer / HorizontalSizer)
//
// Wraps a QBoxLayout. Controls and nested layouts are added via add().
// Manages a parallel list of child proxies to keep their lifetimes alive.
// =============================================================================

class JSLayout : public JSUIControl {
    Q_OBJECT
    Q_PROPERTY(int spacing READ spacing WRITE setSpacing)
    Q_PROPERTY(int margin  READ margin  WRITE setMargin )

public:
    explicit JSLayout(QObject* parent = nullptr);
    ~JSLayout() override = default;

    int spacing() const { return m_spacing; }
    int margin()  const { return m_margin;  }

    Q_INVOKABLE void setSpacing(int px);
    Q_INVOKABLE void setMargin (int px);

    /**
     * @brief Add a widget or nested layout to this sizer.
     * @param control  A JSUIControl or JSLayout proxy object.
     * @param stretch  Optional stretch factor for QBoxLayout (default 0).
     */
    Q_INVOKABLE void add(QObject* control, int stretch = 0);

    /** @brief Insert a flexible spacer that expands to fill available space. */
    Q_INVOKABLE void addStretch(int stretch = 1);

    /** @brief Insert a fixed-size gap of @p pixels. */
    Q_INVOKABLE void addSpacing(int pixels);

    // -- C++ internal API -----------------------------------------------------

    QBoxLayout* boxLayout() const;

protected:
    /** Called by subclasses to create the correct layout type on the GUI thread. */
    void initLayout(Qt::Orientation orientation);

private:
    QVector<QPointer<JSUIControl>> m_children;   ///< Keeps child proxies alive.
    int m_spacing = 6;
    int m_margin  = 0;
};

// =============================================================================
// JSVerticalSizer — Wraps QVBoxLayout
// =============================================================================

class JSVerticalSizer : public JSLayout {
    Q_OBJECT
public:
    explicit JSVerticalSizer(QObject* parent = nullptr);
    ~JSVerticalSizer() override = default;
};

// =============================================================================
// JSHorizontalSizer — Wraps QHBoxLayout
// =============================================================================

class JSHorizontalSizer : public JSLayout {
    Q_OBJECT
public:
    explicit JSHorizontalSizer(QObject* parent = nullptr);
    ~JSHorizontalSizer() override = default;
};

// =============================================================================
// JSLabel — Wraps QLabel
// =============================================================================

class JSLabel : public JSUIControl {
    Q_OBJECT
    Q_PROPERTY(QString text      READ text      WRITE setText     )
    Q_PROPERTY(bool    wordWrap  READ wordWrap  WRITE setWordWrap )
    Q_PROPERTY(int     alignment READ alignment WRITE setAlignment)

public:
    explicit JSLabel(QObject* parent = nullptr);
    ~JSLabel() override = default;

    QString text()      const { return m_text;      }
    bool    wordWrap()  const { return m_wordWrap;  }
    int     alignment() const { return m_alignment; }

    Q_INVOKABLE void setText     (const QString& text);
    Q_INVOKABLE void setWordWrap (bool wrap);
    Q_INVOKABLE void setAlignment(int align);   ///< Qt::Alignment flags cast to int

private:
    QString m_text;
    bool    m_wordWrap  = false;
    int     m_alignment = 0x0001 | 0x0080;   // Qt::AlignLeft | Qt::AlignVCenter
};

// =============================================================================
// JSPushButton — Wraps QPushButton
//
// The onClick callback is a QJSValue (JS function) stored here.
// It is invoked from a Qt signal connected inside the GUI thread worker.
// =============================================================================

class JSPushButton : public JSUIControl {
    Q_OBJECT
    Q_PROPERTY(QString   text      READ text      WRITE setText    )
    Q_PROPERTY(bool      checkable READ checkable WRITE setCheckable)
    Q_PROPERTY(bool      checked   READ checked   WRITE setChecked )
    Q_PROPERTY(QJSValue  onClick   READ onClick   WRITE setOnClick )

public:
    explicit JSPushButton(QObject* parent = nullptr);
    ~JSPushButton() override = default;

    QString  text()      const { return m_text;      }
    bool     checkable() const { return m_checkable; }
    bool     checked()   const { return m_checked;   }
    QJSValue onClick()   const { return m_onClick;   }

    Q_INVOKABLE void setText     (const QString& text);
    Q_INVOKABLE void setCheckable(bool checkable);
    Q_INVOKABLE void setChecked  (bool checked);
    Q_INVOKABLE void setOnClick  (const QJSValue& callback);

public slots:
    /** @brief Called from the QPushButton::clicked signal in the GUI thread. */
    void handleClicked(bool checked = false);

private:
    QString  m_text;
    bool     m_checkable = false;
    bool     m_checked   = false;
    QJSValue m_onClick;
};

// =============================================================================
// JSSlider — Wraps QSlider (horizontal, integer steps)
//
// onValueChanged is called on every slider movement, making it suitable for
// real-time preview updates that are lightweight (i.e. don't require a full
// buffer re-render on every tick).
// =============================================================================

class JSSlider : public JSUIControl {
    Q_OBJECT
    Q_PROPERTY(int      min            READ min            WRITE setMin           )
    Q_PROPERTY(int      max            READ max            WRITE setMax           )
    Q_PROPERTY(int      value          READ value          WRITE setValue         )
    Q_PROPERTY(int      step              READ step              WRITE setStep             )
    Q_PROPERTY(QJSValue onValueChanged    READ onValueChanged    WRITE setOnValueChanged   )
    Q_PROPERTY(QJSValue onSliderPressed   READ onSliderPressed   WRITE setOnSliderPressed  )
    Q_PROPERTY(QJSValue onSliderReleased  READ onSliderReleased  WRITE setOnSliderReleased )

public:
    explicit JSSlider(QObject* parent = nullptr);
    ~JSSlider() override = default;

    int      min()            const { return m_min;            }
    int      max()            const { return m_max;            }
    int      value()          const { return m_value;          }
    int      step()           const { return m_step;           }
    QJSValue onValueChanged() const { return m_onValueChanged; }

    Q_INVOKABLE void setMin              (int v);
    Q_INVOKABLE void setMax              (int v);
    Q_INVOKABLE void setValue            (int v);
    Q_INVOKABLE void setStep             (int v);
    Q_INVOKABLE void setOnValueChanged   (const QJSValue& callback);
    Q_INVOKABLE void setOnSliderPressed  (const QJSValue& callback);
    Q_INVOKABLE void setOnSliderReleased (const QJSValue& callback);

    QJSValue onSliderPressed()  const { return m_onSliderPressed; }
    QJSValue onSliderReleased() const { return m_onSliderReleased;}

public slots:
    void handleValueChanged (int newValue);
    void handleSliderPressed ();
    void handleSliderReleased();

private:
    int      m_min            = 0;
    int      m_max            = 100;
    int      m_value          = 0;
    int      m_step           = 1;
    QJSValue m_onValueChanged;
    QJSValue m_onSliderPressed;
    QJSValue m_onSliderReleased;
};

// =============================================================================
// JSSpinBox — Wraps QDoubleSpinBox
//
// Exposes a floating-point spin control.  Use precision to control the number
// of decimal places displayed in the widget.
// =============================================================================

class JSSpinBox : public JSUIControl {
    Q_OBJECT
    Q_PROPERTY(double   min            READ min            WRITE setMin           )
    Q_PROPERTY(double   max            READ max            WRITE setMax           )
    Q_PROPERTY(double   value          READ value          WRITE setValue         )
    Q_PROPERTY(double   step           READ step           WRITE setStep          )
    Q_PROPERTY(int      precision      READ precision      WRITE setPrecision     )
    Q_PROPERTY(QString  suffix         READ suffix         WRITE setSuffix        )
    Q_PROPERTY(QJSValue onValueChanged READ onValueChanged WRITE setOnValueChanged)

public:
    explicit JSSpinBox(QObject* parent = nullptr);
    ~JSSpinBox() override = default;

    double   min()            const { return m_min;            }
    double   max()            const { return m_max;            }
    double   value()          const { return m_value;          }
    double   step()           const { return m_step;           }
    int      precision()      const { return m_precision;      }
    QString  suffix()         const { return m_suffix;         }
    QJSValue onValueChanged() const { return m_onValueChanged; }

    Q_INVOKABLE void setMin           (double v);
    Q_INVOKABLE void setMax           (double v);
    Q_INVOKABLE void setValue         (double v);
    Q_INVOKABLE void setStep          (double v);
    Q_INVOKABLE void setPrecision     (int p);
    Q_INVOKABLE void setSuffix        (const QString& s);
    Q_INVOKABLE void setOnValueChanged(const QJSValue& callback);

public slots:
    void handleValueChanged(double newValue);

private:
    double   m_min            = 0.0;
    double   m_max            = 1.0;
    double   m_value          = 0.0;
    double   m_step           = 0.01;
    int      m_precision      = 2;
    QString  m_suffix;
    QJSValue m_onValueChanged;
};

// =============================================================================
// JSCheckBox — Wraps QCheckBox
// =============================================================================

class JSCheckBox : public JSUIControl {
    Q_OBJECT
    Q_PROPERTY(QString  text      READ text      WRITE setText     )
    Q_PROPERTY(bool     checked   READ checked   WRITE setChecked  )
    Q_PROPERTY(QJSValue onToggled READ onToggled WRITE setOnToggled)

public:
    explicit JSCheckBox(QObject* parent = nullptr);
    ~JSCheckBox() override = default;

    QString  text()      const { return m_text;      }
    bool     checked()   const { return m_checked;   }
    QJSValue onToggled() const { return m_onToggled; }

    Q_INVOKABLE void setText     (const QString& text);
    Q_INVOKABLE void setChecked  (bool checked);
    Q_INVOKABLE void setOnToggled(const QJSValue& callback);

public slots:
    void handleToggled(bool checked);

private:
    QString  m_text;
    bool     m_checked   = false;
    QJSValue m_onToggled;
};

// =============================================================================
// JSComboBox — Wraps QComboBox
// =============================================================================

class JSComboBox : public JSUIControl {
    Q_OBJECT
    Q_PROPERTY(int         selectedIndex   READ selectedIndex   WRITE setSelectedIndex  )
    Q_PROPERTY(QStringList items           READ items           CONSTANT                )
    Q_PROPERTY(QString     currentText     READ currentText     CONSTANT                )
    Q_PROPERTY(QJSValue    onIndexChanged  READ onIndexChanged  WRITE setOnIndexChanged )

public:
    explicit JSComboBox(QObject* parent = nullptr);
    ~JSComboBox() override = default;

    int         selectedIndex()  const { return m_selectedIndex;  }
    QStringList items()          const { return m_items;          }
    QString     currentText()    const;
    QJSValue    onIndexChanged() const { return m_onIndexChanged; }

    Q_INVOKABLE void setSelectedIndex (int index);
    Q_INVOKABLE void setOnIndexChanged(const QJSValue& callback);

    /** @brief Append a single item to the combo box. */
    Q_INVOKABLE void addItem(const QString& text);

    /** @brief Remove all items. */
    Q_INVOKABLE void clearItems();

public slots:
    void handleIndexChanged(int newIndex);

private:
    QStringList m_items;
    int         m_selectedIndex  = 0;
    QJSValue    m_onIndexChanged;
};

// =============================================================================
// JSDialog — The root container for a script-created tool window
//
// Wraps a CustomMdiSubWindow (via MainWindow::setupToolSubwindow) so that
// script dialogs inherit the native TStar dark theme and MDI behavior.
//
// The execute() method suspends the script thread via a local QEventLoop while
// the dialog remains open, exactly like QDialog::exec() but without blocking
// the GUI event loop.
// =============================================================================

class JSDialog : public JSUIControl {
    Q_OBJECT
    Q_PROPERTY(QString windowTitle READ windowTitle WRITE setWindowTitle)
    Q_PROPERTY(int     width       READ dialogWidth  WRITE setDialogWidth )
    Q_PROPERTY(int     height      READ dialogHeight WRITE setDialogHeight)

public:
    explicit JSDialog(MainWindow* mainWindow, QObject* parent = nullptr);
    ~JSDialog() override;

    QString windowTitle()  const { return m_windowTitle;  }
    int     dialogWidth()  const { return m_dialogWidth;  }
    int     dialogHeight() const { return m_dialogHeight; }

    Q_INVOKABLE void setWindowTitle (const QString& title);
    Q_INVOKABLE void setDialogWidth (int w);
    Q_INVOKABLE void setDialogHeight(int h);

    /**
     * @brief Assign the root layout sizer.
     *
     * Must be called before execute(). The sizer and all its children will
     * be placed as the dialog's content.
     */
    Q_INVOKABLE void setSizer(QObject* sizer);

    /**
     * @brief Show the dialog and block the script thread until it is closed.
     *
     * Internally creates the subwindow on the GUI thread, then enters a local
     * QEventLoop on the script thread. Returns when the user closes the
     * window or when ok() / cancel() is called from a button callback.
     *
     * @return 1 if closed via ok(), 0 if closed via cancel() or the title bar.
     */
    Q_INVOKABLE int execute();

    /**
     * @brief Close the dialog with a success result (return value 1).
     * Call from an onClick callback to accept the dialog.
     */
    Q_INVOKABLE void ok();

    /**
     * @brief Close the dialog with a failure result (return value 0).
     * Call from an onClick callback to cancel the dialog.
     */
    Q_INVOKABLE void cancel();

    // -- Internal signal used by the subwindow close event -------------------

signals:
    void dialogClosed(int result);

private slots:
    /** Relay from the CustomMdiSubWindow destroyed/close signal. */
    void onSubWindowClosed();

private:
    void buildOnGuiThread();
    void tearDownOnGuiThread();

    QPointer<MainWindow>          m_mainWindow;
    QPointer<CustomMdiSubWindow>  m_subWindow;    ///< The actual MDI tool window.
    QPointer<QWidget>             m_contentWidget;///< Content area inside the subwindow.
    JSLayout*                     m_sizer = nullptr; ///< Root layout proxy.

    QString m_windowTitle  = QStringLiteral("Script Dialog");
    int     m_dialogWidth  = 380;
    int     m_dialogHeight = 240;
    int     m_result       = 0;      ///< 0 = cancel, 1 = ok

    QEventLoop* m_eventLoop = nullptr;  ///< Runs on the script thread during execute().
};

} // namespace Scripting

#endif // JSUI_H