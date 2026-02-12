#include "AstroSpikeDialog.h"
#include "../ImageViewer.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QPushButton>
#include <QLabel>
#include <QSlider>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QPainter>
#include <QMouseEvent>
#include <QMenu>
#include <QFileDialog>
#include <QGroupBox>
#include <QtMath>
#include <QDebug>
#include <QTimer>
#include <algorithm>
#include <QSettings>

#ifdef _OPENMP
#include <omp.h>
#endif

// =============================================================================
// STAR DETECTION THREAD
// =============================================================================

StarDetectionThread::StarDetectionThread(const ImageBuffer& buffer, QObject* parent)
    : QThread(parent)
{
    // Convert QImage to cv::Mat (thread-safe copy)
    QImage img = buffer.getDisplayImage(ImageBuffer::Display_Linear);
    if (img.isNull()) return;
    
    m_width = img.width();
    m_height = img.height();
    
    // Build 8-bit BGR and grayscale matrices from the QImage
    m_rgbMat = cv::Mat(m_height, m_width, CV_8UC3);
    m_grayMat = cv::Mat(m_height, m_width, CV_8UC1);
    
    int depth = img.depth() / 8; // bytes per pixel
    
    for (int y = 0; y < m_height; ++y) {
        const uint8_t* line = img.constScanLine(y);
        uint8_t* bgrRow = m_rgbMat.ptr<uint8_t>(y);
        uint8_t* grayRow = m_grayMat.ptr<uint8_t>(y);
        
        for (int x = 0; x < m_width; ++x) {
            uint8_t r = 0, g = 0, b = 0;
            if (depth == 4) {  // ARGB32
                b = line[x*4]; g = line[x*4+1]; r = line[x*4+2];
            } else if (depth == 3) {  // RGB888
                r = line[x*3]; g = line[x*3+1]; b = line[x*3+2];
            }
            bgrRow[x*3+0] = b;
            bgrRow[x*3+1] = g;
            bgrRow[x*3+2] = r;
            // ITU-R BT.709 luminance
            grayRow[x] = (uint8_t)(0.2126f * r + 0.7152f * g + 0.0722f * b);
        }
    }
}

void StarDetectionThread::run() {
    if (m_grayMat.empty()) return;
    
    // --- Pre-processing: Gaussian blur to reduce noise ---
    cv::Mat blurred;
    cv::GaussianBlur(m_grayMat, blurred, cv::Size(3, 3), 0);
    
    // --- Multi-level threshold detection ---
    // Scan from HIGHEST to LOWEST threshold so brightest stars are found first.
    // This prevents dim noise blobs from claiming star positions before bright stars.
    const int threshLevels[] = {230, 200, 160, 120, 80, 60, 40, 20};
    const int numLevels = 8;
    
    const double minArea = 2.0;
    const double maxArea = 20000.0; 
    
    // Use a "visited" mask to avoid detecting the same star at multiple threshold levels
    cv::Mat visited = cv::Mat::zeros(m_height, m_width, CV_8UC1);
    
    QVector<AstroSpike::Star> allDetected;
    
    for (int lvl = 0; lvl < numLevels; ++lvl) {
        cv::Mat binary;
        cv::threshold(blurred, binary, threshLevels[lvl], 255, cv::THRESH_BINARY);
        
        std::vector<std::vector<cv::Point>> contours;
        cv::findContours(binary, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);
        
        int numContours = (int)contours.size();
        
        // Process contours in parallel
        std::vector<AstroSpike::Star> levelStars(numContours);
        std::vector<bool> valid(numContours, false);
        
#ifdef _OPENMP
        int numThreads = std::max(1, omp_get_max_threads() - 1);
        #pragma omp parallel for num_threads(numThreads) schedule(dynamic, 64)
#endif
        for (int i = 0; i < numContours; ++i) {
            const auto& contour = contours[i];
            double area = cv::contourArea(contour);
            
            if (area < minArea || area > maxArea) continue;
            
            // Circularity check: reject very elongated or irregular shapes
            double perimeter = cv::arcLength(contour, true);
            if (perimeter <= 0) continue;
            double circularity = 4.0 * CV_PI * area / (perimeter * perimeter);
            if (circularity < 0.25) continue;

            // Ovality/Elongation Check to reject nebulosity stripes
            if (contour.size() >= 5) {
                cv::RotatedRect minRect = cv::fitEllipse(contour);
                float w = minRect.size.width;
                float h = minRect.size.height;
                float shortAxis = std::min(w, h);
                float longAxis = std::max(w, h);
                
                // Aspect ratio check: < 0.6 means very oval/streak/nebula
                if (longAxis > 0) {
                    float ratio = shortAxis / longAxis;
                    if (ratio < 0.6f) continue;
                }
            }
            
            // Find actual brightest pixel within this contour's bounding rect
            // This avoids the centroid-averaging issue when nearby stars merge
            cv::Rect bbox = cv::boundingRect(contour);
            int bx0 = std::max(0, bbox.x);
            int by0 = std::max(0, bbox.y);
            int bx1 = std::min(m_width - 1, bbox.x + bbox.width - 1);
            int by1 = std::min(m_height - 1, bbox.y + bbox.height - 1);
            
            // Find peak luminance position
            float peakLum = 0;
            int icx = (bx0 + bx1) / 2, icy = (by0 + by1) / 2;
            int peakX = icx, peakY = icy;
            float cx = 0, cy = 0;
            for (int py = by0; py <= by1; ++py) {
                const uint8_t* grayRow = m_grayMat.ptr<uint8_t>(py);
                for (int px = bx0; px <= bx1; ++px) {
                    if (grayRow[px] > peakLum) {
                        peakLum = grayRow[px];
                        peakX = px;
                        peakY = py;
                    }
                }
            }
            
            // Refine centroid: intensity-weighted average in a tight window around peak
            float refinedCx = 0, refinedCy = 0, totalW = 0;
            int refineR = 2; // Small window to stay on the star core
            int ry0 = std::max(0, peakY - refineR);
            int ry1 = std::min(m_height - 1, peakY + refineR);
            int rx0 = std::max(0, peakX - refineR);
            int rx1 = std::min(m_width - 1, peakX + refineR);
            for (int py = ry0; py <= ry1; ++py) {
                const uint8_t* grayRow = m_grayMat.ptr<uint8_t>(py);
                for (int px = rx0; px <= rx1; ++px) {
                    float w = grayRow[px] * grayRow[px]; // Squared weight for tighter peak
                    refinedCx += px * w;
                    refinedCy += py * w;
                    totalW += w;
                }
            }
            if (totalW > 0) {
                cx = refinedCx / totalW;
                cy = refinedCy / totalW;
            } else {
                cx = (float)peakX;
                cy = (float)peakY;
            }
            
            icx = std::max(0, std::min((int)std::round(cx), m_width - 1));
            icy = std::max(0, std::min((int)std::round(cy), m_height - 1));
            
            // Skip if this refined location was already detected
            if (visited.at<uint8_t>(icy, icx) != 0) continue;
            
            // Radius from area
            float radius = (float)std::sqrt(area / CV_PI);
            
            // Color: sample from the bright core around the peak (tight window)
            int colorR = std::max(1, (int)std::ceil(radius * 0.5f));
            float sumR = 0, sumG = 0, sumB = 0;
            float sumWeight = 0;
            
            int cy0 = std::max(0, icy - colorR);
            int cy1 = std::min(m_height - 1, icy + colorR);
            int cx0 = std::max(0, icx - colorR);
            int cx1 = std::min(m_width - 1, icx + colorR);
            
            for (int py = cy0; py <= cy1; ++py) {
                const uint8_t* grayRow = m_grayMat.ptr<uint8_t>(py);
                const uint8_t* bgrRow = m_rgbMat.ptr<uint8_t>(py);
                for (int px = cx0; px <= cx1; ++px) {
                    float lum = grayRow[px];
                    // Only sample from bright pixels for better color accuracy
                    if (lum < peakLum * 0.5f) continue;
                    
                    float w = lum * lum; // Squared weight emphasizes bright core
                    sumB += bgrRow[px*3+0] * w;
                    sumG += bgrRow[px*3+1] * w;
                    sumR += bgrRow[px*3+2] * w;
                    sumWeight += w;
                }
            }
            
            float avgR = 255, avgG = 255, avgB = 255;
            if (sumWeight > 0) {
                avgR = sumR / sumWeight;
                avgG = sumG / sumWeight;
                avgB = sumB / sumWeight;
            }
            
            AstroSpike::Star s;
            s.x = cx;
            s.y = cy;
            s.brightness = peakLum / 255.0f;
            s.radius = radius;
            s.color = QColor((int)std::min(255.0f, avgR),
                             (int)std::min(255.0f, avgG),
                             (int)std::min(255.0f, avgB));
            
            levelStars[i] = s;
            valid[i] = true;
        }
        
        // Collect valid results from this level and mark visited
        for (int i = 0; i < numContours; ++i) {
            if (!valid[i]) continue;
            
            const auto& s = levelStars[i];
            int icx = std::max(0, std::min((int)std::round(s.x), m_width - 1));
            int icy = std::max(0, std::min((int)std::round(s.y), m_height - 1));
            
            // Mark a small region as visited to prevent duplicates across levels
            // Reduced to 0.6 * radius to allow close companions (de-blending)
            int markR = std::max(1, (int)std::ceil(s.radius * 0.6f));
            int my0 = std::max(0, icy - markR);
            int my1 = std::min(m_height - 1, icy + markR);
            int mx0 = std::max(0, icx - markR);
            int mx1 = std::min(m_width - 1, icx + markR);
            for (int py = my0; py <= my1; ++py) {
                uint8_t* row = visited.ptr<uint8_t>(py);
                for (int px = mx0; px <= mx1; ++px) {
                    row[px] = 255;
                }
            }
            
            allDetected.append(s);
        }
    }
    
    qDebug() << "[AstroSpike] Detected" << allDetected.size() << "raw star candidates";
    
    // --- Sort by brightness descending ---
    std::sort(allDetected.begin(), allDetected.end(), [](const AstroSpike::Star& a, const AstroSpike::Star& b) {
        return a.brightness > b.brightness;
    });
    
    // --- Merge close stars (keep brightest) ---
    QVector<AstroSpike::Star> merged;
    merged.reserve(allDetected.size());
    for (const auto& s : allDetected) {
        bool isDuplicate = false;
        for (const auto& existing : merged) {
            float dx = s.x - existing.x;
            float dy = s.y - existing.y;
            float distSq = dx*dx + dy*dy;
            // Radius sum * 0.7 allows significant overlap (visual doubles) but merges identicals
            float mergeR = (existing.radius + s.radius) * 0.7f;
            if (distSq < mergeR * mergeR) {
                isDuplicate = true;
                break;
            }
        }
        if (!isDuplicate) merged.append(s);
    }
    
    qDebug() << "[AstroSpike] After merge:" << merged.size() << "unique stars";
    
    emit detectionComplete(merged);
}

