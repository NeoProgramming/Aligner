#pragma once

#include <QDialog>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QHeaderView>
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
private:
	void setupUI();
	void setupTable();

	Aligner *m_aligner = nullptr;
	QTableWidget* m_table;
	QLabel* m_statusLabel;
};
