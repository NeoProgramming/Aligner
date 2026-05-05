#include "mainwindow.h"
#include "audio_parser.h"
#include <QApplication>
#include <QMenuBar>
#include <QToolBar>
#include <QStatusBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QHeaderView>
#include <QMenu>
#include <QClipboard>
#include <QProcess>
#include <QDir>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QTextEdit>
#include <QMessageBox>
#include <QCloseEvent>
#include <QResizeEvent>
#include <QDebug>
#include "tools.h"
#include "AudioEntriesViewer.h"

MainWindow::MainWindow(QWidget *parent)
	: QMainWindow(parent)
	, m_table(nullptr)
	, m_ffmpegProcess(nullptr)
{
	// Загружаем настройки
	cfg.loadSettings();

	setupUI();
	createMenuBar();
	createToolBar();
	createStatusBar();
	setWindowTitle("Text Alignment Tool");
	resize(1200, 700);

	m_audioEntriesViewer = new AudioEntriesViewer(this);
}

MainWindow::~MainWindow()
{
	cfg.saveSettings();

	if (m_ffmpegProcess) {
		delete m_ffmpegProcess;
	}
}

// Метод для обновления и показа окна
void MainWindow::showAudioEntriesViewer()
{
	QVector<AudioEntry> viewEntries;
	viewEntries.reserve(m_aligner.audioEntries.size());

	for (const auto& entry : m_aligner.audioEntries) {
		AudioEntry viewEntry;
		viewEntry.text = entry.text;
		viewEntry.startMs = entry.startMs;
		viewEntry.endMs = entry.endMs;
		viewEntry.sentenceIdx = entry.sentenceIdx;
		viewEntry.ins = entry.ins;
		viewEntries.append(viewEntry);
	}

	m_audioEntriesViewer->updateEntries(viewEntries);
	m_audioEntriesViewer->show();
	m_audioEntriesViewer->raise();  // Поднять окно на передний план
	m_audioEntriesViewer->activateWindow();  // Активировать окно
}

void MainWindow::setupUI()
{
	m_table = new QTableWidget(this);
	m_table->setColumnCount(4);
	m_table->setHorizontalHeaderLabels(QStringList()
		<< "Source Text"
		<< "Target Text"
		<< "Audio Text"
		<< "Info");

	setupTableProperties();

	setCentralWidget(m_table);

	m_table->setContextMenuPolicy(Qt::CustomContextMenu);
	connect(m_table, &QTableWidget::customContextMenuRequested,
		this, &MainWindow::showContextMenu);
	// Добавляем обработку двойного клика
	connect(m_table, &QTableWidget::cellDoubleClicked,
		this, &MainWindow::onEditCell);
}

void MainWindow::setupTableProperties()
{
	m_table->setWordWrap(true);
	m_table->setTextElideMode(Qt::ElideNone);

	m_table->verticalHeader()->setVisible(true);
	m_table->verticalHeader()->setDefaultSectionSize(60);

	// Включаем растягивание только для последнего столбца
	m_table->horizontalHeader()->setStretchLastSection(false);

	// Для первых трёх столбцов используем пропорциональное растягивание
	m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
	m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
	m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
	// Четвёртый столбец не растягиваем, зададим ему фиксированную ширину
	m_table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
	// Устанавливаем фиксированную ширину для отладочного столбца
	m_table->setColumnWidth(3, 100);
	
	// Разрешаем выделение как ячеек, так и строк
//	m_table->setSelectionBehavior(QAbstractItemView::SelectItems);  // теперь можно выделять ячейки
//	m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);

	m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_table->setSelectionMode(QAbstractItemView::ExtendedSelection);
	
	// ОТКЛЮЧАЕМ встроенный редактор
	m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
}