// =============================================================================
// ASTRO SPIKE CANVAS
// =============================================================================

AstroSpikeCanvas::AstroSpikeCanvas(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    createGlowSprite();
}

void AstroSpikeCanvas::setImage(const QImage& img) {
    m_image = img;
    updateSpikePreview();
    fitToView();
    update();
}

void AstroSpikeCanvas::setStars(const QVector<AstroSpike::Star>& stars) {
    m_stars = stars;
    updateSpikePreview();
    update();
}

void AstroSpikeCanvas::setConfig(const AstroSpike::Config& config) {
    m_config = config;
    updateSpikePreview();
    update();
}

void AstroSpikeCanvas::updateSpikePreview() {
    if (m_image.isNull()) {
        m_spikePreview = QImage();
        return;
    }
    
    QSize targetSize = m_image.size();
    float scale = 1.0f;
    
    const int MAX_PREVIEW_DIM = 2048;
    if (targetSize.width() > MAX_PREVIEW_DIM || targetSize.height() > MAX_PREVIEW_DIM) {
        scale = (float)MAX_PREVIEW_DIM / std::max(targetSize.width(), targetSize.height());
        targetSize = targetSize * scale;
    }
    
    // Reuse buffer if possible
    if (m_spikePreview.size() != targetSize || m_spikePreview.format() != QImage::Format_ARGB32_Premultiplied) {
        m_spikePreview = QImage(targetSize, QImage::Format_ARGB32_Premultiplied);
    }
    
    m_spikePreview.fill(Qt::transparent);
    
    if (!m_stars.isEmpty()) {
        QPainter p(&m_spikePreview);
        // Scale rendering context
        if (scale != 1.0f) p.scale(scale, scale);
        
        render(p, 1.0f, QPointF(0, 0));
        p.end(); 
        
        // Apply 1px Blur (scaled if needed)
        // Use at least 0.5 sigma to avoid artifacts
        float sigma = std::max(0.5f, 1.0f * scale);
        
        cv::Mat spikeMat(m_spikePreview.height(), m_spikePreview.width(), CV_8UC4,
                         m_spikePreview.bits(), m_spikePreview.bytesPerLine());
                         
        cv::GaussianBlur(spikeMat, spikeMat, cv::Size(0, 0), sigma, sigma);
    }
}

void AstroSpikeCanvas::setToolMode(AstroSpike::ToolMode mode) {
    m_toolMode = mode;
    if (mode == AstroSpike::ToolMode::None) {
        setCursor(Qt::OpenHandCursor);
    } else {
        setCursor(Qt::CrossCursor);
    }
}



