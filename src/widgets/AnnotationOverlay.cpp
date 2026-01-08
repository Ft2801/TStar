#include "AnnotationOverlay.h"
#include "ImageViewer.h"
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
    m_wcsVisible = visible;
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

QPointF AnnotationOverlay::mapToImage(const QPointF& widgetPos) const {
    if (!m_viewer) return widgetPos;
    return m_viewer->mapToScene(widgetPos.toPoint());
}

QPointF AnnotationOverlay::mapFromImage(const QPointF& imagePos) const {
    if (!m_viewer) return imagePos;
    return m_viewer->mapFromScene(imagePos);
}

void AnnotationOverlay::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    
    // Skip drawing if viewer is processing (to prevent crashes during stretch/resize)
    // Check the property that gets set during heavy operations
    if (property("isProcessing").toBool() || !m_viewer) {
        return;
    }
    
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Draw WCS objects if visible
    if (m_wcsVisible) {
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
    QFont font = painter.font();
    QList<QRectF> occupiedRects;
    for (const auto& obj : m_wcsObjects) {
        // Map Start Point
        QPointF imagePos(obj.pixelX, obj.pixelY);
        QPointF widgetPos = mapFromImage(imagePos);
        
        // Check if visible (broad check for optimization)
        if (!rect().contains(widgetPos.toPoint()) && !obj.isLine) {
             if (widgetPos.x() < -1000 || widgetPos.x() > width()+1000) continue;
        }
        
        // --- DRAWING LOGIC ---
        
        if (obj.isLine) {
            // CONSTELLATION LINE
            QPointF imagePosEnd(obj.pixelXEnd, obj.pixelYEnd);
            QPointF widgetPosEnd = mapFromImage(imagePosEnd);
            
            // Draw Line
            painter.setPen(QPen(QColor(100, 150, 255, 180), 2)); // Light Blue, semi-transparent
            painter.drawLine(widgetPos, widgetPosEnd);
            continue; // Skip the rest (label/circle)
        }
        
        // --- STANDARD OBJECT DRAWING ---
        
        // Color based on catalog type
        QColor color = Qt::cyan;
        if (obj.longType == "Messier") color = QColor(255, 220, 100); // Yellow-orange
        else if (obj.longType == "NGC") color = QColor(100, 200, 255); // Light blue
        else if (obj.longType == "IC") color = QColor(200, 150, 255); // Light purple
        else if (obj.longType == "Sh2") color = QColor(255, 100, 100); // Reddish
        else if (obj.longType == "LdN") color = QColor(200, 200, 200); // Greyish
        else if (obj.longType == "Star") color = QColor(255, 255, 200); // White-Yellow
        else if (obj.type == "Constellation") color = QColor(100, 150, 255); 

        // Draw marker
        double finalRadiusScreen = 0;
        
        if (obj.diameter > 0) {
            double pixScale = m_viewer ? m_viewer->pixelScale() : 1.0;
            if (pixScale <= 0) pixScale = 1.0;
            
            // obj.diameter is in arcmin -> arcsec = * 60
            // radius in arcsec = diameter * 30
            // radius in pixels (image coords) = radius_arcsec / pixScale
            double radiusImagePx = (obj.diameter * 30.0) / pixScale;
            
            // Convert to screen pixels by applying zoom
            double zoom = m_viewer ? m_viewer->zoomFactor() : 1.0;
            finalRadiusScreen = radiusImagePx * zoom;
        } else {
            // Point source default
            finalRadiusScreen = 3.0;
        }
        
             QString labelText = (obj.longType == "Star") ? formatStarName(obj.name) : obj.name;
             bool showLabel = !labelText.isEmpty();
             
             // Filter star labels by zoom
             if (obj.longType == "Star" && m_viewer->zoomFactor() <= 0.5) showLabel = false;
             
             // 1. Draw Marker (Circle or Crosshair)
             if (finalRadiusScreen > 5.0) {
                 painter.setPen(QPen(color, 1)); // Thin 1px
                 painter.setBrush(Qt::NoBrush);
                 painter.drawEllipse(widgetPos, finalRadiusScreen, finalRadiusScreen);
             } else {
                 double gap = (obj.longType=="Star") ? 3.0 : 5.0;
                 double len = (obj.longType=="Star") ? 7.0 : 10.0;
                 painter.setPen(QPen(color, 1)); // Thin 1px
                 
                 // Crosshair with gap
                 painter.drawLine(widgetPos - QPointF(0, gap + len), widgetPos - QPointF(0, gap));
                 painter.drawLine(widgetPos + QPointF(0, gap), widgetPos + QPointF(0, gap + len));
                 painter.drawLine(widgetPos - QPointF(gap + len, 0), widgetPos - QPointF(gap, 0));
                 painter.drawLine(widgetPos + QPointF(gap, 0), widgetPos + QPointF(gap + len, 0));
             }

             // 2. Draw Label (if enabled)
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
                     
                     painter.setPen(QPen(color, 1));
                     painter.drawLine(circleEdge, textPos);
                     
                     QPointF labelOrigin = textPos + QPointF(2, -2);
                     painter.setPen(color);
                     painter.drawText(labelOrigin, labelText);
                     
                     occupiedRects.append(QRectF(labelOrigin.x(), labelOrigin.y() - 15, labelW, labelH));
                 } else {
                     // Collison-aware placement for Small Objects
                     double offset = 15.0;
                     struct Pos { QPointF p; QRectF r; };
                     std::vector<Pos> candidates;
                     
                     // Right
                     QPointF pRight = widgetPos + QPointF(offset, labelH/2 - 2); 
                     candidates.push_back({ pRight, QRectF(pRight.x(), pRight.y() - labelH + 2, labelW, labelH) });
                     // Top
                     QPointF pTop = widgetPos + QPointF(-labelW/2, -offset);
                     candidates.push_back({ pTop, QRectF(pTop.x(), pTop.y() - labelH + 2, labelW, labelH) });
                     // Left
                     QPointF pLeft = widgetPos + QPointF(-offset - labelW, labelH/2 - 2);
                     candidates.push_back({ pLeft, QRectF(pLeft.x(), pLeft.y() - labelH + 2, labelW, labelH) });
                     // Bottom
                     QPointF pBottom = widgetPos + QPointF(-labelW/2, offset + labelH);
                     candidates.push_back({ pBottom, QRectF(pBottom.x(), pBottom.y() - labelH + 2, labelW, labelH) });
                     
                     int bestIdx = 0;
                     for (int i=0; i<(int)candidates.size(); ++i) {
                         bool collision = false;
                         for (const auto& occupied : occupiedRects) {
                             if (candidates[i].r.intersects(occupied)) {
                                 collision = true;
                                 break;
                             }
                         }
                         if (!collision) {
                             bestIdx = i;
                             break;
                         }
                     }
                     
                     painter.setPen(color);
                     painter.drawText(candidates[bestIdx].p, labelText);
                     occupiedRects.append(candidates[bestIdx].r);
                 }
             }
    }
}


