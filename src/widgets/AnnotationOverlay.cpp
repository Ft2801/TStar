#include "AnnotationOverlay.h"
#include "ImageViewer.h"
#include "../astrometry/WCSUtils.h"
#include <QPainter>
#include <QMouseEvent>
#include <cmath>
#include <QFile>
#include <QDir>
#include <QDateTime>
#include <QTextStream>

static QString formatStarName(QString name) {
    if (name.isEmpty()) return name;
    
    struct GreekMap { const char* lat; const char* grk; };
    static const GreekMap greekTable[] = {
        {"Alp", "\u03B1"}, {"Bet", "\u03B2"}, {"Gam", "\u03B3"}, {"Del", "\u03B4"},
        {"Eps", "\u03B5"}, {"Zet", "\u03B6"}, {"Eta", "\u03B7"}, {"The", "\u03B8"},
        {"Iot", "\u03B9"}, {"Kap", "\u03BA"}, {"Lam", "\u03BB"}, {"Mu",  "\u03BC"},
        {"Nu",  "\u03BD"}, {"Xi",  "\u03BE"}, {"Omi", "\u03BF"}, {"Pi",  "\u03C0"},
        {"Rho", "\u03C1"}, {"Sig", "\u03C3"}, {"Tau", "\u03C4"}, {"Ups", "\u03C5"},
        {"Phi", "\u03C6"}, {"Chi", "\u03C7"}, {"Psi", "\u03C8"}, {"Ome", "\u03C9"},
        {nullptr, nullptr}
    };

    QString pretty = name;
    for (const auto* p = greekTable; p->lat != nullptr; ++p) {
        if (pretty.startsWith(p->lat)) { // Generally starts with Greek letter if it's a Bayer name
             // Replace only the first occurrence to avoid weirdness
             pretty.replace(0, 3, p->grk);
             break; // Bayer names usually have one Greek letter at start
        }
    }
    return pretty;
}

AnnotationOverlay::AnnotationOverlay(ImageViewer* parent)
    : QWidget(parent)
    , m_viewer(parent)
{
    setAttribute(Qt::WA_TranslucentBackground, true);
    setMouseTracking(true);
    setDrawMode(DrawMode::None); // Correctly initializes TransparentForMouseEvents
}

AnnotationOverlay::~AnnotationOverlay() {
    // Log to file for debugging
    QFile logFile(QDir::homePath() + "/TStar_annotation_debug.log");
    if (logFile.open(QIODevice::Append | QIODevice::Text)) {
        QTextStream out(&logFile);
        out << QDateTime::currentDateTime().toString("hh:mm:ss.zzz") << " | [AnnotationOverlay::~DESTRUCTOR] Destroying overlay with " << m_annotations.size() << " annotations\n";
        logFile.close();
    }
}

void AnnotationOverlay::setDrawMode(DrawMode mode) {
    m_drawMode = mode;
    if (mode == DrawMode::None) {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
    } else {
        setAttribute(Qt::WA_TransparentForMouseEvents, false);
    }
    update();
}

void AnnotationOverlay::setWCSObjectsVisible(bool visible) {
    m_wcsObjectsVisible = visible;
    update();
}

void AnnotationOverlay::setWCSObjects(const QVector<CatalogObject>& objects) {
    m_wcsObjects = objects;
    update();
}

void AnnotationOverlay::clearManualAnnotations() {
    m_annotations.clear();
    update();
}

void AnnotationOverlay::clearWCSAnnotations() {
    m_wcsObjects.clear();
    update();
}

void AnnotationOverlay::setAnnotations(const QVector<Annotation>& annotations) {
    m_annotations = annotations;
    update();
}

void AnnotationOverlay::addAnnotation(const Annotation& ann) {
    emit aboutToAddAnnotation();  // Signal for undo
    m_annotations.append(ann);
    emit annotationAdded(ann);
    update();
}

void AnnotationOverlay::placeTextAt(const QPointF& imagePos, const QString& text, const QColor& color) {
    Annotation ann;
    ann.type = AnnotationType::Text;
    ann.start = imagePos;
    ann.text = text;
    ann.color = color;
    ann.penWidth = 2;
    ann.visible = true;
    
    // DON'T emit aboutToAddAnnotation here - dialog already did
    m_annotations.append(ann);
    emit annotationAdded(ann);
    update();
}

void AnnotationOverlay::setWCSGridVisible(bool visible) {
    if (m_wcsGridVisible != visible) {
        m_wcsGridVisible = visible;
        update();
    }
}

void AnnotationOverlay::setCompassVisible(bool visible) {
    if (m_compassVisible != visible) {
        m_compassVisible = visible;
        update();
    }
}

void AnnotationOverlay::setCompassPosition(CompassPosition pos) {
    if (m_compassPosition != pos) {
        m_compassPosition = pos;
        update();
    }
}

// Returns the image-space anchor point for the compass given the current position setting.
// The anchor is placed with a margin relative to the compass arm length so the arrows
// always remain fully inside the image.
static QPointF compassAnchorInImage(AnnotationOverlay::CompassPosition pos, int w, int h) {
    // margin ≈ 1.5× the arm length (arm = h/20)
    double margin = h / 13.0;
    switch (pos) {
    case AnnotationOverlay::CompassPosition::TopLeft:     return QPointF(margin,       margin);
    case AnnotationOverlay::CompassPosition::TopRight:    return QPointF(w - margin,   margin);
    case AnnotationOverlay::CompassPosition::BottomLeft:  return QPointF(margin,       h - margin);
    case AnnotationOverlay::CompassPosition::BottomRight: return QPointF(w - margin,   h - margin);
    default: /* Center */                                 return QPointF(w / 2.0,      h / 2.0);
    }
}