void AstroSpikeCanvas::zoomIn() {
    zoomToPoint(QPointF(width() / 2.0, height() / 2.0), 1.2f);
}

void AstroSpikeCanvas::zoomOut() {
    zoomToPoint(QPointF(width() / 2.0, height() / 2.0), 1.0f / 1.2f);
}

void AstroSpikeCanvas::zoomToPoint(QPointF widgetPos, float factor) {
    // Image point under cursor before zoom
    float cx = width() / 2.0f + m_panOffset.x();
    float cy = height() / 2.0f + m_panOffset.y();
    QPointF imgPt = (widgetPos - QPointF(cx, cy)) / m_zoom;
    
    m_zoom *= factor;
    
    // Adjust pan so same image point stays under cursor
    QPointF newScreenPt = imgPt * m_zoom + QPointF(cx, cy);
    m_panOffset += (widgetPos - newScreenPt);
    
    update();
}

// =============================================================================
// HELPER FOR VIEW ALIGNMENT
// =============================================================================
void AstroSpikeDialog::setViewer(ImageViewer* v) {
    if (m_viewer == v) return;
    
    // Switch viewer
    m_viewer = v;
    if (!m_viewer || !m_viewer->getBuffer().isValid()) {
         m_statusLabel->setText(tr("No valid image."));
         return;
    }
    
    // Update Canvas Image
    m_canvas->setImage(m_viewer->getBuffer().getDisplayImage(ImageBuffer::Display_Linear));
    
    // Clear and Re-Run detection for new image
    m_canvas->setStars({});
    m_allStars.clear();
    m_history.clear();
    m_historyIndex = 0;
    updateHistoryButtons();
    
    runDetection();
}

void AstroSpikeCanvas::fitToView() {
    if (m_image.isNull()) return;
    
    // Fit logic works best when widget has valid size
    // If called too early (size 100x30), it fails.
    // We defer or ensure valid geom.
    if (width() <= 100 || height() <= 100) {
        // Defer
        QTimer::singleShot(50, this, &AstroSpikeCanvas::fitToView);
        return;
    }

    float wFactor = (float)width() / m_image.width();
    float hFactor = (float)height() / m_image.height();
    m_zoom = std::min(wFactor, hFactor) * 0.95f; // 95% fit margin
    m_panOffset = QPointF(0, 0);
    update();
}

void AstroSpikeCanvas::paintEvent([[maybe_unused]] QPaintEvent* event) {
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    
    if (m_image.isNull()) return;
    
    // Draw Image
    // Calculate centered rect
    float drawW = m_image.width() * m_zoom;
    float drawH = m_image.height() * m_zoom;
    float cx = width() / 2.0f + m_panOffset.x();
    float cy = height() / 2.0f + m_panOffset.y();
    QRectF destRect(cx - drawW/2, cy - drawH/2, drawW, drawH);
    
    p.drawImage(destRect, m_image);
    
    // Draw Cached Spikes (if valid)
    if (!m_spikePreview.isNull() && !m_stars.isEmpty()) {
        p.setCompositionMode(QPainter::CompositionMode_Screen);
        // Draw the full-res preview scaled to the destination rect
        // This ensures pixelation matches zooming into the actual image
        p.drawImage(destRect, m_spikePreview);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    }
    
    // Draw Tool Cursor
    if (m_toolMode != AstroSpike::ToolMode::None && rect().contains(mapFromGlobal(QCursor::pos()))) {
        p.setPen(QPen(Qt::white, 1, Qt::DashLine));
        p.setBrush(Qt::NoBrush);
        QPoint pos = mapFromGlobal(QCursor::pos());
        if (m_toolMode == AstroSpike::ToolMode::Add) {
            float r = m_brushRadius * m_zoom;
            p.drawEllipse(pos, (int)r, (int)r);
        } else {
            // Erase
            float r = m_eraserSize * m_zoom;
            p.drawEllipse(pos, (int)r, (int)r);
        }
    }
}

void AstroSpikeCanvas::createGlowSprite() {
    int size = 256;
    m_glowSprite = QImage(size, size, QImage::Format_ARGB32_Premultiplied);
    m_glowSprite.fill(Qt::transparent);
    
    QPainter p(&m_glowSprite);
    p.setRenderHint(QPainter::Antialiasing);
    
    QRadialGradient grad(size/2, size/2, size/2);
    grad.setColorAt(0, QColor(255, 255, 255, 255));
    grad.setColorAt(0.2, QColor(255, 255, 255, 100));
    grad.setColorAt(0.6, QColor(255, 255, 255, 15));
    grad.setColorAt(1.0, QColor(255, 255, 255, 0));
    
    p.setBrush(grad);
    p.setPen(Qt::NoPen);
    p.drawRect(0, 0, size, size);
}

QColor AstroSpikeCanvas::getStarColor(const AstroSpike::Star& star, float hueShift, float sat, float alpha) {
    QColor c = star.color;
    
    // Python Logic Copy
    float h = c.hueF();
    float s = c.saturationF();
    float l = c.lightnessF();
    
    if (h == -1) h = 0;

    float newH = h * 360.0f + hueShift;
    newH = std::fmod(newH, 360.0f);
    if (newH < 0) newH += 360.0f;
    newH /= 360.0f;
    
    float boostedS = std::min(1.0f, s * 16.0f);
    float finalS = 0;
    float finalL = 0;
    
    if (sat <= 1.0f) {
        finalS = boostedS * sat;
        finalL = std::max(l, 0.65f);
    } else {
        float hyper = sat - 1.0f;
        finalS = boostedS + (1.0f - boostedS) * hyper;
        float baseL = std::max(l, 0.65f);
        finalL = baseL + (0.5f - baseL) * hyper;
    }
    
    finalS = qBound(0.0f, finalS, 1.0f);
    finalL = qBound(0.4f, finalL, 0.95f);
    
    return QColor::fromHslF(newH, finalS, finalL, alpha);
}