void AnnotationOverlay::renderToPainter(QPainter& painter, const QRectF& imageRect) {
    // Adaptive scale for burning text/lines into high-res images
    // Base reference: 1000px height.
    double scaleM = std::max(1.0, imageRect.height() / 1000.0);
    
    // Setup Font
    QFont font = painter.font();
    font.setPointSizeF(12.0 * scaleM); 
    painter.setFont(font);

    double pixScale = m_viewer ? m_viewer->pixelScale() : 1.0;
    if (pixScale <= 0) pixScale = 1.0;

    // --- DRAW WCS OBJECTS (Now empty, but kept for logic consistency) ---
    for (const auto& obj : m_wcsObjects) {
        // Direct Image Coordinates
        QPointF imagePos(obj.pixelX, obj.pixelY);
        
        // Bounds heck (relaxed)
        if (!imageRect.contains(imagePos) && !obj.isLine) {
            // Allow some margin for labels
            if (imagePos.x() < -imageRect.width()*0.5 || imagePos.x() > imageRect.width()*1.5) continue;
        }

        // --- DRAWING LOGIC ---
        
        if (obj.isLine) {
            QPointF imagePosEnd(obj.pixelXEnd, obj.pixelYEnd);
            
            // Draw Line
            QPen pen(QColor(100, 150, 255, 255), 2.0 * scaleM); 
            pen.setColor(QColor(100, 150, 255, 200));
            painter.setPen(pen);
            painter.drawLine(imagePos, imagePosEnd);
            continue; 
        }
        
        // Color based on catalog type
        QColor color = Qt::cyan;
        if (obj.longType == "Messier") color = QColor(255, 220, 100); 
        else if (obj.longType == "NGC") color = QColor(100, 200, 255); 
        else if (obj.longType == "IC") color = QColor(200, 150, 255); 
        else if (obj.longType == "Sh2") color = QColor(255, 100, 100); 
        else if (obj.longType == "LdN") color = QColor(200, 200, 200); 
        else if (obj.longType == "Star") color = QColor(255, 255, 200); 
        else if (obj.type == "Constellation") color = QColor(100, 150, 255); 

        // Draw marker
        double radiusImagePx = 0;
        
        if (obj.diameter > 0) {
            // obj.diameter (arcmin) -> radius (arcsec) * 60 / 2 = diam * 30
            radiusImagePx = (obj.diameter * 30.0) / pixScale;
        } else {
            // Point source
            radiusImagePx = 3.0 * scaleM; 
        }
        
        if (radiusImagePx > 5.0 * scaleM) {
             double angle = -M_PI / 4.0;
             
             auto getPt = [&](double r, double a) {
                 return imagePos + QPointF(r * std::cos(a), r * std::sin(a));
             };
             
             QPointF edge = getPt(radiusImagePx, angle);
             QPointF textPos = getPt(radiusImagePx * 1.3, angle);
             
             // Draw Circle
             painter.setPen(QPen(color, 1.0 * scaleM)); // Thin
             painter.setBrush(Qt::NoBrush);
             painter.drawEllipse(imagePos, radiusImagePx, radiusImagePx);
             
             // Draw Leader
             painter.drawLine(edge, textPos);
             
             // Draw Text
             painter.setPen(color);
             painter.drawText(textPos + QPointF(2.0*scaleM, -2.0*scaleM), formatStarName(obj.name));

        } else {
             // Crosshair Gap
             double gap = (obj.longType=="Star") ? 3.0 * scaleM : 5.0 * scaleM;
             double len = (obj.longType=="Star") ? 7.0 * scaleM : 10.0 * scaleM;
             
             painter.setPen(QPen(color, 1.0 * scaleM)); // Thin
             
             painter.drawLine(imagePos - QPointF(0, gap + len), imagePos - QPointF(0, gap));
             painter.drawLine(imagePos + QPointF(0, gap), imagePos + QPointF(0, gap + len));
             painter.drawLine(imagePos - QPointF(gap + len, 0), imagePos - QPointF(gap, 0));
             painter.drawLine(imagePos + QPointF(gap, 0), imagePos + QPointF(gap + len, 0));
             
             // Label
             if (!obj.name.isEmpty()) {
                  // Simple offset for small objects
                  painter.drawText(imagePos + QPointF(gap + len + 3.0*scaleM, 4.0*scaleM), formatStarName(obj.name));
             }
        }
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

void AnnotationOverlay::mousePressEvent(QMouseEvent* event) {
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
