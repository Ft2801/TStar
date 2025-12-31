#include <QApplication>
#include <QStyleFactory>
#include <QPalette>
#include <QTimer>
#include <QThread>
#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <QIcon>
#include <QTranslator>
#include <QLocale>
#include "MainWindow.h"
#include "widgets/SplashScreen.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("FabioTempera");
    QCoreApplication::setApplicationName("TStar");
    
    // --- Show Splash Screen Immediately ---
    QString appDir = QCoreApplication::applicationDirPath();
    QDir dir(appDir);
    
    QStringList searchPaths;
    searchPaths << dir.filePath("images/Logo.png");
    searchPaths << dir.filePath("Logo.png");
    searchPaths << QDir::cleanPath(dir.absoluteFilePath("../src/images/Logo.png"));
    searchPaths << QDir::cleanPath(dir.absoluteFilePath("../../src/images/Logo.png"));
    
    QString logoPath;
    for (const QString& path : searchPaths) {
        if (QFile::exists(path)) {
            logoPath = path;
            break;
        }
    }
    
    SplashScreen* splash = new SplashScreen(logoPath);
    
    // --- Load Translations ---
    QSettings settings("FabioTempera", "TStar");
    QString lang = settings.value("general/language", "System").toString();
    
    QTranslator* translator = new QTranslator(&app);
    bool loaded = false;

    if (lang == "System") {
        const QStringList uiLanguages = QLocale::system().uiLanguages();
        for (const QString &locale : uiLanguages) {
            QString langCode = QLocale(locale).name().left(2);
            if (translator->load(":/translations/tstar_" + langCode)) {
                loaded = true;
                break;
            }
            if (translator->load("tstar_" + langCode, QCoreApplication::applicationDirPath() + "/translations")) {
                loaded = true;
                break;
            }
        }
    } else if (lang != "en") {
        if (translator->load(":/translations/tstar_" + lang)) {
            loaded = true;
        } else if (translator->load("tstar_" + lang, QCoreApplication::applicationDirPath() + "/translations")) {
            loaded = true;
        }
    }

    if (loaded) {
        app.installTranslator(translator);
    }

    // Slight pause before showing splash to allow OS/App to settle (0.2s)
    QThread::msleep(200);
    
    splash->show();
    splash->startFadeIn();
    
    // Block briefly to let fade-in be seen (0.5s)
    for (int i = 0; i < 50; ++i) {
        app.processEvents();
        QThread::msleep(10);
    }
    
    auto waitStep = [&](int ms) {
        for (int i = 0; i < ms / 10; ++i) {
            app.processEvents();
            QThread::msleep(10);
        }
    };

    splash->setMessage(QCoreApplication::translate("main", "Initializing Core Systems..."));
    splash->setProgress(10);
    waitStep(100);
    
    splash->setMessage(QCoreApplication::translate("main", "Loading Configuration..."));
    splash->setProgress(15);
    waitStep(100);
    
    splash->setMessage(QCoreApplication::translate("main", "Initializing Memory Manager..."));
    splash->setProgress(20);
    waitStep(100);
    
    splash->setMessage(QCoreApplication::translate("main", "Loading Image Processing Algorithms..."));
    splash->setProgress(30);
    waitStep(100);
    
    splash->setMessage(QCoreApplication::translate("main", "Initializing OpenCV Backend..."));
    splash->setProgress(35);
    waitStep(100);
    
    splash->setMessage(QCoreApplication::translate("main", "Loading Color Management..."));
    splash->setProgress(40);
    waitStep(100);
    
    splash->setMessage(QCoreApplication::translate("main", "Setting up Dark Theme..."));
    splash->setProgress(50);
    
    // Set Dark Theme
    QApplication::setStyle(QStyleFactory::create("Fusion"));
    QPalette p = qApp->palette();
    p.setColor(QPalette::Window, QColor(53, 53, 53));
    p.setColor(QPalette::WindowText, Qt::white);
    p.setColor(QPalette::Base, QColor(25, 25, 25));
    p.setColor(QPalette::AlternateBase, QColor(53, 53, 53));
    p.setColor(QPalette::ToolTipBase, Qt::white);
    p.setColor(QPalette::ToolTipText, Qt::white);
    p.setColor(QPalette::Text, Qt::white);
    p.setColor(QPalette::Button, QColor(53, 53, 53));
    p.setColor(QPalette::ButtonText, Qt::white);
    p.setColor(QPalette::BrightText, Qt::red);
    p.setColor(QPalette::Link, QColor(42, 130, 218));
    p.setColor(QPalette::Highlight, QColor(42, 130, 218));
    p.setColor(QPalette::HighlightedText, Qt::black);
    qApp->setPalette(p);
    
    waitStep(100);
    
    splash->setMessage(QCoreApplication::translate("main", "Configuring UI Colors..."));
    splash->setProgress(55);
    waitStep(100);
    
    splash->setMessage(QCoreApplication::translate("main", "Loading Stylesheets..."));
    splash->setProgress(60);
    waitStep(100);
    
    // Tooltip style only (don't style QMdiSubWindow as it breaks native title bar)
    qApp->setStyleSheet(
        "QToolTip { color: #ffffff; background-color: #2a82da; border: 1px solid white; }"
        "QScrollBar:vertical { border: 0px; background: #2b2b2b; width: 10px; margin: 0px 0px 0px 0px; border-radius: 5px; }"
        "QScrollBar::handle:vertical { background: #555; min-height: 20px; border-radius: 5px; }"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0px; }"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; }"
        "QScrollBar:horizontal { border: 0px; background: #2b2b2b; height: 10px; margin: 0px 0px 0px 0px; border-radius: 5px; }"
        "QScrollBar::handle:horizontal { background: #555; min-width: 20px; border-radius: 5px; }"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0px; }"
        "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: none; }"
        "QComboBox { color: white; background-color: #353535; border: 1px solid #555; padding: 2px; }"
        "QComboBox:hover { border: 1px solid #2a82da; }"
        "QComboBox QAbstractItemView { background-color: #2a2a2a; color: white; selection-background-color: #4a7ba7; selection-color: white; outline: none; }"
        "QComboBox QAbstractItemView::item { padding: 5px; margin: 0px; color: white; }"
        "QComboBox QAbstractItemView::item:hover { background-color: #4a7ba7; color: white; }"
        "QComboBox QAbstractItemView::item:selected { background-color: #4a7ba7; color: white; }"
    );

    splash->setMessage(QCoreApplication::translate("main", "Applying Custom Widgets..."));
    splash->setProgress(65);
    waitStep(100);

    splash->setMessage(QCoreApplication::translate("main", "Loading Icons & Resources..."));
    splash->setProgress(70);
    waitStep(100);

    splash->setMessage(QCoreApplication::translate("main", "Initializing Tool Dialogs..."));
    splash->setProgress(75);
    waitStep(100);

    splash->setMessage(QCoreApplication::translate("main", "Constructing Main Window..."));
    splash->setProgress(80);
    waitStep(100);

    MainWindow* window = new MainWindow();
    window->setWindowTitle("TStar v" + QString(TSTAR_VERSION));
    window->resize(1280, 800);
    
    // Set application icon for window and taskbar
    app.setWindowIcon(QIcon(logoPath));
    
    splash->setMessage(QCoreApplication::translate("main", "Configuring Workspace..."));
    splash->setProgress(90);
    waitStep(100);
    
    splash->setMessage(QCoreApplication::translate("main", "Finalizing Setup..."));
    splash->setProgress(95);
    waitStep(100);
    
    splash->setMessage(QCoreApplication::translate("main", "Ready!"));
    splash->setProgress(100);
    
    // Final wait to see 100%
    waitStep(150);
    
    splash->startFadeOut();
    window->showMaximized(); 

    return app.exec();
}