void AnnotationOverlay::drawWCSGrid(QPainter& painter) {
    if (!m_viewer) return;
    auto meta = m_viewer->getBuffer().metadata();
    if (!WCSUtils::hasValidWCS(meta)) return;

    painter.save();
    QPen gridPen(QColor(100, 150, 255, 120), 1, Qt::DashLine);
    painter.setPen(gridPen);
    painter.setRenderHint(QPainter::Antialiasing);

    // Estimate field of view to determine grid step size
    double fovX, fovY;
    int w = m_viewer->getBuffer().width();
    int h = m_viewer->getBuffer().height();
    if (!WCSUtils::getFieldOfView(meta, w, h, fovX, fovY)) {
        painter.restore();
        return;
    }

    double range = std::sqrt(fovX * fovX + fovY * fovY) * 0.5;
    double step;

    // DEC step logic
    if (range > 16.0) step = 8.0;
    else if (range > 8.0) step = 4.0;
    else if (range > 4.0) step = 2.0;
    else if (range > 2.0) step = 1.0;
    else if (range > 1.0) step = 0.5;
    else if (range > 0.5) step = 0.25;
    else if (range > 0.3) step = 1.0/6.0;
    else step = 1.0/12.0;

    double centerRa, centerDec;
    WCSUtils::pixelToWorld(meta, w/2.0, h/2.0, centerRa, centerDec);
    
    // RA step logic: scale by cos(dec) and snap to "nice" values
    static const double ra_values[] = { 45, 30, 15, 10, 7.5, 5, 3.75, 2.5, 1.5, 1.25, 1, 0.75, 0.5, 0.25, 0.16666, 0.125, 0.08333, 0.0625, 0.04166, 0.025, 0.02083 };
    double decRad = centerDec * M_PI / 180.0;
    double step2 = std::min(45.0, step / (std::cos(decRad) + 0.000001));
    double stepRA = 45.0;
    for (double val : ra_values) {
        if (val < step2) {
            stepRA = val;
            break;
        }
    }

    // Determine RA and DEC bounds of the image
    double cRa[4], cDec[4];
    WCSUtils::pixelToWorld(meta, 0, 0, cRa[0], cDec[0]);
    WCSUtils::pixelToWorld(meta, w, 0, cRa[1], cDec[1]);
    WCSUtils::pixelToWorld(meta, w, h, cRa[2], cDec[2]);
    WCSUtils::pixelToWorld(meta, 0, h, cRa[3], cDec[3]);

    double minRa = cRa[0], maxRa = cRa[0];
    double minDec = cDec[0], maxDec = cDec[0];
    for (int i=1; i<4; ++i) {
        if (cRa[i] < minRa) minRa = cRa[i];
        if (cRa[i] > maxRa) maxRa = cRa[i];
        if (cDec[i] < minDec) minDec = cDec[i];
        if (cDec[i] > maxDec) maxDec = cDec[i];
    }

    if (maxRa - minRa > 180.0) { minRa = 0.0; maxRa = 360.0; }
    minRa -= stepRA; maxRa += stepRA;
    minDec -= step; maxDec += step;
    minDec = std::max(-90.0, minDec);
    maxDec = std::min(90.0, maxDec);
    
    QFont font = painter.font();
    font.setPointSize(8);
    painter.setFont(font);

    auto drawPath = [&](bool isRA, double constantVal, double startVal, double endVal) {
        QPainterPath path;
        bool first = true;
        int pts = 50;
        QPointF borderPoint;
        bool foundBorder = false;

        for (int i = 0; i <= pts; ++i) {
            double v = startVal + (endVal - startVal) * (i / (double)pts);
            double ra = isRA ? constantVal : v;
            double dec = isRA ? v : constantVal;
            
            double px, py;
            if (WCSUtils::worldToPixel(meta, ra, dec, px, py)) {
                QPointF widgetPt = mapFromImage(QPointF(px, py));
                if (first) {
                    path.moveTo(widgetPt);
                    first = false;
                } else {
                    path.lineTo(widgetPt);
                }

                // Identify if this point is near the widget border for label placement
                if (!foundBorder) {
                    if (widgetPt.x() > 5 && widgetPt.x() < width() - 50 &&
                        widgetPt.y() > 5 && widgetPt.y() < height() - 20) {
                        borderPoint = widgetPt;
                        foundBorder = true;
                    }
                }
            }
        }
        painter.drawPath(path);
        
        // Draw coordinate label
        if (foundBorder && m_viewer->zoomFactor() > 0.05) {
            QString labelText;
            if (isRA) {
                double hVal = constantVal / 15.0;
                int hours = (int)hVal;
                double mRemain = (hVal - hours) * 60.0;
                int mins = (int)mRemain;
                if (stepRA < 0.25) { // Under 1 minute precision
                    int secs = (int)((mRemain - mins) * 60.0 + 0.5);
                    labelText = QString("%1h %2m %3s").arg(hours).arg(mins, 2, 10, QChar('0')).arg(secs, 2, 10, QChar('0'));
                } else {
                    labelText = QString("%1h %2m").arg(hours).arg(mins, 2, 10, QChar('0'));
                }
            } else {
                if (step < 0.1) {
                    double arcmin = std::abs(constantVal - (int)constantVal) * 60.0;
                    labelText = QString("%1° %2'").arg((int)constantVal).arg((int)(arcmin + 0.5), 2, 10, QChar('0'));
                } else {
                    labelText = QString("%1°").arg(constantVal, 0, 'f', 1);
                }
            }
            painter.setPen(QColor(150, 200, 255, 200));
            painter.drawText(QRectF(borderPoint.x() + 3, borderPoint.y() + 3, 100, 20), Qt::AlignLeft | Qt::AlignTop, labelText);
            painter.setPen(gridPen);
        }
    };

    // Draw Meridians (RA)
    double startRa = std::floor(minRa / stepRA) * stepRA;
    for (double ra = startRa; ra <= maxRa; ra += stepRA) {
        double r = ra;
        while (r < 0) r += 360.0;
        while (r >= 360.0) r -= 360.0;
        drawPath(true, r, minDec, maxDec);
    }

    // Draw Parallels (Dec)
    double startDec = std::floor(minDec / step) * step;
    for (double dec = startDec; dec <= maxDec; dec += step) {
        drawPath(false, dec, minRa, maxRa);
    }

    painter.restore();
}

QPointF AnnotationOverlay::mapToImage(const QPointF& widgetPos) const {
    if (!m_viewer) return widgetPos;
    return m_viewer->mapToScene(widgetPos.toPoint());
}