void MainWindow::createMenuBar()
{
	QMenu* fileMenu = menuBar()->addMenu("&File");
	fileMenu->addAction("Load Source Text", this, &MainWindow::onLoadSource);
	fileMenu->addAction("Load Target Text", this, &MainWindow::onLoadTarget);
	fileMenu->addAction("Load Audio Text (SRT/JSON)", this, &MainWindow::onLoadAudioText);
	fileMenu->addAction("Load Audio File (MP3)", this, &MainWindow::onLoadAudioFile);
	fileMenu->addSeparator();
	fileMenu->addAction("New project", this, &MainWindow::onNewProject);
	fileMenu->addAction("Load project...", this, &MainWindow::onLoadProject);
	fileMenu->addAction("Save project", this, &MainWindow::onSaveProject);
	fileMenu->addAction("Save project as...", this, &MainWindow::onSaveProjectAs);
	fileMenu->addSeparator();
	fileMenu->addAction("Exit", this, &MainWindow::onExit, QKeySequence::Quit);

	QMenu* editMenu = menuBar()->addMenu("&Edit");
	editMenu->addAction("Split Cell", this, &MainWindow::onSplitCell, QKeySequence(Qt::CTRL + Qt::Key_T));
	editMenu->addAction("Merge with Previous", this, &MainWindow::onMergeWithPrevious, QKeySequence(Qt::CTRL + Qt::Key_Up));
	editMenu->addAction("Merge with Next", this, &MainWindow::onMergeWithNext, QKeySequence(Qt::CTRL + Qt::Key_Down));
	editMenu->addAction("Exclude Row", this, &MainWindow::onExcludeRow, QKeySequence::Delete);
	editMenu->addSeparator();
	editMenu->addAction("Merge all with Previous", this, &MainWindow::onMergeAllWithPrevious);
	editMenu->addAction("Merge all with Next", this, &MainWindow::onMergeAllWithNext);
	editMenu->addSeparator();
	editMenu->addAction("Clear Source", this, &MainWindow::onClearSource);
	editMenu->addAction("Clear Target", this, &MainWindow::onClearTarget);
	editMenu->addAction("Clear Audio", this, &MainWindow::onClearAudio);
	editMenu->addAction("Normalize rows", this, &MainWindow::onNormalizeRows);


	QMenu* toolsMenu = menuBar()->addMenu("&Tools");
	toolsMenu->addAction("Target align", this, &MainWindow::onTargetAlign);
	toolsMenu->addAction("Audio align", this, &MainWindow::onAudioAlign);
	toolsMenu->addAction("Recalc", this, &MainWindow::onRecalc);
	
	toolsMenu->addSeparator();
	toolsMenu->addAction("Split MP3 by Rows", this, &MainWindow::onSplitAudio);
	toolsMenu->addAction("Generate MP3", this, &MainWindow::onGenerateAudio);
}

void MainWindow::createToolBar()
{
	QToolBar* toolbar = addToolBar("Main");
	toolbar->addAction("Load Project", this, &MainWindow::onLoadProject);
	toolbar->addAction("Load Source", this, &MainWindow::onLoadSource);
	toolbar->addAction("Load Target", this, &MainWindow::onLoadTarget);
	toolbar->addAction("Load Audio Text", this, &MainWindow::onLoadAudioText);
	toolbar->addSeparator();
	toolbar->addAction("Target align", this, &MainWindow::onTargetAlign);
	toolbar->addAction("Audio align", this, &MainWindow::onAudioAlign);
	
	toolbar->addSeparator();
	toolbar->addAction("Split", this, &MainWindow::onSplitCell);
	toolbar->addAction("Merge with Prev", this, &MainWindow::onMergeWithPrevious);
	toolbar->addAction("Merge with Next", this, &MainWindow::onMergeWithNext);
	toolbar->addSeparator();
	toolbar->addAction("Split MP3", this, &MainWindow::onSplitAudio);
	toolbar->addAction("Generate MP3", this, &MainWindow::onGenerateAudio);
	toolbar->addSeparator();
	toolbar->addAction("Show Audio Entires", this, &MainWindow::onShowAudioEntires);
}

void MainWindow::createStatusBar()
{
	statusBar()->showMessage("Ready");
}

void MainWindow::onClearSource()
{
	m_aligner.clearSource();
	syncTableFromAligner();
}

void MainWindow::onClearTarget()
{
	m_aligner.clearTarget();
	syncTableFromAligner();
}

