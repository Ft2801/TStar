#ifndef PLATESOLVINGDIALOG_H
#define PLATESOLVINGDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include "astrometry/SimbadSearcher.h"
#include "astrometry/NativePlateSolver.h"

// For passing the image buffer
#include "../ImageBuffer.h"
#include <QPointer>

class PlateSolvingDialog : public QDialog {
    Q_OBJECT
public:
    explicit PlateSolvingDialog(QWidget* parent = nullptr);
    
    // Instead of image path, we pass the buffer for Native Solving
    void setImageBuffer(const ImageBuffer& img);
    void setViewer(class ImageViewer* v); // Added
    
    bool isSolved() const { return m_solved; }
    NativeSolveResult result() const { return m_result; }

private slots:
    void onSearchSimbad();
    void onSolve();
    void onSolverFinished(const NativeSolveResult& res);
    void onSolverLog(const QString& text);

private:
    QLineEdit* m_objectName;
    QLineEdit* m_raHint;
    QLineEdit* m_decHint;
    QLineEdit* m_fov;
    QLineEdit* m_pixelScale; // New field for scale
    QTextEdit* m_log;
    QPushButton* m_solveBtn;
    
    ImageBuffer m_image;
    QPointer<class ImageViewer> m_viewer; 
    QPointer<class ImageViewer> m_jobTarget; // Track viewer for async jobs
    
    SimbadSearcher* m_simbad;
    NativePlateSolver* m_solver;
    
    bool m_solved = false;
    NativeSolveResult m_result;
};

#endif // PLATESOLVINGDIALOG_H
