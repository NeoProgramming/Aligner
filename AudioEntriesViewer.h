#pragma once

#include <QDialog>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QHeaderView>
#include <QPushButton>
#include <QLineEdit>
#include <QCheckBox>
#include "alignment.h"

class Aligner;

class AudioEntriesViewer : public QDialog
{
	Q_OBJECT

public:
	explicit AudioEntriesViewer(Aligner *aligner, QWidget *parent = nullptr);
	~AudioEntriesViewer();

	void updateEntries();
	void clear();
private slots:
	void onShowInfo();
	void onSetStart();
//	void onCellDoubleClicked(int row, int column);
	void onSearchTextChanged(const QString& text);
	void onSearchNext();
	void onSearchPrevious();
	void onToggleCaseSensitive(bool checked);
	void onToggleWholeWord(bool checked);
	void onClearSearch();
private:
	void setupUI();
	void setupTable();
	void performSearch();
	void scrollToRow(int row);
	void updateStatusLabel();

	Aligner *m_aligner = nullptr;
	QTableWidget* m_table;
	QLabel* m_statusLabel;

	QLineEdit* m_searchEdit;
	QPushButton* m_searchNextBtn;
	QPushButton* m_searchPrevBtn;
	QPushButton* m_clearSearchBtn;
	QCheckBox* m_caseSensitiveCheck;
	QCheckBox* m_wholeWordCheck;
	QPushButton* m_setStartBtn;

	// Результаты поиска
	QVector<int> m_searchResults;  // Индексы строк, где найдено совпадение
	int m_currentResultIndex;
};