void MainWindow::onClearAudio()
{
	m_aligner.clearAudio();
	syncTableFromAligner();
}

void MainWindow::onNormalizeRows()
{
	m_aligner.normalizeRowCount();
	syncTableFromAligner();
}

void MainWindow::syncTableFromAligner()
{
	int rows = m_aligner.rowCount();
	m_table->setRowCount(rows);

	for (int i = 0; i < rows; ++i) {
		// Получаем текст для каждого столбца
		QString enText = (i < m_aligner.sourceCells.size()) ? m_aligner.sourceCells[i].text : "";
		QString ruText = (i < m_aligner.targetCells.size()) ? m_aligner.targetCells[i].text : "";
		QString audioText = (i < m_aligner.audioCells.size()) ? m_aligner.audioCells[i].text : "";
		QString infoText = (i < m_aligner.audioCells.size()) ? 
			QString::asprintf("%g:%g", m_aligner.audioCells[i].audioStartMs/1000.0, 
			(m_aligner.audioCells[i].audioEndMs - m_aligner.audioCells[i].audioStartMs)/1000.0) : "";

		// Статус excluded для каждого столбца
		bool enExcl = (i < m_aligner.sourceCells.size()) && m_aligner.sourceCells[i].isExcluded;
		bool ruExcl = (i < m_aligner.targetCells.size()) && m_aligner.targetCells[i].isExcluded;
		bool audioExcl = (i < m_aligner.audioCells.size()) && m_aligner.audioCells[i].isExcluded;
		bool audioErr = (i < m_aligner.audioCells.size()) && m_aligner.audioCells[i].isError;

		// Цвета фона
		QColor enBg = enExcl ? Qt::lightGray : Qt::white;
		QColor ruBg = ruExcl ? Qt::lightGray : Qt::white;
		QColor audioBg = 
			audioErr ? Qt::red :
			audioExcl ? Qt::lightGray : Qt::white;
		QColor infoBg = Qt::lightGray;  // Отладочный столбец всегда серый

		// Обновляем ячейки с правильным приведением типов
		updateCell(i, 0, enText, enBg);
		updateCell(i, 1, ruText, ruBg);
		updateCell(i, 2, audioText, audioBg);
		updateCell(i, 3, infoText, infoBg);
	}

	updateRowHeights();
}

void MainWindow::updateCell(int row, int column, const QString& text, const QColor& bgColor)
{
	QTableWidgetItem* item = m_table->item(row, column);
	if (!item) {
		item = new QTableWidgetItem();
		// Правильный способ установить флаги
		Qt::ItemFlags flags = item->flags();
		flags = Qt::ItemFlags(flags | Qt::TextWordWrap);
		item->setFlags(flags);
		m_table->setItem(row, column, item);
	}
	item->setText(text);
	item->setBackground(bgColor);
}

void MainWindow::syncAlignerFromTable()
{
	int rows = m_table->rowCount();

	// Расширяем массивы если нужно
	while (m_aligner.sourceCells.size() < rows) m_aligner.sourceCells.append(TextSentence());
	while (m_aligner.targetCells.size() < rows) m_aligner.targetCells.append(TextSentence());
	while (m_aligner.audioCells.size() < rows) m_aligner.audioCells.append(AudioSentence());

	for (int i = 0; i < rows; ++i) {
		if (m_table->item(i, 0)) m_aligner.sourceCells[i].text = m_table->item(i, 0)->text();
		if (m_table->item(i, 1)) m_aligner.targetCells[i].text = m_table->item(i, 1)->text();
		if (m_table->item(i, 2)) m_aligner.audioCells[i].text = m_table->item(i, 2)->text();
	}

	m_aligner.modified = true;
}

void MainWindow::updateRowHeights()
{
	for (int i = 0; i < m_table->rowCount(); ++i) {
		m_table->resizeRowToContents(i);
	}
}

void MainWindow::setModified(bool modified)
{
	m_aligner.modified = modified;
	QString title = "Text Alignment Tool";
	if (modified) title += " [modified]";
	setWindowTitle(title);
}