// Removed QPoint AnnotationOverlay::imageToWidget(const QPointF& imgPt) const

QPointF AnnotationOverlay::mapFromImage(const QPointF& imagePos) const {
    if (!m_viewer) return imagePos;
    return m_viewer->mapFromScene(imagePos);
}

void AnnotationOverlay::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    
    if (!m_viewer) return; // Added check
    // Skip drawing if viewer is processing (to prevent crashes during stretch/resize)
    // Check the property that gets set during heavy operations
    if (property("isProcessing").toBool()) { // Modified condition
        return;
    }
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    if (m_wcsGridVisible && WCSUtils::hasValidWCS(m_viewer->getBuffer().metadata())) {
        drawWCSGrid(painter);
    }
    
    // Draw compass if visible
    if (m_compassVisible && WCSUtils::hasValidWCS(m_viewer->getBuffer().metadata())) {
        drawCompass(painter);
    }
    
    // Draw WCS objects if visible
    if (m_wcsObjectsVisible) {
        drawWCSObjects(painter);
    }

    // Draw manual annotations
    for (const auto& ann : m_annotations) {
        if (ann.visible) {
            drawAnnotation(painter, ann);
        }
    }

    // Draw current drawing in progress
    if (m_isDrawing && m_drawMode != DrawMode::None && m_drawMode != DrawMode::Text) {
        QPointF startWidget = mapFromImage(m_drawStart);
        QPointF currentWidget = mapFromImage(m_drawCurrent);
        
        switch (m_drawMode) {
            case DrawMode::Circle: {
                double radius = std::sqrt(
                    std::pow(currentWidget.x() - startWidget.x(), 2) +
                    std::pow(currentWidget.y() - startWidget.y(), 2)
                );
                painter.setPen(QPen(m_drawColor, 2, Qt::DashLine));
                painter.setBrush(Qt::NoBrush);
                painter.drawEllipse(startWidget, radius, radius);
                break;
            }
            case DrawMode::Rectangle: {
                painter.setPen(QPen(m_drawColor, 2, Qt::DashLine));
                painter.setBrush(Qt::NoBrush);
                painter.drawRect(QRectF(startWidget, currentWidget).normalized());
                break;
            }
            case DrawMode::Arrow: {
                painter.setPen(QPen(m_drawColor, 2, Qt::DashLine));
                painter.drawLine(startWidget, currentWidget);
                
                // Draw arrowhead
                double angle = std::atan2(currentWidget.y() - startWidget.y(),
                                         currentWidget.x() - startWidget.x());
                double arrowSize = 15;
                QPointF p1(currentWidget.x() - arrowSize * std::cos(angle - M_PI/6),
                           currentWidget.y() - arrowSize * std::sin(angle - M_PI/6));
                QPointF p2(currentWidget.x() - arrowSize * std::cos(angle + M_PI/6),
                           currentWidget.y() - arrowSize * std::sin(angle + M_PI/6));
                painter.drawLine(currentWidget, p1);
                painter.drawLine(currentWidget, p2);
                break;
            }
            default:
                break;
        }
    }
}

void AnnotationOverlay::drawAnnotation(QPainter& painter, const Annotation& ann) {
    QPen pen(ann.color, ann.penWidth);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    
    QPointF startWidget = mapFromImage(ann.start);
    QPointF endWidget = mapFromImage(ann.end);
    
    switch (ann.type) {
        case AnnotationType::Circle: {
            double radiusWidget = ann.radius * m_viewer->zoomFactor();
            painter.drawEllipse(startWidget, radiusWidget, radiusWidget);
            break;
        }
        case AnnotationType::Rectangle: {
            painter.drawRect(QRectF(startWidget, endWidget).normalized());
            break;
        }
        case AnnotationType::Arrow: {
            painter.drawLine(startWidget, endWidget);
            
            // Arrowhead
            double angle = std::atan2(endWidget.y() - startWidget.y(),
                                     endWidget.x() - startWidget.x());
            double arrowSize = 12;
            QPointF p1(endWidget.x() - arrowSize * std::cos(angle - M_PI/6),
                       endWidget.y() - arrowSize * std::sin(angle - M_PI/6));
            QPointF p2(endWidget.x() - arrowSize * std::cos(angle + M_PI/6),
                       endWidget.y() - arrowSize * std::sin(angle + M_PI/6));
            painter.drawLine(endWidget, p1);
            painter.drawLine(endWidget, p2);
            break;
        }
        case AnnotationType::Text: {
            QFont font = painter.font();
            font.setPointSize(12);
            painter.setFont(font);
            painter.drawText(startWidget, ann.text);
            break;
        }
        default:
            break;
    }
}

