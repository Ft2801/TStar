// =============================================================================
// HelpDialog.cpp
// Constructs the Help & Tutorial dialog with full HTML documentation covering
// all features, tools, keyboard shortcuts, scripting reference, and best
// practices for the TStar astrophotography application.
// =============================================================================

#include "HelpDialog.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QPushButton>
#include <QScreen>
#include <QGuiApplication>

// =============================================================================
// Construction
// =============================================================================

HelpDialog::HelpDialog(QWidget* parent)
    : DialogBase(parent, tr("Help & Tutorial"), 800, 600)
{
    setWindowIcon(QIcon(":/images/Logo.png"));
    setupUI();
}

// =============================================================================
// UI construction
// =============================================================================

void HelpDialog::setupUI()
{
    resize(800, 600);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(10, 10, 10, 10);

    // -- HTML text browser ----------------------------------------------------
    m_browser = new QTextBrowser(this);
    m_browser->setOpenExternalLinks(true);
    m_browser->setStyleSheet(
        "QTextBrowser { "
        "  background-color: #1e1e1e; "
        "  color: #e0e0e0; "
        "  border: none; "
        "  font-family: 'Segoe UI', Arial, sans-serif; "
        "  font-size: 13px; "
        "  padding: 10px; "
        "}");
    m_browser->setHtml(buildHelpContent());
    mainLayout->addWidget(m_browser);

    // -- Close button ---------------------------------------------------------
    QPushButton* closeBtn = new QPushButton(tr("Close"), this);
    closeBtn->setFixedWidth(100);
    closeBtn->setStyleSheet(
        "QPushButton { background-color: #3a7d44; color: white; "
        "  padding: 6px 16px; border-radius: 4px; }"
        "QPushButton:hover { background-color: #4a8d54; }");
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    btnLayout->addWidget(closeBtn);
    mainLayout->addLayout(btnLayout);
}

// =============================================================================
// HTML content generation
// =============================================================================

