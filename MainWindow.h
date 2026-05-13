#pragma once

#include <QMainWindow>
#include <QTableWidget>
#include "aligner.h"


class QAction;
class QProcess;
class QCloseEvent;
class QResizeEvent;
class AudioEntriesViewer;

enum class InfoMode {
	AudioSim,
	TransSim,
	Times
};

class MainWindow : public QMainWindow
{
	Q_OBJECT

public:
	MainWindow(QWidget *parent = nullptr);
	~MainWindow();

private slots:
	// File menu
	void onLoadSource();
	void onLoadTranslated();
	void onLoadAudioText();
	void onLoadAudioFile();
	
	void onExit();

	void onShowAudioEntires();

	// Edit menu
	void onSplitCell();
	void onMergeWithPrevious();
	void onMergeWithNext();
	void onExcludeRow();
	void onMergeAllWithPrevious();
	void onMergeAllWithNext();

	void onDebugInfo();

	void onClearSource();
	void onClearTranslated();
	void onClearAudio();
	void onNormalizeRows();

	// Tools menu
	void onTranslatedAlign();
	void onAudioAlign();
	void onRecalc();
	void onStat();

	void onSplitAudio();
	void onGenerateAudio();

	void onLoadProject();
	void onSaveProject();
	void onSaveProjectAs();
	void onNewProject();

	void onMoveAudioWordsToPrev();
	void onMoveAudioWordsToNext();
	void onSplitSentence();

	// Context menu
	void showContextMenu(const QPoint& pos);
protected:
	void closeEvent(QCloseEvent* event) override;

private slots:
	void onEditCell();  // новый слот для редактирования
	
private:
	bool loadAudioTextFile(const QString& filename);
	void setupUI();
	void setupTableProperties();
	void createMenuBar();
	void createToolBar();
	void createStatusBar();

	void splitRowAtPosition(int row, int col, int cursorPos);

	void syncTableFromAligner();	
	void updateRowHeights();
	void setModified(bool modified);
	void updateCell(int row, int column, const QString& text, const QColor& bgColor);
	void showAudioEntriesViewer();

	InfoMode m_imode = InfoMode::AudioSim;
	
	Aligner m_aligner;
	QTableWidget* m_table;	
	AudioEntriesViewer* m_audioEntriesViewer;
};

