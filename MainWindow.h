#pragma once

#include <QMainWindow>
#include <QTableWidget>
#include "aligner.h"

class QAction;
class QProcess;
class QCloseEvent;
class QResizeEvent;

class MainWindow : public QMainWindow
{
	Q_OBJECT

public:
	MainWindow(QWidget *parent = nullptr);
	~MainWindow();

private slots:
	// File menu
	void onLoadSource();
	void onLoadTarget();
	void onLoadAudioText();
	void onLoadAudioFile();
	void onExport();
	void onExit();

	// Edit menu
	void onSplitCell();
	void onMergeWithPrevious();
	void onMergeWithNext();
	void onExcludeRow();
	void onMergeAllWithPrevious();
	void onMergeAllWithNext();

	void onClearSource();
	void onClearTarget();
	void onClearAudio();
	void onNormalizeRows();

	// Tools menu
	void onTargetAlign();
	void onAudioAlign();
	void onRecalc();

	void onSplitAudio();
	void onGenerateAudio();

	void onLoadProject();
	void onSaveProject();
	void onSaveProjectAs();
	void onNewProject();

	// Context menu
	void showContextMenu(const QPoint& pos);
protected:
	void closeEvent(QCloseEvent* event) override;

private slots:
	void onEditCell();  // новый слот для редактирования
	void onHunalignAlign();
private:
	bool loadAudioTextFile(const QString& filename);
	void setupUI();
	void setupTableProperties();
	void createMenuBar();
	void createToolBar();
	void createStatusBar();
	void splitRowAtPosition(int row, int col, int cursorPos);

	// UI sync
	void syncTableFromAligner();
	void syncAlignerFromTable();
	void updateRowHeights();
	void setModified(bool modified);
	void updateCell(int row, int column, const QString& text, const QColor& bgColor);

	
	Aligner m_aligner;
	QTableWidget* m_table;
	QProcess* m_ffmpegProcess;
};

