#include "SplashScreen.h"
#include <QPainter>
#include <QGuiApplication>
#include <QScreen>
#include <QFont>
#include <QEasingCurve>
#include <QLinearGradient>
#include <cmath>

SplashScreen::SplashScreen(const QString& logoPath, QWidget* parent)
    : QWidget(parent)
{
    // Window setup
    setWindowFlags(
        Qt::SplashScreen |
        Qt::FramelessWindowHint |
        Qt::WindowStaysOnTopHint
    );
    setAttribute(Qt::WA_TranslucentBackground, false);
    setAttribute(Qt::WA_DeleteOnClose, true);
    
    setFixedSize(m_splashWidth, m_splashHeight);
    
    // Center on screen
    if (QScreen* screen = QGuiApplication::primaryScreen()) {
        QRect geo = screen->availableGeometry();
        int x = (geo.width() - m_splashWidth) / 2 + geo.x();
        int y = (geo.height() - m_splashHeight) / 2 + geo.y();
        move(x, y);
    }
    
    // Load logo
    if (!logoPath.isEmpty()) {
        m_logoPixmap.load(logoPath);
        if (!m_logoPixmap.isNull()) {
            m_logoPixmap = m_logoPixmap.scaled(
                120, 120,
                Qt::KeepAspectRatio,
                Qt::SmoothTransformation
            );
        }
        
        // Try to load background from same directory
        QString bgPath = logoPath;
        bgPath.replace("Logo.png", "background.jpg");
        m_bgPixmap.load(bgPath);
        if (!m_bgPixmap.isNull()) {
            m_bgPixmap = m_bgPixmap.scaled(
                m_splashWidth, m_splashHeight,
                Qt::KeepAspectRatioByExpanding,
                Qt::SmoothTransformation
            );
            // Crop to exact size if needed
            if (m_bgPixmap.width() > m_splashWidth || m_bgPixmap.height() > m_splashHeight) {
                int x = (m_bgPixmap.width() - m_splashWidth) / 2;
                int y = (m_bgPixmap.height() - m_splashHeight) / 2;
                m_bgPixmap = m_bgPixmap.copy(x, y, m_splashWidth, m_splashHeight);
            }
        }
    }
    
    m_currentMessage = tr("Starting...");
    
    // Setup smooth progress timer (60 FPS)
    m_progressTimer = new QTimer(this);
    connect(m_progressTimer, &QTimer::timeout, this, &SplashScreen::updateSmoothProgress);
    m_progressTimer->start(16);
}

void SplashScreen::setMessage(const QString& message) {
    m_currentMessage = message;
    update(); // Schedule repaint, don't force it immediately to keep main thread fluid
    QGuiApplication::processEvents();
}

void SplashScreen::setProgress(int value) {
    m_targetProgress = static_cast<float>(qBound(0, value, 100));
    // Don't force repaint here, let timer handle it
    QGuiApplication::processEvents();
}

void SplashScreen::updateSmoothProgress() {
    if (std::abs(m_displayedProgress - m_targetProgress) < 0.1f) {
        m_displayedProgress = m_targetProgress;
    } else {
        // Smooth interpolation
        m_displayedProgress += (m_targetProgress - m_displayedProgress) * 0.2f;
    }
    update();
}

