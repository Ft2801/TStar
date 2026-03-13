#include "RightSidebarWidget.h"
#include "CustomMdiSubWindow.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVariantAnimation>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QPainter>

// ─────────────────────────────────────────────────────────────────────────────
// ThumbnailItem: fixed-size preview tile with elided title label
// ─────────────────────────────────────────────────────────────────────────────
class ThumbnailItem : public QWidget {
    Q_OBJECT
public:
    ThumbnailItem(const QPixmap& thumb, const QString& title,
                  CustomMdiSubWindow* sub, QWidget* parent = nullptr)
        : QWidget(parent), m_sub(sub)
    {
        setFixedWidth(RightSidebarWidget::THUMB_W + 8);  // 4px padding each side
        setAttribute(Qt::WA_StyledBackground, true);
        setStyleSheet("background: transparent;");
        setCursor(Qt::PointingHandCursor);

        QVBoxLayout* vl = new QVBoxLayout(this);
        vl->setContentsMargins(4, 4, 4, 4);
        vl->setSpacing(3);

        // ── Thumbnail (fixed safe-zone size) ──────────────────────────────
        m_thumbLabel = new QLabel(this);
        m_thumbLabel->setFixedSize(RightSidebarWidget::THUMB_W, RightSidebarWidget::THUMB_H);
        m_thumbLabel->setAlignment(Qt::AlignCenter);
        m_thumbLabel->setStyleSheet("background: #1a1a1a; border: 1px solid #444;");
        setThumb(thumb);
        vl->addWidget(m_thumbLabel);

        // ── Title (safe zone: fixed height, text elided to fit) ───────────
        m_titleLabel = new QLabel(this);
        m_titleLabel->setFixedWidth(RightSidebarWidget::THUMB_W);
        m_titleLabel->setFixedHeight(18);
        m_titleLabel->setAlignment(Qt::AlignCenter);
        m_titleLabel->setStyleSheet("color: #ccc; font-size: 10px; background: transparent;");
        setTitle(title);
        vl->addWidget(m_titleLabel);
    }

    void setThumb(const QPixmap& px) {
        if (px.isNull()) {
            m_thumbLabel->clear();
        } else {
            m_thumbLabel->setPixmap(
                px.scaled(RightSidebarWidget::THUMB_W, RightSidebarWidget::THUMB_H,
                          Qt::KeepAspectRatio, Qt::SmoothTransformation));
        }
    }

    void setTitle(const QString& title) {
        QFontMetrics fm(m_titleLabel->font());
        m_titleLabel->setText(fm.elidedText(title, Qt::ElideRight, RightSidebarWidget::THUMB_W - 4));
        m_titleLabel->setToolTip(title);
    }

    CustomMdiSubWindow* sub() const { return m_sub; }

signals:
    void clicked(CustomMdiSubWindow* sub);

protected:
    void mousePressEvent(QMouseEvent* e) override {
        if (e->button() == Qt::LeftButton) emit clicked(m_sub);
        QWidget::mousePressEvent(e);
    }
    void enterEvent(QEnterEvent* e) override {
        setStyleSheet("background: #333; border-radius: 3px;");
        QWidget::enterEvent(e);
    }
    void leaveEvent(QEvent* e) override {
        setStyleSheet("background: transparent;");
        QWidget::leaveEvent(e);
    }

private:
    CustomMdiSubWindow* m_sub;
    QLabel*             m_thumbLabel;
    QLabel*             m_titleLabel;
};