// Replaces drawPreview with render
void AstroSpikeCanvas::render(QPainter& p, float scale, const QPointF& offset) {
    if (m_stars.isEmpty()) return;
    
    // Filter by quantity limit logic (just total cap)
    int limit = (int)(m_stars.size() * m_config.starAmount / 100.0f);
    int count = 0;
    
    p.setRenderHint(QPainter::Antialiasing);
    p.setCompositionMode(QPainter::CompositionMode_Screen);
    
    float degToRad = M_PI / 180.0f;
    float angleRad = m_config.angle * degToRad;
    float secAngleRad = (m_config.angle + m_config.secondaryOffset) * degToRad;

    for (const auto& star : m_stars) {
        if (count++ >= limit) break;
        
        // Min Size Filter Logic
        // Original python: if radius < min_size: continue
        if (star.radius < m_config.minStarSize) continue;
        if (star.radius > m_config.maxStarSize) continue; // Optional max cap check if needed
        
        // ... (Flare Logic) same ...
        // ...
        
        float sx = star.x * scale + offset.x();
        float sy = star.y * scale + offset.y();
        
        if (m_config.softFlareIntensity > 0) {
             float glowR = (star.radius * m_config.softFlareSize * 0.4f + (star.radius * 2)) * scale;
             if (glowR > 2) {
                 float opacity = m_config.softFlareIntensity * 0.8f * star.brightness;
                 p.setOpacity(std::min(1.0f, opacity));
                 p.drawImage(QRectF(sx - glowR, sy - glowR, glowR*2, glowR*2), m_glowSprite);
             }
        }
        p.setOpacity(1.0);
        
        float radiusFactor = std::pow(star.radius, 1.2f);
        float baseLen = radiusFactor * (m_config.length / 40.0f) * m_config.globalScale * scale;
        float thick = std::max(0.5f, star.radius * m_config.spikeWidth * 0.15f * m_config.globalScale * scale);
        
        if (baseLen < 2) continue;
        
        QColor color = getStarColor(star, m_config.hueShift, m_config.colorSaturation, m_config.intensity);
        QColor secColor = getStarColor(star, m_config.hueShift, m_config.colorSaturation, m_config.secondaryIntensity);
        
        // --- FIX SPIKE QUANTITY LOGIC ---
        // User reports 4->2 spikes.
        // If config.quantity = 4, we want 4 rays (cross).
        // Each loop iteration draws 1 ray? 
        // Previously: quantity was lines? 
        // Let's implement full rays. 
        // 4 rays = 0, 90, 180, 270 (if symmetrical)
        // If the UI quantity is "points" (4 points):
        // We iterate N times. 360/N separation.
        
        if (m_config.intensity > 0) {
            float rainbowStr = (m_config.enableRainbow && m_config.rainbowSpikes) ? m_config.rainbowIntensity : 0.0f;
            
            // Check for min spikes
            int spikes = std::max(2, (int)m_config.quantity);
            
            for (int i = 0; i < spikes; ++i) {
                // Symmetrical distribution
                float theta = angleRad + (i * M_PI * 2.0f / spikes);
                
                // Draw single RAY from center outwards
                float dx = std::cos(theta);
                float dy = std::sin(theta);
                
                QPointF start(sx, sy); // Start exactly at center? Or offset?
                // Usually diffraction spikes go through center. 
                // We draw from center out.
                // NOTE: Previous logic might have been "lines" (drawing full diameter).
                // Rays is safer for odd numbers (3, 5).
                
                // Let's use slight offset to avoid center buildup
                QPointF rayStart(sx + dx * 1.5f * scale, sy + dy * 1.5f * scale);
                QPointF rayEnd(sx + dx * baseLen, sy + dy * baseLen);
                
                // 1. Standard Spike
                if (rainbowStr > 0) p.setOpacity(0.4);
                
                QLinearGradient grad(rayStart, rayEnd);
                grad.setColorAt(0, color);
                QColor endC = color;
                endC.setAlpha(0);
                grad.setColorAt(1, endC);
                
                QPen pen(QBrush(grad), thick, Qt::SolidLine, Qt::FlatCap);
                p.setPen(pen);
                p.drawLine(rayStart, rayEnd);
                
                p.setOpacity(1.0);
                
                // 2. Rainbow Overlay
                if (rainbowStr > 0) {
                     QLinearGradient rGrad(rayStart, rayEnd);
                     rGrad.setColorAt(0, color);
                     
                     int stops = 10;
                     for (int s=1; s<=stops; ++s) {
                         float pos = (float)s / stops;
                         if (pos > m_config.rainbowLength) break;
                         
                         float hue = std::fmod(pos * 360.0f * m_config.rainbowFrequency, 360.0f);
                         float a = std::min(1.0f, m_config.intensity * rainbowStr * 2.0f) * (1.0f - pos);
                         
                         QColor c = QColor::fromHslF(hue / 360.0f, 0.8f, 0.6f, std::min(1.0f, a));
                         rGrad.setColorAt(pos, c);
                     }
                     rGrad.setColorAt(1, QColor(0,0,0,0));
                     
                     QPen rPen(QBrush(rGrad), thick, Qt::SolidLine, Qt::FlatCap);
                     p.setPen(rPen);
                     p.drawLine(rayStart, rayEnd);
                }
            }
        }
        
        // Secondary Spikes
        if (m_config.secondaryIntensity > 0) {
            float secLen = baseLen * (m_config.secondaryLength / m_config.length);
            // Same logic: rays
            int spikes = std::max(2, (int)m_config.quantity);
            
            for (int i = 0; i < spikes; ++i) {
                float theta = secAngleRad + (i * M_PI * 2.0f / spikes);
                 float dx = std::cos(theta);
                 float dy = std::sin(theta);
                 
                 QPointF start(sx + dx * 2.0f * scale, sy + dy * 2.0f * scale);
                 QPointF end(sx + dx * secLen, sy + dy * secLen);
                 
                 QLinearGradient grad(start, end);
                 grad.setColorAt(0, secColor);
                 grad.setColorAt(1, QColor(0,0,0,0));
                 
                 QPen pen(QBrush(grad), thick * 0.6f, Qt::SolidLine, Qt::FlatCap);
                 p.setPen(pen);
                 p.drawLine(start, end);
            }
        }
        
        // ... (Halo logic constant) ...
        if (m_config.enableHalo && m_config.haloIntensity > 0) {
            float classScore = star.radius * star.brightness;
            float intensityWeight = std::pow(std::min(1.0f, classScore / 10.0f), 2.0f);
            
            if (intensityWeight > 0.01f) {
                float finalHaloInt = m_config.haloIntensity * intensityWeight;
                QColor haloColor = getStarColor(star, m_config.hueShift, m_config.haloSaturation, finalHaloInt);
                
                float rHalo = star.radius * m_config.haloScale * scale;
                if (rHalo > 0.5f) {
                    float blurExpand = m_config.haloBlur * 20.0f * scale;
                    float relWidth = rHalo * (m_config.haloWidth * 0.15f);
                    float innerR = std::max(0.0f, rHalo - relWidth/2.0f);
                    float outerR = rHalo + relWidth/2.0f;
                    float drawOuter = outerR + blurExpand;
                    
                    QRadialGradient grad(sx, sy, drawOuter);
                    float stopStart = innerR / drawOuter;
                    float stopEnd = outerR / drawOuter;
                    
                    grad.setColorAt(0, Qt::transparent);
                    grad.setColorAt(std::max(0.0f, stopStart - 0.05f), Qt::transparent);
                    grad.setColorAt((stopStart + stopEnd)/2.0f, haloColor);
                    grad.setColorAt(std::min(1.0f, stopEnd + 0.05f), Qt::transparent);
                    grad.setColorAt(1, Qt::transparent);
                    
                    p.setBrush(QBrush(grad));
                    p.setPen(Qt::NoPen);
                    p.drawEllipse(QPointF(sx, sy), drawOuter, drawOuter);
                }
            }
        }
    }
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);
}

