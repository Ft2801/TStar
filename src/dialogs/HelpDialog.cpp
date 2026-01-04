#include "HelpDialog.h"
#include <QVBoxLayout>
#include <QScrollArea>
#include <QPushButton>
#include <QScreen>
#include <QGuiApplication>

HelpDialog::HelpDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("TStar Help & Tutorial"));
    setWindowIcon(QIcon(":/images/Logo.png"));
    setupUI();
}

void HelpDialog::setupUI()
{
    // Fixed size as requested: 800x600
    setFixedSize(800, 600);
    
    // Center on screen (like GHT dialog)
    if (QScreen* screen = QGuiApplication::primaryScreen()) {
        QRect screenGeometry = screen->availableGeometry();
        int x = (screenGeometry.width() - width()) / 2;
        int y = (screenGeometry.height() - height()) / 2;
        move(x, y);
    }
    
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);
    
    // Text Browser for HTML content
    m_browser = new QTextBrowser(this);
    m_browser->setOpenExternalLinks(true);
    m_browser->setStyleSheet(
        "QTextBrowser { "
        "   background-color: #1e1e1e; "
        "   color: #e0e0e0; "
        "   border: none; "
        "   font-family: 'Segoe UI', Arial, sans-serif; "
        "   font-size: 13px; "
        "   padding: 10px; "
        "}"
    );
    m_browser->setHtml(buildHelpContent());
    mainLayout->addWidget(m_browser);
    
    // Close Button
    QPushButton* closeBtn = new QPushButton(tr("Close"), this);
    closeBtn->setFixedWidth(100);
    closeBtn->setStyleSheet(
        "QPushButton { background-color: #3a7d44; color: white; padding: 6px 16px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #4a8d54; }"
    );
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    
    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    btnLayout->addWidget(closeBtn);
    mainLayout->addLayout(btnLayout);
}