void AnnotationOverlay::drawWCSObjects(QPainter& painter) {
    if (!m_viewer) return;
    
    QFont font = painter.font();
    font.setPointSize(10);
    painter.setFont(font);

    auto meta = m_viewer->getBuffer().metadata();

    QList<QRectF> occupiedRects;
    int imgW = m_viewer->getBuffer().width();
    int imgH = m_viewer->getBuffer().height();

    for (const CatalogObject& obj : m_wcsObjects) {
        double px, py;
        if (!WCSUtils::worldToPixel(meta, obj.ra, obj.dec, px, py)) continue;
        
        // --- STRICT IMAGE CLIPPING ---
        if (!obj.isLine) {
            // Standard objects must be inside the image buffer
            if (px < 0 || px >= imgW || py < 0 || py >= imgH) continue;
        }

        QPointF imagePos(px, py);
        QPointF widgetPos = mapFromImage(imagePos);
        
        // --- DRAWING LOGIC ---
        
        if (obj.isLine) {
            // CONSTELLATION LINE
            double pxEnd, pyEnd;
            if (!WCSUtils::worldToPixel(meta, obj.raEnd, obj.decEnd, pxEnd, pyEnd)) continue;
            
            QPointF imagePosEnd(pxEnd, pyEnd);
            QPointF widgetPosEnd = mapFromImage(imagePosEnd);
            
            // Clip to viewport to avoid drawing outside the image area
            painter.save();
            QPointF p0 = mapFromImage(QPointF(0, 0));
            QPointF p1 = mapFromImage(QPointF(imgW, imgH));
            painter.setClipRect(QRectF(p0, p1).normalized());
            painter.setPen(QPen(QColor(100, 150, 255, 180), 2)); // Light Blue, semi-transparent
            painter.drawLine(widgetPos, widgetPosEnd);
            painter.restore();
            continue; 
        }
        
        // --- STANDARD OBJECT DRAWING ---
        
        // Color based on catalog type
        QColor color = Qt::cyan;
        if (obj.longType == "Messier") color = QColor(255, 200, 50);   // Golden Yellow
        else if (obj.longType == "NGC") color = QColor(80, 180, 255);  // Sky Blue
        else if (obj.longType == "IC") color = QColor(200, 120, 255);  // Purple
        else if (obj.longType == "Sh2") color = QColor(255, 80, 80);   // Soft Red
        else if (obj.longType == "LdN") color = QColor(160, 160, 160); // Dark Grey
        else if (obj.longType == "Star") color = QColor(255, 255, 220); // Warm White
        else if (obj.type == "Constellation") color = QColor(120, 160, 255); // Blue-ish

        // Draw marker
        double finalRadiusScreen = 3.0; // Point source default
        if (obj.diameter > 0) {
            double pixScale = m_viewer->pixelScale();
            if (pixScale > 0) {
                double radiusImagePx = (obj.diameter * 30.0) / pixScale;
                finalRadiusScreen = radiusImagePx * m_viewer->zoomFactor();
                
                QRectF markerRect(widgetPos.x() - finalRadiusScreen, widgetPos.y() - finalRadiusScreen, 
                                  finalRadiusScreen * 2, finalRadiusScreen * 2);
                QPointF p0_v = mapFromImage(QPointF(0, 0));
                QPointF p1_v = mapFromImage(QPointF(imgW, imgH));
                QRectF imgRectWidget = QRectF(p0_v, p1_v).normalized();
                
                if (imgRectWidget.contains(markerRect.center()) && !imgRectWidget.contains(markerRect)) {
                    // Object is partially clipped - always reduce by 10%
                    finalRadiusScreen *= 0.9;
                }
            }
        }
        
        bool isConstellationName = (obj.longType == "ConstellationName");
        
        if (obj.longType == "Star") {
            finalRadiusScreen = 3.0; // Force cross-hair for stars
        }        // --- LABEL PREPARATION ---
        QString labelText;
        if (obj.longType == "Star") {
            if (!obj.alias.isEmpty()) {
                labelText = obj.alias.split('/').first();
            } else {
                QString namePart = formatStarName(obj.name);
                bool isBayer = (namePart.length() > 0 && (unsigned short)namePart.at(0).unicode() >= 0x0370 && (unsigned short)namePart.at(0).unicode() <= 0x03FF);
                bool isFlamsteed = !namePart.isEmpty() && namePart.at(0).isDigit() && namePart.contains(' ');
                
                if (isBayer || isFlamsteed) {
                    labelText = namePart;
                } else {
                    labelText = ""; // Hide catalog numbers
                }
            }
        } else if (isConstellationName) {
            labelText = obj.name;
        } else {
            // "gli alis SOLO PER LE STELLE NON ANCHE PER GLI ALTRI OGGETTI DA CATALOGO"
            labelText = obj.name;
        }

        bool showLabel = !labelText.isEmpty();
        // zoomFactor check: disabled for stars
        // if (obj.longType == "Star" && m_viewer->zoomFactor() <= 0.4 && obj.alias.isEmpty()) {
        //     showLabel = false;
        // }
        
        // 1. Draw Marker (skip for constellation names)
        if (!isConstellationName && finalRadiusScreen > 5.0) {
            painter.setPen(QPen(color, 1));
            painter.setBrush(Qt::NoBrush);
            painter.drawEllipse(widgetPos, finalRadiusScreen, finalRadiusScreen);
        } else {
            double gap = (obj.longType=="Star") ? 3.0 : 5.0;
            double len = (obj.longType=="Star") ? 7.0 : 10.0;
            painter.setPen(QPen(color, 1));
            painter.drawLine(widgetPos - QPointF(0, gap + len), widgetPos - QPointF(0, gap));
            painter.drawLine(widgetPos + QPointF(0, gap), widgetPos + QPointF(0, gap + len));
            painter.drawLine(widgetPos - QPointF(gap + len, 0), widgetPos - QPointF(gap, 0));
            painter.drawLine(widgetPos + QPointF(gap, 0), widgetPos + QPointF(gap + len, 0));
        }

        // 2. Draw Label
        if (showLabel) {
            QFontMetricsF fm(painter.font());
            QRectF textRect = fm.boundingRect(labelText);
            double labelW = textRect.width();
            double labelH = textRect.height();
            
            if (finalRadiusScreen > 5.0) {
                double angle = -M_PI / 4.0; 
                auto getCirclePoint = [&](double r, double a) {
                    return widgetPos + QPointF(r * std::cos(a), r * std::sin(a));
                };
                
                QPointF circleEdge = getCirclePoint(finalRadiusScreen, angle);
                QPointF textPos = getCirclePoint(finalRadiusScreen * 1.3, angle); 
                
                // EDGE AWARENESS: if textPos is out of widget bounds, flip angle 180 deg
                if (textPos.x() < 20 || textPos.x() > width() - labelW - 20 || 
                    textPos.y() < 20 || textPos.y() > height() - 20) {
                    angle += M_PI; 
                    circleEdge = getCirclePoint(finalRadiusScreen, angle);
                    textPos = getCirclePoint(finalRadiusScreen * 1.3, angle);
                }

                painter.setPen(QPen(color, 1));
                painter.drawLine(circleEdge, textPos);
                
                QPointF labelOrigin = textPos + QPointF(2, -2);
                
                // Edge push-in
                if (labelOrigin.x() < 5) labelOrigin.setX(5);
                if (labelOrigin.x() > width() - labelW - 5) labelOrigin.setX(width() - labelW - 5);
                if (labelOrigin.y() < 15) labelOrigin.setY(15);
                if (labelOrigin.y() > height() - 5) labelOrigin.setY(height() - 5);

                painter.setPen(color);
                painter.drawText(labelOrigin, labelText);
                occupiedRects.append(QRectF(labelOrigin.x(), labelOrigin.y() - 15, labelW, labelH));
            } else {
                double offset = 15.0;
                struct Pos { QPointF p; QRectF r; };
                std::vector<Pos> candidates;
                
                QPointF pRight = widgetPos + QPointF(offset, labelH/2 - 2); 
                candidates.push_back({ pRight, QRectF(pRight.x(), pRight.y() - labelH + 2, labelW, labelH) });
                QPointF pTop = widgetPos + QPointF(-labelW/2, -offset);
                candidates.push_back({ pTop, QRectF(pTop.x(), pTop.y() - labelH + 2, labelW, labelH) });
                QPointF pLeft = widgetPos + QPointF(-offset - labelW, labelH/2 - 2);
                candidates.push_back({ pLeft, QRectF(pLeft.x(), pLeft.y() - labelH + 2, labelW, labelH) });
                QPointF pBottom = widgetPos + QPointF(-labelW/2, offset + labelH);
                candidates.push_back({ pBottom, QRectF(pBottom.x(), pBottom.y() - labelH + 2, labelW, labelH) });
                
                Pos best = candidates[0];
                bool found = false;
                for (const auto& cand : candidates) {
                    bool collision = false;
                    if (cand.r.left() < 5 || cand.r.right() > width() - 5 || 
                        cand.r.top() < 5 || cand.r.bottom() > height() - 5) {
                        collision = true;
                    }
                    if (!collision) {
                        for (const auto& occupied : occupiedRects) {
                            if (cand.r.intersects(occupied)) { collision = true; break; }
                        }
                    }
                    if (!collision) { best = cand; found = true; break; }
                }
                
                if (!found) {
                    if (best.r.left() < 5) best.p.rx() += (5 - best.r.left());
                    if (best.r.right() > width() - 5) { best.p.rx() -= (best.r.right() - (width() - 5)); best.r.moveRight(width() - 5); }
                    if (best.r.top() < 5) best.p.ry() += (5 - best.r.top());
                    if (best.r.bottom() > height() - 5) { best.p.ry() -= (best.r.bottom() - (height() - 5)); best.r.moveBottom(height() - 5); }
                }

                painter.setPen(color);
                painter.drawText(best.p, labelText);
                occupiedRects.append(best.r);
            }
        }
    }
}