// ─────────────────────────────────────────────────────────────────────────────
// RightSidebarWidget
// ─────────────────────────────────────────────────────────────────────────────
RightSidebarWidget::RightSidebarWidget(QWidget* parent)
    : QWidget(parent)
{
    QHBoxLayout* mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    setStyleSheet("background-color: transparent;");
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);

    // 1. Content area (left of tab, slides horizontally)
    m_contentContainer = new QScrollArea(this);
    m_contentContainer->setFixedWidth(0);
    m_contentContainer->setStyleSheet("background-color: rgba(0, 0, 0, 128); border: none;");
    m_contentContainer->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_contentContainer->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);

    m_listWidget = new QWidget();
    m_listLayout = new QVBoxLayout(m_listWidget);
    m_listLayout->setContentsMargins(4, 8, 4, 8);
    m_listLayout->setSpacing(6);
    m_listLayout->addStretch();

    m_listWidget->setMinimumWidth(m_expandedWidth);
    m_contentContainer->setWidget(m_listWidget);
    m_contentContainer->setWidgetResizable(true);

    // 2. Tab strip (right edge)
    m_tabContainer = new QWidget(this);
    m_tabContainer->setFixedWidth(32);
    m_tabContainer->setStyleSheet("background-color: #252525; border-left: 1px solid #1a1a1a;");

    QVBoxLayout* tabLayout = new QVBoxLayout(m_tabContainer);
    tabLayout->setContentsMargins(2, 5, 2, 5);
    tabLayout->setSpacing(5);

    m_tabBtn = new QPushButton(tr("Views"), m_tabContainer);
    m_tabBtn->setCheckable(true);
    m_tabBtn->setFixedSize(30, 100);
    m_tabBtn->setToolTip(tr("Collapsed Views"));
    m_tabBtn->setStyleSheet("border: none;");
    connect(m_tabBtn, &QPushButton::clicked, this, &RightSidebarWidget::onTabClicked);

    tabLayout->addStretch();
    tabLayout->addWidget(m_tabBtn);
    tabLayout->addStretch();

    mainLayout->addWidget(m_contentContainer);
    mainLayout->addWidget(m_tabContainer);

    // Animation
    m_widthAnim = new QVariantAnimation(this);
    m_widthAnim->setDuration(250);
    m_widthAnim->setEasingCurve(QEasingCurve::OutQuad);
    connect(m_widthAnim, &QVariantAnimation::valueChanged, [this](const QVariant& val) {
        m_contentContainer->setFixedWidth(val.toInt());
        int newW = totalVisibleWidth();
        resize(newW, height());
        // Keep the right edge anchored
        if (m_anchorRight >= 0) {
            move(m_anchorRight - newW + 1, y());
        }
    });
}

int RightSidebarWidget::totalVisibleWidth() const {
    return m_tabContainer->width() + m_contentContainer->width();
}

void RightSidebarWidget::setAnchorGeometry(int rightX, int topY, int h) {
    m_anchorRight = rightX;
    int newW = totalVisibleWidth();
    move(rightX - newW + 1, topY);
    resize(newW, h);
}

void RightSidebarWidget::onTabClicked() {
    setExpanded(!m_expanded);
    m_tabBtn->setChecked(m_expanded);
}

void RightSidebarWidget::setExpanded(bool expanded) {
    if (m_expanded == expanded) return;
    m_expanded = expanded;
    emit expandedToggled(expanded);

    m_widthAnim->stop();
    m_widthAnim->setStartValue(m_contentContainer->width());
    m_widthAnim->setEndValue(expanded ? m_expandedWidth : 0);
    m_widthAnim->start();
}

void RightSidebarWidget::addThumbnail(CustomMdiSubWindow* sub, const QPixmap& thumb, const QString& title) {
    if (!sub || m_items.contains(sub)) return;

    auto* item = new ThumbnailItem(thumb, title, sub, m_listWidget);
    connect(item, &ThumbnailItem::clicked, this, &RightSidebarWidget::thumbnailActivated);

    // Insert before the trailing stretch (last item)
    m_listLayout->insertWidget(m_listLayout->count() - 1, item);
    m_items[sub] = item;

    // Auto-open if first thumbnail added
    if (m_items.size() == 1 && !m_expanded) {
        setExpanded(true);
        m_tabBtn->setChecked(true);
    }
}

void RightSidebarWidget::removeThumbnail(CustomMdiSubWindow* sub) {
    if (!sub || !m_items.contains(sub)) return;

    QWidget* w = m_items.take(sub);
    m_listLayout->removeWidget(w);
    w->deleteLater();

    // Auto-collapse when empty
    if (m_items.isEmpty() && m_expanded) {
        setExpanded(false);
        m_tabBtn->setChecked(false);
    }
}

// Required for AUTOMOC to process the Q_OBJECT class defined in this .cpp file
#include "RightSidebarWidget.moc"

