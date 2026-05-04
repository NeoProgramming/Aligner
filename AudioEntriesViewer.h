#pragma once

#include <QDialog>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QLabel>
#include <QHeaderView>
#include "alignment.h"

class AudioEntriesViewer : public QDialog
{
	Q_OBJECT

public:
	explicit AudioEntriesViewer(QWidget *parent = nullptr);
	~AudioEntriesViewer();

	void updateEntries(const QVector<AudioEntry>& entries);
	void clear();

private:
	void setupUI();
	void setupTable();

	QTableWidget* m_table;
	QLabel* m_statusLabel;
};
