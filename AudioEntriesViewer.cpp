#include "AudioEntriesViewer.h"
#include <QDebug>
#include <QMessageBox>
#include <QPushButton>
#include <QLineEdit>
#include <QCheckBox>
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

	// ---- Панель поиска ----
	QHBoxLayout* searchLayout = new QHBoxLayout();
	searchLayout->setSpacing(8);

	// кнопка Set Start
	m_setStartBtn = new QPushButton("Start", this);
	m_setStartBtn->setFixedSize(50, 30);
	m_setStartBtn->setToolTip("Set Start");
	m_setStartBtn->setEnabled(true);
	connect(m_setStartBtn, &QPushButton::clicked, this, &AudioEntriesViewer::onSetStart);
	searchLayout->addWidget(m_setStartBtn);

	// Метка "Search:"
	QLabel* searchLabel = new QLabel("Search:", this);
	searchLayout->addWidget(searchLabel);

	// Поле ввода для поиска
	m_searchEdit = new QLineEdit(this);
	m_searchEdit->setPlaceholderText("Enter word or phrase...");
	m_searchEdit->setMinimumWidth(300);
	connect(m_searchEdit, &QLineEdit::textChanged, this, &AudioEntriesViewer::onSearchTextChanged);
	connect(m_searchEdit, &QLineEdit::returnPressed, this, &AudioEntriesViewer::onSearchNext);
	searchLayout->addWidget(m_searchEdit);

	// Кнопки навигации по результатам
	m_searchPrevBtn = new QPushButton("Up", this);
	m_searchPrevBtn->setFixedSize(50, 30);
	m_searchPrevBtn->setToolTip("Previous match (Shift+F3)");
	m_searchPrevBtn->setEnabled(false);
	connect(m_searchPrevBtn, &QPushButton::clicked, this, &AudioEntriesViewer::onSearchPrevious);
	searchLayout->addWidget(m_searchPrevBtn);

	m_searchNextBtn = new QPushButton("Down", this);
	m_searchNextBtn->setFixedSize(50, 30);
	m_searchNextBtn->setToolTip("Next match (F3)");
	m_searchNextBtn->setEnabled(false);
	connect(m_searchNextBtn, &QPushButton::clicked, this, &AudioEntriesViewer::onSearchNext);
	searchLayout->addWidget(m_searchNextBtn);

	// Кнопка очистки поиска
	m_clearSearchBtn = new QPushButton("X", this);
	m_clearSearchBtn->setFixedSize(30, 30);
	m_clearSearchBtn->setToolTip("Clear search");
	m_clearSearchBtn->setEnabled(false);
	connect(m_clearSearchBtn, &QPushButton::clicked, this, &AudioEntriesViewer::onClearSearch);
	searchLayout->addWidget(m_clearSearchBtn);

	// Настройки поиска
	m_caseSensitiveCheck = new QCheckBox("Aa", this);
	m_caseSensitiveCheck->setToolTip("Case sensitive");
	m_caseSensitiveCheck->setChecked(false);
	connect(m_caseSensitiveCheck, &QCheckBox::toggled, this, &AudioEntriesViewer::onToggleCaseSensitive);
	searchLayout->addWidget(m_caseSensitiveCheck);

	m_wholeWordCheck = new QCheckBox("\" \"", this);
	m_wholeWordCheck->setToolTip("Whole word only");
	m_wholeWordCheck->setChecked(false);
	connect(m_wholeWordCheck, &QCheckBox::toggled, this, &AudioEntriesViewer::onToggleWholeWord);
	searchLayout->addWidget(m_wholeWordCheck);

	searchLayout->addStretch();

	// Статус поиска
	m_statusLabel = new QLabel("Audio entries: 0", this);
	searchLayout->addWidget(m_statusLabel);

	mainLayout->addLayout(searchLayout);

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
		if (i == m_aligner->m_startAudioIndex) {
			wordItem->setBackground(Qt::green);
		}
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

void AudioEntriesViewer::onSetStart()
{
	int row = m_table->currentRow();
	// снимаем со старого
	QTableWidgetItem* item = m_table->item(m_aligner->m_startAudioIndex, 0);
	if (item) {
		item->setBackground(QBrush());
	}

	// выставляем на новое
	m_aligner->m_startAudioIndex = row;
	item = m_table->item(row, 0);
	if (item) {
		item->setBackgroundColor(Qt::green);
	}
}


// ---- Поиск ----

