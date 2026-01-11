#include "NewProjectDialog.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QGroupBox>
#include <QDir>

NewProjectDialog::NewProjectDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("New Stacking Project"));
    setMinimumWidth(500);
    setupUI();
}

void NewProjectDialog::setupUI() {
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    
    // Info
    QLabel* infoLabel = new QLabel(tr(
        "Create a new stacking project with organized folder structure:\n"
        "• biases/ - Bias frames\n"
        "• darks/ - Dark frames\n"
        "• flats/ - Flat frames\n"
        "• lights/ - Light frames\n"
        "• process/ - Calibrated and registered files\n"
        "• output/ - Final stacked results"
    ));
    infoLabel->setWordWrap(true);
    infoLabel->setStyleSheet("color: #aaa; padding: 10px;");
    mainLayout->addWidget(infoLabel);
    
    // Project Location
    QGroupBox* locationGroup = new QGroupBox(tr("Project Location"));
    QVBoxLayout* locationLayout = new QVBoxLayout(locationGroup);
    
    QHBoxLayout* pathLayout = new QHBoxLayout();
    m_pathEdit = new QLineEdit();
    m_pathEdit->setPlaceholderText(tr("Select folder for project..."));
    m_browseBtn = new QPushButton(tr("Browse..."));
    pathLayout->addWidget(m_pathEdit);
    pathLayout->addWidget(m_browseBtn);
    locationLayout->addLayout(pathLayout);
    
    QHBoxLayout* nameLayout = new QHBoxLayout();
    nameLayout->addWidget(new QLabel(tr("Project Name:")));
    m_nameEdit = new QLineEdit();
    m_nameEdit->setPlaceholderText(tr("My Stacking Project"));
    nameLayout->addWidget(m_nameEdit);
    locationLayout->addLayout(nameLayout);
    
    mainLayout->addWidget(locationGroup);
    
    // Symlink Info (Windows)
#ifdef Q_OS_WIN
    if (!Stacking::StackingProject::canCreateSymlinks()) {
        QLabel* symlinkWarning = new QLabel(tr(
            "⚠️ Symbolic links require Administrator or Developer Mode.\n"
            "Files will be copied instead of linked."
        ));
        symlinkWarning->setStyleSheet("color: orange; padding: 5px;");
        mainLayout->addWidget(symlinkWarning);
    }
#endif
    
    // Buttons
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    
    m_cancelBtn = new QPushButton(tr("Cancel"));
    m_createBtn = new QPushButton(tr("Create Project"));
    m_createBtn->setEnabled(false);
    m_createBtn->setDefault(true);
    
    buttonLayout->addWidget(m_cancelBtn);
    buttonLayout->addWidget(m_createBtn);
    mainLayout->addLayout(buttonLayout);
    
    // Connections
    connect(m_browseBtn, &QPushButton::clicked, this, &NewProjectDialog::onBrowse);
    connect(m_createBtn, &QPushButton::clicked, this, &NewProjectDialog::onAccept);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_pathEdit, &QLineEdit::textChanged, this, &NewProjectDialog::validateInputs);
    connect(m_nameEdit, &QLineEdit::textChanged, this, &NewProjectDialog::validateInputs);
}

void NewProjectDialog::onBrowse() {
    QString dir = QFileDialog::getExistingDirectory(this,
        tr("Select Project Location"),
        QDir::homePath());
    
    if (!dir.isEmpty()) {
        m_pathEdit->setText(dir);
        
        // Auto-fill name if empty
        if (m_nameEdit->text().isEmpty()) {
            m_nameEdit->setText(QDir(dir).dirName());
        }
    }
}

void NewProjectDialog::onAccept() {
    QString path = projectPath();
    
    // Check if directory already exists and has content
    QDir dir(path);
    if (dir.exists() && !dir.isEmpty()) {
        if (Stacking::StackingProject::isProjectDirectory(path)) {
            QMessageBox::warning(this, tr("Project Exists"),
                tr("A project already exists at this location."));
            return;
        }
        
        int ret = QMessageBox::question(this, tr("Directory Not Empty"),
            tr("The selected directory is not empty.\nCreate project structure anyway?"),
            QMessageBox::Yes | QMessageBox::No);
        
        if (ret != QMessageBox::Yes) {
            return;
        }
    }
    
    accept();
}

void NewProjectDialog::validateInputs() {
    bool valid = !m_pathEdit->text().isEmpty() && !m_nameEdit->text().isEmpty();
    m_createBtn->setEnabled(valid);
}

QString NewProjectDialog::projectPath() const {
    QString base = m_pathEdit->text();
    QString name = m_nameEdit->text();
    
    // If name is different from the folder name, create subfolder
    if (!base.isEmpty() && !name.isEmpty()) {
        QString folderName = QDir(base).dirName();
        if (folderName != name) {
            return base + "/" + name;
        }
    }
    
    return base;
}

QString NewProjectDialog::projectName() const {
    return m_nameEdit->text();
}

Stacking::StackingProject* NewProjectDialog::createProject() {
    auto* project = new Stacking::StackingProject();
    
    if (!project->create(projectPath())) {
        delete project;
        return nullptr;
    }
    
    project->setName(projectName());
    project->save();
    
    return project;
}