void AnnotationOverlay::renderToPainter(QPainter& painter, const QRectF& imageRect) {
    if (!m_viewer) return; 
    
    // Calculate what the zoom factor WOULD BE if the user clicked "Fit to window"
    // ImageViewer uses 0.98 safety margin.
    double vwrW = m_viewer->viewport()->width();
    double vwrH = m_viewer->viewport()->height();
    double imgW = m_viewer->getBuffer().width();
    double imgH = m_viewer->getBuffer().height();
    
    double zoomX = vwrW / imgW;
    double zoomY = vwrH / imgH;
    double fittedZoom = std::min(zoomX, zoomY) * 0.98;
    
    if (fittedZoom <= 0) fittedZoom = 0.1;
    double scaleM = 1.0 / fittedZoom;
    
    // Setup Font
    QFont font = painter.font();
    font.setPointSizeF(10.0 * scaleM); 
    painter.setFont(font);

    if (m_wcsGridVisible) {
        drawWCSGridToImage(painter, imageRect, scaleM);
    }

    double pixScale = m_viewer->pixelScale();
    if (pixScale <= 0) pixScale = 1.0;
    
    if (m_wcsObjectsVisible) {
        // We draw WCS objects using the same logic but adapted for image pixels
        // (The user's previous manual edit had a loop here, I'll use it or a better one)
        for (const auto& obj : m_wcsObjects) {
            double tx, ty;
            if (!WCSUtils::worldToPixel(m_viewer->getBuffer().metadata(), obj.ra, obj.dec, tx, ty)) continue;
            QPointF imagePos(tx, ty);
            
            if (tx < -200 || tx > imageRect.width() + 200 || ty < -200 || ty > imageRect.height() + 200) continue;

            if (obj.isLine) {
                double tx2, ty2;
                if (WCSUtils::worldToPixel(m_viewer->getBuffer().metadata(), obj.raEnd, obj.decEnd, tx2, ty2)) {
                    painter.save();
                    painter.setClipRect(imageRect);
                    painter.setPen(QPen(QColor(100, 150, 255, 180), 2.0 * scaleM));
                    painter.drawLine(imagePos, QPointF(tx2, ty2));
                    painter.restore();
                }
                continue;
            }

            QColor color = Qt::cyan;
            if (obj.longType == "Messier") color = QColor(255, 200, 50); 
            else if (obj.longType == "NGC") color = QColor(80, 180, 255); 
            else if (obj.longType == "IC") color = QColor(200, 120, 255); 
            else if (obj.longType == "Sh2") color = QColor(255, 80, 80); 
            else if (obj.longType == "LdN") color = QColor(160, 160, 160); 
            else if (obj.longType == "Star") color = QColor(255, 255, 220); 
            else if (obj.type == "Constellation") color = QColor(120, 160, 255); 

            double radiusImagePx = 3.0 * scaleM;
            if (obj.diameter > 0 && obj.longType != "Star") {
                radiusImagePx = (obj.diameter * 30.0) / pixScale;
                
                // --- 10% SCALING FOR PARTIALLY CLIPPED LARGE OBJECTS ---
                QRectF markerRect(imagePos.x() - radiusImagePx, imagePos.y() - radiusImagePx, 
                                  radiusImagePx * 2, radiusImagePx * 2);
                if (imageRect.contains(markerRect.center()) && !imageRect.contains(markerRect)) {
                    // Object is partially clipped - reduce by 10% to match screen view
                    radiusImagePx *= 0.9;
                }
            }

            // Draw Marker
            if (obj.longType != "ConstellationName") {
                painter.setPen(QPen(color, 1.0 * scaleM));
                if (radiusImagePx > 5.0 * scaleM) {
                    painter.setBrush(Qt::NoBrush);
                    painter.drawEllipse(imagePos, radiusImagePx, radiusImagePx);
                } else {
                    double gap = (obj.longType=="Star") ? 3.0 * scaleM : 5.0 * scaleM;
                    double len = (obj.longType=="Star") ? 7.0 * scaleM : 10.0 * scaleM;
                    painter.drawLine(imagePos - QPointF(0, gap + len), imagePos - QPointF(0, gap));
                    painter.drawLine(imagePos + QPointF(0, gap), imagePos + QPointF(0, gap + len));
                    painter.drawLine(imagePos - QPointF(gap + len, 0), imagePos - QPointF(gap, 0));
                    painter.drawLine(imagePos + QPointF(gap, 0), imagePos + QPointF(gap + len, 0));
                }
            }

            // Label
            QString labelText;
            if (obj.longType == "Star") {
                if (!obj.alias.isEmpty()) labelText = obj.alias.split('/').first();
                else {
                    QString namePart = formatStarName(obj.name);
                    bool isBayer = (namePart.length() > 0 && (unsigned short)namePart.at(0).unicode() >= 0x0370 && (unsigned short)namePart.at(0).unicode() <= 0x03FF);
                    bool isFlamsteed = !namePart.isEmpty() && namePart.at(0).isDigit() && namePart.contains(' ');
                    if (isBayer || isFlamsteed) labelText = namePart;
                }
            } else labelText = obj.name;

            if (!labelText.isEmpty()) {
                double offset = 15.0 * scaleM;
                painter.setPen(QPen(color, 1.0 * scaleM));
                if (radiusImagePx > 5.0 * scaleM) {
                    double angle = -M_PI / 4.0;
                    QPointF tp = imagePos + QPointF(radiusImagePx * 1.3 * std::cos(angle), radiusImagePx * 1.3 * std::sin(angle));
                    if (tp.x() < 10 || tp.x() > imageRect.width() - 50 || tp.y() < 10 || tp.y() > imageRect.height() - 10) {
                        angle += M_PI;
                        tp = imagePos + QPointF(radiusImagePx * 1.3 * std::cos(angle), radiusImagePx * 1.3 * std::sin(angle));
                    }
                    if (obj.longType != "ConstellationName") {
                         painter.drawLine(imagePos + QPointF(radiusImagePx * std::cos(angle), radiusImagePx * std::sin(angle)), tp);
                    }
                    painter.drawText(tp + QPointF(2.0*scaleM, -2.0*scaleM), labelText);
                } else {
                    QFontMetricsF ifm(painter.font());
                    painter.drawText(imagePos + QPointF(offset, ifm.height()/2 - 2*scaleM), labelText);
                }
            }
        }
    }
    
    // Draw compass if visible
    if (m_compassVisible) {
        drawCompassToImage(painter, imageRect, scaleM);
    }

    // --- DRAW MANUAL ANNOTATIONS ---
    for (const auto& ann : m_annotations) {
        if (!ann.visible) continue;

        QPen pen(ann.color, std::max(1.0, ann.penWidth * scaleM));
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        
        QPointF start = ann.start; // Already in image coordinates
        QPointF end = ann.end;     // Already in image coordinates
        
        switch (ann.type) {
            case AnnotationType::Circle: {
                // Radius is in image pixels
                double radius = ann.radius; 
                painter.drawEllipse(start, radius, radius);
                break;
            }
            case AnnotationType::Rectangle: {
                painter.drawRect(QRectF(start, end).normalized());
                break;
            }
            case AnnotationType::Arrow: {
                painter.drawLine(start, end);
                
                // Arrowhead
                double angle = std::atan2(end.y() - start.y(),
                                         end.x() - start.x());
                double arrowSize = 12.0 * scaleM;
                QPointF p1(end.x() - arrowSize * std::cos(angle - M_PI/6),
                           end.y() - arrowSize * std::sin(angle - M_PI/6));
                QPointF p2(end.x() - arrowSize * std::cos(angle + M_PI/6),
                           end.y() - arrowSize * std::sin(angle + M_PI/6));
                painter.drawLine(end, p1);
                painter.drawLine(end, p2);
                break;
            }
            case AnnotationType::Text: {
                // Setup Font for Text
                QFont f = painter.font();
                f.setPointSizeF(12.0 * scaleM); // Consistent size
                painter.setFont(f);
                painter.drawText(start, ann.text);
                break;
            }
            default: break;
        }
    }
}