void AstroSpikeCanvas::mousePressEvent(QMouseEvent* event) {
    if (m_toolMode == AstroSpike::ToolMode::None) {
        if (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton) {
            m_dragging = true;
            setCursor(Qt::ClosedHandCursor);
            m_lastMousePos = event->pos();
        }
    } else {
        // Handle Brush
        if (event->button() == Qt::LeftButton) {
            // Unproject mouse pos to image pos
            float drawW = m_image.width() * m_zoom;
            float drawH = m_image.height() * m_zoom;
            float offsetX = width() / 2.0f + m_panOffset.x() - drawW/2;
            float offsetY = height() / 2.0f + m_panOffset.y() - drawH/2;
            
            QPointF imgPos = (event->position() - QPointF(offsetX, offsetY)) / m_zoom;
            handleTool(imgPos);
        }
    }
}

void AstroSpikeCanvas::mouseMoveEvent(QMouseEvent* event) {
    if (m_dragging) {
        QPoint delta = event->pos() - m_lastMousePos;
        m_panOffset += delta;
        m_lastMousePos = event->pos();
        update();
    } else {
        update(); // For cursor circle
    }
}

void AstroSpikeCanvas::mouseReleaseEvent([[maybe_unused]] QMouseEvent* event) {
    if (m_dragging) {
        m_dragging = false;
        setCursor(Qt::OpenHandCursor);
    }
}

void AstroSpikeCanvas::wheelEvent(QWheelEvent* event) {
    float factor = event->angleDelta().y() > 0 ? 1.2f : (1.0f / 1.2f);
    zoomToPoint(event->position(), factor);
}

// Event Filter to block scroll on sliders
// Handled in main dialog logic via installEventFilter

void AstroSpikeCanvas::resizeEvent(QResizeEvent* event) {
    if (m_firstResize) {
        fitToView();
        m_firstResize = false;
    }
    QWidget::resizeEvent(event);
}

void AstroSpikeCanvas::handleTool(const QPointF& imgPos) {
    if (m_image.isNull()) return;
    
    if (m_toolMode == AstroSpike::ToolMode::Add) {
        AstroSpike::Star s;
        s.x = imgPos.x();
        s.y = imgPos.y();
        s.radius = m_brushRadius; // Use correct size
        s.brightness = 1.0f;
        s.color = Qt::white;
        
        QVector<AstroSpike::Star> newStars = m_stars;
        newStars.append(s);
        emit starsUpdated(newStars);
        
    } else if (m_toolMode == AstroSpike::ToolMode::Erase) {
        QVector<AstroSpike::Star> newStars;
        float rSq = m_eraserSize * m_eraserSize;
        
        bool changed = false;
        for (const auto& s : m_stars) {
             float dx = s.x - imgPos.x();
             float dy = s.y - imgPos.y();
             if (dx*dx + dy*dy > rSq) {
                 newStars.append(s); // Keep
             } else {
                 changed = true; // Remove
             }
        }
        if (changed) emit starsUpdated(newStars);
    }
}


// =============================================================================
// DIALOG IMPLEMENTATION
// =============================================================================

AstroSpikeDialog::AstroSpikeDialog(ImageViewer* viewer, QWidget* parent)
    : DialogBase(parent, tr("AstroSpike"), 1300, 700), m_viewer(viewer)
{
    setWindowFlags(windowFlags() | Qt::Dialog | Qt::WindowMaximizeButtonHint | Qt::WindowCloseButtonHint);
    setModal(true);
    
    // Setup UI
    setupUI();
    
    // Initial Config
    m_canvas->setConfig(m_config);
    
    // Auto Detect
    if (m_viewer && m_viewer->getBuffer().isValid()) {
        m_canvas->setImage(m_viewer->getBuffer().getDisplayImage(ImageBuffer::Display_Linear));
        m_detectTimer.start(200, this); // Trigger detection
    }

    if (parentWidget()) {
        move(parentWidget()->window()->geometry().center() - rect().center());
    }
}

// Event Filter to block scroll on sliders
bool AstroSpikeDialog::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::Wheel) {
        if (qobject_cast<QAbstractSlider*>(obj)) {
            event->ignore();
            return true; // Block wheel
        }
    }
    return QDialog::eventFilter(obj, event);
}

AstroSpikeDialog::~AstroSpikeDialog() {
    if (m_thread) {
        m_thread->wait();
        m_thread = nullptr;
    }
}

void AstroSpikeDialog::closeEvent(QCloseEvent* event) {
    if (m_thread && m_thread->isRunning()) {
        m_thread->wait();
    }
    QDialog::closeEvent(event);
}

void AstroSpikeDialog::timerEvent(QTimerEvent* event) {
    if (event->timerId() == m_detectTimer.timerId()) {
        m_detectTimer.stop();
        runDetection();
    }
}
// Continuing implementation... fixing timer logic properly via signals in next block or just direct call

void AstroSpikeDialog::runDetection() {
    if (!m_viewer) return;
    if (m_thread && m_thread->isRunning()) return;
    
    m_statusLabel->setText(tr("Detecting all stars..."));
    
    // Previous thread (if any) is handled by deleteLater; just null our pointer
    m_thread = new StarDetectionThread(m_viewer->getBuffer(), this);
    connect(m_thread, &StarDetectionThread::detectionComplete, this, &AstroSpikeDialog::onStarsDetected);
    connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
    m_thread->start();
}

void AstroSpikeDialog::onStarsDetected(const QVector<AstroSpike::Star>& stars) {
    // Cache the full pre-computed star list (sorted by brightness descending)
    m_allStars = stars;
    m_thread = nullptr; // Will be deleted by deleteLater
    
    m_statusLabel->setText(tr("Detected %1 total stars. Filtering...").arg(stars.size()));
    
    // Apply current threshold filter
    filterStarsByThreshold();
}

void AstroSpikeDialog::filterStarsByThreshold() {
    if (m_allStars.isEmpty()) return;
    
    // Map threshold slider (1-100) to internal brightness cutoff
    // Slider 1 -> internal 35 (low cutoff, many stars)
    // Slider 100 -> internal 100 (high cutoff, only brightest)
    float internalThresh = 35.0f + (m_config.threshold - 1.0f) * (65.0f / 99.0f);
    float cutoff = (internalThresh - 1.0f) / 99.0f; // 0.34 to 1.0
    
    // Since m_allStars is already sorted by brightness descending,
    // we can use a simple linear scan and stop early
    QVector<AstroSpike::Star> filtered;
    filtered.reserve(m_allStars.size());
    
    for (const auto& s : m_allStars) {
        if (s.brightness >= cutoff) {
            filtered.append(s);
        }
        // Since sorted by brightness desc, once we go below cutoff, all subsequent are too dim
        // BUT stars might not be perfectly monotonic after merge, so do full scan
    }
    
    m_statusLabel->setText(tr("%1 stars (of %2 total)").arg(filtered.size()).arg(m_allStars.size()));
    m_canvas->setStars(filtered);
    
    // Reset history with filtered result
    m_history.clear();
    m_history.append(filtered);
    m_historyIndex = 0;
    updateHistoryButtons();
}

