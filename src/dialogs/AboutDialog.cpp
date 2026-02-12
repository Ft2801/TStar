#include "AboutDialog.h"
#include <QVBoxLayout>
#include <QLabel>
#include <QCoreApplication>

AboutDialog::AboutDialog(QWidget* parent, const QString& version, const QString& buildTimestamp)
    : DialogBase(parent, tr("About TStar"), 450, 250)
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 15, 20, 15); // Reduced margins
    layout->setSpacing(10); // Reduced spacing between sections

    QString v = version;
    if (v.isEmpty()) v = "1.0.0"; // Default or fetch from somewhere

    QString bt = buildTimestamp;
    if (bt.isEmpty()) bt = __DATE__ " " __TIME__;

    QStringList aboutLines;
    aboutLines << QString("<h2>TStar %1</h2>").arg(v);
    aboutLines << QString("<p>%1</p>").arg(tr("Written by Fabio Tempera"));
    
    QString linkStyle = "color: #4da6ff; text-decoration: none;";
    aboutLines << QString("<p>%1 <a href='https://github.com/ft2801' style='%2'>%3</a></p>")
                    .arg(tr("link to my ")).arg(linkStyle).arg(tr("GitHub profile"));
    aboutLines << QString("<p>%1 <a href='https://ft2801.github.io/Portfolio' style='%2'>%3</a></p>")
                    .arg(tr("link to my ")).arg(linkStyle).arg(tr("portfolio"));
    aboutLines << QString("<p>%1 <a href='https://ft2801.github.io/FT-Astrophotography' style='%2'>%3</a></p>")
                    .arg(tr("link to my ")).arg(linkStyle).arg(tr("astronomy website"));

    aboutLines << QString("<p>%1</p>").arg(tr("Copyright © 2026")); 
    
    if (!bt.isEmpty()) {
        aboutLines << QString("<p><b>%1</b> %2</p>").arg(tr("Build:")).arg(bt);
    }
    
    QString aboutText = aboutLines.join("");
    // Remove extra paragraph spacing in HTML if needed, but <p> is standard.
    
    QLabel* infoLabel = new QLabel(aboutText, this);
    infoLabel->setTextFormat(Qt::RichText);
    infoLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    infoLabel->setOpenExternalLinks(true);
    infoLabel->setAlignment(Qt::AlignCenter);
    infoLabel->setWordWrap(true);

    QLabel* descLabel = new QLabel(tr("TStar is a professional astrophotography image processing application."), this);
    descLabel->setStyleSheet("font-style: italic; color: #aaaaaa;");
    descLabel->setAlignment(Qt::AlignCenter);
    descLabel->setWordWrap(true);

    layout->addWidget(infoLabel);
    layout->addWidget(descLabel);
    
    // Set a reasonable minimum width and let the layout determine height
    setMinimumWidth(350); // Slightly reduced width constraint too
    adjustSize();

}