void AnnotationOverlay::drawWCSGridToImage(QPainter& painter, const QRectF& imageRect, double scaleM) {
    if (!m_viewer) return;
    auto meta = m_viewer->getBuffer().metadata();
    if (!WCSUtils::hasValidWCS(meta)) return;
    int w = m_viewer->getBuffer().width();
    int h = m_viewer->getBuffer().height();
    
    painter.save();
    painter.setClipRect(imageRect);
    painter.setRenderHint(QPainter::Antialiasing);

    QPen gridPen(QColor(100, 150, 255, 170), std::max(1.0, 1.2 * scaleM), Qt::DashLine);
    painter.setPen(gridPen);

    double fovX, fovY;
    if (!WCSUtils::getFieldOfView(meta, w, h, fovX, fovY)) { painter.restore(); return; }
    
    double range = std::sqrt(fovX*fovX + fovY*fovY) * 0.5;
    double step = (range > 16.0) ? 8.0 : (range > 8.0) ? 4.0 : (range > 4.0) ? 2.0 : (range > 2.0) ? 1.0 : (range > 1.0) ? 0.5 : (range > 0.5) ? 0.25 : (range > 0.3) ? 1.0/6.0 : 1.0/12.0;

    double cRa[4], cDec[4];
    WCSUtils::pixelToWorld(meta, 0, 0, cRa[0], cDec[0]);
    WCSUtils::pixelToWorld(meta, w, 0, cRa[1], cDec[1]);
    WCSUtils::pixelToWorld(meta, w, h, cRa[2], cDec[2]);
    WCSUtils::pixelToWorld(meta, 0, h, cRa[3], cDec[3]);
    double minRa = cRa[0], maxRa = cRa[0], minDec = cDec[0], maxDec = cDec[0];
    for(int i=1; i<4; ++i) {
        if(cRa[i] < minRa) minRa = cRa[i];
        if(cRa[i] > maxRa) maxRa = cRa[i];
        if(cDec[i] < minDec) minDec = cDec[i];
        if(cDec[i] > maxDec) maxDec = cDec[i];
    }
    if (maxRa-minRa > 180) { minRa=0; maxRa=360; }
    double stepRA = step; // Simplified for this implementation
    
    auto drawPath = [&](bool isRA, double constantVal, double startVal, double endVal) {
        QPainterPath path;
        bool first = true;
        QPointF labelPoint; bool foundLabel = false;
        for (int i=0; i<=72; ++i) {
            double v = startVal + (endVal-startVal)*(i/72.0);
            double px, py;
            if (!WCSUtils::worldToPixel(meta, isRA?constantVal:v, isRA?v:constantVal, px, py)) continue;
            QPointF p(px, py);
            if (first) { path.moveTo(p); first=false; } else path.lineTo(p);
            if (!foundLabel && imageRect.adjusted(20,20,-100,-20).contains(p)) { labelPoint=p; foundLabel=true; }
        }
        if (!first) painter.drawPath(path);
        if (foundLabel) {
            QString txt = isRA ? QString("%1h").arg(constantVal/15.0, 0, 'f', 1) : QString("%1\u00B0").arg(constantVal, 0, 'f', 1);
            painter.setPen(QColor(150, 200, 255, 215));
            painter.drawText(labelPoint + QPointF(5*scaleM, 5*scaleM), txt);
            painter.setPen(gridPen);
        }
    };

    QFont gFont = painter.font(); gFont.setPointSizeF(8.0 * scaleM); painter.setFont(gFont);
    for (double ra = std::floor(minRa/stepRA)*stepRA; ra <= maxRa; ra += stepRA) drawPath(true, std::fmod(ra+360.0, 360.0), minDec, maxDec);
    for (double dec = std::floor(minDec/step)*step; dec <= maxDec; dec += step) drawPath(false, dec, minRa, maxRa);
    painter.restore();
}

