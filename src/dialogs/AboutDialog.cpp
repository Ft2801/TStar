#include "AboutDialog.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QCoreApplication>

AboutDialog::AboutDialog(QWidget* parent, const QString& version, const QString& buildTimestamp) : QDialog(parent) {
    setWindowTitle(tr("About TStar"));
    
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(30, 20, 30, 20); // Balanced margins
    layout->setSpacing(20); // Spacing between sections

    QString v = version;
    if (v.isEmpty()) v = "1.0.0"; // Default or fetch from somewhere

    QString bt = buildTimestamp;
    if (bt.isEmpty()) bt = __DATE__ " " __TIME__;

    QStringList aboutLines;
    aboutLines << QString("<h2>TStar %1</h2>").arg(v);
    aboutLines << QString("<p>%1</p>").arg(tr("Written by Fabio Tempera"));
    aboutLines << QString("<p>%1</p>").arg(tr("Copyright © 2026")); 
    
    if (!bt.isEmpty()) {
        aboutLines << QString("<p><b>%1</b> %2</p>").arg(tr("Build:")).arg(bt);
    }
    
    QString aboutText = aboutLines.join("");

    QLabel* infoLabel = new QLabel(aboutText, this);
    infoLabel->setTextFormat(Qt::RichText);
    infoLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    infoLabel->setOpenExternalLinks(true);
    infoLabel->setAlignment(Qt::AlignCenter);
    infoLabel->setWordWrap(true);

    QLabel* descLabel = new QLabel(tr("TStar is a professional astrophotography image processing application designed to provide advanced tools for enhancing and analyzing astronomical images."), this);
    descLabel->setStyleSheet("font-style: italic; color: #aaaaaa;");
    descLabel->setAlignment(Qt::AlignCenter);
    descLabel->setWordWrap(true);

    layout->addWidget(infoLabel);
    layout->addWidget(descLabel);
    
    // Set a reasonable minimum width and let the layout determine height
    setMinimumWidth(400);
    adjustSize();

    if (parentWidget()) {
        move(parentWidget()->window()->geometry().center() - rect().center());
    }
}
