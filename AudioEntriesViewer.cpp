#include "AudioEntriesViewer.h"
#include <QDebug>
#include <QMessageBox>
#include "aligner.h"

AudioEntriesViewer::AudioEntriesViewer(Aligner *aligner, QWidget *parent)
	: QDialog(parent)
	, m_aligner(aligner)
{
	setupUI();
	setWindowTitle("Audio Entries Viewer");
	setMinimumSize(600, 400);

	// Немодальное окно
	setModal(false);

	// Разрешаем закрытие по крестику
	setAttribute(Qt::WA_DeleteOnClose, false);
}

AudioEntriesViewer::~AudioEntriesViewer()
{
}

void AudioEntriesViewer::setupUI()
{
	QVBoxLayout* mainLayout = new QVBoxLayout(this);

	// Заголовок
	m_statusLabel = new QLabel("Audio entries: 0", this);
	mainLayout->addWidget(m_statusLabel);

	// Таблица
	setupTable();
	mainLayout->addWidget(m_table);

	setLayout(mainLayout);
}

void AudioEntriesViewer::setupTable()
{
	m_table = new QTableWidget(this);
	m_table->setColumnCount(5);

	QStringList headers;
	headers << "Word" << "Start Ms" << "End Ms" << "Sentence Idx" << "Insertion";
	m_table->setHorizontalHeaderLabels(headers);

	// Настройка внешнего вида
	m_table->horizontalHeader()->setStretchLastSection(true);
	m_table->setAlternatingRowColors(true);
	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);

	// ОТКЛЮЧАЕМ автоматическую сортировку
	m_table->setSortingEnabled(false);
	
	// Ширина столбцов
	m_table->setColumnWidth(0, 200);  // Word
	m_table->setColumnWidth(1, 80);   // Start Ms
	m_table->setColumnWidth(2, 80);   // End Ms
	m_table->setColumnWidth(3, 100);  // Sentence Idx
	m_table->setColumnWidth(4, 80);   // Insertion

	connect(m_table, &QTableWidget::cellDoubleClicked,
		this, &AudioEntriesViewer::onShowInfo);
}

void AudioEntriesViewer::updateEntries()
{
	if (!m_aligner)
		return;
	m_table->setRowCount(m_aligner->audioEntries.size());
	m_statusLabel->setText(QString("Audio entries: %1").arg(m_aligner->audioEntries.size()));
	
	// Настройка вертикальных заголовков (номера строк с 0)
	for (int i = 0; i < m_aligner->audioEntries.size(); ++i) {
		QTableWidgetItem* headerItem = new QTableWidgetItem(QString::number(i));
		m_table->setVerticalHeaderItem(i, headerItem);
	}

	for (int i = 0; i < m_aligner->audioEntries.size(); ++i) {
		const AudioEntry& entry = m_aligner->audioEntries[i];

		// Слово
		QTableWidgetItem* wordItem = new QTableWidgetItem(entry.text);
		wordItem->setFlags(wordItem->flags() & ~Qt::ItemIsEditable);
		m_table->setItem(i, 0, wordItem);

		// Start Ms
		QTableWidgetItem* startItem = new QTableWidgetItem(QString::number(entry.startMs));
		startItem->setFlags(startItem->flags() & ~Qt::ItemIsEditable);
		m_table->setItem(i, 1, startItem);

		// End Ms
		QTableWidgetItem* endItem = new QTableWidgetItem(QString::number(entry.endMs));
		endItem->setFlags(endItem->flags() & ~Qt::ItemIsEditable);
		m_table->setItem(i, 2, endItem);

		// Sentence Idx
		QTableWidgetItem* sentItem = new QTableWidgetItem(QString::number(entry.sentenceIdx));
		sentItem->setFlags(sentItem->flags() & ~Qt::ItemIsEditable);
		m_table->setItem(i, 3, sentItem);

		// Insertion (bool -> "Yes"/"No")
		QTableWidgetItem* insItem = new QTableWidgetItem(entry.ins ? "Yes" : "No");
		insItem->setFlags(insItem->flags() & ~Qt::ItemIsEditable);

		// Подсветка вставок
		if (entry.ins) {
			insItem->setBackground(Qt::lightGray);
		}

		m_table->setItem(i, 4, insItem);
	}
}

void AudioEntriesViewer::clear()
{
	m_table->clearContents();
	m_table->setRowCount(0);
	m_statusLabel->setText("Audio entries: 0");
}

void AudioEntriesViewer::onShowInfo()
{
	int row = m_table->currentRow();
	// Получаем sentenceIdx из таблицы
	QTableWidgetItem* sentItem = m_table->item(row, 3);  // Столбец 3 = Sentence Idx
	if (!sentItem) return;

	int sentenceIdx = sentItem->text().toInt();
	if (sentenceIdx < 0) {
		QMessageBox::information(this, "Info",
			"This audio word is not assigned to any sentence (insertion or pending)");
		return;
	}
	if (m_aligner && sentenceIdx >= m_aligner->sourceCells.size()) {
		QMessageBox::information(this, "Info",
			"This audio word is not assigned to any sentence (insertion or pending)");
		return;
	}

	// Получаем исходное предложение
	QString sourceText;
	if (m_aligner) {
		sourceText = m_aligner->sourceCells[sentenceIdx].text;
	}

	if (sourceText.isEmpty()) {
		QMessageBox::information(this, "Info",
			QString("No source text found for sentence %1").arg(sentenceIdx));
		return;
	}

	// Получаем аудио слово
	QTableWidgetItem* wordItem = m_table->item(row, 0);
	QString word = wordItem ? wordItem->text() : "";

	// Показываем диалог
	QMessageBox msgBox(this);
	msgBox.setWindowTitle(QString("Source Sentence for \"%1\"").arg(word));
	msgBox.setText(QString("Sentence %1:\n\n%2").arg(sentenceIdx).arg(sourceText));
	msgBox.setIcon(QMessageBox::Information);
	msgBox.setStandardButtons(QMessageBox::Ok);

	// Делаем текст выделяемым и копируемым
	QSpacerItem* spacer = new QSpacerItem(500, 0, QSizePolicy::Minimum, QSizePolicy::Expanding);
	QGridLayout* layout = (QGridLayout*)msgBox.layout();
	if (layout) {
		layout->addItem(spacer, layout->rowCount(), 0, 1, layout->columnCount());
	}

	msgBox.exec();
}