void MainWindow::onLoadSource()
{
	QString filename = QFileDialog::getOpenFileName(this, "Load Source Text",
		QString(), "Text files (*.txt);;All files (*.*)");
	if (filename.isEmpty()) return;

	m_aligner.loadSourceText(filename);
	syncTableFromAligner();
	setModified(false);
	statusBar()->showMessage(QString("Loaded %1 sentences").arg(m_aligner.rowCount()), 3000);
}

void MainWindow::onLoadTarget()
{
	QString filename = QFileDialog::getOpenFileName(this, "Load Target Text",
		QString(), "Text files (*.txt);;All files (*.*)");
	if (filename.isEmpty()) return;

	m_aligner.loadTargetText(filename);
	statusBar()->showMessage("Target text loaded. Use Target align to match with Source", 3000);
}

bool MainWindow::loadAudioTextFile(const QString& filename)
{
	if (filename.isEmpty()) return false;

	QVector<AudioEntry> entries = AudioParser::parseFile(filename);

	if (entries.isEmpty()) {
		QMessageBox::warning(this, "Error", "No valid entries found in file");
		return false;
	}

	m_aligner.loadAudioEntries(entries, filename);
	statusBar()->showMessage(QString("Loaded %1 audio entries from %2")
		.arg(entries.size())
		.arg(QFileInfo(filename).suffix().toUpper()), 3000);

	return true;
}

void MainWindow::onLoadAudioText()
{
	QString filename = QFileDialog::getOpenFileName(this, "Load Audio Text",
		QString(), "SRT/JSON files (*.srt *.json);;All files (*.*)");

	loadAudioTextFile(filename);
}

void MainWindow::onLoadAudioFile()
{
	QString filename = QFileDialog::getOpenFileName(this, "Load Audio File",
		QString(), "MP3 files (*.mp3);;All files (*.*)");
	if (filename.isEmpty()) return;

	m_aligner.loadAudioFile(filename);
	statusBar()->showMessage("Audio file loaded: " + filename, 3000);
}

void MainWindow::onExit()
{
	close();
}

void MainWindow::onShowAudioEntires()
{
	showAudioEntriesViewer();
}

void MainWindow::onSplitCell()
{
	int row = m_table->currentRow();
	int col = m_table->currentColumn();

	if (row < 0 || (col != 0 && col != 1 && col != 2)) {
		QMessageBox::information(this, "Info", "Select a cell to split");
		return;
	}

	onEditCell();  // Открываем диалог, где пользователь поставит курсор
}

void MainWindow::onMergeWithPrevious()
{
	int row = m_table->currentRow();
	int col = m_table->currentColumn();

	if (row <= 0) {
		QMessageBox::information(this, "Info", "Cannot merge first row with previous");
		return;
	}

	if (col < 0 || col > 2) {
		QMessageBox::information(this, "Info", "Select a cell in the column you want to merge");
		return;
	}

	m_aligner.mergeCells(row - 1, row, col);
	syncTableFromAligner();
	setModified(true);

	QString columnName = (col == 0) ? "Source" : (col == 1) ? "Target" : "Audio";
	statusBar()->showMessage(QString("Merged %1 column").arg(columnName), 2000);
}

void MainWindow::onMergeWithNext()
{
	int row = m_table->currentRow();
	int col = m_table->currentColumn();

	if (row < 0 || row >= m_table->rowCount() - 1) {
		QMessageBox::information(this, "Info", "Cannot merge last row with next");
		return;
	}

	if (col < 0 || col > 2) {
		QMessageBox::information(this, "Info", "Select a cell in the column you want to merge");
		return;
	}

	m_aligner.mergeCells(row, row + 1, col);
	syncTableFromAligner();
	setModified(true);

	QString columnName = (col == 0) ? "Source" : (col == 1) ? "Target" : "Audio";
	statusBar()->showMessage(QString("Merged %1 column").arg(columnName), 2000);
}

