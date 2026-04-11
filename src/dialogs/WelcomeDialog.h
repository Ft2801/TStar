/**
 * @file    WelcomeDialog.h
 * @brief   Splash-style welcome dialog shown at application startup.
 *
 * Displays an HTML overview of the application, a quick-start guide,
 * and a "Do not show again" checkbox whose state is persisted via QSettings.
 */

#ifndef WELCOMEDIALOG_H
#define WELCOMEDIALOG_H

#include "DialogBase.h"
#include <QCheckBox>
#include <QTextBrowser>

// ─────────────────────────────────────────────────────────────────────────────

/**
 * @class WelcomeDialog
 * @brief Modal dialog that greets the user on first launch.
 *
 * The dialog embeds a QTextBrowser to render styled HTML content and exposes
 * a checkbox that, when checked on close, writes a suppression flag to the
 * application QSettings so the dialog is not shown again on subsequent runs.
 */
class WelcomeDialog : public DialogBase
{
    Q_OBJECT

public:
    explicit WelcomeDialog(QWidget* parent = nullptr);

protected:
    // Persists the "do not show again" preference when the dialog is hidden.
    void hideEvent(QHideEvent* event) override;

private:
    // Builds and arranges all child widgets inside the dialog.
    void setupUI();

    // Generates and returns the full HTML string rendered in m_browser.
    QString buildWelcomeContent();

    // ── Widgets ───────────────────────────────────────────────────────────────

    QTextBrowser* m_browser;             // Renders the styled HTML welcome content.
    QCheckBox*    m_doNotShowAgainCheck; // Suppresses the dialog on future launches.
};

#endif // WELCOMEDIALOG_H