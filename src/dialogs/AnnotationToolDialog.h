#ifndef ANNOTATION_TOOL_DIALOG_H
#define ANNOTATION_TOOL_DIALOG_H

#include "DialogBase.h"
#include <QPushButton>
#include <QLabel>
#include <QStack>
#include <QVector>
#include <QPointer>
#include "../ImageViewer.h"
#include <QComboBox>
#include <QButtonGroup>
#include <QToolButton>
#include "../ImageBuffer.h"

class ImageViewer;
class MainWindowCallbacks;
class AnnotationOverlay;
struct CatalogObject;
struct Annotation;

class AnnotationToolDialog : public DialogBase {
    Q_OBJECT
public:
    explicit AnnotationToolDialog(QWidget* parent = nullptr);
    ~AnnotationToolDialog();

    void setViewer(ImageViewer* viewer);

    void renderAnnotations(QPainter& painter, const QRectF& imageRect);

private slots:
    void onToolSelected(int toolId);

    void onClearAnnotations();
    void onColorChanged(int index);
    void onAboutToAddAnnotation();  // Called BEFORE annotation added
    void onTextInputRequested(const QPointF& imagePos);  // Text mode click
    void onUndo();
    void onRedo();


private:
    void setupUI();

    void promptForTextInput();
    void pushUndoState();
    void updateUndoRedoButtons();

    QPointer<ImageViewer> m_viewer;
    AnnotationOverlay* m_overlay = nullptr;

    // Tool buttons
    QButtonGroup* m_toolGroup;
    QToolButton* m_selectBtn;
    QToolButton* m_circleBtn;
    QToolButton* m_rectBtn;
    QToolButton* m_arrowBtn;
    QToolButton* m_textBtn;
    
    // Filtering


    // Options
    QComboBox* m_colorCombo;
    QPushButton* m_clearBtn;
    QPushButton* m_undoBtn;
    QPushButton* m_redoBtn;

    // Status
    QLabel* m_statusLabel;


    // Pending text for text tool
    QString m_pendingText = "Label";

    // Undo/Redo stacks
    QStack<QVector<Annotation>> m_undoStack;
    QStack<QVector<Annotation>> m_redoStack;

    // WCS data from viewer

};

#endif // ANNOTATION_TOOL_DIALOG_H
