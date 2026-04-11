/**
 * @file    WelcomeDialog.cpp
 * @brief   Implementation of WelcomeDialog.
 *
 * The dialog layout consists of:
 *   - A QTextBrowser that renders the HTML welcome page (built by buildWelcomeContent()).
 *   - A bottom bar with a "Do not show again" checkbox and a Close button.
 *
 * On hide, if the checkbox is checked the flag "general/show_welcome = false"
 * is written to the "TStar/TStar" QSettings scope.
 */

#include "WelcomeDialog.h"

#include <QHBoxLayout>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────

WelcomeDialog::WelcomeDialog(QWidget* parent)
    : DialogBase(parent, tr("Welcome to TStar"), 700, 500)
{
    setupUI();
}

// ─────────────────────────────────────────────────────────────────────────────
// Private – UI construction
// ─────────────────────────────────────────────────────────────────────────────

void WelcomeDialog::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);

    // ── HTML content browser ──────────────────────────────────────────────────

    m_browser = new QTextBrowser(this);
    m_browser->setOpenExternalLinks(true);
    m_browser->setStyleSheet(
        "QTextBrowser {"
        "  background-color: #1e1e1e;"
        "  color: #e0e0e0;"
        "  border: none;"
        "  font-family: 'Segoe UI', Arial, sans-serif;"
        "  font-size: 14px;"
        "  padding: 10px;"
        "}"
    );
    m_browser->setHtml(buildWelcomeContent());
    mainLayout->addWidget(m_browser);

    // ── Bottom bar: checkbox + close button ───────────────────────────────────

    QHBoxLayout* bottomLayout = new QHBoxLayout();

    m_doNotShowAgainCheck = new QCheckBox(tr("Do not show this welcome screen again"), this);
    m_doNotShowAgainCheck->setStyleSheet("color: #e0e0e0;");
    bottomLayout->addWidget(m_doNotShowAgainCheck);

    bottomLayout->addStretch();

    QPushButton* closeBtn = new QPushButton(tr("Close"), this);
    closeBtn->setFixedWidth(100);
    closeBtn->setStyleSheet(
        "QPushButton       { background-color: #3a7d44; color: white;"
        "                    padding: 6px 16px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #4a8d54; }"
    );
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    bottomLayout->addWidget(closeBtn);
    mainLayout->addLayout(bottomLayout);
}

// ─────────────────────────────────────────────────────────────────────────────
// Private – HTML content builder
// ─────────────────────────────────────────────────────────────────────────────

QString WelcomeDialog::buildWelcomeContent()
{
    QString html;

    // ── Embedded CSS ──────────────────────────────────────────────────────────

    html += R"(
<style>
    h1 { color: #4a9eff; margin-bottom: 5px; }
    h2 { color: #7ec8e3; margin-top: 15px; margin-bottom: 5px;
         border-bottom: 1px solid #444; padding-bottom: 3px; }
    p  { margin: 8px 0; line-height: 1.5; }
    ul { margin: 5px 0 10px 20px; }
    li { margin: 4px 0; }
    .shortcut  { color: #ffcc00; font-weight: bold; }
    .highlight { color: #88ff88; }
</style>
)";

    // ── Introduction ──────────────────────────────────────────────────────────

    html += "<h1>" + tr("Welcome to TStar") + "</h1>";
    html += "<p>" + tr("TStar is a comprehensive, high-performance astrophotography processing suite designed to handle everything from RAW calibration to final dynamic stretch and presentation. It provides an intuitive, non-destructive workflow with both established algorithms and modern AI-driven tools.") + "</p>";

    // ── Preliminary setup ─────────────────────────────────────────────────────

    html += "<h2>" + tr("Preliminary Setup") + "</h2>";
    html += "<p>" + tr("To get the most out of TStar, please complete these initial setup steps in the") + " <b>" + tr("Settings") + "</b> " + tr("menu:") + "</p>";
    html += "<ul>";
    html += "<li><b>" + tr("External Tools:") + "</b> " + tr("set the execution paths for") + " <span class='highlight'>GraXpert</span> " + tr("and") + " <span class='highlight'>StarNet++</span> " + tr("to enable advanced AI workflow integration.") + "</li>";
    html += "<li><b>" + tr("Star Catalog:") + "</b> " + tr("download or link the") + " <span class='highlight'>" + tr("ASTAP star database") + "</span> " + tr("for professional plate solving and accurate photometric color calibration.") + "</li>";
    html += "<li><b>" + tr("AI Models:") + "</b> " + tr("download the") + " <span class='highlight'>" + tr("Cosmic Clarity models") + "</span> " + tr("via Settings to enable AI-powered deconvolution and noise reduction.") + "</li>";
    html += "</ul>";

    // ── Quick-start guide ─────────────────────────────────────────────────────

    html += "<h2>" + tr("Quick Start Guide") + "</h2>";
    html += "<p>" + tr("Follow this standard processing workflow to get started quickly:") + "</p>";
    html += "<ol>";
    html += "<li><b>" + tr("Open:") + "</b> " + tr("press") + " <span class='shortcut'>Ctrl+O</span> " + tr("to load your stacked image (FITS/XISF/TIFF).") + "</li>";
    html += "<li><b>" + tr("Plate Solving:") + "</b> " + tr("run Plate Solving (Utilities -> Plate Solving) to attach sky coordinates.") + "</li>";
    html += "<li><b>" + tr("Color Calibration:") + "</b> " + tr("use SPCC or PCC (Color Management) to accurately balance colors.") + "</li>";
    html += "<li><b>" + tr("Extract Background:") + "</b> " + tr("apply ABE or CBE (Color Management) to remove light pollution gradients.") + "</li>";
    html += "<li><b>" + tr("Detail & Denoise:") + "</b> " + tr("apply AI sharpening and noise reduction (Cosmic Clarity and GraXpert are recommended).") + "</li>";
    html += "<li><b>" + tr("Stretch:") + "</b> " + tr("transition to non-linear space using GHS, Histogram, or ArcSinh Stretch tools.") + "</li>";
    html += "<li><b>" + tr("Explore:") + "</b> " + tr("continue enhancing the image exploring the many tools available in TStar.") + "</li>";
    html += "</ol>";

    html += "<p style=\"margin-top: 15px;\"><i>"
          + tr("For a full review of tools and shortcuts, open the Help & Tutorial manual from the main menu.")
          + "</i></p>";

    return html;
}

// ─────────────────────────────────────────────────────────────────────────────
// Protected event handlers
// ─────────────────────────────────────────────────────────────────────────────

void WelcomeDialog::hideEvent(QHideEvent* event)
{
    // Persist the suppression flag so the dialog is skipped on future launches.
    if (m_doNotShowAgainCheck->isChecked())
    {
        QSettings settings("TStar", "TStar");
        settings.setValue("general/show_welcome", false);
    }

    DialogBase::hideEvent(event);
}