void AnnotationOverlay::drawCompass(QPainter& painter) {
    if (!m_viewer) return;
    auto meta = m_viewer->getBuffer().metadata();
    if (!WCSUtils::hasValidWCS(meta)) return;
    
    int w = m_viewer->getBuffer().width();
    int h = m_viewer->getBuffer().height();

    // Anchor point in image coordinates (depends on m_compassPosition)
    QPointF anchorImg = compassAnchorInImage(m_compassPosition, w, h);

    double anchorRa, anchorDec;
    if (!WCSUtils::pixelToWorld(meta, anchorImg.x(), anchorImg.y(), anchorRa, anchorDec)) return;
    
    if (90.0 - std::abs(anchorDec) < 2.78e-3) return; // anchor too close to a pole
    
    double northPixX, northPixY, eastPixX, eastPixY;
    if (!WCSUtils::worldToPixel(meta, anchorRa, anchorDec + 0.1, northPixX, northPixY)) return;
    if (!WCSUtils::worldToPixel(meta, anchorRa + 0.1, anchorDec, eastPixX, eastPixY)) return;
    
    QPointF centerWidgetPos = mapFromImage(anchorImg);
    QPointF northWidgetPos  = mapFromImage(QPointF(northPixX, northPixY));
    QPointF eastWidgetPos   = mapFromImage(QPointF(eastPixX,  eastPixY));
    
    double angleN = std::atan2(northWidgetPos.y() - centerWidgetPos.y(), 
                               northWidgetPos.x() - centerWidgetPos.x());
                               
    double angleE = std::atan2(eastWidgetPos.y() - centerWidgetPos.y(), 
                               eastWidgetPos.x() - centerWidgetPos.x());
                               
    double len_scaled = ((double)h / 20.0) * m_viewer->zoomFactor();
    
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(Qt::red, 3.0));
    painter.setBrush(Qt::red);
    
    QFont font = painter.font();
    font.setPointSizeF(len_scaled / 3.0);
    painter.setFont(font);
    
    // Draw North
    painter.save();
    painter.translate(centerWidgetPos);
    painter.rotate(angleN * 180.0 / M_PI);
    
    painter.drawLine(QPointF(0, 0), QPointF(len_scaled, 0));
    
    QPainterPath northArrow;
    northArrow.moveTo(len_scaled, 0);
    northArrow.lineTo(0.75 * len_scaled, -0.15 * len_scaled);
    northArrow.lineTo(0.75 * len_scaled, 0.15 * len_scaled);
    northArrow.closeSubpath();
    painter.drawPath(northArrow);
    
    painter.translate(len_scaled * 1.3, 0.1 * len_scaled);
    painter.rotate(-angleN * 180.0 / M_PI);
    
    QFontMetricsF fm(font);
    QRectF textBoundsN = fm.boundingRect("N");
    painter.drawText(QRectF(-textBoundsN.width()/2, -textBoundsN.height()/2, textBoundsN.width(), textBoundsN.height()), Qt::AlignCenter, "N");
    painter.restore();
    
    // Draw East
    painter.save();
    painter.translate(centerWidgetPos);
    painter.rotate(angleE * 180.0 / M_PI);
    
    painter.setPen(QPen(Qt::white, 3.0));
    painter.setBrush(Qt::white);
    
    painter.drawLine(QPointF(0, 0), QPointF(len_scaled / 2.0, 0));
    
    painter.translate(len_scaled, -0.1 * len_scaled);
    painter.rotate(-angleE * 180.0 / M_PI);
    
    QRectF textBoundsE = fm.boundingRect("E");
    painter.drawText(QRectF(-textBoundsE.width()/2, -textBoundsE.height()/2, textBoundsE.width(), textBoundsE.height()), Qt::AlignCenter, "E");
    painter.restore();
    
    painter.restore();
}

