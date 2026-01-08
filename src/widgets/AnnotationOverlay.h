#ifndef ANNOTATION_OVERLAY_H
#define ANNOTATION_OVERLAY_H

#include <QWidget>
#include <QPainter>
#include <QMouseEvent>
#include <QVector>
#include <QString>

class ImageViewer;

// Annotation types
enum class AnnotationType {
    Circle,
    Rectangle,
    Arrow,
    Text,
    WCSObject  // Auto-generated from catalog
};

struct Annotation {
    AnnotationType type;
    QPointF start;      // In image coordinates
    QPointF end;        // For arrows/rectangles
    double radius = 0;  // For circles
    QString text;       // For text/WCS labels
    QColor color = Qt::yellow;
    int penWidth = 2;
    bool visible = true;
};

// WCS Catalog Object
struct CatalogObject {
    double ra = 0;
    double dec = 0;
    QString name;
    QString alias; // Added
    QString type;
    QString longType;
    double diameter = 0;  // arcmin
    double mag = 99;
    double redshift = 0;
    double pmra = 0;  // mas/yr
    double pmdec = 0; // mas/yr
    double pixelX = 0;
    double pixelY = 0;
    
    // For line segments (e.g. Constellations)
    double raEnd = 0;
    double decEnd = 0;
    double pixelXEnd = 0;
    double pixelYEnd = 0;
    bool isLine = false;
};

class AnnotationOverlay : public QWidget {
    Q_OBJECT
public:
    enum class DrawMode {
        None,
        Circle,
        Rectangle,
        Arrow,
        Text
    };

    explicit AnnotationOverlay(ImageViewer* parent);
    ~AnnotationOverlay();

    void setDrawMode(DrawMode mode);
    DrawMode drawMode() const { return m_drawMode; }

    void setDrawColor(const QColor& color) { m_drawColor = color; }
    QColor drawColor() const { return m_drawColor; }
    
    // Pending text for text mode
    void setPendingText(const QString& text) { m_pendingText = text; }
    QString pendingText() const { return m_pendingText; }

    // WCS Annotations
    void setWCSObjectsVisible(bool visible);
    bool wcsObjectsVisible() const { return m_wcsVisible; }
    void setWCSObjects(const QVector<CatalogObject>& objects);
    const QVector<CatalogObject>& wcsObjects() const { return m_wcsObjects; }

    // Manual Annotations (with undo/redo support)
    void clearManualAnnotations();
    void clearWCSAnnotations();
    const QVector<Annotation>& annotations() const { return m_annotations; }
    void setAnnotations(const QVector<Annotation>& annotations);
    void addAnnotation(const Annotation& ann);
    void placeTextAt(const QPointF& imagePos, const QString& text, const QColor& color);

    // Render directly to painter for saving images (e.g. burn-in)
    void renderToPainter(QPainter& painter, const QRectF& imageRect);

signals:
    void aboutToAddAnnotation();    // Fires BEFORE annotation is added (for undo)
    void annotationAdded(const Annotation& ann);  // Fires AFTER annotation is added
    void textInputRequested(const QPointF& imagePos);  // User clicked in text mode
    void requestCatalogQuery();  // Emitted when user wants to query catalogs

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;

private:
    void drawAnnotation(QPainter& painter, const Annotation& ann);
    void drawWCSObjects(QPainter& painter);
    QPointF mapToImage(const QPointF& widgetPos) const;
    QPointF mapFromImage(const QPointF& imagePos) const;

    ImageViewer* m_viewer;
    DrawMode m_drawMode = DrawMode::None;
    QColor m_drawColor = Qt::yellow;
    QString m_pendingText = "Label";

    // Drawing state
    bool m_isDrawing = false;
    QPointF m_drawStart;
    QPointF m_drawCurrent;

    // Annotations
    QVector<Annotation> m_annotations;

    // WCS Objects
    QVector<CatalogObject> m_wcsObjects;
    bool m_wcsVisible = false;
};

#endif // ANNOTATION_OVERLAY_H
