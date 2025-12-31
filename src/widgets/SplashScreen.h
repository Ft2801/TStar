#ifndef SPLASHSCREEN_H
#define SPLASHSCREEN_H

#include <QWidget>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QTimer>

class SplashScreen : public QWidget {
    Q_OBJECT
public:
    explicit SplashScreen(const QString& logoPath, QWidget* parent = nullptr);
    
    void setMessage(const QString& message);
    void setProgress(int value);
    
    void startFadeIn();
    void startFadeOut();

protected:
    void paintEvent(QPaintEvent* event) override;

private slots:
    void finish();
    void updateSmoothProgress();

private:
    QPixmap m_logoPixmap;
    QPixmap m_bgPixmap;
    QString m_currentMessage;
    
    // Smooth progress
    float m_displayedProgress = 0.0f;
    float m_targetProgress = 0.0f;
    
    int m_splashWidth = 500;
    int m_splashHeight = 320;
    
    QPropertyAnimation* m_anim = nullptr;
    QTimer* m_progressTimer = nullptr;
};

#endif // SPLASHSCREEN_H