void AnnotationOverlay::drawCompassToImage(QPainter& painter, const QRectF& imageRect, double scaleM) {
    if (!m_viewer) return;
    auto meta = m_viewer->getBuffer().metadata();
    if (!WCSUtils::hasValidWCS(meta)) return;
    
    int w = m_viewer->getBuffer().width();
    int h = m_viewer->getBuffer().height();

    // Anchor point in image coordinates (depends on m_compassPosition)
    QPointF anchorImg = compassAnchorInImage(m_compassPosition, w, h);

    double anchorRa, anchorDec;
    if (!WCSUtils::pixelToWorld(meta, anchorImg.x(), anchorImg.y(), anchorRa, anchorDec)) return;
    
    if (90.0 - std::abs(anchorDec) < 2.78e-3) return; // anchor too close to a pole
    
    double northPixX, northPixY, eastPixX, eastPixY;
    if (!WCSUtils::worldToPixel(meta, anchorRa, anchorDec + 0.1, northPixX, northPixY)) return;
    if (!WCSUtils::worldToPixel(meta, anchorRa + 0.1, anchorDec, eastPixX, eastPixY)) return;
    
    // When drawing directly to image, scaleM compensates for painter zoom.
    // The image pixel coordinates act as the base geometry.
    QPointF centerImagePos = anchorImg;
    QPointF northImagePos(northPixX, northPixY);
    QPointF eastImagePos(eastPixX, eastPixY);
    
    double angleN = std::atan2(northImagePos.y() - centerImagePos.y(), 
                               northImagePos.x() - centerImagePos.x());
                               
    double angleE = std::atan2(eastImagePos.y() - centerImagePos.y(), 
                               eastImagePos.x() - centerImagePos.x());
                               
    double len = (double)h / 20.0;
    
    painter.save();
    painter.setClipRect(imageRect);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(Qt::red, 3.0 * scaleM));
    painter.setBrush(Qt::red);
    
    QFont font = painter.font();
    font.setPointSizeF((len / 3.0) * scaleM);
    painter.setFont(font);
    
    // Draw North
    painter.save();
    painter.translate(centerImagePos);
    painter.rotate(angleN * 180.0 / M_PI);
    
    painter.drawLine(QPointF(0, 0), QPointF(len, 0));
    
    QPainterPath northArrow;
    northArrow.moveTo(len, 0);
    northArrow.lineTo(0.75 * len, -0.15 * len);
    northArrow.lineTo(0.75 * len, 0.15 * len);
    northArrow.closeSubpath();
    painter.drawPath(northArrow);
    
    painter.translate(len * 1.3, 0.1 * len);
    painter.rotate(-angleN * 180.0 / M_PI);
    
    QFontMetricsF fm(font);
    QRectF textBoundsN = fm.boundingRect("N");
    painter.drawText(QRectF(-textBoundsN.width()/2, -textBoundsN.height()/2, textBoundsN.width(), textBoundsN.height()), Qt::AlignCenter, "N");
    painter.restore();
    
    // Draw East
    painter.save();
    painter.translate(centerImagePos);
    painter.rotate(angleE * 180.0 / M_PI);
    
    painter.setPen(QPen(Qt::white, 3.0 * scaleM));
    painter.setBrush(Qt::white);
    
    painter.drawLine(QPointF(0, 0), QPointF(len / 2.0, 0));
    
    painter.translate(len, -0.1 * len);
    painter.rotate(-angleE * 180.0 / M_PI);
    
    QRectF textBoundsE = fm.boundingRect("E");
    painter.drawText(QRectF(-textBoundsE.width()/2, -textBoundsE.height()/2, textBoundsE.width(), textBoundsE.height()), Qt::AlignCenter, "E");
    painter.restore();
    
    painter.restore();
}

void AnnotationOverlay::mousePressEvent(QMouseEvent* event) {
    if (!m_viewer) { // Added check
        event->ignore();
        return;
    }
    // If not in drawing mode, explicitly ignore to let parent (ImageViewer) handle pan/zoom
    // BUT: we should technically set WA_TransparentForMouseEvents when mode is None
    // efficiently to avoid even needing this.
    if (m_drawMode == DrawMode::None) {
        event->ignore();
        return;
    }
    
    if (event->button() == Qt::LeftButton) {
        m_drawStart = mapToImage(event->pos());
        
        // For text mode, emit signal to request text input from dialog
        if (m_drawMode == DrawMode::Text) {
            emit textInputRequested(m_drawStart);  // Let dialog handle input
            event->accept();
            return;
        }
        
        m_isDrawing = true;
        m_drawCurrent = m_drawStart;
        event->accept();
    } else {
        event->ignore();
    }
}

void AnnotationOverlay::mouseMoveEvent(QMouseEvent* event) {
    if (!m_viewer) { // Added check
        event->ignore();
        return;
    }
    if (m_isDrawing) {
        m_drawCurrent = mapToImage(event->pos());
        update();
        event->accept();
    } else {
        // Essential for pan/zoom to work when mouse is moving over overlay
        event->ignore(); 
    }
}

void AnnotationOverlay::mouseReleaseEvent(QMouseEvent* event) {
    if (!m_viewer) { // Added check
        event->ignore();
        return;
    }
    if (!m_isDrawing) {
        event->ignore();
        return;
    }
    
    m_isDrawing = false; // Reset first logic

    
    // Create the annotation
    Annotation ann;
    ann.start = m_drawStart;
    ann.end = m_drawCurrent;
    ann.color = m_drawColor;
    ann.penWidth = 2;
    ann.visible = true;
    
    switch (m_drawMode) {
        case DrawMode::Circle:
            ann.type = AnnotationType::Circle;
            ann.radius = std::sqrt(
                std::pow(m_drawCurrent.x() - m_drawStart.x(), 2) +
                std::pow(m_drawCurrent.y() - m_drawStart.y(), 2)
            );
            break;
        case DrawMode::Rectangle:
            ann.type = AnnotationType::Rectangle;
            break;
        case DrawMode::Arrow:
            ann.type = AnnotationType::Arrow;
            break;
        default:
            return;
    }
    
    // Signal BEFORE adding for undo
    emit aboutToAddAnnotation();
    m_annotations.append(ann);
    emit annotationAdded(ann);
    update();
    event->accept();
}