void AstroSpikeDialog::onCanvasStarsUpdated(const QVector<AstroSpike::Star>& stars) {
    m_canvas->setStars(stars);
    pushHistory(stars);
}

void AstroSpikeDialog::pushHistory(const QVector<AstroSpike::Star>& stars) {
    if (m_historyIndex < m_history.size() - 1) {
        m_history.resize(m_historyIndex + 1);
    }
    m_history.append(stars);
    m_historyIndex++;
    updateHistoryButtons();
}

void AstroSpikeDialog::undo() {
    if (m_historyIndex > 0) {
        m_historyIndex--;
        m_canvas->setStars(m_history[m_historyIndex]);
        updateHistoryButtons();
    }
}

void AstroSpikeDialog::redo() {
    if (m_historyIndex < m_history.size() - 1) {
        m_historyIndex++;
        m_canvas->setStars(m_history[m_historyIndex]);
        updateHistoryButtons();
    }
}

void AstroSpikeDialog::updateHistoryButtons() {
    m_btnUndo->setEnabled(m_historyIndex > 0);
    m_btnRedo->setEnabled(m_historyIndex < m_history.size() - 1);
}

// UI Setup
void AstroSpikeDialog::setupUI() {
    QVBoxLayout* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    
    // Top Bar
    QWidget* topBar = new QWidget(this);
    topBar->setStyleSheet("background: #252526; border-bottom: 1px solid #333;");
    QHBoxLayout* topLayout = new QHBoxLayout(topBar);
    
    QPushButton* btnApply = new QPushButton(tr("Apply"), this);
    connect(btnApply, &QPushButton::clicked, this, &AstroSpikeDialog::applyToDocument);
    
    m_btnUndo = new QPushButton(tr("Undo"), this);
    m_btnUndo->setEnabled(false);
    connect(m_btnUndo, &QPushButton::clicked, this, &AstroSpikeDialog::undo);
    
    m_btnRedo = new QPushButton(tr("Redo"), this);
    m_btnRedo->setEnabled(false);
    connect(m_btnRedo, &QPushButton::clicked, this, &AstroSpikeDialog::redo);
    
    QPushButton* btnAdd = new QPushButton(tr("Add Star"), this);
    QPushButton* btnErase = new QPushButton(tr("Eraser"), this);
    
    connect(btnAdd, &QPushButton::clicked, [this](){ setToolMode(AstroSpike::ToolMode::Add); });
    connect(btnErase, &QPushButton::clicked, [this](){ setToolMode(AstroSpike::ToolMode::Erase); });
    
    m_statusLabel = new QLabel(tr("Ready"), this);
    m_statusLabel->setStyleSheet("color: #aaa; margin-left: 20px;");
    
    topLayout->addWidget(btnApply);
    topLayout->addSpacing(20);
    topLayout->addWidget(m_btnUndo);
    topLayout->addWidget(m_btnRedo);
    topLayout->addSpacing(20);
    topLayout->addWidget(btnAdd);
    topLayout->addWidget(btnErase);
    
    // Brush/Eraser Size Sliders in Toolbar
    topLayout->addSpacing(10);
    QLabel* lSize = new QLabel(tr("Size:"), this);
    lSize->setStyleSheet("color:#888");
    topLayout->addWidget(lSize);
    
    QSlider* sldSize = new QSlider(Qt::Horizontal);
    sldSize->setFixedWidth(80);
    sldSize->setRange(1, 50);
    sldSize->setValue(4);
    sldSize->installEventFilter(this);
    // connect(sldSize, &QSlider::sliderPressed, [this](){ m_canvas->setPreviewQuality(true); });
    // connect(sldSize, &QSlider::sliderReleased, [this](){ m_canvas->setPreviewQuality(false); });
    QLabel* lSizeVal = new QLabel("4", this);
    lSizeVal->setFixedWidth(25);
    
    connect(sldSize, &QSlider::valueChanged, [=](int v){ 
        m_canvas->setStarInputRadius(v); 
        lSizeVal->setText(QString::number(v)); 
    });
    
    topLayout->addWidget(sldSize);
    topLayout->addWidget(lSizeVal);
    
    QLabel* lErase = new QLabel(tr("Erase:"), this);
    lErase->setStyleSheet("color:#888");
    topLayout->addWidget(lErase);
    
    QSlider* sldErase = new QSlider(Qt::Horizontal);
    sldErase->setFixedWidth(80);
    sldErase->setRange(5, 200);
    sldErase->setValue(20);
    sldErase->installEventFilter(this);
    // connect(sldErase, &QSlider::sliderPressed, [this](){ m_canvas->setPreviewQuality(true); });
    // connect(sldErase, &QSlider::sliderReleased, [this](){ m_canvas->setPreviewQuality(false); });
    QLabel* lEraseVal = new QLabel("20", this);
    lEraseVal->setFixedWidth(25);
    
    connect(sldErase, &QSlider::valueChanged, [=](int v){ 
        m_canvas->setEraserInputSize(v); 
        lEraseVal->setText(QString::number(v)); 
    });
    
    topLayout->addWidget(sldErase);
    topLayout->addWidget(lEraseVal);
    topLayout->addWidget(m_statusLabel);
    topLayout->addStretch();
    
    // Zoom buttons
    QPushButton* btnZoomIn = new QPushButton(tr("+"), this);
    btnZoomIn->setFixedSize(28, 28);
    btnZoomIn->setToolTip(tr("Zoom In"));
    connect(btnZoomIn, &QPushButton::clicked, [this](){ m_canvas->zoomIn(); });
    topLayout->addWidget(btnZoomIn);
    
    QPushButton* btnZoomOut = new QPushButton(tr("−"), this);
    btnZoomOut->setFixedSize(28, 28);
    btnZoomOut->setToolTip(tr("Zoom Out"));
    connect(btnZoomOut, &QPushButton::clicked, [this](){ m_canvas->zoomOut(); });
    topLayout->addWidget(btnZoomOut);
    
    QPushButton* btnFit = new QPushButton(tr("Fit"), this);
    btnFit->setFixedSize(40, 28);
    btnFit->setToolTip(tr("Fit to Screen"));
    connect(btnFit, &QPushButton::clicked, [this](){ m_canvas->fitToView(); });
    topLayout->addWidget(btnFit);
    
    topLayout->addSpacing(10);
    
    // Reset button in toolbar
    QPushButton* btnReset = new QPushButton(tr("Reset"), this);
    btnReset->setToolTip(tr("Reset to Defaults"));
    connect(btnReset, &QPushButton::clicked, [this](){ resetConfig(); });
    topLayout->addWidget(btnReset);
    
    root->addWidget(topBar);
    
    // Content
    QWidget* content = new QWidget(this);
    QHBoxLayout* contentLayout = new QHBoxLayout(content);
    contentLayout->setContentsMargins(0,0,0,0);
    
    // Canvas
    m_canvas = new AstroSpikeCanvas(this);
    m_canvas->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_canvas->setMinimumSize(100, 100); // Allow shrinking
    connect(m_canvas, &AstroSpikeCanvas::starsUpdated, this, &AstroSpikeDialog::onCanvasStarsUpdated);
    contentLayout->addWidget(m_canvas, 1);
    
    // Controls
    m_controlsScroll = new QScrollArea(this);
    m_controlsScroll->setFixedWidth(300);
    m_controlsScroll->setWidgetResizable(true);
    
    QWidget* controlsWidget = new QWidget();
    QVBoxLayout* controlsLayout = new QVBoxLayout(controlsWidget);
    setupControls(controlsLayout);
    controlsLayout->addStretch();
    
    m_controlsScroll->setWidget(controlsWidget);
    contentLayout->addWidget(m_controlsScroll);
    
    root->addWidget(content);

    // Copyright at the very bottom
    QLabel* copyright = new QLabel(tr("© 2026 Fabio Tempera"), this);
    copyright->setAlignment(Qt::AlignRight);
    copyright->setStyleSheet("color: #888; font-size: 10px; padding: 3px 10px; background: #1e1e1e; border-top: 1px solid #333;");
    root->addWidget(copyright);
    
    // Force window to respect layout's minimum size (prevent cutting)
    root->setSizeConstraint(QLayout::SetMinimumSize);
}