void SplashScreen::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    
    int w = m_splashWidth;
    int h = m_splashHeight;
    
    // Background gradient (deep space theme) - base layer
    QLinearGradient baseGradient(0, 0, 0, h);
    baseGradient.setColorAt(0.0, QColor(15, 15, 25));
    baseGradient.setColorAt(0.5, QColor(25, 25, 45));
    baseGradient.setColorAt(1.0, QColor(10, 10, 20));
    p.fillRect(0, 0, w, h, baseGradient);
    
    // Background image with 35% opacity and fade from middle to bottom
    if (!m_bgPixmap.isNull()) {
        // Draw background at 35% opacity
        p.setOpacity(0.35);
        p.drawPixmap(0, 0, m_bgPixmap);
        p.setOpacity(1.0);
        
        // Fade overlay: solid from middle to bottom (gradient that fades to background)
        QLinearGradient fadeGradient(0, h * 0.5, 0, h);
        fadeGradient.setColorAt(0.0, QColor(15, 15, 25, 0));       // Transparent at middle
        fadeGradient.setColorAt(0.7, QColor(15, 15, 25, 200));     // Mostly opaque
        fadeGradient.setColorAt(1.0, QColor(10, 10, 20, 255));     // Fully opaque at bottom
        p.fillRect(0, (int)(h * 0.5), w, (int)(h * 0.5), fadeGradient);
    }
    
    // Border
    p.setPen(QColor(60, 60, 80));
    p.drawRect(0, 0, w - 1, h - 1);
    
    // Logo (centered upper area)
    if (!m_logoPixmap.isNull()) {
        int logoX = (w - m_logoPixmap.width()) / 2;
        int logoY = 40;
        p.drawPixmap(logoX, logoY, m_logoPixmap);
    }
    
    // Title
    p.setFont(QFont("Segoe UI", 24, QFont::Bold));
    p.setPen(Qt::white);
    QRect titleRect(0, 170, w, 35);
    p.drawText(titleRect, Qt::AlignCenter, tr("TStar v") + QString(TSTAR_VERSION));
    
    // Subtitle
    p.setFont(QFont("Segoe UI", 16));
    p.setPen(QColor(180, 180, 200));
    QRect subtitleRect(0, 205, w, 30);
    p.drawText(subtitleRect, Qt::AlignCenter, tr("Professional astro editing app"));
    
    // Progress bar
    int barMargin = 50;
    int barHeight = 4;
    int barY = h - 60;
    int barWidth = w - (barMargin * 2);
    
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(40, 40, 60));
    p.drawRoundedRect(barMargin, barY, barWidth, barHeight, 2, 2);
    
    if (m_displayedProgress > 0.0f) {
        int fillWidth = static_cast<int>(barWidth * m_displayedProgress / 100.0f);
        QLinearGradient barGrad(barMargin, 0, barMargin + barWidth, 0);
        barGrad.setColorAt(0.0, QColor(80, 140, 220));
        barGrad.setColorAt(1.0, QColor(140, 180, 255));
        p.setBrush(barGrad);
        p.drawRoundedRect(barMargin, barY, fillWidth, barHeight, 2, 2);
    }
    
    // Loading message
    p.setFont(QFont("Segoe UI", 9));
    p.setPen(QColor(150, 150, 180));
    QRect msgRect(barMargin, barY + 10, barWidth, 20);
    p.drawText(msgRect, Qt::AlignLeft | Qt::AlignVCenter, m_currentMessage);
    
    // Copyright
    p.setFont(QFont("Segoe UI", 8));
    p.setPen(QColor(100, 100, 130));
    QRect copyrightRect(0, h - 25, w, 20);
    p.drawText(copyrightRect, Qt::AlignCenter, "© 2026 Fabio Tempera");
    
    p.end();
}

void SplashScreen::startFadeIn() {
    setWindowOpacity(0.0);
    m_anim = new QPropertyAnimation(this, "windowOpacity", this);
    m_anim->setDuration(800); // More noticeable fade
    m_anim->setStartValue(0.0);
    m_anim->setEndValue(1.0);
    m_anim->setEasingCurve(QEasingCurve::OutQuad);
    m_anim->start();
}

void SplashScreen::startFadeOut() {
    m_progressTimer->stop(); // Stop progress updates
    m_anim = new QPropertyAnimation(this, "windowOpacity", this);
    m_anim->setDuration(800);
    m_anim->setStartValue(1.0);
    m_anim->setEndValue(0.0);
    m_anim->setEasingCurve(QEasingCurve::OutQuad);
    connect(m_anim, &QPropertyAnimation::finished, this, &SplashScreen::finish);
    m_anim->start();
}

void SplashScreen::finish() {
    hide();
    close();
    deleteLater();
}