void AudioEntriesViewer::onSearchTextChanged(const QString& text)
{
	if (text.isEmpty()) {
		
		m_searchResults.clear();
		m_currentResultIndex = -1;
		m_searchPrevBtn->setEnabled(false);
		m_searchNextBtn->setEnabled(false);
		m_clearSearchBtn->setEnabled(false);
		updateStatusLabel();
		return;
	}

	m_clearSearchBtn->setEnabled(true);
	performSearch();
}

void AudioEntriesViewer::onSearchNext()
{
	if (m_searchResults.isEmpty()) return;

	// Переход к следующему результату
	m_currentResultIndex = (m_currentResultIndex + 1) % m_searchResults.size();
	int row = m_searchResults[m_currentResultIndex];
	scrollToRow(row);

	updateStatusLabel();
}

void AudioEntriesViewer::onSearchPrevious()
{
	if (m_searchResults.isEmpty()) return;

	// Переход к предыдущему результату
	m_currentResultIndex = (m_currentResultIndex - 1 + m_searchResults.size()) % m_searchResults.size();
	int row = m_searchResults[m_currentResultIndex];
	scrollToRow(row);

	updateStatusLabel();
}

void AudioEntriesViewer::onToggleCaseSensitive(bool checked)
{
	Q_UNUSED(checked);
	if (!m_searchEdit->text().isEmpty()) {
		performSearch();
	}
}

void AudioEntriesViewer::onToggleWholeWord(bool checked)
{
	Q_UNUSED(checked);
	if (!m_searchEdit->text().isEmpty()) {
		performSearch();
	}
}

void AudioEntriesViewer::onClearSearch()
{
	m_searchEdit->clear();
	m_searchEdit->setFocus();
}

void AudioEntriesViewer::performSearch()
{
	m_searchResults.clear();
	m_currentResultIndex = -1;

	QString searchText = m_searchEdit->text().trimmed();
	if (searchText.isEmpty()) {
		m_searchPrevBtn->setEnabled(false);
		m_searchNextBtn->setEnabled(false);
		updateStatusLabel();
		return;
	}

	// Разбиваем поисковый запрос на слова
	QStringList searchWords = searchText.split(' ', Qt::SkipEmptyParts);
	if (searchWords.isEmpty()) return;

	Qt::CaseSensitivity caseSensitivity = m_caseSensitiveCheck->isChecked() ?
		Qt::CaseSensitive : Qt::CaseInsensitive;

	// Проходим по всем словам в таблице
	for (int row = 0; row < m_table->rowCount(); ++row) {
		QTableWidgetItem* wordItem = m_table->item(row, 0);
		if (!wordItem) continue;

		QString wordText = wordItem->text();
		bool found = true;

		// Проверяем каждое слово из запроса
		for (const QString& searchWord : searchWords) {
			bool wordFound = false;

			if (m_wholeWordCheck->isChecked()) {
				// Поиск целого слова
				if (wordText.compare(searchWord, caseSensitivity) == 0) {
					wordFound = true;
				}
			}
			else {
				// Поиск подстроки
				if (wordText.contains(searchWord, caseSensitivity)) {
					wordFound = true;
				}
			}

			if (!wordFound) {
				found = false;
				break;
			}
		}

		if (found) {
			m_searchResults.append(row);
		}
	}

	// Обновляем состояние кнопок
	m_searchPrevBtn->setEnabled(!m_searchResults.isEmpty());
	m_searchNextBtn->setEnabled(!m_searchResults.isEmpty());

	// Если есть результаты, переходим к первому
	if (!m_searchResults.isEmpty()) {
		m_currentResultIndex = 0;
		int row = m_searchResults[0];
		scrollToRow(row);
	}

	updateStatusLabel();
}

void AudioEntriesViewer::scrollToRow(int row)
{
	if (row < 0 || row >= m_table->rowCount()) return;
	m_table->scrollToItem(m_table->item(row, 0), QAbstractItemView::PositionAtCenter);
	m_table->selectRow(row);
}

void AudioEntriesViewer::updateStatusLabel()
{
	QString status = QString("Audio entries: %1").arg(m_aligner->audioEntries.size());

	if (!m_searchResults.isEmpty()) {
		status += QString(" | Found: %1 match(es)").arg(m_searchResults.size());
		if (m_currentResultIndex >= 0 && m_currentResultIndex < m_searchResults.size()) {
			status += QString(" (current: %1/%2)").arg(m_currentResultIndex + 1).arg(m_searchResults.size());
		}
	}

	m_statusLabel->setText(status);
}