void MainWindow::onMergeAllWithPrevious()
{
	int row = m_table->currentRow();
	if (row < 0 || row >= m_table->rowCount() - 1) {
		QMessageBox::information(this, "Info", "Cannot merge last row with next");
		return;
	}
	m_aligner.mergeCells(row - 1, row, 0);
	m_aligner.mergeCells(row - 1, row, 1);
	m_aligner.mergeCells(row - 1, row, 2);
	syncTableFromAligner();
	setModified(true);
	statusBar()->showMessage(QString("Merged"), 2000);
}

void MainWindow::onMergeAllWithNext()
{
	int row = m_table->currentRow();
	if (row < 0 || row >= m_table->rowCount() - 1) {
		QMessageBox::information(this, "Info", "Cannot merge last row with next");
		return;
	}
	m_aligner.mergeCells(row, row + 1, 0);
	m_aligner.mergeCells(row, row + 1, 1);
	m_aligner.mergeCells(row, row + 1, 2);
	syncTableFromAligner();
	setModified(true);
	statusBar()->showMessage(QString("Merged"), 2000);
}


void MainWindow::onExcludeRow()
{
	int row = m_table->currentRow();
	int col = m_table->currentColumn();

	if (row < 0) return;
	if (col < 0 || col > 2) {
		QMessageBox::information(this, "Info", "Select a cell in the column to exclude");
		return;
	}

	m_aligner.excludeCell(row, col);
	syncTableFromAligner();
	setModified(true);

	QString columnName = (col == 0) ? "Source" : (col == 1) ? "Target" : "Audio";
	statusBar()->showMessage(QString("Cell in %1 column %2").arg(columnName)
		.arg(m_aligner.sourceCells[row].isExcluded ? "excluded" : "included"), 2000);
}

void MainWindow::onDebugInfo()
{
	int row = m_table->currentRow();
	int col = m_table->currentColumn();

	if (row < 0) return;
	if (col < 0 || col > 2) {
		QMessageBox::information(this, "Info", "Select a cell in the column to exclude");
		return;
	}

	QString s = m_aligner.sourceWordsBySentence(row);
	QMessageBox::information(this, "Info", s);
}

void MainWindow::onRecalc()
{
	m_aligner.calcLexicalSimilarity();
	syncTableFromAligner();
}

void MainWindow::onTargetAlign()
{
	if (m_aligner.rowCount()==0) {
		QMessageBox::warning(this, "Error", "Load Source text first");
		return;
	}

	if (m_aligner.currentTargetFile.isEmpty()) {
		QMessageBox::warning(this, "Error", "Load Target text first");
		return;
	}

	m_aligner.alignTargetToSource();
	syncTableFromAligner();
	setModified(true);
	statusBar()->showMessage("Target alignment completed", 3000);
}

void MainWindow::onAudioAlign()
{
	if (m_aligner.rowCount() == 0) {
		QMessageBox::warning(this, "Error", "Load Source text first");
		return;
	}

	if (m_aligner.currentAudioTextFile.isEmpty()) {
		QMessageBox::warning(this, "Error", "Load Audio text first");
		return;
	}

	// Если аудио текст ещё не загружен, но путь есть в проекте
	if (m_aligner.audioEntries.isEmpty() && !m_aligner.currentAudioTextFile.isEmpty()) {
		// Пробуем загрузить автоматически
		if (loadAudioTextFile(m_aligner.currentAudioTextFile)) {
			statusBar()->showMessage("Audio text auto-loaded from project path", 2000);
		}
		else {
			QMessageBox::warning(this, "Error",
				QString("Failed to load audio text file:\n%1").arg(m_aligner.currentAudioTextFile));
			return;
		}
	}

	// Проверяем, что теперь аудио текст загружен
	if (m_aligner.audioEntries.isEmpty()) {
		QMessageBox::warning(this, "Error", "Load audio text (SRT/JSON) first");
		return;
	}

	m_aligner.alignAudioToSource();

	syncTableFromAligner();
	setModified(true);
	statusBar()->showMessage("Audio-alignment completed", 3000);
}

void MainWindow::onSplitAudio()
{
	if (m_aligner.currentAudioFile.isEmpty()) {
		QMessageBox::warning(this, "Error", "Load audio file first");
		return;
	}
	
	m_aligner.splitAudioToMp3(cfg.ffmpegPath);
	
	statusBar()->showMessage(QString("Audio file splitted"), 3000);
}

