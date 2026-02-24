
#include "StackingEngine.h"


#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <algorithm>
#include <cmath>
#include <cstring>

#ifdef _OPENMP
#include <omp.h>
#endif

#include "CosmeticCorrection.h"
#include "../io/FitsLoader.h"
#include "Distortion.h"
#include "StackDataBlock.h"
#include "../preprocessing/Debayer.h"
#include "StackingInterpolation.h"
#include "InlineRejection.h"
#include "../core/ThreadState.h"
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <emmintrin.h>
#endif


namespace Stacking {

using namespace InlineRejection;

//=============================================================================
// CONSTRUCTOR / DESTRUCTOR
//=============================================================================

StackingEngine::StackingEngine(QObject* parent)
    : QObject(parent)
{
}

StackingEngine::~StackingEngine() = default;

//=============================================================================
// MAIN ENTRY POINT
//=============================================================================

void StackingEngine::configureForMasterBias(StackingArgs& args) {
    args.params.method = Method::Mean;
    args.params.rejection = Rejection::Winsorized;
    args.params.normalization = NormalizationMethod::None;
    args.params.weighting = WeightingType::None;
    
    // Bias frames usually have very little signal variance, so standard sigma clipping
    // or winsorized with tighter bounds is best.
    args.params.sigmaLow = 3.0f;
    args.params.sigmaHigh = 3.0f;
    
    // No alignment/registration for calibration frames
    args.params.maximizeFraming = false;
    args.params.upscaleAtStacking = false;
    args.params.drizzle = false;
}

void StackingEngine::configureForMasterDark(StackingArgs& args) {
    args.params.method = Method::Mean;
    args.params.rejection = Rejection::Winsorized; // Essential for hot pixel rejection
    args.params.normalization = NormalizationMethod::None; // Never normalize Darks
    args.params.weighting = WeightingType::None; // Exposure weighting is irrelevant for same-exposure darks
    
    // Darks are prone to hot pixels (high outliers)
    // Low sigma can be loose (thermal noise), High sigma strict (hot pixels)
    args.params.sigmaLow = 3.0f;
    args.params.sigmaHigh = 3.0f; 
    
    args.params.maximizeFraming = false;
    args.params.upscaleAtStacking = false;
    args.params.drizzle = false;
}

void StackingEngine::configureForMasterFlat(StackingArgs& args) {
    args.params.method = Method::Mean;
    args.params.rejection = Rejection::Winsorized;
    
    // Flats MUST be normalized to account for varying light source intensity
    args.params.normalization = NormalizationMethod::Multiplicative; 
    
    args.params.weighting = WeightingType::None;
    
    args.params.sigmaLow = 3.0f;
    args.params.sigmaHigh = 3.0f;
    
    args.params.maximizeFraming = false;
    args.params.upscaleAtStacking = false;
    args.params.drizzle = false;
}

StackResult StackingEngine::execute(StackingArgs& args) {
    m_cancelled = false;
    
    // Validate inputs
    if (!args.sequence || !args.sequence->isValid()) {
        args.log(tr("Error: Invalid or empty sequence"), "red");
        return StackResult::SequenceError;
    }
    
    // Filter images
    if (!filterImages(args)) {
        args.log(tr("Error: No images selected for stacking"), "red");
        return StackResult::GenericError;
    }
    args.log(tr("Stacking %1 images using %2 method...")
            .arg(args.nbImagesToStack)
            .arg(methodToString(args.params.method)), "green");
    

    if (args.params.useCometMode) {
        emit logMessage(tr("Preparing Comet Alignment..."), "neutral");
        
        // Ensure effectiveRegs is resized
        int totalImages = args.sequence->count();
        args.effectiveRegs.resize(totalImages);
        
        // Get Reference Comet Position
        // If Ref Image has explicit comet position, use it.
        // Otherwise we define the Reference Comet Position as "where it would be at Ref Date" (usually 0 shift relative to stars if we pick Ref Frame as Ref)
        
        double refCometX = 0.0;
        double refCometY = 0.0;
        bool hasRefPos = false;
        
        const auto& refImg = args.sequence->image(args.params.refImageIndex);
        if (refImg.registration.cometX > 0 && refImg.registration.cometY > 0) {
            refCometX = refImg.registration.cometX;
            refCometY = refImg.registration.cometY;
            hasRefPos = true;
        }

        // Velocity fallback data
        QDateTime refDate = QDateTime::fromString(args.params.refDate, Qt::ISODate);
        if (!refDate.isValid()) {
             refDate = QDateTime::fromString(refImg.metadata.dateObs, Qt::ISODate); 
        }

        for (int idx : args.imageIndices) {
             RegistrationData reg = args.sequence->image(idx).registration;
             const auto& imgInfo = args.sequence->image(idx);
             
             // Strategy:
             // 1. If we have explicit comet positions (Position Mode):
             //    - Project Image Comet (raw) -> Ref Frame (using H_star)
             //    - Calculate Delta = RefComet - ProjectedComet
             //    - Add Delta to H_star translation
             
             // 2. If no positions, use Velocity (Velocity Mode):
             //    - Calculate Delta based on (Time - RefTime) * Velocity
             //    - Subtract Delta from H_star (to "freeze" the comet, we move the frame opposite to comet motion?)
             //      Wait. If Comet moves +10px. To keep it centered, we must shift the image -10px. 
             //      So subtract shift.
             
             bool usedPosition = false;
             
             if (hasRefPos && reg.cometX > 0 && reg.cometY > 0) {
                 // Project current comet position to Reference Frame using Star Alignment
                 QPointF projected = reg.transform(reg.cometX, reg.cometY);
                 
                 // We want Projected to be at RefCometX/Y
                 // Shift = Target - Current
                 double dx = refCometX - projected.x();
                 double dy = refCometY - projected.y();
                 
                 // Apply shift to Homography (translation components are [0][2] and [1][2])
                 // H' = T(dx,dy) * H
                 reg.H[0][2] += dx;
                 reg.H[1][2] += dy;
                 reg.shiftX += dx;
                 reg.shiftY += dy;
                 
                 usedPosition = true;
             }
             
             if (!usedPosition) {
                 // Fallback to velocity
                 QDateTime imgDate = QDateTime::fromString(imgInfo.metadata.dateObs, Qt::ISODate);
                 
                 if (imgDate.isValid() && refDate.isValid()) {
                     qint64 secs = refDate.secsTo(imgDate);
                     double hours = secs / 3600.0;
                     
                     // Comet displacement relative to stars
                     double comDisX = hours * args.params.cometVx;
                     double comDisY = hours * args.params.cometVy;
                     
                     // To align on comet, shift image so comet stays fixed.
                     // If comet moves +X, we shift image -X relative to star-aligned.
                     reg.H[0][2] -= comDisX;
                     reg.H[1][2] -= comDisY;
                     reg.shiftX -= comDisX;
                     reg.shiftY -= comDisY;
                 }
             }
             
             args.effectiveRegs[idx] = reg;
        }
    } else {
        args.effectiveRegs.clear();
    }

    // Compute normalization if needed
    if (args.params.hasNormalization()) {
        if (!computeNormalization(args)) {
            if (m_cancelled) return StackResult::CancelledError;
            args.log(tr("Warning: Normalization computation failed, continuing without"), "salmon");
        }
    }
    
    if (m_cancelled) return StackResult::CancelledError;
    
    // Compute weights if enabled
    if (args.params.weighting != WeightingType::None) {
        args.progress(tr("Computing image weights..."), -1);
        if (!Weighting::computeWeights(*args.sequence, args.params.weighting, args.coefficients, args.weights)) {
             args.log(tr("Warning: Weight computation failed, continuing without weighting"), "salmon");
             args.weights.clear();
        }
    }

    // 4. Auto-detect Bayer Pattern if needed
    if (args.params.debayer && (args.params.bayerPattern == Preprocessing::BayerPattern::Auto || 
                                args.params.bayerPattern == Preprocessing::BayerPattern::None)) {
        // Try to get pattern from reference image
        auto refInfo = args.sequence->image(args.params.refImageIndex);
        QString patternStr = refInfo.metadata.bayerPattern;
        
        if (patternStr.isEmpty()) {
            // Check headers directly if metadata is empty
            patternStr = refInfo.metadata.xisfProperties.value("BayerPattern").toString();
        }

        if (!patternStr.isEmpty()) {
            if (patternStr == "RGGB") args.params.bayerPattern = Preprocessing::BayerPattern::RGGB;
            else if (patternStr == "BGGR") args.params.bayerPattern = Preprocessing::BayerPattern::BGGR;
            else if (patternStr == "GBRG") args.params.bayerPattern = Preprocessing::BayerPattern::GBRG;
            else if (patternStr == "GRBG") args.params.bayerPattern = Preprocessing::BayerPattern::GRBG;
            
            if (args.params.bayerPattern != Preprocessing::BayerPattern::None) {
                args.log(tr("Auto-detected Bayer pattern: %1").arg(patternStr), "blue");
            }
        }
    }

    if (args.params.useCosmetic) {
        args.progress(tr("Computing Cosmetic Correction Map..."), -1);
        
        // Check if we have a pre-computed bad pixel map file
        if (!args.params.cosmeticMapFile.isEmpty()) {
            // Load map from file - expected format: master dark FITS/TIFF
            ImageBuffer masterDark;
            QString ext = QFileInfo(args.params.cosmeticMapFile).suffix().toLower();
            bool loaded = false;
            
            if (ext == "fit" || ext == "fits" || ext == "fts") {
                loaded = FitsLoader::load(args.params.cosmeticMapFile, masterDark);
            }
            // Add other formats if needed
            
            if (loaded && masterDark.isValid()) {
                args.cosmeticMap = CosmeticCorrection::findDefects(
                    masterDark,
                    args.params.cosmeticHotSigma,
                    args.params.cosmeticColdSigma,
                    args.params.cosmeticIsCFA
                );
                
                if (args.cosmeticMap.isValid()) {
                    args.log(tr("Cosmetic Correction: Found %1 defects in master dark")
                            .arg(args.cosmeticMap.count), "blue");
                } else {
                    args.log(tr("Warning: No defects found in master dark, cosmetic correction disabled"), "salmon");
                    args.params.useCosmetic = false;
                }
            } else {
                args.log(tr("Warning: Could not load cosmetic map file: %1")
                        .arg(args.params.cosmeticMapFile), "salmon");
                args.params.useCosmetic = false;
            }
        } else {
            args.log(tr("Warning: Cosmetic correction enabled but no master dark provided"), "salmon");
            args.params.useCosmetic = false;
        }
    }
    
    // Initialize rejection maps if requested
    if (args.params.createRejectionMaps) {
         int width, height, offsetX, offsetY;
         computeOutputDimensions(args, width, height, offsetX, offsetY);
         args.rejectionMaps.initialize(width, height, args.sequence->channels(),
                                      true, true);
    }
    
    // Drizzle Integration
    if (args.params.drizzle) {
        if (args.params.hasRejection()) {
            args.log(tr("Drizzle enabled: Running first pass for rejection..."), "blue");
            args.params.createRejectionMaps = true;
            
            // Run mean stacking to get rejection maps and reference
            StackResult res = stackMean(args);
            if (res != StackResult::OK) return res;
            
            if (m_cancelled) return StackResult::CancelledError;
        }
        
        args.log(tr("Starting Drizzle stacking (Scale: %1x, PixFrac: %2)...")
                .arg(args.params.drizzleScale)
                .arg(args.params.drizzlePixFrac), "blue");
        return stackDrizzle(args);
    }
    
    // Execute appropriate stacking method
    StackResult result;
    switch (args.params.method) {
        case Method::Sum:
            result = stackSum(args);
            break;
        case Method::Mean:
        case Method::Median: // Median now uses the optimized block engine
            result = stackMean(args);
            break;
        case Method::Max:
            result = stackMax(args);
            break;
        case Method::Min:
            result = stackMin(args);
            break;
        default:
            result = StackResult::GenericError;
    }
    
    if (result != StackResult::OK) {
        if (m_cancelled) {
            args.log(tr("Stacking cancelled by user"), "salmon");
        } else {
            args.log(tr("Stacking failed"), "red");
        }
        return result;
    }
    
    // Post-processing
    if (args.params.outputNormalization) {
        args.progress(tr("Normalizing output..."), -1);
        Normalization::normalizeOutput(args.result);
    }
    
    if (args.params.equalizeRGB && args.result.channels() >= 3) {
        args.progress(tr("Equalizing RGB channels..."), -1);
        Normalization::equalizeRGB(args.result);
    }
    
    // Update metadata
    updateMetadata(args);
    
    // Generate summary
    QString summary = generateSummary(args);
    args.log(summary, "green");
    
    emit finished(true);
    return StackResult::OK;
}

void StackingEngine::requestCancel() {
    m_cancelled = true;
}

//=============================================================================
// IMAGE FILTERING
//=============================================================================

bool StackingEngine::filterImages(StackingArgs& args) {
    ImageSequence* seq = args.sequence;
    
    // Apply filter criteria
    seq->applyFilter(args.params.filter, args.params.filterMode, 
                     args.params.filterParameter);
    
    // Get filtered indices
    args.imageIndices = seq->getFilteredIndices();
    args.nbImagesToStack = static_cast<int>(args.imageIndices.size());
    
    return args.nbImagesToStack >= 2;  // Need at least 2 images
}

//=============================================================================
// NORMALIZATION
//=============================================================================

bool StackingEngine::computeNormalization(StackingArgs& args) {
    args.progress(tr("Computing normalization coefficients..."), -1);
    
    int totalImages = args.nbImagesToStack;
    auto progressCallback = [this, &args, totalImages](const QString& msg, double pct) mutable {
        args.progress(msg, pct);
        emit progressChanged(msg, pct);
        
        // Log explicitly
        int current = static_cast<int>(pct * totalImages + 0.5);
        if (current > 0 && current <= totalImages) {
             // args.log is thread-safe (emits signal)
             // Use "neutral" or "white" to ensure visibility
             args.log(tr("Normalized image %1/%2").arg(current).arg(totalImages), "white"); 
        }
    };
    
    return Normalization::computeCoefficients(
        *args.sequence,
        args.params,
        args.coefficients,
        progressCallback
    );
}

//=============================================================================
// OUTPUT PREPARATION
//=============================================================================

bool StackingEngine::prepareOutput(StackingArgs& args, int width, int height, int channels) {
    // Determine output bit depth
    args.result = ImageBuffer(width, height, channels);
    
    // Initialize to zero
    std::memset(args.result.data().data(), 0, 
                sizeof(float) * width * height * channels);
    
    return args.result.isValid();
}

void StackingEngine::computeOutputDimensions(const StackingArgs& args,
                                              int& width, int& height,
                                              int& offsetX, int& offsetY) {
    const ImageSequence* seq = args.sequence;
    
    if (args.params.maximizeFraming && seq->hasRegistration()) {
        // Calculate bounding box that encompasses all images
        double minX = 0, maxX = seq->width();
        double minY = 0, maxY = seq->height();
        
        for (int idx : args.imageIndices) {
            const auto& img = seq->image(idx);
            double dx = img.registration.shiftX;
            double dy = img.registration.shiftY;
            
            minX = std::min(minX, dx);
            minY = std::min(minY, dy);
            maxX = std::max(maxX, img.width + dx);
            maxY = std::max(maxY, img.height + dy);
        }
        
        width = static_cast<int>(std::ceil(maxX - minX));
        height = static_cast<int>(std::ceil(maxY - minY));
        offsetX = static_cast<int>(std::floor(minX));
        offsetY = static_cast<int>(std::floor(minY));
    } else {
        // Use reference image dimensions
        width = seq->width();
        height = seq->height();
        offsetX = 0;
        offsetY = 0;
    }
    
    // Apply upscaling if requested
    if (args.params.upscaleAtStacking) {
        width *= 2;
        height *= 2;
        offsetX *= 2;
        offsetY *= 2;
    }
}

bool StackingEngine::getShiftedPixel(const ImageBuffer& buffer,
                                      int x, int y, int channel,
                                      const RegistrationData& reg,
                                      int offsetX, int offsetY,
                                      float& outValue,
                                      int srcOffsetX, int srcOffsetY) {
    if (reg.isShiftOnly()) {
        
        int shiftX = static_cast<int>(std::round(reg.shiftX - offsetX));
        int shiftY = static_cast<int>(std::round(reg.shiftY - offsetY));
        
        int nx = x - shiftX - srcOffsetX;
        int ny = y - shiftY - srcOffsetY;
        
        if (nx < 0 || nx >= buffer.width() || ny < 0 || ny >= buffer.height()) {
            return false;
        }
        
        size_t idx = static_cast<size_t>(channel) * buffer.width() * buffer.height() +
                     static_cast<size_t>(ny) * buffer.width() + nx;
        outValue = buffer.data().data()[idx];
        return true;
    } else {
        
        double H[3][3];
        std::memcpy(H, reg.H, 9 * sizeof(double));
        
        // Invert 3x3
        double det = H[0][0] * (H[1][1] * H[2][2] - H[2][1] * H[1][2]) -
                     H[0][1] * (H[1][0] * H[2][2] - H[1][2] * H[2][0]) +
                     H[0][2] * (H[1][0] * H[2][1] - H[1][1] * H[2][0]);
                     
        if (std::abs(det) < 1e-9) return false; // Singular
        
        double invDet = 1.0 / det;
        
        double invH[3][3];
        invH[0][0] = (H[1][1] * H[2][2] - H[2][1] * H[1][2]) * invDet;
        invH[0][1] = (H[0][2] * H[2][1] - H[0][1] * H[2][2]) * invDet;
        invH[0][2] = (H[0][1] * H[1][2] - H[0][2] * H[1][1]) * invDet;
        invH[1][0] = (H[1][2] * H[2][0] - H[1][0] * H[2][2]) * invDet;
        invH[1][1] = (H[0][0] * H[2][2] - H[0][2] * H[2][0]) * invDet;
        invH[1][2] = (H[1][0] * H[0][2] - H[0][0] * H[1][2]) * invDet;
        invH[2][0] = (H[1][0] * H[2][1] - H[2][0] * H[1][1]) * invDet;
        invH[2][1] = (H[2][0] * H[0][1] - H[0][0] * H[2][1]) * invDet;
        invH[2][2] = (H[0][0] * H[1][1] - H[1][0] * H[0][1]) * invDet;
        
        // Apply inverse transform to output pixel center
        double rx = x + offsetX + 0.5; 
        double ry = y + offsetY + 0.5;
        
        // If Reference has Distortion (Reverse SIP Ref -> Linear Ref), apply meaningful check here
        // Current model: Reference is flat. Output corresponds to Reference.
        // H maps Input Linear -> Output (Reference).
        // We need Input Linear = H_inv * Output.
        
        double z_in = invH[2][0] * rx + invH[2][1] * ry + invH[2][2];
        double scale_in = (std::abs(z_in) > 1e-9) ? 1.0 / z_in : 1.0;
        
        double u_lin = (invH[0][0] * rx + invH[0][1] * ry + invH[0][2]) * scale_in;
        double v_lin = (invH[1][0] * rx + invH[1][1] * ry + invH[1][2]) * scale_in;
        
        double u_pix = u_lin;
        double v_pix = v_lin;
        
        if (reg.hasDistortion && reg.sipOrder > 0) {
             // Apply Reverse Distortion
             double du = Distortion::computePoly(u_lin, v_lin, reg.sipOrder, reg.sipAP);
             double dv = Distortion::computePoly(u_lin, v_lin, reg.sipOrder, reg.sipBP);
             u_pix += du;
             v_pix += dv;
        }
        
        u_pix -= 0.5;
        v_pix -= 0.5;
        
        // Apply source offset to map to buffer coordinates
        u_pix -= srcOffsetX;
        v_pix -= srcOffsetY;
        
        float val = getInterpolatedPixel(buffer, u_pix, v_pix, channel);
        
        if (val < 0.0f) return false; // convention for out of bounds
        
        outValue = val;
        return true;
    }
}

float StackingEngine::getInterpolatedPixel(const ImageBuffer& buffer, 
                                          double x, double y, int channel) {
    // Bicubic Interpolation with Edge Clamping to prevent black borders.
    // Pixels near edges use clamped coordinates instead of returning 0.
    
    int width = buffer.width();
    int height = buffer.height();

    // Reject pixels clearly out of bounds (more than 1 pixel outside)
    if (x < -1.0 || x >= width || y < -1.0 || y >= height) return -1.0f;

    int x0 = static_cast<int>(std::floor(x));
    int y0 = static_cast<int>(std::floor(y));
    
    float dx = static_cast<float>(x - x0);
    float dy = static_cast<float>(y - y0);
    
    float t = dx;
    float t2 = t * t;
    float t3 = t2 * t;
    
    float w0 = -0.5f*t3 + t2 - 0.5f*t;
    float w1 = 1.5f*t3 - 2.5f*t2 + 1.0f;
    float w2 = -1.5f*t3 + 2.0f*t2 + 0.5f*t;
    float w3 = 0.5f*t3 - 0.5f*t2;
    
    float arr[4];

    for (int j = 0; j < 4; ++j) {
        int py = std::clamp(y0 - 1 + j, 0, height - 1);
        
        int px0 = std::clamp(x0 - 1, 0, width - 1);
        int px1 = std::clamp(x0,     0, width - 1);
        int px2 = std::clamp(x0 + 1, 0, width - 1);
        int px3 = std::clamp(x0 + 2, 0, width - 1);
        
        float r0 = buffer.value(px0, py, channel);
        float r1 = buffer.value(px1, py, channel);
        float r2 = buffer.value(px2, py, channel);
        float r3 = buffer.value(px3, py, channel);
        
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
        __m128 mw = _mm_set_ps(w3, w2, w1, w0);
        float rv[4] = {r0, r1, r2, r3};
        __m128 mr = _mm_loadu_ps(rv);
        __m128 mprod = _mm_mul_ps(mr, mw);
        
        __m128 shuf = _mm_shuffle_ps(mprod, mprod, _MM_SHUFFLE(3, 3, 1, 1));
        __m128 sums = _mm_add_ps(mprod, shuf);
        __m128 shuf2 = _mm_movehl_ps(shuf, sums);
        sums = _mm_add_ss(sums, shuf2);
        _mm_store_ss(&arr[j], sums);
#else
        arr[j] = r0 * w0 + r1 * w1 + r2 * w2 + r3 * w3;
#endif
    }
    
    float res = cubicHermite(arr[0], arr[1], arr[2], arr[3], dy);

    // Prevent negative ringing (which creates black artifacts in averaging)
    if (res < 0.0f) res = 0.0f;
    
    return res;
}

//=============================================================================
// SUM STACKING
//=============================================================================

StackResult StackingEngine::stackSum(StackingArgs& args) {
    int outputWidth, outputHeight, offsetX, offsetY;
    computeOutputDimensions(args, outputWidth, outputHeight, offsetX, offsetY);
    
    int channels = args.sequence->channels();
    
    if (!prepareOutput(args, outputWidth, outputHeight, channels)) {
        return StackResult::AllocError;
    }
    
    // Accumulator for sums (use double for precision)
    // Use Interleaved layout for sums to match output
    size_t totalPixels = static_cast<size_t>(outputWidth) * outputHeight * channels;
    std::vector<double> sums(totalPixels, 0.0);
    std::vector<int> counts(totalPixels, 0);
    
    int processed = 0;
    double totalExposure = 0.0;
    
    for (int idx : args.imageIndices) {
        if (m_cancelled) return StackResult::CancelledError;
        
        args.progress(tr("Stacking image %1/%2...")
                     .arg(processed + 1).arg(args.nbImagesToStack),
                     static_cast<double>(processed) / args.nbImagesToStack);
        
        // Load image
        ImageBuffer buffer;
        QFileInfo fi(args.sequence->image(idx).filePath);
        if (!fi.exists()) {
             args.log(tr("Error: File not found: %1").arg(fi.filePath()), "salmon");
             continue;
        }
        
        // Use native separators to minimize CFITSIO parsing issues
        QString nativePath = QDir::toNativeSeparators(fi.absoluteFilePath());
        if (!args.sequence->readImage(idx, buffer)) { 
            args.log(tr("Warning: Failed to read image %1, skipping").arg(idx), "salmon");
            continue;
        }
        
        const auto& imgInfo = args.sequence->image(idx);
        totalExposure += imgInfo.exposure;
        
        // Scale registration for upscaling if needed
        RegistrationData reg;
        if (!args.effectiveRegs.empty() && idx < static_cast<int>(args.effectiveRegs.size())) {
            reg = args.effectiveRegs[idx];
        } else {
            reg = imgInfo.registration;
        }
        if (args.params.upscaleAtStacking) {
            reg.shiftX *= 2.0;
            reg.shiftY *= 2.0;
        }

        // Add pixels
        #pragma omp parallel for collapse(2)
        for (int y = 0; y < outputHeight; ++y) {
            for (int x = 0; x < outputWidth; ++x) {
                for (int c = 0; c < channels; ++c) {
                    float value;
                    
                    if (getShiftedPixel(buffer, x, y, c, reg, offsetX, offsetY, value)) {
                        // Interleaved Indexing: (y * W + x) * Ch + c
                        size_t outIdx = (static_cast<size_t>(y) * outputWidth + x) * channels + c;
                        
                        // Atomic update needed if parallel? 
                        // collapse(2) effectively loops y,x. 'outIdx' is unique for (y,x,c).
                        // But c is inner loop. 
                        // Wait, counts logic? 
                        // If we loop y,x parallel, different threads maximize spread. 
                        // No race condition on 'outIdx' if we loop x,y.
                        
                        sums[outIdx] += value;
                        counts[outIdx]++;
                    }
                }
            }
        }
        
        processed++;
    }
    
    // Normalize by max value (or count if desired)
    float* output = args.result.data().data();
    
    // Find max sum for normalization (parallel reduction)
    double maxSum = 0.0;
    #ifdef _OPENMP
    #pragma omp parallel for reduction(max:maxSum)
    #endif
    for (size_t i = 0; i < totalPixels; ++i) {
        if (sums[i] > maxSum) maxSum = sums[i];
    }
    
    if (maxSum > 0) {
        double invMax = 1.0 / maxSum;
        #ifdef _OPENMP
        #pragma omp parallel for
        #endif
        for (size_t i = 0; i < totalPixels; ++i) {
            output[i] = static_cast<float>(sums[i] * invMax);
        }
    }
    
    args.log(tr("Sum stacking complete. Total exposure: %1s").arg(totalExposure), "");
    
    return StackResult::OK;
}

//=============================================================================
// MEAN STACKING WITH REJECTION 
//=============================================================================

static int64_t computeMaxRowsInMemory(int width, int height, int channels, int nbImages) {
    // Use up to 4GB — halves block count vs 2GB, cutting I/O reads by 2×.
    // Actual peak usage ≈ nbThreads × rowsPerBlock × width × nbImages × ch × 4 bytes.
    constexpr int64_t maxMemoryMB = 4096;
    constexpr int64_t bytesPerMB = 1024 * 1024;
    
    // Linked Rejection: We process R,G,B rows simultaneously.
    // So total task size is just height (rows).
    int64_t totalRows = static_cast<int64_t>(height);
    int elemSize = sizeof(float);
    
    // Bytes per row includes ALL channels
    int64_t bytesPerRow = static_cast<int64_t>(width) * nbImages * channels * elemSize;
    
    // Also need stack/scratch buffers per thread 
    // (StackDataBlock has stackRGB[3], rejectedRGB[3] etc -> significant)
    // Add overhead: ~ nbImages * channels * 4 * sizeof(float|int) per pixel? No, per thread.
    // Scratch buffer is reusable. But bytesPerRow is large.
    
    if (bytesPerRow == 0) return 1;

    int64_t maxRows = (maxMemoryMB * bytesPerMB) / bytesPerRow;
    
    if (maxRows > totalRows) return totalRows;
    if (maxRows < 1) return 1; // At minimum, process 1 row
    return maxRows;
}

StackResult StackingEngine::stackMean(StackingArgs& args) {
    int outputWidth, outputHeight, offsetX, offsetY;
    computeOutputDimensions(args, outputWidth, outputHeight, offsetX, offsetY);
    
    // Determine effective channels
    int channels = args.sequence->channels();
    if (args.params.debayer) {
        channels = 3; // Force RGB output for Debayer mode
    }
    int nbImages = args.nbImagesToStack;
    
    // ===== FAST C OPTIMIZATION =====
    // Now supports: Rejection (Sigma, MAD, Percentile, GESDT), Weighted Mean, Homographies
    if (!args.params.debayer &&
        !args.params.upscaleAtStacking &&
        !args.params.drizzle &&
        args.params.rejection != Rejection::LinearFit &&
        args.params.rejection != Rejection::SigmaMedian) {
        
        // Try to use accelerated C function (handles normalization, weights, rejection, registration)
        // StackResult cResult = tryStackMeanC(args, outputWidth, outputHeight, channels);
        // if (cResult == StackResult::OK) {
        //     args.log(tr("Mean stacking complete (accelerated C)"), "green");
        //     return StackResult::OK;
        // }
    }
    
    if (!prepareOutput(args, outputWidth, outputHeight, channels)) {
        args.log("Error: Failed to allocate output buffer", "red");
        return StackResult::AllocError;
    }
    
    QString methodStr = (args.params.method == Method::Median) ? "Median" : "Mean";
    args.log(tr("Starting %1 stacking of %2 images (%3 x %4, %5 channels)...")
             .arg(methodStr).arg(nbImages).arg(outputWidth).arg(outputHeight).arg(channels), "neutral");
    
    int nbThreads = 1;
#ifdef _OPENMP
    nbThreads = omp_get_max_threads();
    if (nbThreads > 8) nbThreads = 8; // Limit for memory reasons
#endif
    
    long maxRowsInMemory = computeMaxRowsInMemory(outputWidth, outputHeight, channels, nbImages);
    
    std::vector<ImageBlock> blocks;
    int largestBlockHeight;
    if (computeParallelBlocks(blocks, maxRowsInMemory, outputWidth, outputHeight, 
                               channels, nbThreads, largestBlockHeight) != 0) {
        args.log("Error: Failed to compute parallel blocks", "red");
        return StackResult::GenericError;
    }
    
    args.log(tr("Using %1 parallel blocks of max %2 rows each")
             .arg(blocks.size()).arg(largestBlockHeight), "neutral");
    int poolSize = nbThreads;
    // No per-frame pixel buffer: images are preloaded into RAM.
    // pixelsPerBlock=0 → blockDataSize=0, so allocate() only reserves the tiny
    // stackRGB/rejection scratch arrays (a few KB per thread).
    size_t pixelsPerBlock = 0;
    
    std::vector<StackDataBlock> dataPool(poolSize);
    bool useFeathering = (args.params.featherDistance > 0);
    for (int i = 0; i < poolSize; ++i) {
        if (!dataPool[i].allocate(nbImages, pixelsPerBlock, channels,
                                   args.params.rejection, useFeathering, false)) {
            args.log("Error: Failed to allocate data pool", "red");
            return StackResult::AllocError;
        }
    }
    
    args.log(tr("Allocated %1 per-thread rejection scratch blocks (stackRGB only, no pixel buffers)")
             .arg(poolSize), "neutral");
    
    // =====================================================================
    // PRELOAD ALL REGISTERED IMAGES INTO RAM
    // =====================================================================
    // Replaces 112+ partial readImageRegion() disk seeks with 14 sequential
    // full-image reads.  Net RAM cost is the same as the old pix[] buffers
    // (~4 GB for 14 × 290 MB images) while I/O drops by ~8×.
    // =====================================================================
    using Preprocessing::BayerPattern;
    args.log(tr("Preloading %1 registered images into RAM...").arg(nbImages), "neutral");
    std::vector<ImageBuffer> preloadedImages(nbImages);
    for (int frame = 0; frame < nbImages; ++frame) {
        int imgIdx = args.imageIndices[frame];
        if (!args.sequence->readImage(imgIdx, preloadedImages[frame])) {
            args.log(tr("Error: Failed to preload image %1").arg(imgIdx), "salmon");
            return StackResult::SequenceError;
        }
        // Debayer at preload time so the block loop sees fully-demosaiced data.
        if (args.params.debayer && args.params.bayerPattern != BayerPattern::None) {
            ImageBuffer debayered;
            bool ok = false;
            if (args.params.debayerMethod == Preprocessing::DebayerAlgorithm::Bilinear) {
                ok = Preprocessing::Debayer::bilinear(preloadedImages[frame], debayered, args.params.bayerPattern);
            } else {
                ok = Preprocessing::Debayer::vng(preloadedImages[frame], debayered, args.params.bayerPattern);
            }
            if (ok) preloadedImages[frame] = std::move(debayered);
        }
    }
    args.log(tr("Preload complete."), "neutral");
    
    // Cache per-frame registration shifts so they are not re-fetched per pixel.
    struct FrameShift { double sx, sy; int iw, ih; };
    std::vector<FrameShift> frameShifts(nbImages);
    for (int frame = 0; frame < nbImages; ++frame) {
        int imgIdx = args.imageIndices[frame];
        const auto& imgInfo = args.sequence->image(imgIdx);
        double scale = args.params.upscaleAtStacking ? 2.0 : 1.0;
        frameShifts[frame].sx = imgInfo.registration.shiftX * scale - offsetX;
        frameShifts[frame].sy = imgInfo.registration.shiftY * scale - offsetY;
        frameShifts[frame].iw = preloadedImages[frame].width();
        frameShifts[frame].ih = preloadedImages[frame].height();
    }
    
    float featherDist = static_cast<float>(args.params.featherDistance);
    
    // ===== PRE-CALCULATE NORMALIZATION COEFFICIENTS =====
    struct NormCoeffs { double offset; double mul; double scale; };
    std::vector<std::vector<NormCoeffs>> allCoeffs(channels);
    for (int c = 0; c < channels; ++c) {
        allCoeffs[c].resize(nbImages, {0.0, 1.0, 1.0});
        if (args.params.hasNormalization()) {
            for (int i = 0; i < nbImages; ++i) {
                // Fix: Must use the original sequence index, not the stack loop index
                int seqIdx = args.imageIndices[i];
                
                // If coefficients exist for this channel (and it's a valid index), use them.
                // If not (e.g. Debayer enabled but normalization ran on Mono), use Channel 0.
                int sourceCh = c;
                if (c >= 3 || args.coefficients.poffset[c].empty()) {
                    sourceCh = 0;
                }
                
                if (sourceCh < 3 && seqIdx < static_cast<int>(args.coefficients.poffset[sourceCh].size())) {
                    allCoeffs[c][i].offset = args.coefficients.poffset[sourceCh][seqIdx];
                    allCoeffs[c][i].mul = args.coefficients.pmul[sourceCh][seqIdx];
                    allCoeffs[c][i].scale = args.coefficients.pscale[sourceCh][seqIdx];
                }
            }
        }
    }
    
    // ===== REJECTION STATS =====
    std::atomic<long> totalRejLow{0};
    std::atomic<long> totalRejHigh{0};
    
    // ===== GESDT CRITICAL VALUES =====
    std::vector<float> gesdtCriticalValues;
    if (args.params.rejection == Rejection::GESDT) {
        int maxOutliers = static_cast<int>(nbImages * args.params.sigmaLow); // sigmaLow is misused as fraction here
        if (maxOutliers < 1) maxOutliers = 1;
        gesdtCriticalValues.resize(maxOutliers);
        
        for (int i = 0; i < maxOutliers; ++i) {
            int ni = nbImages - i;
            if (ni <= 2) break;
            double alpha = 0.05 / (2.0 * ni); // Standard 0.05 significance
            double p = 1.0 - alpha;
            double t = std::sqrt(-2.0 * std::log(p > 0.5 ? 1.0-p : p));
            double c0 = 2.515517, c1 = 0.802853, c2 = 0.010328;
            double d1 = 1.432788, d2 = 0.189269, d3 = 0.001308;
            t = t - (c0 + c1*t + c2*t*t) / (1 + d1*t + d2*t*t + d3*t*t*t);
            t = std::abs(t);
            double num = (ni - 1) * t;
            double den = std::sqrt((ni - 2 + t*t) * ni);
            gesdtCriticalValues[i] = static_cast<float>(num / den);
        }
    }
    float* pGesdt = gesdtCriticalValues.empty() ? nullptr : gesdtCriticalValues.data();
    
    float* outputData = args.result.data().data();
    std::atomic<bool> failed{false};
    std::atomic<int> processedBlocks{0};
    int nbBlocks = static_cast<int>(blocks.size());
    
    args.log(tr("Starting stacking..."), "neutral");
    
#ifdef _OPENMP
    #pragma omp parallel for num_threads(nbThreads) schedule(dynamic) if(nbBlocks > 1)
#endif
    for (int blockIdx = 0; blockIdx < nbBlocks; ++blockIdx) {
        if (failed || m_cancelled || args.isCancelled() || !Threading::getThreadRun()) {
            continue;
        }
        
        // ===== Step 1: Get allocated data block for this thread =====
        int dataIdx = 0;
#ifdef _OPENMP
        dataIdx = omp_get_thread_num();
#endif
        StackDataBlock& data = dataPool[dataIdx];
        ImageBlock& block = blocks[blockIdx];
        // data.layer = block.channel; // Not used anymore (we process all channels)
        
        long blockRejLow = 0, blockRejHigh = 0;
        
        if (m_cancelled || args.isCancelled()) {
            failed = true;
            continue;
        }
        
        // ===== PIXEL STACKING (single pass, reads from preloaded RAM images) =====
        // No I/O here — preloadedImages[] already holds every registered frame.
        // Border exclusion uses Siril's exact strategy:
        //   (1) Hard geometric out-of-bounds → skip frame immediately.
        //   (2) For pre-warped r_ files: warpPerspective(BORDER_CONSTANT=0) + mask
        //       zeroing marks out-of-bounds pixels with 0.0f in ALL channels.
        //       We replicate Siril's apply_rejection_float() per-pixel:
        //         "if (stack[frame] != 0.f) { stack[kept++] = stack[frame]; }"
        //       → skip frames where r==g==b==0.0f (mono: v==0.0f).
        for (int y = 0; y < block.height; ++y) {
            int globalY = block.startRow + y;
            size_t outRowIdx = static_cast<size_t>(globalY) * outputWidth;
            
            for (int x = 0; x < outputWidth; ++x) {
                // Fill stackRGB[c][effectiveFrames] directly from preloaded images.
                int effectiveFrames = 0;
                for (int frame = 0; frame < nbImages; ++frame) {
                    if (m_cancelled || args.isCancelled()) break;
                    
                    const FrameShift& fs = frameShifts[frame];
                    // Source coordinates in this frame's image space.
                    // For pre-warped r_ files fs.sx==fs.sy==0, so srcX==x, srcGY==globalY.
                    // For sequences with live registration these will be non-zero.
                    double srcX  = x       - fs.sx;
                    double srcGY = globalY - fs.sy;
                    
                    // Hard geometric out-of-bounds: skip immediately.
                    if (srcX  < 0.0 || srcX  >= static_cast<double>(fs.iw) ||
                        srcGY < 0.0 || srcGY >= static_cast<double>(fs.ih)) continue;
                    
                    const ImageBuffer& img = preloadedImages[frame];
                    const float* imgBase = img.data().data();
                    int iw = img.width(), ih = img.height(), ich = img.channels();
                    
                    // Integer vs sub-pixel fetch.
                    // Pre-registered r_ files always use integer coordinates (shift=0)
                    // so the branch predictor will practically never take the bicubic path.
                    double fx = srcX  - std::floor(srcX);
                    double fy = srcGY - std::floor(srcGY);
                    bool intCoords = (fx < 1e-6 && fy < 1e-6);
                    
                    if (channels == 3) {
                        float r, g, b;
                        if (intCoords) {
                            int ix = static_cast<int>(srcX);
                            int iy = static_cast<int>(srcGY);
                            // Guard for exact-edge case (srcX == iw after truncation)
                            if (ix >= iw || iy >= ih) continue;
                            const float* px = imgBase + (static_cast<size_t>(iy) * iw + ix) * ich;
                            r = px[0];
                            g = (ich > 1) ? px[1] : px[0];
                            b = (ich > 2) ? px[2] : px[0];
                        } else {
                            r = interpolateBicubic(imgBase, iw, ih, srcX, srcGY, 0, ich);
                            g = (ich > 1) ? interpolateBicubic(imgBase, iw, ih, srcX, srcGY, 1, ich) : r;
                            b = (ich > 2) ? interpolateBicubic(imgBase, iw, ih, srcX, srcGY, 2, ich) : r;
                        }
                        // === SIRIL-EQUIVALENT ZERO EXCLUSION ===
                        // Pre-warped r_ files: warpPerspective(BORDER_CONSTANT=0) + mask zeroing
                        // set ALL channels to exactly 0.0f for pixels outside the source frame.
                        // This matches Siril's apply_rejection_float():
                        //   "if (stack[frame] != 0.f) { stack[kept++] = stack[frame]; }"
                        // Each channel is tested independently; for geometrically-warped images
                        // all three channels are zero simultaneously at the same border pixels.
                        if (r == 0.0f && g == 0.0f && b == 0.0f) continue;
                        
                        // Normalization (guard: skip if channel still 0 after warp ringing)
                        if (args.params.hasNormalization()) {
                            auto applyNorm = [&](float& v, int c) {
                                if (v != 0.0f) {
                                    const auto& coeff = allCoeffs[c][frame];
                                    switch (args.params.normalization) {
                                        case NormalizationMethod::Additive:
                                        case NormalizationMethod::AdditiveScaling:
                                            v = static_cast<float>(v * coeff.scale - coeff.offset); break;
                                        case NormalizationMethod::Multiplicative:
                                        case NormalizationMethod::MultiplicativeScaling:
                                            v = static_cast<float>(v * coeff.scale * coeff.mul); break;
                                        default: break;
                                    }
                                }
                            };
                            applyNorm(r, 0); applyNorm(g, 1); applyNorm(b, 2);
                        }
                        data.stackRGB[0][effectiveFrames] = r;
                        data.stackRGB[1][effectiveFrames] = g;
                        data.stackRGB[2][effectiveFrames] = b;
                    } else {
                        float v;
                        if (intCoords) {
                            int ix = static_cast<int>(srcX);
                            int iy = static_cast<int>(srcGY);
                            if (ix >= iw || iy >= ih) continue;
                            v = imgBase[(static_cast<size_t>(iy) * iw + ix) * ich];
                        } else {
                            v = interpolateBicubic(imgBase, iw, ih, srcX, srcGY, 0, ich);
                        }
                        // Siril-equivalent zero exclusion for mono pre-warped images
                        if (v == 0.0f) continue;
                        if (args.params.hasNormalization()) {
                            const auto& coeff = allCoeffs[0][frame];
                            switch (args.params.normalization) {
                                case NormalizationMethod::Additive:
                                case NormalizationMethod::AdditiveScaling:
                                    v = static_cast<float>(v * coeff.scale - coeff.offset); break;
                                case NormalizationMethod::Multiplicative:
                                case NormalizationMethod::MultiplicativeScaling:
                                    v = static_cast<float>(v * coeff.scale * coeff.mul); break;
                                default: break;
                            }
                        }
                        data.stack[effectiveFrames] = v;
                    }
                    // Feathering mask: distance of pixel from its source frame border.
                    // Zero-excluded frames are not reached here, so mstack stays consistent.
                    if (useFeathering && data.mstack) {
                        float dL = static_cast<float>(srcX);
                        float dT = static_cast<float>(srcGY);
                        float dR = static_cast<float>(fs.iw - 1.0 - srcX);
                        float dB = static_cast<float>(fs.ih - 1.0 - srcGY);
                        float minDist = std::min({dL, dT, dR, dB});
                        if (minDist < 0.0f) minDist = 0.0f;
                        float weight = (minDist >= featherDist) ? 1.0f :
                            [&]{ float t = minDist / featherDist; return t * t * (3.0f - 2.0f * t); }();
                        data.mstack[effectiveFrames] = weight;
                    }
                    effectiveFrames++;
                }

                // 2. Rejection (operates on effectiveFrames valid pixels only)
                int keptPixels = effectiveFrames;
                if (effectiveFrames > 0 && args.params.hasRejection()) {
                    int rej[2] = {0,0};
                    if (channels == 3) {
                         keptPixels = applyRejectionLinked(data, effectiveFrames, args.params.rejection,
                                                           args.params.sigmaLow, args.params.sigmaHigh, pGesdt, rej);
                    } else {
                         keptPixels = applyRejection(data, effectiveFrames, args.params.rejection,
                                                     args.params.sigmaLow, args.params.sigmaHigh, pGesdt, rej);
                    }
                    blockRejLow += rej[0];
                    blockRejHigh += rej[1];
                }
                
                // 3. Compute Result (using optimized C functions)
                if (channels == 3) {
                    for(int c=0; c<3; ++c) {
                        float result = 0.0f;
                        if (keptPixels > 0) {
                            float* s = data.stackRGB[c];
                                if (args.params.method == Method::Median) {
                                    // Use C++ median (sort in place)
                                    std::sort(s, s + keptPixels);
                                    if (keptPixels % 2 == 1) result = s[keptPixels / 2];
                                    else result = (s[keptPixels / 2 - 1] + s[keptPixels / 2]) * 0.5f;
                                } else {
                                    // Use weighted mean if enabled
                                    if ((args.params.weighting != WeightingType::None && !args.weights.empty()) || useFeathering) {
                                        float* mstackPtr = useFeathering ? data.mstack : nullptr;
                                        result = computeWeightedMean(data, keptPixels, nbImages, args.weights.data(), mstackPtr, c);
                                    } else {
                                        // Standard Mean
                                        float sum = 0.0f;
                                        for(int k=0; k<keptPixels; ++k) sum += s[k];
                                        result = sum / keptPixels;
                                    }
                                }
                        }
                        size_t outIdx = (outRowIdx + x) * channels + c;
                        outputData[outIdx] = result;
                    }
                } else {
                    // Mono (using optimized C functions)
                    float result = 0.0f;
                    if (keptPixels > 0) {
                         if (args.params.method == Method::Median) {
                             // Use C++ median
                             std::sort(data.stack, data.stack + keptPixels);
                             if (keptPixels % 2 == 1) result = data.stack[keptPixels / 2];
                             else result = (data.stack[keptPixels / 2 - 1] + data.stack[keptPixels / 2]) * 0.5f;
                         } else {
                             // Use weighted mean if enabled
                             if ((args.params.weighting != WeightingType::None && !args.weights.empty()) || useFeathering) {
                                  float* mstackPtr = useFeathering ? data.mstack : nullptr;
                                  result = computeWeightedMean(data, keptPixels, nbImages, args.weights.data(), mstackPtr, -1);
                             } else {
                                 // Standard Mean
                                 float sum = 0.0f;
                                 for(int k=0; k<keptPixels; ++k) sum += data.stack[k];
                                 result = sum / keptPixels;
                             }
                         }
                    }
                    size_t outIdx = (outRowIdx + x) * channels + block.channel;
                    if(block.channel != -1) outIdx = (outRowIdx + x) * channels + block.channel;
                    else outIdx = (outRowIdx + x) * channels + 0;
                    outputData[outIdx] = result;
                }
            }
        }
        
        // Accumulate rejection stats
        totalRejLow += blockRejLow;
        totalRejHigh += blockRejHigh;
        
        processedBlocks++;
        
        // Progress update
        if (processedBlocks % std::max(1, nbBlocks / 20) == 0) {
            args.progress(tr("Processing block %1/%2...").arg(processedBlocks.load()).arg(nbBlocks),
                         static_cast<double>(processedBlocks) / nbBlocks);
        }
    }
    
    for (auto& data : dataPool) {
        data.deallocate();
    }
    
    if (failed || m_cancelled || args.isCancelled() || !Threading::getThreadRun()) {
        args.log("Stacking cancelled or failed", "salmon");
        return StackResult::CancelledError;
    }
    
    long totalPixels = static_cast<long>(outputWidth) * outputHeight * nbImages;
    if (args.params.hasRejection() && totalPixels > 0) {
        double rejLowPct = 100.0 * totalRejLow / totalPixels;
        double rejHighPct = 100.0 * totalRejHigh / totalPixels;
        args.log(tr("Rejection statistics: low=%1% high=%2%")
                 .arg(rejLowPct, 0, 'f', 3).arg(rejHighPct, 0, 'f', 3), "neutral");
    }
    
    args.log(tr("%1 stacking complete").arg(methodStr), "green");
    return StackResult::OK;
}



void StackingEngine::loadBlockData(const StackingArgs& args,
                                   int startRow, int endRow,
                                   int outputWidth, int channel,
                                   int offsetX, int offsetY,
                                   std::vector<float>& blockData)
{
    int nbImages = args.nbImagesToStack;
    int blockHeight = endRow - startRow;
    size_t pixelsPerBlock = static_cast<size_t>(outputWidth) * blockHeight;
    
    if (blockData.size() != nbImages * pixelsPerBlock) {
        blockData.resize(nbImages * pixelsPerBlock);
    }
    
    const int margin = 3;
    
    // ===== PHASE 1: SEQUENTIAL I/O =====
    // Load all image ROIs sequentially (disk I/O cannot be parallelized efficiently)
    struct LoadedROI {
        ImageBuffer buffer;
        int rX, rY, rW, rH;
        double shiftX, shiftY;
        bool loaded;
    };
    std::vector<LoadedROI> rois(nbImages);

    for (int i = 0; i < nbImages; ++i) {
        int imgIdx = args.imageIndices[i];
        const auto& imgInfo = args.sequence->image(imgIdx);
        
        // Diagnostic: Log each file being read
        if (args.logCallback) {
            args.logCallback(QObject::tr("  Loading %1 (%2/%3)...").arg(imgInfo.fileName()).arg(i+1).arg(nbImages), "");
        }
        
        // Use effective registration if available (e.g. Comet Mode)
        RegistrationData reg;
        if (!args.effectiveRegs.empty() && imgIdx < static_cast<int>(args.effectiveRegs.size())) {
            reg = args.effectiveRegs[imgIdx];
        } else {
             reg = imgInfo.registration;
        }
        
        double shiftX = reg.isShiftOnly() ? reg.shiftX : 0;
        double shiftY = reg.isShiftOnly() ? reg.shiftY : 0;
        
        double srcMinX = offsetX - shiftX;
        double srcMaxX = offsetX + outputWidth - shiftX;
        double srcMinY = offsetY + startRow - shiftY;
        double srcMaxY = offsetY + endRow - shiftY;

        int rX = static_cast<int>(std::floor(srcMinX)) - margin;
        int rY = static_cast<int>(std::floor(srcMinY)) - margin;
        int rW = static_cast<int>(std::ceil(srcMaxX)) - rX + margin;
        int rH = static_cast<int>(std::ceil(srcMaxY)) - rY + margin;
        
        rois[i].rX = rX; rois[i].rY = rY; 
        rois[i].rW = rW; rois[i].rH = rH;
        rois[i].shiftX = shiftX; rois[i].shiftY = shiftY;
        rois[i].loaded = args.sequence->readImageRegion(imgIdx, rois[i].buffer, rX, rY, rW, rH, channel);
        
        // Apply cosmetic correction if enabled and map is valid
        if (rois[i].loaded && args.params.useCosmetic && args.cosmeticMap.isValid()) {
            CosmeticCorrection::apply(rois[i].buffer, args.cosmeticMap, rX, rY, args.params.cosmeticIsCFA);
        }
    }
    
    // ===== PHASE 2: PARALLEL INTERPOLATION =====
    // Add check for cancellation
    if (m_cancelled || args.isCancelled()) return;

    #ifdef _OPENMP
    #pragma omp parallel for schedule(dynamic)
    #endif
    for (int i = 0; i < nbImages; ++i) {
        if (m_cancelled || args.isCancelled()) continue;
        
        float* imgBlockPtr = &blockData[i * pixelsPerBlock];
        
        if (!rois[i].loaded) {
            std::fill(imgBlockPtr, imgBlockPtr + pixelsPerBlock, 0.0f);
            continue;
        }

        const ImageBuffer& roi = rois[i].buffer;
        int rX = rois[i].rX, rY = rois[i].rY;
        int rW = rois[i].rW, rH = rois[i].rH;
        double shiftX = rois[i].shiftX, shiftY = rois[i].shiftY;


        
        // Bicubic Interpolation into Block
        for (int y = 0; y < blockHeight; ++y) {
            for (int x = 0; x < outputWidth; ++x) {
                double gX = offsetX + x;
                double gY = offsetY + startRow + y;
                double sX = gX - shiftX;
                double sY = gY - shiftY;
                
                double lX = sX - rX;
                double lY = sY - rY;
                
                float val = 0.0f;
                if (lX >= 2.0 && lX < rW - 2.0 && lY >= 2.0 && lY < rH - 2.0) {
                    val = getInterpolatedPixel(roi, lX, lY, 0);
                } else if (lX >= 0 && lX < rW && lY >= 0 && lY < rH) {
                    val = roi.value((int)lX, (int)lY, 0);
                }
                
                if (!std::isfinite(val)) val = 0.0f;
                
                imgBlockPtr[y * outputWidth + x] = val;
            }
        }
    }
}

//=============================================================================
// ACCELERATED MEAN STACKING (C-based, with rejection support)
//=============================================================================

StackResult StackingEngine::tryStackMeanC(StackingArgs& args, int width, int height, int channels) {
    Q_UNUSED(args);
    Q_UNUSED(width);
    Q_UNUSED(height);
    Q_UNUSED(channels);
    // Optimized C stacking removed.
    return StackResult::GenericError;
}

//=============================================================================
// MEDIAN STACKING
//=============================================================================

StackResult StackingEngine::stackMedian(StackingArgs& args) {
    int outputWidth, outputHeight, offsetX, offsetY;
    computeOutputDimensions(args, outputWidth, outputHeight, offsetX, offsetY);
    
    int channels = args.sequence->channels();
    int nbImages = args.nbImagesToStack;
    
    if (!prepareOutput(args, outputWidth, outputHeight, channels)) {
        return StackResult::AllocError;
    }
    
    float* output = args.result.data().data();
    
    // Load all images into memory (for small sequences)
    // For large sequences, should process in blocks
    std::vector<ImageBuffer> images(nbImages);
    
    for (int i = 0; i < nbImages; ++i) {
        if (m_cancelled) return StackResult::CancelledError;
        
        args.progress(tr("Loading image %1/%2...").arg(i + 1).arg(nbImages),
                     static_cast<double>(i) / (nbImages * 2));
        
        if (!args.sequence->readImage(args.imageIndices[i], images[i])) {
            args.log(tr("Warning: Failed to load image %1").arg(i), "salmon");
        }
    }
    
    // Process each pixel
    // Parallelize over Y
    #pragma omp parallel for collapse(1)
    for (int y = 0; y < outputHeight; ++y) {
        if (m_cancelled) continue; // Difficult to break OpenMP
        
        // Progress (not thread safe to call args.progress frequently, limit it)
        if (y % 100 == 0 && omp_get_thread_num() == 0) {
            args.progress(tr("Computing median (row %1/%2)...").arg(y).arg(outputHeight),
                         0.5 + 0.5 * y / outputHeight);
        }
        
        for (int x = 0; x < outputWidth; ++x) {
            for (int c = 0; c < channels; ++c) {
                std::vector<float> stack;
                stack.reserve(nbImages);
                
                for (int i = 0; i < nbImages; ++i) {
                    if (!images[i].isValid()) continue;
                    
                    const auto& imgInfo = args.sequence->image(args.imageIndices[i]);
                    
                    float value;
                    if (getShiftedPixel(images[i], x, y, c, imgInfo.registration, offsetX, offsetY, value)) {
                        if (value != 0.0f) {
                            stack.push_back(value);
                        }
                    }
                }
                
                float result = 0.0f;
                if (!stack.empty()) {
                    result = Statistics::quickMedian(stack);
                }
                
                // Interleaved Indexing
                size_t outIdx = (static_cast<size_t>(y) * outputWidth + x) * channels + c;
                output[outIdx] = result;
            }
        }
    }
    
    if (m_cancelled) return StackResult::CancelledError;
    
    return StackResult::OK;
}

//=============================================================================
// MIN/MAX STACKING
//=============================================================================

StackResult StackingEngine::stackMax(StackingArgs& args) {
    int outputWidth, outputHeight, offsetX, offsetY;
    computeOutputDimensions(args, outputWidth, outputHeight, offsetX, offsetY);
    
    int channels = args.sequence->channels();
    
    if (!prepareOutput(args, outputWidth, outputHeight, channels)) {
        return StackResult::AllocError;
    }
    
    float* output = args.result.data().data();
    size_t totalPixels = static_cast<size_t>(outputWidth) * outputHeight * channels;
    
    // Initialize to minimum possible value
    for (size_t i = 0; i < totalPixels; ++i) {
        output[i] = 0.0f;
    }
    
    int processed = 0;
    for (int idx : args.imageIndices) {
        if (m_cancelled) return StackResult::CancelledError;
        
        args.progress(tr("Processing image %1/%2...")
                     .arg(processed + 1).arg(args.nbImagesToStack),
                     static_cast<double>(processed) / args.nbImagesToStack);
        
        ImageBuffer buffer;
        if (!args.sequence->readImage(idx, buffer)) {
            continue;
        }
        // Get registration data
        RegistrationData reg;
        if (!args.effectiveRegs.empty() && idx < (int)args.effectiveRegs.size()) {
             reg = args.effectiveRegs[idx];
        } else {
             reg = args.sequence->image(idx).registration;
        }
        
        #pragma omp parallel for collapse(2)
        for (int y = 0; y < outputHeight; ++y) {
            for (int x = 0; x < outputWidth; ++x) {
                for (int c = 0; c < channels; ++c) {
                    float value;
                    if (getShiftedPixel(buffer, x, y, c, reg, offsetX, offsetY, value)) {
                        size_t outIdx = (static_cast<size_t>(y) * outputWidth + x) * channels + c;
                        
                        // NOT THREAD SAFE without atomic/critical if looping images in parallel,
                        // but here we loop images sequential, pixels parallel.
                        // So multiple threads write to DIFFERENT outIdx. Safe.
                        
                        if (value > output[outIdx]) {
                            output[outIdx] = value;
                        }
                    }
                }
            }
        }
        
        processed++;
    }
    
    return StackResult::OK;
}

StackResult StackingEngine::stackMin(StackingArgs& args) {
    int outputWidth, outputHeight, offsetX, offsetY;
    computeOutputDimensions(args, outputWidth, outputHeight, offsetX, offsetY);
    
    int channels = args.sequence->channels();
    
    if (!prepareOutput(args, outputWidth, outputHeight, channels)) {
        return StackResult::AllocError;
    }
    
    float* output = args.result.data().data();
    size_t totalPixels = static_cast<size_t>(outputWidth) * outputHeight * channels;
    
    // Initialize to maximum possible value
    for (size_t i = 0; i < totalPixels; ++i) {
        output[i] = 1.0f;
    }
    
    int processed = 0;
    for (int idx : args.imageIndices) {
        if (m_cancelled) return StackResult::CancelledError;
        
        args.progress(tr("Processing image %1/%2...")
                     .arg(processed + 1).arg(args.nbImagesToStack),
                     static_cast<double>(processed) / args.nbImagesToStack);
        
        ImageBuffer buffer;
        if (!args.sequence->readImage(idx, buffer)) {
            continue;
        }
        
        const auto& imgInfo = args.sequence->image(idx);
        RegistrationData reg = imgInfo.registration;
        if (!args.effectiveRegs.empty() && idx < (int)args.effectiveRegs.size()) {
             reg = args.effectiveRegs[idx];
        }
        
        #pragma omp parallel for collapse(2)
        for (int y = 0; y < outputHeight; ++y) {
            for (int x = 0; x < outputWidth; ++x) {
                for (int c = 0; c < channels; ++c) {
                    float value;
                    if (getShiftedPixel(buffer, x, y, c, reg, offsetX, offsetY, value)) {
                        if (value != 0.0f) {  // Don't count zeros as minimum
                            size_t outIdx = (static_cast<size_t>(y) * outputWidth + x) * channels + c;
                            if (value < output[outIdx]) {
                                output[outIdx] = value;
                            }
                        }
                    }
                }
            }
        }
        
        processed++;
    }
    
    return StackResult::OK;
}

//=============================================================================
// DRIZZLE STACKING
//=============================================================================

StackResult StackingEngine::stackDrizzle(StackingArgs& args) {
    // 1. Initialize Drizzle Engine
    DrizzleStacking drizzle;
    
    DrizzleStacking::DrizzleParams dParams;
    dParams.scaleFactor = args.params.drizzleScale;
    dParams.dropSize = args.params.drizzlePixFrac;
    dParams.kernelType = static_cast<int>(args.params.drizzleKernel);
    dParams.useWeightMaps = true;

    drizzle.initialize(args.sequence->width(), args.sequence->height(),
                       args.sequence->channels(),
                       dParams);
    
    // Pass 1 Result (Reference Stack) for rejection
    ImageBuffer referenceStack;
    if (args.params.hasRejection() && args.result.isValid()) {
         referenceStack = args.result; // Copy or move? Move is safer since we overwrite args.result later
         // But wait, args.result might be needed if we fail?
         // Let's keep a copy.
    }

    // 2. Iterate input images
    int processed = 0;
    
    for (int idx : args.imageIndices) {
        if (m_cancelled) return StackResult::CancelledError;
        
        args.progress(tr("Drizzling image %1/%2...")
                     .arg(processed + 1).arg(args.nbImagesToStack),
                     static_cast<double>(processed) / args.nbImagesToStack);
        
        // Load Image
        ImageBuffer buffer;
        if (!args.sequence->readImage(idx, buffer)) {
            continue;
        }
        
        // Normalize Buffer BEFORE Drizzle
        if (args.params.hasNormalization()) {
             // Apply normalization to the whole buffer
             for (int c=0; c<buffer.channels(); ++c) {
                 float* data = buffer.data().data() + c * buffer.width() * buffer.height();
                 size_t count = static_cast<size_t>(buffer.width()) * buffer.height();
                 for(size_t i=0; i<count; ++i) {
                     if (data[i] != 0.0f) {
                         data[i] = Normalization::applyToPixel(data[i], args.params.normalization, 
                                                               processed, c, args.coefficients);
                     }
                 }
             }
        }
        
        // Compute Rejection Mask on-the-fly if enabled
        std::unique_ptr<ImageBuffer> dimRejectionMap;
        
        // Use effective registration if available
        RegistrationData reg;
        if (!args.effectiveRegs.empty() && idx < static_cast<int>(args.effectiveRegs.size())) {
            reg = args.effectiveRegs[idx];
        } else {
             reg = args.sequence->image(idx).registration;
        }

        if (referenceStack.isValid()) {
             dimRejectionMap = std::make_unique<ImageBuffer>(buffer.width(), buffer.height(), 1);
             float* maskData = dimRejectionMap->data().data();
             // Zero init
             std::memset(maskData, 0, sizeof(float) * buffer.width() * buffer.height());
             
             // Check each pixel against reference
             // Need to handle registration shift for comparison!
             int shiftX = 0, shiftY = 0;
             if (reg.hasRegistration) {
                 shiftX = static_cast<int>(std::round(reg.shiftX));
                 shiftY = static_cast<int>(std::round(reg.shiftY));
             }
             
             // Sigma threshold
             // Estimate noise level of the current image
             double ch0Noise = Statistics::computeNoise(buffer.data().data(), buffer.width(), buffer.height());
             
             float sigmaParam = std::max(args.params.sigmaLow, args.params.sigmaHigh);
             if (sigmaParam <= 0.0f) sigmaParam = 3.0f;
             
             float threshold = static_cast<float>(sigmaParam * ch0Noise);
             // Safety minimum?
             if (threshold < 1e-6f) threshold = 1e-6f;
             
             for (int y = 0; y < buffer.height(); ++y) {
                 for (int x = 0; x < buffer.width(); ++x) {
                     // Get Ref Value
                     // Coordinate in Ref: x - shiftX, y - shiftY
                     int rx = x - shiftX;
                     int ry = y - shiftY;
                     
                     if (rx >= 0 && rx < referenceStack.width() && ry >= 0 && ry < referenceStack.height()) {
                         bool reject = false;
                         for(int c=0; c<buffer.channels(); ++c) {
                             float val = buffer.value(x, y, c);
                             float ref = referenceStack.value(rx, ry, c);
                             
                             if (std::abs(val - ref) > threshold) {
                                 reject = true;
                                 break;
                             }
                         }
                         if (reject) maskData[y * buffer.width() + x] = 1.0f;
                     } 
                 }
             }
        }
        
        // Prepare weights
        std::vector<float> imgWeights; // Per-pixel weights
        if (args.params.weighting != WeightingType::None && !args.weights.empty()) {
            imgWeights.resize(args.sequence->channels());
             int totalSeqImages = args.sequence->count();
             for(int c=0; c<args.sequence->channels(); ++c) {
                 int wIdx = c * totalSeqImages + idx;
                 if (wIdx < static_cast<int>(args.weights.size())) {
                     imgWeights[c] = static_cast<float>(args.weights[wIdx]);
                 } else {
                     imgWeights[c] = 1.0f;
                 }
             }
        }
        
        // Add to Drizzle
        const float* rejMapPtr = nullptr;
        if (dimRejectionMap) rejMapPtr = dimRejectionMap->data().data(); 
        
        drizzle.addImage(buffer, reg, imgWeights, rejMapPtr); 
        
        processed++;
    }
    
    // 3. Resolve result
    args.progress(tr("Finalizing Drizzle..."), -1);
    // Prepare output buffer with VALID dimensions from drizzle output
    if (!prepareOutput(args, drizzle.outputWidth(), drizzle.outputHeight(), args.sequence->channels())) {
         // Restore reference stack if drizzle fails?
        return StackResult::AllocError;
    }
    
    if (!drizzle.resolve(args.result)) {
        return StackResult::GenericError;
    }
    
    // Copy metadata from ref stack if valid?
    if (referenceStack.isValid()) {
        args.result.metadata() = referenceStack.metadata();
    }
    
    return StackResult::OK;
}

//=============================================================================
// METADATA AND SUMMARY
//=============================================================================

void StackingEngine::updateMetadata(StackingArgs& args) {
    auto& meta = args.result.metadata();
    
    // Stack count
    meta.stackCount = args.nbImagesToStack;
    
    // Total exposure
    double totalExposure = 0.0;
    for (int idx : args.imageIndices) {
        totalExposure += args.sequence->image(idx).exposure;
    }
    meta.exposure = totalExposure;
    
    // Copy other metadata from reference image
    int refIdx = args.params.refImageIndex;
    if (refIdx >= 0 && refIdx < args.sequence->count()) {
        const auto& refMeta = args.sequence->image(refIdx).metadata;
        meta.focalLength = refMeta.focalLength;
        meta.pixelSize = refMeta.pixelSize;
        meta.ra = refMeta.ra;
        meta.dec = refMeta.dec;
        // Copy WCS if present...
    }
}

QString StackingEngine::generateSummary(const StackingArgs& args) {
    QString summary;
    
    summary = tr("Stacking complete: %1 images using %2")
             .arg(args.nbImagesToStack)
             .arg(methodToString(args.params.method));
    
    if (args.params.hasRejection()) {
        summary += tr(", rejection: %1 (%2%)")
                  .arg(rejectionToString(args.params.rejection))
                  .arg(args.rejectionStats.rejectionPercentage(), 0, 'f', 1);
    }
    
    if (args.params.hasNormalization()) {
        summary += tr(", normalization: %1")
                  .arg(normalizationToString(args.params.normalization));
    }
    
    return summary;
}

//=============================================================================
// WORKER THREAD
//=============================================================================

StackingWorker::StackingWorker(StackingArgs args, QObject* parent)
    : QThread(parent)
    , m_args(std::move(args))
{
    // Connect engine signals to worker signals
    connect(&m_engine, &StackingEngine::progressChanged,
            this, &StackingWorker::progressChanged);
    connect(&m_engine, &StackingEngine::logMessage,
            this, &StackingWorker::logMessage);
}

void StackingWorker::run() {
    // Set up callbacks
    m_args.progressCallback = [this](const QString& msg, double pct) {
        emit progressChanged(msg, pct);
    };
    
    m_args.logCallback = [this](const QString& msg, const QString& color) {
        emit logMessage(msg, color);
    };
    
    m_args.cancelCheck = [this]() {
        return m_engine.isCancelled();
    };
    
    StackResult result = m_engine.execute(m_args);
    m_args.returnValue = static_cast<int>(result);
    
    emit finished(result == StackResult::OK);
}




//=============================================================================
// BLOCK LOADING HELPER
//=============================================================================


int StackingEngine::computeOptimalBlockSize(const StackingArgs& args, int outputWidth, int channels) {
    Q_UNUSED(args);
    
    if (outputWidth <= 0 || channels <= 0) return 128;
    
    // Target ~64 MB per block
    // Row size = width * channels * 4 bytes
    size_t rowBytes = static_cast<size_t>(outputWidth) * channels * sizeof(float);
    if (rowBytes == 0) return 128;
    
    size_t targetBytes = 64 * 1024 * 1024; // 64 MB
    int height = static_cast<int>(targetBytes / rowBytes);
    
    if (height < 32) height = 32;
    if (height > 4096) height = 4096;
    
    return height;
}

} // namespace Stacking