QString HelpDialog::buildHelpContent()
{
    QString html;
    
    // CSS Style (not translatable)
    html += R"(
<style>
    h1 { color: #4a9eff; margin-bottom: 5px; }
    h2 { color: #7ec8e3; margin-top: 20px; margin-bottom: 8px; border-bottom: 1px solid #444; padding-bottom: 4px; }
    h3 { color: #a0d0ff; margin-top: 15px; margin-bottom: 5px; }
    p { margin: 6px 0; line-height: 1.5; }
    ul { margin: 5px 0 10px 20px; }
    li { margin: 3px 0; }
    code { background-color: #333; padding: 2px 5px; border-radius: 3px; font-family: Consolas, monospace; }
    .shortcut { color: #ffcc00; }
    .tip { color: #88ff88; font-style: italic; }
</style>
)";

    // Title
    html += "<h1>" + tr("TStar - Astrophotography Processing") + "</h1>";
    html += "<p>" + tr("Welcome to TStar! This guide covers all features and tools available in the application.") + "</p>";

    // Getting Started
    html += "<h2>" + tr("Getting Started") + "</h2>";
    html += "<p>" + tr("TStar supports FITS, XISF, TIFF, and PNG image formats commonly used in astrophotography.") + "</p>";
    html += "<ul>";
    html += "<li><b>" + tr("Open Image:") + "</b> " + tr("Click Open or press Ctrl+O") + "</li>";
    html += "<li><b>" + tr("Save Image:") + "</b> " + tr("Click Save or press Ctrl+S") + "</li>";
    html += "<li><b>" + tr("Drag & Drop:") + "</b> " + tr("Drag files directly onto the workspace") + "</li>";
    html += "</ul>";

    // Navigation Controls
    html += "<h2>" + tr("Navigation Controls") + "</h2>";
    html += "<ul>";
    html += "<li><b>" + tr("Zoom In/Out:") + "</b> " + tr("Mouse wheel or Ctrl++ and Ctrl+-") + "</li>";
    html += "<li><b>" + tr("Pan:") + "</b> " + tr("Click and drag to move around the image") + "</li>";
    html += "<li><b>" + tr("Fit to Window:") + "</b> " + tr("Press Ctrl+0") + "</li>";
    html += "<li><b>" + tr("1:1 Zoom:") + "</b> " + tr("Click the 1:1 button for 100% zoom") + "</li>";
    html += "<li><b>" + tr("Undo/Redo:") + "</b> " + tr("Ctrl+Z / Ctrl+Shift+Z") + "</li>";
    html += "</ul>";

    // Display Modes
    html += "<h2>" + tr("Display Modes") + "</h2>";
    html += "<p>" + tr("Use the dropdown menu in the toolbar to change visualization:") + "</p>";
    html += "<ul>";
    html += "<li><b>" + tr("Linear:") + "</b> " + tr("Raw pixel values without stretch") + "</li>";
    html += "<li><b>" + tr("Auto Stretch:") + "</b> " + tr("Automatic histogram stretch for best visibility") + "</li>";
    html += "<li><b>" + tr("Histogram:") + "</b> " + tr("Histogram equalization") + "</li>";
    html += "<li><b>" + tr("ArcSinh:") + "</b> " + tr("Non-linear stretch preserving star colors") + "</li>";
    html += "<li><b>" + tr("Square Root / Logarithmic:") + "</b> " + tr("Alternative stretches") + "</li>";
    html += "</ul>";
    html += "<p class=\"tip\">" + tr("Tip: Toggle RGB Link to stretch channels independently or together.") + "</p>";

    // Stretch Tools
    html += "<h2>" + tr("Stretch Tools") + "</h2>";
    
    html += "<h3>" + tr("Auto Stretch (Statistical)") + "</h3>";
    html += "<p>" + tr("Automatically stretches the image based on statistical analysis. Ideal for quick previews.") + "</p>";
    
    html += "<h3>" + tr("GHS (Generalized Hyperbolic Stretch)") + "</h3>";
    html += "<p>" + tr("Advanced stretch tool with full control:") + "</p>";
    html += "<ul>";
    html += "<li><b>D (" + tr("Stretch") + "):</b> " + tr("Controls stretch intensity (0-10)") + "</li>";
    html += "<li><b>B (" + tr("Intensity") + "):</b> " + tr("Local intensity adjustment (-5 to 15)") + "</li>";
    html += "<li><b>SP (" + tr("Symmetry Point") + "):</b> " + tr("Focus point for stretch (click on image to pick)") + "</li>";
    html += "<li><b>LP/HP (" + tr("Protection") + "):</b> " + tr("Protect shadows/highlights from clipping") + "</li>";
    html += "<li><b>BP (" + tr("Black Point") + "):</b> " + tr("Set black clipping level") + "</li>";
    html += "</ul>";
    
    html += "<h3>" + tr("Curves Transformation") + "</h3>";
    html += "<p>" + tr("Adjust tonal curves for each RGB channel independently or together.") + "</p>";
    
    html += "<h3>" + tr("Histogram Transformation") + "</h3>";
    html += "<p>" + tr("Manual histogram stretch with shadows, midtones, and highlights controls.") + "</p>";
    
    html += "<h3>" + tr("ArcSinh Stretch") + "</h3>";
    html += "<p>" + tr("Specialized stretch that preserves star colors while increasing contrast.") + "</p>";

    // Color Management
    html += "<h2>" + tr("Color Management") + "</h2>";
    
    html += "<h3>" + tr("Auto Background Extraction (ABE)") + "</h3>";
    html += "<p>" + tr("Removes gradients and light pollution from your image:") + "</p>";
    html += "<ul>";
    html += "<li>" + tr("Click to add sample points on the background") + "</li>";
    html += "<li>" + tr("Adjust polynomial order for model complexity") + "</li>";
    html += "<li>" + tr("Choose Subtract or Divide mode") + "</li>";
    html += "</ul>";
    
    html += "<h3>" + tr("Photometric Color Calibration (PCC)") + "</h3>";
    html += "<p>" + tr("Calibrates colors using star catalog data:") + "</p>";
    html += "<ul>";
    html += "<li>" + tr("Requires plate-solved image (WCS data)") + "</li>";
    html += "<li>" + tr("Uses reference stars for accurate color calibration") + "</li>";
    html += "</ul>";
    
    html += "<h3>" + tr("Background Neutralization") + "</h3>";
    html += "<p>" + tr("Neutralizes color casts in the background sky:") + "</p>";
    html += "<ul>";
    html += "<li>" + tr("Select a region of pure background") + "</li>";
    html += "<li>" + tr("The tool will balance RGB channels") + "</li>";
    html += "</ul>";
    
    html += "<h3>" + tr("SCNR (Selective Color Noise Reduction)") + "</h3>";
    html += "<p>" + tr("Removes green color cast common in OSC/DSLR images.") + "</p>";
    
    html += "<h3>" + tr("Saturation") + "</h3>";
    html += "<p>" + tr("Adjust color saturation with protection for highlights and shadows.") + "</p>";

    // AI Processing
    html += "<h2>" + tr("AI Processing") + "</h2>";
    
    html += "<h3>" + tr("Cosmic Clarity") + "</h3>";
    html += "<p>" + tr("AI-powered deconvolution and noise reduction:") + "</p>";
    html += "<ul>";
    html += "<li>" + tr("Sharpens stars and details") + "</li>";
    html += "<li>" + tr("Reduces noise without losing detail") + "</li>";
    html += "<li>" + tr("Requires external Cosmic Clarity installation") + "</li>";
    html += "</ul>";
    
    html += "<h3>" + tr("GraXpert") + "</h3>";
    html += "<p>" + tr("AI-based gradient removal:") + "</p>";
    html += "<ul>";
    html += "<li>" + tr("Automatically detects and removes gradients") + "</li>";
    html += "<li>" + tr("More powerful than traditional ABE") + "</li>";
    html += "<li>" + tr("Requires external GraXpert installation") + "</li>";
    html += "</ul>";
    
    html += "<h3>" + tr("StarNet++") + "</h3>";
    html += "<p>" + tr("AI star removal for starless processing:") + "</p>";
    html += "<ul>";
    html += "<li>" + tr("Creates a starless version of your image") + "</li>";
    html += "<li>" + tr("Optionally creates a star-only mask") + "</li>";
    html += "<li>" + tr("Requires external StarNet installation") + "</li>";
    html += "</ul>";
    
    html += "<h3>" + tr("Aberration Remover (RAR)") + "</h3>";
    html += "<p>" + tr("Removes chromatic aberration and optical artifacts.") + "</p>";

    // Channel Operations
    html += "<h2>" + tr("Channel Operations") + "</h2>";
    
    html += "<h3>" + tr("Extract Channels") + "</h3>";
    html += "<p>" + tr("Splits RGB image into separate R, G, B windows.") + "</p>";
    
    html += "<h3>" + tr("Combine Channels") + "</h3>";
    html += "<p>" + tr("Combines separate channel images into one RGB image.") + "</p>";
    
    html += "<h3>" + tr("Star Recomposition") + "</h3>";
    html += "<p>" + tr("Blends starless and star-only images with adjustable parameters.") + "</p>";
    
    html += "<h3>" + tr("Debayer") + "</h3>";
    html += "<p>" + tr("Converts RAW Bayer pattern images to full color.") + "</p>";

    // Utilities
    html += "<h2>" + tr("Utilities") + "</h2>";
    
    html += "<h3>" + tr("Plate Solving") + "</h3>";
    html += "<p>" + tr("Identifies the exact sky coordinates of your image:") + "</p>";
    html += "<ul>";
    html += "<li>" + tr("Adds WCS (World Coordinate System) metadata") + "</li>";
    html += "<li>" + tr("Required for PCC and annotation tools") + "</li>";
    html += "</ul>";
    
    html += "<h3>" + tr("Pixel Math") + "</h3>";
    html += "<p>" + tr("Apply mathematical expressions to images:") + "</p>";
    html += "<ul>";
    html += "<li>" + tr("Combine images with formulas") + "</li>";
    html += "<li>" + tr("Use variables like $T (target), $R, $G, $B") + "</li>";
    html += "</ul>";
    
    html += "<h3>" + tr("Star Analysis") + "</h3>";
    html += "<p>" + tr("Analyzes star quality metrics (FWHM, roundness, etc.)") + "</p>";
    
    html += "<h3>" + tr("FITS Header Editor") + "</h3>";
    html += "<p>" + tr("View and edit FITS header metadata.") + "</p>";
    
    html += "<h3>" + tr("Image Annotator") + "</h3>";
    html += "<p>" + tr("Add object labels and annotations using catalog data.") + "</p>";

    // Masks
    html += "<h2>" + tr("Masks") + "</h2>";
    html += "<p>" + tr("Create and apply luminosity masks for selective processing:") + "</p>";
    html += "<ul>";
    html += "<li><b>" + tr("Create Mask:") + "</b> " + tr("Generate mask from image luminosity") + "</li>";
    html += "<li><b>" + tr("Apply Mask:") + "</b> " + tr("Load and apply existing mask") + "</li>";
    html += "<li><b>" + tr("Invert Mask:") + "</b> " + tr("Invert mask selection") + "</li>";
    html += "<li><b>" + tr("Show Overlay:") + "</b> " + tr("Toggle mask visualization") + "</li>";
    html += "</ul>";

    // Effects
    html += "<h2>" + tr("Effects") + "</h2>";
    
    html += "<h3>" + tr("AstroSpike") + "</h3>";
    html += "<p>" + tr("Adds artificial diffraction spikes to bright stars for aesthetic effect.") + "</p>";

    // Keyboard Shortcuts
    html += "<h2>" + tr("Keyboard Shortcuts") + "</h2>";
    html += "<table border=\"0\" cellpadding=\"3\">";
    html += "<tr><td><span class=\"shortcut\">Ctrl+O</span></td><td>" + tr("Open file") + "</td></tr>";
    html += "<tr><td><span class=\"shortcut\">Ctrl+S</span></td><td>" + tr("Save file") + "</td></tr>";
    html += "<tr><td><span class=\"shortcut\">Ctrl+Z</span></td><td>" + tr("Undo") + "</td></tr>";
    html += "<tr><td><span class=\"shortcut\">Ctrl+Shift+Z</span></td><td>" + tr("Redo") + "</td></tr>";
    html += "<tr><td><span class=\"shortcut\">Ctrl+0</span></td><td>" + tr("Fit to window") + "</td></tr>";
    html += "<tr><td><span class=\"shortcut\">Ctrl++</span></td><td>" + tr("Zoom in") + "</td></tr>";
    html += "<tr><td><span class=\"shortcut\">Ctrl+-</span></td><td>" + tr("Zoom out") + "</td></tr>";
    html += "</table>";

    // Tips
    html += "<h2>" + tr("Tips & Best Practices") + "</h2>";
    html += "<ul>";
    html += "<li>" + tr("Always work on a copy of your original data") + "</li>";
    html += "<li>" + tr("Use Undo frequently - every operation is reversible") + "</li>";
    html += "<li>" + tr("For best results, stretch AFTER background removal") + "</li>";
    html += "<li>" + tr("Enable RGB Link for color images to maintain color balance") + "</li>";
    html += "<li>" + tr("Check the Console panel for processing messages") + "</li>";
    html += "</ul>";

    // Footer
    html += "<p style=\"margin-top: 30px; color: #888;\">";
    html += "TStar © 2026 Fabio Tempera | <a href=\"https://github.com/Ft2801/TStar\" style=\"color: #4a9eff;\">GitHub</a>";
    html += "</p>";

    return html;
}