void AstroSpikeDialog::setupControls(QVBoxLayout* layout) {
    // Detection
    QGroupBox* grpDetect = new QGroupBox(tr("Detection"));
    QVBoxLayout* lDetect = new QVBoxLayout(grpDetect);
    // Threshold now filters the pre-computed star list instantly (no re-detection)
    lDetect->addWidget(createSlider(tr("Threshold"), 1, 100, 1, m_config.threshold, &m_config.threshold, ""));
    lDetect->addWidget(createSlider(tr("Amount %"), 0, 100, 1, m_config.starAmount, &m_config.starAmount, "%"));
    lDetect->addWidget(createSlider(tr("Min Size"), 0, 100, 1, m_config.minStarSize, &m_config.minStarSize, ""));
    lDetect->addWidget(createSlider(tr("Max Size"), 10, 200, 1, m_config.maxStarSize, &m_config.maxStarSize, ""));
    layout->addWidget(grpDetect);
    
    // Geometry
    QGroupBox* grpGeo = new QGroupBox(tr("Spikes"));
    QVBoxLayout* lGeo = new QVBoxLayout(grpGeo);
    lGeo->addWidget(createSlider(tr("Points"), 2, 8, 1, m_config.quantity, &m_config.quantity, ""));
         
    lGeo->addWidget(createSlider(tr("Length"), 10, 800, 10, m_config.length, &m_config.length, ""));
    lGeo->addWidget(createSlider(tr("Angle"), 0, 180, 1, m_config.angle, &m_config.angle, "deg"));
    lGeo->addWidget(createSlider(tr("Thickness"), 0.1, 5.0, 0.1, m_config.spikeWidth, &m_config.spikeWidth, ""));
    lGeo->addWidget(createSlider(tr("Global Scale"), 0.1, 5.0, 0.1, m_config.globalScale, &m_config.globalScale, "x"));
    lGeo->addWidget(createSlider(tr("Intensity"), 0, 1.0, 0.05, m_config.intensity, &m_config.intensity, ""));
    layout->addWidget(grpGeo);

    // Appearance
    QGroupBox* grpApp = new QGroupBox(tr("Appearance"));
    QVBoxLayout* lApp = new QVBoxLayout(grpApp);
    lApp->addWidget(createSlider(tr("Saturation"), 0.0, 3.0, 0.1, m_config.colorSaturation, &m_config.colorSaturation, ""));
    lApp->addWidget(createSlider(tr("Hue Shift"), 0, 360, 5, m_config.hueShift, &m_config.hueShift, "deg"));
    layout->addWidget(grpApp);
    
    // Secondary Spikes
    QGroupBox* grpSec = new QGroupBox(tr("Secondary Spikes"));
    QVBoxLayout* lSec = new QVBoxLayout(grpSec);
    lSec->addWidget(createSlider(tr("Intensity"), 0, 1.0, 0.05, m_config.secondaryIntensity, &m_config.secondaryIntensity, ""));
    lSec->addWidget(createSlider(tr("Length"), 10, 500, 10, m_config.secondaryLength, &m_config.secondaryLength, ""));
    lSec->addWidget(createSlider(tr("Offset"), 0, 90, 1, m_config.secondaryOffset, &m_config.secondaryOffset, "deg"));
    layout->addWidget(grpSec);
    
    // Halo
    QGroupBox* grpHalo = new QGroupBox(tr("Halo"));
    QVBoxLayout* lHalo = new QVBoxLayout(grpHalo);
    QCheckBox* chkHalo = new QCheckBox(tr("Enable Halo"));
    chkHalo->setChecked(m_config.enableHalo);
    connect(chkHalo, &QCheckBox::toggled, [this](bool c){ m_config.enableHalo = c; onConfigChanged(); });
    lHalo->addWidget(chkHalo);
    lHalo->addWidget(createSlider(tr("Intensity"), 0, 2.0, 0.1, m_config.haloIntensity, &m_config.haloIntensity, ""));
    lHalo->addWidget(createSlider(tr("Scale"), 1.0, 20.0, 1.0, m_config.haloScale, &m_config.haloScale, "x"));
    lHalo->addWidget(createSlider(tr("Width"), 0.1, 5.0, 0.1, m_config.haloWidth, &m_config.haloWidth, ""));
    lHalo->addWidget(createSlider(tr("Blur"), 0.0, 2.0, 0.1, m_config.haloBlur, &m_config.haloBlur, ""));
    lHalo->addWidget(createSlider(tr("Saturation"), 0.0, 2.0, 0.1, m_config.haloSaturation, &m_config.haloSaturation, ""));
    layout->addWidget(grpHalo);
    
    // Rainbow
    QGroupBox* grpRain = new QGroupBox(tr("Rainbow"));
    QVBoxLayout* lRain = new QVBoxLayout(grpRain);
    QCheckBox* chkRain = new QCheckBox(tr("Enable Rainbow"));
    chkRain->setChecked(m_config.enableRainbow);
    connect(chkRain, &QCheckBox::toggled, [this](bool c){ m_config.enableRainbow = c; onConfigChanged(); });
    lRain->addWidget(chkRain);
    lRain->addWidget(createSlider(tr("Intensity"), 0, 2.0, 0.1, m_config.rainbowIntensity, &m_config.rainbowIntensity, ""));
    lRain->addWidget(createSlider(tr("Frequency"), 0.1, 5.0, 0.1, m_config.rainbowFrequency, &m_config.rainbowFrequency, ""));
    lRain->addWidget(createSlider(tr("Length"), 0.1, 1.0, 0.1, m_config.rainbowLength, (float*)&m_config.rainbowLength, "")); // cast strictness
    layout->addWidget(grpRain);
    
    // Soft Flare
    QGroupBox* grpFlare = new QGroupBox(tr("Flare"));
    QVBoxLayout* lFlare = new QVBoxLayout(grpFlare);
    lFlare->addWidget(createSlider(tr("Intensity"), 0, 1.0, 0.05, m_config.softFlareIntensity, &m_config.softFlareIntensity, ""));
    lFlare->addWidget(createSlider(tr("Size"), 1, 50, 1, m_config.softFlareSize, &m_config.softFlareSize, ""));
    layout->addWidget(grpFlare);
}