QString HelpDialog::buildHelpContent()
{
    QString html;

    // -- Embedded CSS (not translatable) --------------------------------------
    html += R"(
<style>
    h1 { color: #4a9eff; margin-bottom: 5px; }
    h2 { color: #7ec8e3; margin-top: 20px; margin-bottom: 8px;
         border-bottom: 1px solid #444; padding-bottom: 4px; }
    h3 { color: #a0d0ff; margin-top: 15px; margin-bottom: 5px; }
    p  { margin: 6px 0; line-height: 1.5; }
    ul { margin: 5px 0 10px 20px; }
    li { margin: 3px 0; }
    code { background-color: #333; padding: 2px 5px; border-radius: 3px;
           font-family: Consolas, monospace; }
    .shortcut { color: #ffcc00; }
    .tip      { color: #88ff88; font-style: italic; }
</style>
)";

    // -- Title ----------------------------------------------------------------
    html += "<h1>" + tr("TStar - Astrophotography Processing") + "</h1>";
    html += "<p>" + tr("Welcome to TStar! This guide covers all features and "
                       "tools available in the application.") + "</p>";

    // -- Getting Started ------------------------------------------------------
    html += "<h2>" + tr("Getting Started") + "</h2>";
    html += "<p>" + tr("TStar supports FITS/FIT, XISF, TIFF/TIF, PNG, "
                       "JPG/JPEG, BMP and (when LibRaw support is available) "
                       "major camera RAW formats such as CR2/CR3/NEF/ARW/DNG/"
                       "ORF/RW2/RAF and others.") + "</p>";
    html += "<ul>";
    html += "<li><b>" + tr("Open Image:") + "</b> "
            + tr("Click Open or press Ctrl+O") + "</li>";
    html += "<li><b>" + tr("Save Image:") + "</b> "
            + tr("Click Save or press Ctrl+S") + "</li>";
    html += "<li><b>" + tr("Drag & Drop:") + "</b> "
            + tr("Drag files directly onto the workspace") + "</li>";
    html += "</ul>";

    // -- Workspace Projects ---------------------------------------------------
    html += "<h2>" + tr("Workspace Projects") + "</h2>";
    html += "<p>" + tr("Organize your astrophotography workflow using Workspace "
                       "Projects. A project maintains a dedicated working "
                       "directory where all associated images, calibration "
                       "files, and processing results are stored.") + "</p>";

    html += "<h3>" + tr("Creating a New Project") + "</h3>";
    html += "<p>" + tr("To create a new workspace project:") + "</p>";
    html += "<ul>";
    html += "<li>" + tr("Click File > New Project or use the Project Manager") + "</li>";
    html += "<li>" + tr("Enter a project name and select a directory location") + "</li>";
    html += "<li>" + tr("The project becomes active and sets its directory as the working location") + "</li>";
    html += "<li>" + tr("All subsequent File > Open and File > Save operations default to the project directory") + "</li>";
    html += "</ul>";

    html += "<h3>" + tr("Opening an Existing Project") + "</h3>";
    html += "<p>" + tr("To work with an existing project:") + "</p>";
    html += "<ul>";
    html += "<li>" + tr("Click File > Open Project and select the project file (.tsproj)") + "</li>";
    html += "<li>" + tr("Or use File > Recent Projects to quickly access recently opened projects") + "</li>";
    html += "<li>" + tr("The project's working directory automatically becomes the default location for file operations") + "</li>";
    html += "</ul>";

    html += "<h3>" + tr("Project Benefits") + "</h3>";
    html += "<ul>";
    html += "<li>" + tr("Isolation: Keep different imaging sessions completely separate") + "</li>";
    html += "<li>" + tr("Organization: All related files (lights, darks, flats, processed results) in one place") + "</li>";
    html += "<li>" + tr("Context: Scripts and processing operations maintain awareness of project-specific paths") + "</li>";
    html += "<li>" + tr("Portability: Move entire projects to different machines with all relative paths intact") + "</li>";
    html += "</ul>";

    html += "<h3>" + tr("Working with Projects") + "</h3>";
    html += "<ul>";
    html += "<li>" + tr("Multiple Projects: Open only one project at a time; closing a project resets working directory to AppData") + "</li>";
    html += "<li>" + tr("Project Info: View current project name and directory path in the window title and statusbar") + "</li>";
    html += "<li>" + tr("Auto-save: Project state is automatically preserved between sessions") + "</li>";
    html += "<li>" + tr("Closing Project: Use File > Close Project to deactivate the current project") + "</li>";
    html += "</ul>";

    // -- History Panel --------------------------------------------------------
    html += "<h3>" + tr("History Panel") + "</h3>";
    html += "<p>" + tr("The History panel, located in the sidebar, shows the complete undo/redo stack "
                    "for the currently active image window:") + "</p>";
    html += "<ul>";
    html += "<li>" + tr("The topmost entry represents the original unmodified state of the image") + "</li>";
    html += "<li>" + tr("Each subsequent entry corresponds to a processing operation that has been applied") + "</li>";
    html += "<li>" + tr("The current state is displayed in bold so you can immediately locate your position in the history") + "</li>";
    html += "</ul>";

    // -- Navigation Controls --------------------------------------------------
    html += "<h2>" + tr("Navigation Controls") + "</h2>";
    html += "<ul>";
    html += "<li><b>" + tr("Zoom In/Out:") + "</b> " + tr("Mouse wheel or Ctrl + and Ctrl -") + "</li>";
    html += "<li><b>" + tr("Pan:") + "</b> " + tr("Click and drag to move around the image") + "</li>";
    html += "<li><b>" + tr("Fit to Window:") + "</b> " + tr("Press Ctrl+0") + "</li>";
    html += "<li><b>" + tr("1:1 Zoom:") + "</b> " + tr("Click the 1:1 button for 100% zoom") + "</li>";
    html += "<li><b>" + tr("Undo/Redo:") + "</b> " + tr("Ctrl+Z / Ctrl+Shift+Z") + "</li>";
    html += "</ul>";

    html += "<h3>" + tr("View Management") + "</h3>";
    html += "<ul>";
    html += "<li>" + tr("Open multiple image views in independent subwindows with preserved image aspect ratio") + "</li>";
    html += "<li>" + tr("Tile views automatically (grid, horizontal, vertical) for side-by-side analysis") + "</li>";
    html += "<li>" + tr("Collapse (shade) views to title bars and monitor them from the right-side preview panel") + "</li>";
    html += "<li>" + tr("Optionally hide minimized/collapsed views from the workspace while keeping quick preview access") + "</li>";
    html += "<li>" + tr("Use the magnifier (cursor-following loupe) for precise focus checks while navigating") + "</li>";
    html += "</ul>";

    // -- Display Modes --------------------------------------------------------
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

    html += "<h3>" + tr("24-bit Display Stretch") + "</h3>";
    html += "<p>" + tr("Enable in Settings for smoother gradients and reduced banding in auto-stretched previews. "
                       "Uses high-precision floating-point calculations instead of 16-bit histogram binning.") + "</p>";

    // -- Stretch Tools --------------------------------------------------------
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

    html += "<h3>" + tr("Star Stretch") + "</h3>";
    html += "<p>" + tr("Specialized tool to stretch stars while preserving their color and size:") + "</p>";
    html += "<ul>";
    html += "<li><b>" + tr("Stretch Amount:") + "</b> " + tr("Controls the non-linear expansion of stars.") + "</li>";
    html += "<li><b>" + tr("Color Boost:") + "</b> " + tr("Increases color saturation specifically for stars.") + "</li>";
    html += "<li><b>" + tr("SCNR:") + "</b> " + tr("Optional green noise removal during the stretch.") + "</li>";
    html += "</ul>";

    // -- Color Management -----------------------------------------------------
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

    html += "<h3>" + tr("PCC Distribution") + "</h3>";
    html += "<p>" + tr("Visualizes the color distribution of stars after Photometric Color Calibration:") + "</p>";
    html += "<ul>";
    html += "<li>" + tr("Shows Red/Green and Blue/Green scatter plots") + "</li>";
    html += "<li>" + tr("Compares measured star colors (Image) vs expected colors (Catalog)") + "</li>";
    html += "<li>" + tr("Useful for verifying calibration accuracy") + "</li>";
    html += "</ul>";

    html += "<h3>" + tr("SCNR (Selective Color Noise Reduction)") + "</h3>";
    html += "<p>" + tr("Removes green color cast common in OSC/DSLR images.") + "</p>";

    html += "<h3>" + tr("SPCC (Spectrophotometric Color Calibration)") + "</h3>";
    html += "<p>" + tr("Scientific color calibration based on spectral response curves and stellar photometry:") + "</p>";
    html += "<ul>";
    html += "<li>" + tr("Uses spectral database data (filters, sensors, SED curves) for physically grounded calibration") + "</li>";
    html += "<li>" + tr("Works with plate-solved images and measured star photometry") + "</li>";
    html += "<li>" + tr("Supports model fitting and optional chromatic gradient correction") + "</li>";
    html += "</ul>";

    html += "<h3>" + tr("Saturation") + "</h3>";
    html += "<p>" + tr("Adjust color saturation with protection for highlights and shadows.") + "</p>";

    html += "<h3>" + tr("Workspace Color Management") + "</h3>";
    html += "<p>" + tr("TStar includes workspace-level color management to keep display and processing behavior consistent across tools and sessions.") + "</p>";

    html += "<h3>" + tr("Catalog Background Extraction (CBE)") + "</h3>";
    html += "<p>" + tr("Advanced background extraction using catalog reference images:") + "</p>";
    html += "<ul>";
    html += "<li>" + tr("Downloads a reference image from online surveys (e.g., DSS2) based on the image's WCS coordinates") + "</li>";
    html += "<li>" + tr("Analyzes the difference between your image and the reference to build a highly accurate gradient model") + "</li>";
    html += "<li>" + tr("Requires a plate-solved image to accurately determine the sky region") + "</li>";
    html += "</ul>";

    html += "<h3>" + tr("Temperature / Tint") + "</h3>";
    html += "<p>" + tr("Adjust the color balance of the image by shifting towards warm (red) or cool (blue) tones and balancing green/magenta tints. Useful for manual white balance correction.") + "</p>";

    html += "<h3>" + tr("Magenta Correction") + "</h3>";
    html += "<p>" + tr("Specialized tool for removing magenta color casts commonly found in deep-sky astrophotography:") + "</p>";
    html += "<ul>";
    html += "<li><b>" + tr("Correction Strength:") + "</b> " + tr("Controls the intensity of magenta removal (0-100%)") + "</li>";
    html += "<li><b>" + tr("Preserve Details:") + "</b> " + tr("Maintains fine structures while removing color cast") + "</li>";
    html += "<li><b>" + tr("Channel Mode:") + "</b> " + tr("Choose between affecting all channels or specific color ranges") + "</li>";
    html += "<li>" + tr("Ideal for correcting color casts caused by light pollution filters or atmospheric conditions") + "</li>";
    html += "<li>" + tr("Works best when combined with Background Neutralization for optimal color balance") + "</li>";
    html += "</ul>";

    html += "<h3>" + tr("Selective Color Correction") + "</h3>";
    html += "<p>" + tr("Adjust colors within a specific hue range without affecting other colors:") + "</p>";
    html += "<ul>";
    html += "<li>" + tr("Select a color range using presets (Red, Green, Blue, etc.) or custom hue values") + "</li>";
    html += "<li>" + tr("Adjust Cyan/Magenta/Yellow, RGB, Luminance, Saturation, and Contrast for the selected range") + "</li>";
    html += "<li>" + tr("Use Smoothness to feather the selection edges") + "</li>";
    html += "</ul>";

    // -- AI Processing --------------------------------------------------------
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
    html += "<li>" + tr("Includes dedicated Denoise operation for AI noise cleanup") + "</li>";
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

    // -- Image Pipeline -------------------------------------------------------
    html += "<h2>" + tr("Image Pipeline") + "</h2>";
    html += "<p>" + tr("TStar includes a comprehensive pipeline for preprocessing and stacking astronomical images. "
                       "Follow these steps in order: Conversion -> Calibration -> Registration -> Stacking.") + "</p>";

    html += "<h3>" + tr("Image Conversion") + "</h3>";
    html += "<p>" + tr("Convert raw images to a standardized format before processing:") + "</p>";
    html += "<ul>";
    html += "<li><b>" + tr("RAW to FITS:") + "</b> " + tr("Converts camera RAW (CR2, NEF, ARW, etc.) to FITS format with metadata preservation") + "</li>";
    html += "<li><b>" + tr("Debayer:") + "</b> " + tr("Converts Bayer pattern (OSC/DSLR) images to full RGB color") + "</li>";
    html += "<li><b>" + tr("Format Normalization:") + "</b> " + tr("Ensures all input images are in the same format (FITS recommended for astrophotography)") + "</li>";
    html += "<li><b>" + tr("Batch Processing:") + "</b> " + tr("Convert multiple files at once") + "</li>";
    html += "<li><b>" + tr("Metadata Handling:") + "</b> " + tr("FITS keywords and image properties are automatically transferred to preserve EXIF data") + "</li>";
    html += "</ul>";

    html += "<h3>" + tr("Image Calibration") + "</h3>";
    html += "<p>" + tr("Corrects light frames using master calibration images to remove instrumental signature and improve image quality:") + "</p>";
    html += "<ul>";
    html += "<li><b>" + tr("Master Bias:") + "</b> " + tr("Removes the electronic noise floor introduced by the camera sensor") + "</li>";
    html += "<li><b>" + tr("Master Dark:") + "</b> " + tr("Removes thermal noise that accumulates over exposure time") + "</li>";
    html += "<li><b>" + tr("Master Flat:") + "</b> " + tr("Corrects vignetting and uneven illumination across the field of view") + "</li>";
    html += "</ul>";
    html += "<p>" + tr("Calibration Workflow:") + "</p>";
    html += "<ol>";
    html += "<li>" + tr("Create Master Frames: Average multiple bias/dark/flat exposures per filter") + "</li>";
    html += "<li>" + tr("Load Light Frames: Select all science images to be calibrated") + "</li>";
    html += "<li>" + tr("Apply Calibration: The Calibration Dialog applies masters in the correct order") + "</li>";
    html += "<li>" + tr("Optional Normalization: Flat field normalization normalizes the flat frame before division") + "</li>";
    html += "<li>" + tr("Output: Calibrated light frames ready for registration") + "</li>";
    html += "</ol>";
    html += "<p class=\"tip\">" + tr("Tip: Create separate master frames for each filter (e.g., L, R, G, B, Ha, OIII, SII)") + "</p>";

    html += "<h3>" + tr("Image Registration") + "</h3>";
    html += "<p>" + tr("Aligns a sequence of calibrated images to a common reference frame using star-based registration with sub-pixel accuracy:") + "</p>";
    html += "<ul>";
    html += "<li><b>" + tr("Star Detection:") + "</b> " + tr("Identifies bright stars in each image as reference points") + "</li>";
    html += "<li><b>" + tr("Transformation:") + "</b> " + tr("Computes translation, rotation, and optional scale corrections") + "</li>";
    html += "<li><b>" + tr("Sub-pixel Accuracy:") + "</b> " + tr("Aligns images with precision better than a single pixel for optimal stacking results") + "</li>";
    html += "<li><b>" + tr("Reference Frame:") + "</b> " + tr("First image in sequence used as reference (or manually selected)") + "</li>";
    html += "</ul>";
    html += "<p>" + tr("Registration Parameters:") + "</p>";
    html += "<ul>";
    html += "<li><b>" + tr("Star Threshold:") + "</b> " + tr("Adjust sensitivity for star detection (lower = more stars detected)") + "</li>";
    html += "<li><b>" + tr("Min Stars:") + "</b> " + tr("Minimum number of matching stars required for successful registration") + "</li>";
    html += "<li><b>" + tr("Max Rotation:") + "</b> " + tr("Maximum expected rotation angle between exposures") + "</li>";
    html += "</ul>";
    html += "<p class=\"tip\">" + tr("Tip: Registration must be completed before stacking; misaligned frames will produce low-quality stacks") + "</p>";

    html += "<h3>" + tr("Image Stacking") + "</h3>";
    html += "<p>" + tr("Combines registered images to reduce noise and increase signal-to-noise ratio (SNR). "
                       "Different stacking modes offer various trade-offs between noise reduction and artifact rejection:") + "</p>";
    html += "<ul>";
    html += "<li><b>" + tr("Average:") + "</b> " + tr("Simple mean of all pixels. Fast but sensitive to outliers (cosmic rays, hot pixels).") + "</li>";
    html += "<li><b>" + tr("Median:") + "</b> " + tr("Middle value of sorted pixels. Excellent outlier rejection but slower than average.") + "</li>";
    html += "<li><b>" + tr("Kappa-Sigma:") + "</b> " + tr("Statistical clipping removes pixels beyond N standard deviations from the mean. Customizable rejection strength.") + "</li>";
    html += "<li><b>" + tr("Winsorized Sigma:") + "</b> " + tr("Similar to Kappa-Sigma but replaces outliers with clipped values instead of rejecting them completely.") + "</li>";
    html += "<li><b>" + tr("Iterative Sigma:") + "</b> " + tr("Applies Kappa-Sigma rejection multiple times for aggressive outlier removal.") + "</li>";
    html += "</ul>";
    html += "<p>" + tr("Advanced Stacking Options:") + "</p>";
    html += "<ul>";
    html += "<li><b>" + tr("Image Weighting:") + "</b> " + tr("Weight frames by quality (FWHM, star count, background) before combining") + "</li>";
    html += "<li><b>" + tr("Scaling:") + "</b> " + tr("Normalize brightness variations between frames") + "</li>";
    html += "<li><b>" + tr("Normalization Mode:") + "</b> " + tr("Fast normalization or K-Sigma-based normalization per frame") + "</li>";
    html += "<li><b>" + tr("Feathering:") + "</b> " + tr("Blend overlapping image edges smoothly to reduce seaming artifacts") + "</li>";
    html += "<li><b>" + tr("Percent Rejection:") + "</b> " + tr("Simple rejection of brightest/darkest N%% of pixels") + "</li>";
    html += "<li><b>" + tr("Fast Drizzle:") + "</b> " + tr("Accelerated drizzle integration mode for faster reconstruction when undersampled data benefits from drizzle.") + "</li>";
    html += "</ul>";
    html += "<p>" + tr("Stacking Workflow:") + "</p>";
    html += "<ol>";
    html += "<li>" + tr("Load Registered Images: Select the aligned image sequence") + "</li>";
    html += "<li>" + tr("Choose Stacking Mode: Select algorithm matching your needs and data quality") + "</li>";
    html += "<li>" + tr("Configure Rejection: Set sigma levels or percentages based on frame count") + "</li>";
    html += "<li>" + tr("Configure Weighting: Optionally weight by quality metrics") + "</li>";
    html += "<li>" + tr("Configure Normalization: Select normalization method for frame scaling") + "</li>";
    html += "<li>" + tr("Execute Stack: Combine and output final integrated image") + "</li>";
    html += "</ol>";
    html += "<p class=\"tip\">" + tr("Tip: More frames allow aggressive rejection. With N=3 frames, use median. "
                                     "With N>10, can use Kappa-Sigma with ?=2.5") + "</p>";

    html += "<h3>" + tr("Output & Quality Assessment") + "</h3>";
    html += "<p>" + tr("After stacking, the result is ready for post-processing:") + "</p>";
    html += "<ul>";
    html += "<li>" + tr("Statistical Quality: Check SNR improvement from number of stacked frames") + "</li>";
    html += "<li>" + tr("Visual Inspection: Inspect for comet trails, airplane tracks, or registration errors") + "</li>";
    html += "<li>" + tr("Background Removal: Apply ABE or CBE to eliminate gradients") + "</li>";
    html += "<li>" + tr("Color Correction: Calibrate colors using PCC or manual temperature adjustment") + "</li>";
    html += "<li>" + tr("Stretching & Enhancement: Apply GHS, curves, or AI tools for final presentation") + "</li>";
    html += "</ul>";

    // -- Channel Operations ---------------------------------------------------
    html += "<h2>" + tr("Channel Operations") + "</h2>";

    html += "<h3>" + tr("Extract Channels") + "</h3>";
    html += "<p>" + tr("Splits RGB image into separate R, G, B windows.") + "</p>";

    html += "<h3>" + tr("Combine Channels") + "</h3>";
    html += "<p>" + tr("Combines separate channel images into one RGB image.") + "</p>";

    html += "<h3>" + tr("Linear Fit") + "</h3>";
    html += "<p>" + tr("Equalizes the intensity of RGB channels by matching their medians. "
                       "This is essential for achieving a neutral color balance before combining "
                       "separate channels into a color image.") + "</p>";

    html += "<h3>" + tr("Star Recomposition") + "</h3>";
    html += "<p>" + tr("Blends starless and star-only images with adjustable parameters.") + "</p>";

    html += "<h3>" + tr("Image Blending") + "</h3>";
    html += "<p>" + tr("Advanced tool to blend two images (Base and Top) with various Photoshop-style blending modes and range masking:") + "</p>";
    html += "<ul>";
    html += "<li><b>" + tr("Supported Modes:") + "</b> " + tr("Normal, Multiply, Screen, Overlay, Add, Subtract, Difference, Soft Light, Hard Light.") + "</li>";
    html += "<li><b>" + tr("Range Masking:") + "</b> " + tr("Control the range of pixels from the top image that are applied using Low/High range and feathering.") + "</li>";
    html += "<li><b>" + tr("Channel Choice:") + "</b> " + tr("Select specific channels (R, G, B, or All) when blending a monochrome image onto a color one.") + "</li>";
    html += "</ul>";

    html += "<h3>" + tr("Debayer") + "</h3>";
    html += "<p>" + tr("Converts RAW Bayer pattern images to full color.") + "</p>";

    html += "<h3>" + tr("Extract Luminance") + "</h3>";
    html += "<p>" + tr("Extracts the luminance (brightness) channel from an RGB image into a separate grayscale window. "
                       "Useful for processing the L channel independently before recombining.") + "</p>";

    html += "<h3>" + tr("Recombine Luminance") + "</h3>";
    html += "<p>" + tr("Replaces the luminance channel of an RGB image with a processed version. "
                       "Select the source luminance and target color image, then blend with adjustable intensity.") + "</p>";

    html += "<h3>" + tr("Remove Pedestal") + "</h3>";
    html += "<p>" + tr("Automatically detects and subtracts the minimum pixel value (black floor) from the image. "
                       "Essential before stretching to ensure true black levels.") + "</p>";

    html += "<h3>" + tr("Perfect Palette Picker") + "</h3>";
    html += "<p>" + tr("Create stunning narrowband composites with full control:") + "</p>";
    html += "<ul>";
    html += "<li>" + tr("Mix Ha, OIII, and SII channels with adjustable intensity") + "</li>";
    html += "<li>" + tr("Choose from presets like SHO (Hubble), HOO, HSO, Foraxx, and more") + "</li>";
    html += "<li>" + tr("Real-time preview of the color palette") + "</li>";
    html += "</ul>";

    html += "<h3>" + tr("Continuum Subtraction") + "</h3>";
    html += "<p>" + tr("Enhances narrowband details by subtracting broadband continuum/star light:") + "</p>";
    html += "<ul>";
    html += "<li>" + tr("Subtracts a scaled continuum image (or RGB channel) from narrowband data") + "</li>";
    html += "<li>" + tr("Adjust Q-Factor to control subtraction strength") + "</li>";
    html += "<li>" + tr("Reveals faint nebular structures hidden by stars") + "</li>";
    html += "</ul>";

    html += "<h3>" + tr("Align Channels") + "</h3>";
    html += "<p>" + tr("Aligns multiple open images to a reference using star registration:") + "</p>";
    html += "<ul>";
    html += "<li>" + tr("Ideal for aligning separate narrowband or RGB channel exposures") + "</li>";
    html += "<li>" + tr("Supports translation, rotation, and scale correction") + "</li>";
    html += "</ul>";

    html += "<h3>" + tr("NB \u2192 RGB Stars") + "</h3>";
    html += "<p>" + tr("Blends narrowband star channels with RGB star data:") + "</p>";
    html += "<ul>";
    html += "<li>" + tr("Load Ha, OIII, and SII narrowband star channels") + "</li>";
    html += "<li>" + tr("Optionally add OSC broadband star data") + "</li>";
    html += "<li>" + tr("Adjust Ha:OIII ratio, star stretch, and color saturation") + "</li>";
    html += "</ul>";

    html += "<h3>" + tr("Narrowband Normalization") + "</h3>";
    html += "<p>" + tr("Normalizes and balances narrowband channels for compositing:") + "</p>";
    html += "<ul>";
    html += "<li>" + tr("Supports Ha/OIII/SII with multiple blend and lightness modes") + "</li>";
    html += "<li>" + tr("Dynamic range compression with highlight recovery") + "</li>";
    html += "<li>" + tr("Compatible with pure narrowband and mixed narrowband/OSC workflows") + "</li>";
    html += "</ul>";

    html += "<h3>" + tr("Multiscale Decomposition") + "</h3>";
    html += "<p>" + tr("Decomposes the image into multiple wavelet layers for local editing:") + "</p>";
    html += "<ul>";
    html += "<li>" + tr("Each layer represents structures at a specific spatial scale") + "</li>";
    html += "<li>" + tr("Independently adjust gain, threshold, and noise reduction per layer") + "</li>";
    html += "<li>" + tr("Preview individual layers or the final recomposed result") + "</li>";
    html += "<li>" + tr("Export individual layers to new windows") + "</li>";
    html += "</ul>";

    // -- Utilities ------------------------------------------------------------
    html += "<h2>" + tr("Utilities") + "</h2>";

    html += "<h3>" + tr("Plate Solving") + "</h3>";
    html += "<p>" + tr("Identifies the exact sky coordinates of your image:") + "</p>";
    html += "<ul>";
    html += "<li>" + tr("Adds WCS (World Coordinate System) metadata") + "</li>";
    html += "<li>" + tr("Required for PCC and annotation tools") + "</li>";
    html += "<li>" + tr("Supports ASTAP integration for professional solving and automatic database path handling") + "</li>";
    html += "<li>" + tr("Can use bundled/local solver resources and catalog data for robust plate solutions") + "</li>";
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
    html += "<p>" + tr("Manual + automatic annotation system for scientific overlays and presentation exports:") + "</p>";
    html += "<ul>";
    html += "<li>" + tr("Automatic catalogs: Messier, NGC, IC, LdN, Sh2, Stars, and Constellations") + "</li>";
    html += "<li>" + tr("Optional WCS RA/Dec grid overlay with dynamic spacing") + "</li>";
    html += "<li>" + tr("Manual drawing tools: circles, rectangles, arrows, and text labels") + "</li>";
    html += "<li>" + tr("Undo/Redo editing workflow with persistent annotations across reopen") + "</li>";
    html += "<li>" + tr("Use Save with Burn Annotations to imprint overlays into exported display images") + "</li>";
    html += "</ul>";

    html += "<h3>" + tr("CLAHE") + "</h3>";
    html += "<p>" + tr("Contrast Limited Adaptive Histogram Equalization. Enhances local contrast in different regions of the image:") + "</p>";
    html += "<ul>";
    html += "<li><b>" + tr("Clip Limit:") + "</b> " + tr("Controls contrast amplification. Higher values increase contrast but may introduce noise.") + "</li>";
    html += "<li><b>" + tr("Grid Size:") + "</b> " + tr("Size of tiles for local processing. Smaller tiles increase local detail.") + "</li>";
    html += "</ul>";

    html += "<h3>" + tr("Wavescale HDR") + "</h3>";
    html += "<p>" + tr("Multiscale HDR transformation to reveal details in both highlights and shadows:") + "</p>";
    html += "<ul>";
    html += "<li>" + tr("Uses wavelet decomposition to process different structural scales") + "</li>";
    html += "<li><b>" + tr("Compression:") + "</b> " + tr("Controls dynamic range compression strength") + "</li>";
    html += "<li><b>" + tr("Scales:") + "</b> " + tr("Number of wavelet layers to process") + "</li>";
    html += "<li><b>" + tr("Mask Gamma:") + "</b> " + tr("Adjusts protection of bright areas") + "</li>";
    html += "</ul>";

    html += "<h3>" + tr("Aberration Inspector") + "</h3>";
    html += "<p>" + tr("Displays a 3x3 grid of zoomed panels from the corners, edges, and center of your image. "
                       "Useful for evaluating optical quality, coma, and field curvature across your frame.") + "</p>";

    html += "<h3>" + tr("Blink Comparator") + "</h3>";
    html += "<p>" + tr("Overlay and compare two active views by alternating their display:") + "</p>";
    html += "<ul>";
    html += "<li>" + tr("Select two views from the dropdown menus") + "</li>";
    html += "<li>" + tr("Adjust blink rate (ms) and use Play/Pause to toggle the animation") + "</li>";
    html += "<li>" + tr("Use zoom controls (+, -, Fit) and AutoStretch for better inspection") + "</li>";
    html += "</ul>";

    html += "<h3>" + tr("Correction Brush") + "</h3>";
    html += "<p>" + tr("Interactive tool to remove artifacts and blemishes:") + "</p>";
    html += "<ul>";
    html += "<li><b>" + tr("Content-Aware:") + "</b> " + tr("Uses AI inpainting for seamless removal (slower but best quality)") + "</li>";
    html += "<li><b>" + tr("Standard:") + "</b> " + tr("Uses median sampling from surrounding areas (faster)") + "</li>";
    html += "</ul>";

    html += "<h3>" + tr("Rotate & Crop") + "</h3>";
    html += "<p>" + tr("Crop and rotate the image with precision:") + "</p>";
    html += "<ul>";
    html += "<li>" + tr("Draw a crop selection directly on the image") + "</li>";
    html += "<li>" + tr("Set an aspect ratio constraint or crop freely") + "</li>";
    html += "<li>" + tr("Rotate by any angle in degrees") + "</li>";
    html += "<li>" + tr("Batch Crop: applies the same crop to all currently open images") + "</li>";
    html += "</ul>";

    html += "<h3>" + tr("Image Binning") + "</h3>";
    html += "<p>" + tr("Reduce image dimensions by combining adjacent pixels:") + "</p>";
    html += "<ul>";
    html += "<li>" + tr("Improves signal-to-noise ratio by coherent data aggregation") + "</li>";
    html += "<li>" + tr("Supported binning factors: 1x1 (no binning), 2x2, 3x3") + "</li>";
    html += "<li>" + tr("Useful for reducing file size while preserving essential data integrity") + "</li>";
    html += "<li>" + tr("Ideal for preprocessing undersampled or noisy data") + "</li>";
    html += "</ul>";

    html += "<h3>" + tr("Image Upscale") + "</h3>";
    html += "<p>" + tr("Enlarge images with selectable interpolation methods:") + "</p>";
    html += "<ul>";
    html += "<li><b>" + tr("Nearest Neighbor:") + "</b> " + tr("Fastest method, preserves sharp transitions, suitable for specific use cases") + "</li>";
    html += "<li><b>" + tr("Bilinear:") + "</b> " + tr("Good balance between speed and quality") + "</li>";
    html += "<li><b>" + tr("Bicubic:") + "</b> " + tr("Higher quality with smooth gradations (recommended for most astrophotography images)") + "</li>";
    html += "<li><b>" + tr("Lanczos4:") + "</b> " + tr("Highest quality interpolation, best for detailed astronomical data (slower)") + "</li>";
    html += "</ul>";

    html += "<h3>" + tr("Star Halo Removal") + "</h3>";
    html += "<p>" + tr("Tool to detect and subtract halos around bright stars, improving image clarity and preventing halo overlaps.") + "</p>";

    html += "<h3>" + tr("Morphology") + "</h3>";
    html += "<p>" + tr("Applies morphological operations (Erosion, Dilation, Opening, Closing) to modify the shape and size of structures in the image, such as stars. Useful for star reduction or enhancing fine details.") + "</p>";

    html += "<h3>" + tr("RAW to FITS Converter") + "</h3>";
    html += "<p>" + tr("Batch converts camera RAW files to FITS format:") + "</p>";
    html += "<ul>";
    html += "<li>" + tr("Supports all major RAW formats (Canon, Nikon, Sony, etc.)") + "</li>";
    html += "<li>" + tr("Preserves capture metadata in FITS headers") + "</li>";
    html += "</ul>";

    html += "<h3>" + tr("WCS Mosaic Builder") + "</h3>";
    html += "<p>" + tr("Assembles multiple plate-solved images into a single seamless mosaic:") + "</p>";
    html += "<ul>";
    html += "<li>" + tr("Select two or more open images — all must carry valid WCS astrometric solutions") + "</li>";
    html += "<li>" + tr("Choose a reference anchor image that defines the output projection plane and pixel scale") + "</li>";
    html += "<li>" + tr("Set the feathering distance (pixels) to control the width of the blending transition between panels") + "</li>";
    html += "</ul>";

    // -- Masks ----------------------------------------------------------------
    html += "<h2>" + tr("Masks") + "</h2>";
    html += "<p>" + tr("Create and apply masks for selective processing:") + "</p>";
    html += "<ul>";
    html += "<li><b>" + tr("Create Mask:") + "</b> " + tr("Generate masks using Binary, Range Selection, Lightness, Chrominance, Star Mask, and Color-based workflows") + "</li>";
    html += "<li><b>" + tr("Apply Mask:") + "</b> " + tr("Load and apply existing mask") + "</li>";
    html += "<li><b>" + tr("Invert Mask:") + "</b> " + tr("Invert mask selection") + "</li>";
    html += "<li><b>" + tr("Range Selection:") + "</b> " + tr("Control lower/upper limits, fuzziness, linked limits, inversion, and screening options") + "</li>";
    html += "<li><b>" + tr("Color Range:") + "</b> " + tr("Target Red/Orange/Yellow/Green/Cyan/Blue/Violet/Magenta with hue fuzziness") + "</li>";
    html += "<li><b>" + tr("Show Overlay:") + "</b> " + tr("Toggle mask visualization") + "</li>";
    html += "</ul>";

    // -- Main Toolbar and View Controls ---------------------------------------
    html += "<h2>" + tr("Main Toolbar & View Controls") + "</h2>";
    html += "<ul>";
    html += "<li><b>" + tr("Display Modes:") + "</b> " + tr("Linear, Auto Stretch, Histogram, ArcSinh, Square Root, and Logarithmic") + "</li>";
    html += "<li><b>" + tr("Channel View:") + "</b> " + tr("Switch display between RGB, R, G, and B channels") + "</li>";
    html += "<li><b>" + tr("RGB Link:") + "</b> " + tr("Toggle linked/unlinked channel stretching in preview") + "</li>";
    html += "<li><b>" + tr("AutoStretch Target:") + "</b> " + tr("Quick target-median presets for display stretch behavior") + "</li>";
    html += "<li><b>" + tr("Burn Display:") + "</b> " + tr("Permanently apply current display transform to image data") + "</li>";
    html += "<li><b>" + tr("Invert Colors:") + "</b> " + tr("Diagnostic inverted display toggle") + "</li>";
    html += "<li><b>" + tr("False Color:") + "</b> " + tr("False-color visualization for tonal/structural inspection") + "</li>";
    html += "<li><b>" + tr("Link Views:") + "</b> " + tr("Synchronize zoom/pan across multiple image windows") + "</li>";
    html += "<li><b>" + tr("View Tiling:") + "</b> " + tr("Smart grid, vertical, and horizontal tiling modes") + "</li>";
    html += "<li><b>" + tr("Magnifier Loupe:") + "</b> " + tr("Cursor-following magnifier for precise local inspection") + "</li>";
    html += "</ul>";

    // -- Effects --------------------------------------------------------------
    html += "<h2>" + tr("Effects") + "</h2>";

    html += "<h3>" + tr("AstroSpike") + "</h3>";
    html += "<p>" + tr("Adds artificial diffraction spikes to bright stars for aesthetic effect.") + "</p>";

    html += "<h3>" + tr("RAW Editor") + "</h3>";
    html += "<p>" + tr("Lightroom-style RAW editor for fast global light/color adjustments:") + "</p>";
    html += "<ul>";
    html += "<li>" + tr("Exposure, contrast, highlights, shadows, whites, and blacks controls") + "</li>";
    html += "<li>" + tr("White balance and color controls integrated with the TStar workflow") + "</li>";
    html += "</ul>";

    // -- Scripting & Automation -----------------------------------------------
    html += "<h2>" + tr("Scripting & Automation") + "</h2>";
    html += "<p>" + tr("TStar supports scripting for automating processing workflows:") + "</p>";

    html += "<h3>" + tr("TStar Scripts") + "</h3>";
    html += "<p>" + tr("Browse and execute built-in processing scripts:") + "</p>";
    html += "<ul>";
    html += "<li>" + tr("Pre-built workflows for common astrophotography tasks") + "</li>";
    html += "<li>" + tr("Preview script content before executing") + "</li>";
    html += "<li>" + tr("Double-click to run a script on the active image") + "</li>";
    html += "</ul>";

    html += "<h3>" + tr("Script Runner") + "</h3>";
    html += "<p>" + tr("Write and run custom TStar scripts (.tss):") + "</p>";
    html += "<ul>";
    html += "<li>" + tr("Define named variables to parameterize script execution") + "</li>";
    html += "<li>" + tr("Automate complex multi-step image processing workflows") + "</li>";
    html += "</ul>";

    // -- Keyboard Shortcuts ---------------------------------------------------
    html += "<h2>" + tr("Keyboard Shortcuts") + "</h2>";
    html += "<table border=\"0\" cellpadding=\"3\">";
    html += "<tr><td><span class=\"shortcut\">Ctrl+O</span></td><td>"       + tr("Open file")      + "</td></tr>";
    html += "<tr><td><span class=\"shortcut\">Ctrl+S</span></td><td>"       + tr("Save file")      + "</td></tr>";
    html += "<tr><td><span class=\"shortcut\">Ctrl+Z</span></td><td>"       + tr("Undo")           + "</td></tr>";
    html += "<tr><td><span class=\"shortcut\">Ctrl+Shift+Z</span></td><td>" + tr("Redo")           + "</td></tr>";
    html += "<tr><td><span class=\"shortcut\">Ctrl+0</span></td><td>"       + tr("Fit to window")  + "</td></tr>";
    html += "<tr><td><span class=\"shortcut\">Ctrl +</span></td><td>"       + tr("Zoom in")        + "</td></tr>";
    html += "<tr><td><span class=\"shortcut\">Ctrl -</span></td><td>"       + tr("Zoom out")       + "</td></tr>";
    html += "</table>";

    // -- Tips & Best Practices ------------------------------------------------
    html += "<h2>" + tr("Tips & Best Practices") + "</h2>";
    html += "<ul>";
    html += "<li>" + tr("Always work on a copy of your original data") + "</li>";
    html += "<li>" + tr("Use Undo frequently - every operation is reversible") + "</li>";
    html += "<li>" + tr("For best results, stretch AFTER background removal") + "</li>";
    html += "<li>" + tr("Enable RGB Link for color images to maintain color balance") + "</li>";
    html += "<li>" + tr("Check the Console panel for processing messages") + "</li>";
    html += "</ul>";

    // -- Scripting Reference Appendix -----------------------------------------
    html += "<h2>" + tr("Appendix: Scripting Commands Reference") + "</h2>";
    html += "<p>" + tr("TStar scripts (.tss) use a simple command-based syntax. "
                       "Parameters in [brackets] are optional.") + "</p>";

    html += "<h3>" + tr("File & Directory") + "</h3>";
    html += "<ul>";
    html += "<li><code>cd &lt;path&gt;</code>: "                              + tr("Change working directory")                         + "</li>";
    html += "<li><code>load &lt;filename&gt;</code>: "                        + tr("Load an image into the script context")             + "</li>";
    html += "<li><code>save &lt;filename&gt; [--32b|--16b]</code>: "          + tr("Save current image (default 32-bit float)")         + "</li>";
    html += "<li><code>close</code>: "                                        + tr("Close the current image")                           + "</li>";
    html += "</ul>";

    html += "<h3>" + tr("Stacking & Sequencing") + "</h3>";
    html += "<ul>";
    html += "<li><code>stack &lt;prefix&gt; [method] [rejection] [sigma] [--out=...]</code>: "  + tr("Stack a sequence")             + "</li>";
    html += "<li><code>calibrate &lt;prefix&gt; [--bias=...] [--dark=...] [--flat=...]</code>: " + tr("Calibrate sequence")           + "</li>";
    html += "<li><code>register &lt;prefix&gt; [--drizzle] [--norotation]</code>: "             + tr("Align images in sequence")      + "</li>";
    html += "<li><code>autostack &lt;project_path&gt;</code>: "                                  + tr("Run full automated pipeline")   + "</li>";
    html += "</ul>";

    html += "<h3>" + tr("Preprocessing & Recovery") + "</h3>";
    html += "<ul>";
    html += "<li><code>debayer [--pattern=...] [--algo=...]</code>: "                            + tr("Debayer current image or sequence")    + "</li>";
    html += "<li><code>background [--degree=...]</code>: "                                       + tr("Remove gradients (ABE)")               + "</li>";
    html += "<li><code>starnet [--nostarmask] [--stride=...]</code>: "                           + tr("Run StarNet++ star removal")           + "</li>";
    html += "<li><code>rgbcomp &lt;r&gt; &lt;g&gt; &lt;b&gt; [-out=...]</code>: "               + tr("Create RGB from mono channels")        + "</li>";
    html += "</ul>";

    html += "<h3>" + tr("Image math & Geometry") + "</h3>";
    html += "<ul>";
    html += "<li><code>pm &lt;expression&gt;</code>: "                                                                     + tr("Execute PixelMath formula")    + "</li>";
    html += "<li><code>rotate &lt;angle&gt;</code> | <code>resample &lt;factor&gt;</code>: "                                + tr("Geometric transformations")    + "</li>";
    html += "<li><code>crop [x y w h]</code>: "                                                                            + tr("Crop image to box or selection") + "</li>";
    html += "<li><code>fmul &lt;val&gt;</code> | <code>offset &lt;val&gt;</code> | <code>neg</code>: "                     + tr("Basic pixel math")             + "</li>";
    html += "</ul>";

    // -- Footer ---------------------------------------------------------------
    html += "<p style=\"margin-top: 30px; color: #888;\">";
    html += "TStar (C) 2026 Fabio Tempera | "
            "<a href=\"https://github.com/Ft2801/TStar\" style=\"color: #4a9eff;\">GitHub</a>";
    html += "</p>";

    return html;
}