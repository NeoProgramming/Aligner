#include "ProjectSettingsDialog.h"
#include <QFileDialog>
#include <QMessageBox>
#include <QDir>

ProjectSettingsDialog::ProjectSettingsDialog(QWidget *parent)
    : QDialog(parent)
    , m_sourceEdit(nullptr)
    , m_translatedEdit(nullptr)
    , m_audioTextEdit(nullptr)
    , m_audioFileEdit(nullptr)
    , m_outputDirEdit(nullptr)
{
    setupUI();
    setWindowTitle("Project Settings");
    resize(700, 300);
}

void ProjectSettingsDialog::setupUI()
{
	QVBoxLayout* mainLayout = new QVBoxLayout(this);

	// Группа путей
	QGroupBox* pathsGroup = new QGroupBox("Project Files");
	QFormLayout* formLayout = new QFormLayout(pathsGroup);
	formLayout->setSpacing(8);
	formLayout->setLabelAlignment(Qt::AlignRight);

	// Создаем строки для каждого пути с кнопкой справа
	createFileRow("Source Text:", m_sourceEdit, m_sourceBrowseBtn, formLayout,
		&ProjectSettingsDialog::onBrowseSource);
	createFileRow("Translated Text:", m_translatedEdit, m_translatedBrowseBtn, formLayout,
		&ProjectSettingsDialog::onBrowseTranslated);
	createFileRow("Audio Text (JSON/SRT):", m_audioTextEdit, m_audioTextBrowseBtn, formLayout,
		&ProjectSettingsDialog::onBrowseAudioText);
	createFileRow("Audio File (MP3):", m_audioFileEdit, m_audioFileBrowseBtn, formLayout,
		&ProjectSettingsDialog::onBrowseAudioFile);
	createFileRow("Output Directory:", m_outputDirEdit, m_outputDirBrowseBtn, formLayout,
		&ProjectSettingsDialog::onBrowseOutputDir);

	mainLayout->addWidget(pathsGroup);

	// Кнопки OK/Cancel
	QHBoxLayout* buttonLayout = new QHBoxLayout();
	buttonLayout->addStretch();

	QPushButton* okButton = new QPushButton("OK", this);
	QPushButton* cancelButton = new QPushButton("Cancel", this);
	okButton->setFixedWidth(80);
	cancelButton->setFixedWidth(80);

	connect(okButton, &QPushButton::clicked, this, &ProjectSettingsDialog::onAccept);
	connect(cancelButton, &QPushButton::clicked, this, &QDialog::reject);

	buttonLayout->addWidget(okButton);
	buttonLayout->addWidget(cancelButton);

	mainLayout->addLayout(buttonLayout);
	setLayout(mainLayout);
}

void ProjectSettingsDialog::createFileRow(const QString& label,
	QLineEdit*& lineEdit,
	QPushButton*& browseButton,
	QFormLayout* formLayout,
	void (ProjectSettingsDialog::*slot)())
{
	// Создаем горизонтальный layout для строки (поле + кнопка)
	QHBoxLayout* rowLayout = new QHBoxLayout();
	rowLayout->setSpacing(6);

	// Поле для ввода пути
	lineEdit = new QLineEdit(this);
	lineEdit->setReadOnly(true);
	lineEdit->setMinimumWidth(400);

	// Кнопка "Browse..."
	browseButton = new QPushButton("Browse...", this);
	browseButton->setFixedWidth(80);
	browseButton->setCursor(Qt::PointingHandCursor);

	// Подключаем переданный слот к кнопке
	if (slot) {
		connect(browseButton, &QPushButton::clicked, this, slot);
	}

	// Добавляем поле и кнопку в строку
	rowLayout->addWidget(lineEdit);
	rowLayout->addWidget(browseButton);

	// Добавляем строку в форму
	formLayout->addRow(label, rowLayout);
}

void ProjectSettingsDialog::onBrowseSource()
{
    QString path = QFileDialog::getOpenFileName(this, "Select Source Text File",
        m_sourceEdit->text(), "Text files (*.txt);;All files (*.*)");
    if (!path.isEmpty()) {
        m_sourceEdit->setText(QDir::toNativeSeparators(path));
    }
}

void ProjectSettingsDialog::onBrowseTranslated()
{
    QString path = QFileDialog::getOpenFileName(this, "Select Translated Text File",
        m_translatedEdit->text(), "Text files (*.txt);;All files (*.*)");
    if (!path.isEmpty()) {
        m_translatedEdit->setText(QDir::toNativeSeparators(path));
    }
}

void ProjectSettingsDialog::onBrowseAudioText()
{
    QString path = QFileDialog::getOpenFileName(this, "Select Audio Text File",
        m_audioTextEdit->text(), "JSON/SRT files (*.json *.srt);;All files (*.*)");
    if (!path.isEmpty()) {
        m_audioTextEdit->setText(QDir::toNativeSeparators(path));
    }
}

void ProjectSettingsDialog::onBrowseAudioFile()
{
    QString path = QFileDialog::getOpenFileName(this, "Select Audio File",
        m_audioFileEdit->text(), "MP3 files (*.mp3);;All files (*.*)");
    if (!path.isEmpty()) {
        m_audioFileEdit->setText(QDir::toNativeSeparators(path));
    }
}

void ProjectSettingsDialog::onBrowseOutputDir()
{
    QString path = QFileDialog::getExistingDirectory(this, "Select Output Directory",
        m_outputDirEdit->text());
    if (!path.isEmpty()) {
        m_outputDirEdit->setText(QDir::toNativeSeparators(path));
    }
}

// Установка значений
void ProjectSettingsDialog::setSourceFile(const QString& path)
{
    m_sourceEdit->setText(QDir::toNativeSeparators(path));
}

void ProjectSettingsDialog::setTranslatedFile(const QString& path)
{
    m_translatedEdit->setText(QDir::toNativeSeparators(path));
}

void ProjectSettingsDialog::setAudioTextFile(const QString& path)
{
    m_audioTextEdit->setText(QDir::toNativeSeparators(path));
}

void ProjectSettingsDialog::setAudioFile(const QString& path)
{
    m_audioFileEdit->setText(QDir::toNativeSeparators(path));
}

void ProjectSettingsDialog::setOutputDir(const QString& path)
{
    m_outputDirEdit->setText(QDir::toNativeSeparators(path));
}

// Получение значений
QString ProjectSettingsDialog::getSourceFile() const
{
    return m_sourceEdit->text();
}

QString ProjectSettingsDialog::getTranslatedFile() const
{
    return m_translatedEdit->text();
}

QString ProjectSettingsDialog::getAudioTextFile() const
{
    return m_audioTextEdit->text();
}

QString ProjectSettingsDialog::getAudioFile() const
{
    return m_audioFileEdit->text();
}

QString ProjectSettingsDialog::getOutputDir() const
{
    return m_outputDirEdit->text();
}

void ProjectSettingsDialog::onAccept()
{
    // Проверка директории вывода
    QString outputDir = m_outputDirEdit->text();
    if (!outputDir.isEmpty()) {
        QDir dir(outputDir);
        if (!dir.exists()) {
            QMessageBox::StandardButton reply = QMessageBox::question(this,
                "Create Directory",
                QString("Output directory does not exist:\n%1\n\nCreate it?").arg(outputDir),
                QMessageBox::Yes | QMessageBox::No);
            if (reply == QMessageBox::Yes) {
                if (!dir.mkpath(".")) {
                    QMessageBox::warning(this, "Error", "Failed to create directory");
                    return;
                }
            }
        }
    }
    
    accept();
}