QWidget* AstroSpikeDialog::createSlider(const QString& label, float min, float max, float step, float initial, float* target, const QString& unit) {
    QWidget* w = new QWidget();
    QVBoxLayout* l = new QVBoxLayout(w);
    l->setContentsMargins(0, 5, 0, 5);
    l->setSpacing(2);
    
    QHBoxLayout* head = new QHBoxLayout();
    QLabel* lblName = new QLabel(label);
    QLabel* lblVal = new QLabel(QString::number(initial, 'f', 1) + unit);
    head->addWidget(lblName);
    head->addStretch();
    head->addWidget(lblVal);
    
    QSlider* slider = new QSlider(Qt::Horizontal);
    int steps = (max - min) / step;
    slider->setRange(0, steps);
    slider->setValue((initial - min) / step);
    
    // Install event filter to block scroll
    slider->installEventFilter(this);
    
    connect(slider, &QSlider::valueChanged, [=](int v){
        float fVal = min + v * step;
        lblVal->setText(QString::number(fVal, 'f', 1) + unit);
        if (target) {
            *target = fVal;
            onConfigChanged();
        }
    });
    
    l->addLayout(head);
    l->addWidget(slider);
    return w;
}

void AstroSpikeDialog::onConfigChanged() {
    m_canvas->setConfig(m_config);
    // Re-filter stars when threshold (or any config) changes — instant, no recompute
    filterStarsByThreshold();
}

// Force rebuild
void AstroSpikeDialog::setToolMode(AstroSpike::ToolMode mode) {
    m_canvas->setToolMode(mode);
}

void AstroSpikeDialog::resetConfig() {
    m_config = AstroSpike::Config();
    
    // Rebuild UI to reflect defaults
    if (m_controlsScroll) {
        QWidget* w = new QWidget();
        QVBoxLayout* l = new QVBoxLayout(w);
        setupControls(l);
        l->addStretch();
        
        // Use takeWidget() to safely remove ownership before deleting
        QWidget* old = m_controlsScroll->takeWidget();
        if (old) old->deleteLater();
        
        m_controlsScroll->setWidget(w);
    }
    
    onConfigChanged();
}

void AstroSpikeDialog::applyToDocument() {
    if (!m_viewer) return;
    
    // Get full resolution image in ARGB32 to ensure compatible format
    QImage fullImg = m_viewer->getBuffer().getDisplayImage(ImageBuffer::Display_Linear);
    if (fullImg.format() != QImage::Format_RGB32 && fullImg.format() != QImage::Format_ARGB32) {
        fullImg = fullImg.convertToFormat(QImage::Format_ARGB32);
    }
    
    // Render spikes to offscreen layer, blur, then composite
    QImage spikeLayer(fullImg.size(), QImage::Format_ARGB32_Premultiplied);
    spikeLayer.fill(Qt::transparent);
    {
        QPainter sp(&spikeLayer);
        m_canvas->render(sp, 1.0f, QPointF(0, 0));
    }
    
    // Apply Gaussian blur for softer spikes
    cv::Mat spikeMat(spikeLayer.height(), spikeLayer.width(), CV_8UC4,
                     spikeLayer.bits(), spikeLayer.bytesPerLine());
    cv::GaussianBlur(spikeMat, spikeMat, cv::Size(3, 3), 1.0, 1.0);
    
    // Composite over original
    QPainter p(&fullImg);
    p.setCompositionMode(QPainter::CompositionMode_Screen);
    p.drawImage(0, 0, spikeLayer);
    p.end();

    m_viewer->pushUndo();
    
    ImageBuffer newBuffer = m_viewer->getBuffer();
    
    // Safety check size match
    if (newBuffer.width() != fullImg.width() || newBuffer.height() != fullImg.height()) {
        // This shouldn't happen unless getDisplayImage resizes, which we assume it doesn't here
        return; 
    }

    std::vector<float>& data = newBuffer.data();
    int w = newBuffer.width();
    int c = newBuffer.channels();
    
    for(int y=0; y<fullImg.height(); ++y) {
        const QRgb* line = (const QRgb*)fullImg.constScanLine(y);
        for(int x=0; x<fullImg.width(); ++x) {
            float r = qRed(line[x]) / 255.0f;
            float g = qGreen(line[x]) / 255.0f;
            float b = qBlue(line[x]) / 255.0f;
            
            int idx = (y * w + x) * c;
            if (c >= 3) {
                data[idx+0] = r;
                data[idx+1] = g;
                data[idx+2] = b;
            } else if (c == 1) {
                // Convert back to gray? simple avg or luma
                data[idx] = 0.2126f * r + 0.7152f * g + 0.0722f * b;
            }
        }
    }
    
    m_viewer->setBuffer(newBuffer, m_viewer->windowTitle(), true);
    accept(); 
}

void AstroSpikeDialog::saveImage() {
    if (!m_viewer) return;
    QString filePath = QFileDialog::getSaveFileName(this, tr("Save Image"), "astrospike_output.png", 
                                                    tr("PNG Images (*.png);;JPEG Images (*.jpg);;TIFF Images (*.tif)"));
    if (filePath.isEmpty()) return;
    
    QImage tempImg = m_viewer->getBuffer().getDisplayImage(ImageBuffer::Display_Linear);
    if (tempImg.format() != QImage::Format_RGB32 && tempImg.format() != QImage::Format_ARGB32) {
        tempImg = tempImg.convertToFormat(QImage::Format_ARGB32);
    }
    
    QPainter p(&tempImg);
    m_canvas->render(p, 1.0f, QPointF(0, 0));
    p.end();
    
    tempImg.save(filePath);
    m_statusLabel->setText(tr("Saved to %1").arg(filePath));
}