void MainWindow::onGenerateAudio()
{

}


void MainWindow::showContextMenu(const QPoint& pos)
{
	QModelIndex index = m_table->indexAt(pos);
	if (!index.isValid()) return;

	QMenu menu;
	menu.addAction("Edit", this, &MainWindow::onEditCell);
	menu.addSeparator();
	menu.addAction("Split", this, &MainWindow::onSplitCell);
	menu.addAction("Merge with previous", this, &MainWindow::onMergeWithPrevious);
	menu.addAction("Merge with next", this, &MainWindow::onMergeWithNext);
	menu.addSeparator();
	menu.addAction("Merge all with previous", this, &MainWindow::onMergeAllWithPrevious);
	menu.addAction("Merge all with next", this, &MainWindow::onMergeAllWithNext);
	menu.addSeparator();
	menu.addAction("Exclude", this, &MainWindow::onExcludeRow);
	menu.addSeparator();
	menu.addAction("Copy", [this]() {
		if (m_table->currentItem()) {
			QApplication::clipboard()->setText(m_table->currentItem()->text());
		}
	});
	menu.addSeparator();
	menu.addAction("Debug info", this, &MainWindow::onDebugInfo);

	menu.exec(m_table->viewport()->mapToGlobal(pos));
}

void MainWindow::onEditCell()
{
	int row = m_table->currentRow();
	int col = m_table->currentColumn();

	if (row < 0) return;

	QTableWidgetItem* item = m_table->item(row, col);
	if (!item) return;

	QString originalText = item->text();

	// Создаём диалог с многострочным редактором
	QDialog dialog(this);
	dialog.setWindowTitle(QString("Edit %1").arg(m_table->horizontalHeaderItem(col)->text()));
	dialog.resize(600, 400);

	QVBoxLayout* layout = new QVBoxLayout(&dialog);

	QTextEdit* textEdit = new QTextEdit(&dialog);
	textEdit->setPlainText(originalText);
	textEdit->setLineWrapMode(QTextEdit::WidgetWidth);

	// Увеличиваем шрифт в 2 раза
	QFont font = textEdit->font();
	font.setPointSize(font.pointSize() * 2);
	textEdit->setFont(font);

	layout->addWidget(textEdit);

	QHBoxLayout* buttonLayout = new QHBoxLayout();

	QPushButton* okButton = new QPushButton("OK", &dialog);
	QPushButton* cancelButton = new QPushButton("Cancel", &dialog);
	QPushButton* splitButton = new QPushButton("Split at Cursor", &dialog);

	buttonLayout->addWidget(okButton);
	buttonLayout->addWidget(splitButton);
	buttonLayout->addWidget(cancelButton);
	layout->addLayout(buttonLayout);

	// Split button logic
	connect(splitButton, &QPushButton::clicked, [&]() {
		int cursorPos = textEdit->textCursor().position();
		if (cursorPos > 0 && cursorPos < textEdit->toPlainText().length()) {
			// Закрываем диалог и выполняем split
			dialog.accept();

			// Сохраняем изменения перед split
			QString newText = textEdit->toPlainText();
			if (newText != originalText) {
				if (col == 0) m_aligner.sourceCells[row].text = newText;
				if (col == 1) m_aligner.targetCells[row].text = newText;
				if (col == 2) m_aligner.audioCells[row].text = newText;
			}
			
			// Вызываем split
			splitRowAtPosition(row, col, cursorPos);
		}
		else {
			QMessageBox::information(&dialog, "Info",
				"Place cursor inside text to split (not at beginning or end)");
		}
	});

	connect(okButton, &QPushButton::clicked, [&]() {
		QString newText = textEdit->toPlainText();
		if (newText != originalText) {
			item->setText(newText);
			if (col == 0) m_aligner.sourceCells[row].text = newText;
			if (col == 1) m_aligner.targetCells[row].text = newText;
			if (col == 2) m_aligner.audioCells[row].text = newText;
			m_aligner.modified = true;
			setModified(true);
			updateRowHeights();
		}
		dialog.accept();
	});

	connect(cancelButton, &QPushButton::clicked, &dialog, &QDialog::reject);

	dialog.exec();
}

