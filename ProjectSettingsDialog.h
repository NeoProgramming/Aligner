#pragma once

#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>

class ProjectSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ProjectSettingsDialog(QWidget *parent = nullptr);
    
    // Установка/получение значений
    void setSourceFile(const QString& path);
    void setTranslatedFile(const QString& path);
    void setAudioTextFile(const QString& path);
    void setAudioFile(const QString& path);
    void setOutputDir(const QString& path);
    
    QString getSourceFile() const;
    QString getTranslatedFile() const;
    QString getAudioTextFile() const;
    QString getAudioFile() const;
    QString getOutputDir() const;

private slots:
    void onBrowseSource();
    void onBrowseTranslated();
    void onBrowseAudioText();
    void onBrowseAudioFile();
    void onBrowseOutputDir();
    void onAccept();

private:
    void setupUI();
	void createFileRow(const QString& label,
		QLineEdit*& lineEdit,
		QPushButton*& browseButton,
		QFormLayout* formLayout,
		void (ProjectSettingsDialog::*slot)());
    
    QLineEdit* m_sourceEdit;
    QLineEdit* m_translatedEdit;
    QLineEdit* m_audioTextEdit;
    QLineEdit* m_audioFileEdit;
    QLineEdit* m_outputDirEdit;
    
    QPushButton* m_sourceBrowseBtn;
    QPushButton* m_translatedBrowseBtn;
    QPushButton* m_audioTextBrowseBtn;
    QPushButton* m_audioFileBrowseBtn;
    QPushButton* m_outputDirBrowseBtn;
};