void MainWindow::splitRowAtPosition(int row, int col, int cursorPos)
{
	m_aligner.splitCell(row, cursorPos, col);
	syncTableFromAligner();
	setModified(true);
	statusBar()->showMessage("Cell split", 2000);
}

void MainWindow::onSaveProject()
{
	if (m_aligner.projectPath.isEmpty()) {
		onSaveProjectAs();
	}
	else {
		if (m_aligner.saveProjectTxt(m_aligner.projectPath)) {
			statusBar()->showMessage("Project saved: " + m_aligner.projectPath, 3000);
			setModified(false);
		}
		else {
			QMessageBox::warning(this, "Error", "Failed to save project");
		}
	}
}

void MainWindow::onSaveProjectAs()
{
	QString filename = QFileDialog::getSaveFileName(this, "Save Project",
		QString(), "Alignment Project (*.align);;All files (*.*)");
	if (filename.isEmpty()) return;

	if (!filename.endsWith(".align")) filename += ".align";

	if (m_aligner.saveProjectTxt(filename)) {
		m_aligner.projectPath = filename;
		setModified(false);
		statusBar()->showMessage("Project saved: " + filename, 3000);
	}
	else {
		QMessageBox::warning(this, "Error", "Failed to save project");
	}
}

void MainWindow::onLoadProject()
{
	QString filename = QFileDialog::getOpenFileName(this, "Load Project",
		cfg.recentProjectPath, "Alignment Project (*.align);;All files (*.*)");
	if (filename.isEmpty()) return;

	if (m_aligner.loadProjectTxt(filename)) {
		m_aligner.projectPath = filename;
		cfg.recentProjectPath = filename;
		syncTableFromAligner();
		setModified(false);
		statusBar()->showMessage("Project loaded: " + filename, 3000);
	}
	else {
		QMessageBox::warning(this, "Error", "Failed to load project");
	}
}

void MainWindow::closeEvent(QCloseEvent* event)
{
	// Если нет изменений, закрываемся без вопросов
	if (!m_aligner.modified) {
		event->accept();
		return;
	}

	// Спрашиваем пользователя
	QMessageBox msgBox(this);
	msgBox.setWindowTitle("Save Changes");
	msgBox.setText("The project has been modified.\nDo you want to save your changes?");
	msgBox.setIcon(QMessageBox::Question);

	QPushButton* saveButton = msgBox.addButton("Save", QMessageBox::AcceptRole);
	QPushButton* discardButton = msgBox.addButton("Discard", QMessageBox::DestructiveRole);
	QPushButton* cancelButton = msgBox.addButton("Cancel", QMessageBox::RejectRole);

	msgBox.setDefaultButton(saveButton);

	msgBox.exec();

	QAbstractButton* clicked = msgBox.clickedButton();

	if (clicked == saveButton) {
		// Сохраняем проект
		onSaveProject();

		// Если сохранение прошло успешно (modified сброшен), закрываемся
		if (!m_aligner.modified) {
			event->accept();
		}
		else {
			// Сохранение не удалось или было отменено
			event->ignore();
		}
	}
	else if (clicked == discardButton) {
		// Отбрасываем изменения и закрываемся
		event->accept();
	}
	else {
		// Cancel
		event->ignore();
	}
}

void MainWindow::onNewProject()
{
	// Проверяем, есть ли несохранённые изменения
	if (m_aligner.modified) {
		QMessageBox::StandardButton reply = QMessageBox::question(
			this, "New Project",
			"You have unsaved changes. Do you want to save them?",
			QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel
		);

		if (reply == QMessageBox::Save) {
			onSaveProject();
			if (m_aligner.modified) return; // сохранение отменено
		}
		else if (reply == QMessageBox::Cancel) {
			return;
		}
	}

	// Очищаем всё
	m_aligner.clear();
	syncTableFromAligner();
	setModified(false);
	statusBar()->showMessage("New project created", 3000);
}

