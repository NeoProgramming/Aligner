#include "aligner.h"
#include <QFile>
#include <QTextStream>
#include <QTextBoundaryFinder>
#include <QDebug>
#include <QDir>
#include <QProcess>
#include <QRegularExpression>
#include <QDateTime>

#include "tools.h"
#include "MyAligner.h"
#include "NeedlemanWunschAligner.h"
#include "AdvancedAligner1.h"
#include "AdvancedAligner2.h"
#include "StreamingAligner.h"

void Aligner::clearSource()
{
	for (auto &cell : enCells)
		cell.clear();
	modified = true;
}

void Aligner::clearTarget()
{
	for (auto &cell : ruCells)
		cell.clear();
	modified = true;
}

void Aligner::clearAudio()
{
	for (auto &cell : audioCells)
		cell.clear();
	modified = true;
}

int Aligner::getSourceWordsCount() const
{
	return m_enWordsCache.size();
}

int Aligner::getAudioWordsCount() const
{
	return audioEntries.size();
}

const QString& Aligner::getSourceWord(int index) const
{
	static const QString EMPTY;
	if (index < 0 || index >= m_enWordsCache.size()) {
		qWarning() << "getSourceWord: invalid index" << index << "size:" << m_enWordsCache.size();
		assert(false);
		return EMPTY;
	}
	return m_enWordsCache[index].text;
}

int Aligner::getSourceSentence(int index) const
{
	if (index < 0 || index >= m_enWordsCache.size()) {
		return -1;
	}
	return m_enWordsCache[index].sentenceIndex;
}

void Aligner::setAudioSentence(int index, int sentidx, bool ins)
{
	if (index < 0 || index >= audioEntries.size()) {
		return;
	}
	audioEntries[index].sentenceIdx = sentidx;
	audioEntries[index].ins = ins;
}

const QString& Aligner::getAudioWord(int index) const
{
	static const QString EMPTY;
	if (index < 0 || index >= audioEntries.size()) {
		qWarning() << "getAudioWord: invalid index" << index << "size:" << audioEntries.size();
		assert(false);
		return EMPTY;
	}
	return audioEntries[index].text;
}

void Aligner::assignMatchedGroup(int sourceStart, int sourceCount, int audioStart, int audioCount)
{
	int sourceEnd = qMin(sourceStart + sourceCount, m_enWordsCache.size());
	int audioEnd = qMin(audioStart + audioCount, audioEntries.size());

	int i = sourceStart;
	int j = audioStart;
	int currentSentence = -1;
	
	// ОСНОВНОЙ ЦИКЛ - пока просто сопоставляем попарно, игнорируя возможные различия
	// в будущем переделать, используя более умное сопоставление
	while (i < sourceEnd && j < audioEnd) {
		int sentIdx = getSourceSentence(i);

		// Нормальное сопоставление (не вставка)
		setAudioSentence(j, sentIdx, false);

		i++;
		j++;
	}

	// Оставшиеся аудио слова (если audioCount > sourceCount) добавляем к последнему предложению
	while (j < audioEnd) {
		setAudioSentence(j, currentSentence, true);
		j++;
	}

	// Примечание: если sourceCount > audioCount (удаления),
	// они просто игнорируются — для них не вызывается setAudioSentence
}

void Aligner::flushPendingGroup(int sourceIndex, int audioStart, int audioCount)
{
	// Определяем, к какому предложению привязать вставку
	int targetSentence = -1;

	if (sourceIndex < m_enWordsCache.size()) {
		// Если есть привязка к исходному тексту, берём предложение этого слова
		targetSentence = m_enWordsCache[sourceIndex].sentenceIndex;
	}
	else {
		// Если sourceIndex за пределами исходного текста, вставляем в конец
		// Здесь нужно определить максимальный индекс предложения
		// Вариант: взять последнее существующее предложение или создать новое
		targetSentence = enCells.size() - 1; // или audioCells.size() - 1
	}

	// Проходим по всем аудиословам в группе
	for (int i = 0; i < audioCount; ++i) {
		int audioWordIndex = audioStart + i;

		// Устанавливаем слово как вставку (ins=true) с указанным предложением
		setAudioSentence(audioWordIndex, targetSentence, true);
	}
}

void Aligner::assignMatchedGroup2(int sourceStart, int sourceCount, int audioStart, int audioCount)
{
	int sourceEnd = qMin(sourceStart + sourceCount, m_enWordsCache.size());
	int audioEnd = qMin(audioStart + audioCount, audioEntries.size());

	int i = sourceStart;
	int j = audioStart;

	int currentSentence = m_enWordsCache[i].sentenceIndex;
	QStringList currentAudioText;

	int currentStartMs = audioEntries[j].startMs;
	int currentEndMs = audioEntries[j].endMs;

	// ЛЯМБДА для сохранения текущего предложения
	auto flushCurrentSentence = [&]() {
		if (currentAudioText.isEmpty()) {
			return;
		}

		// Расширяем вектор если нужно
		while (audioCells.size() <= currentSentence) {
			audioCells.append(CellData());
		}

		// Добавляем текст
		if (!audioCells[currentSentence].text.isEmpty()) {
			audioCells[currentSentence].text += " ";
		}
		QString ntext = currentAudioText.join(" ");
		
		qDebug() << "FMATCH S=" << currentSentence << audioCells[currentSentence].text << " : " << ntext;

		audioCells[currentSentence].text += ntext;

		// Обновляем время начала
		if (audioCells[currentSentence].audioStartMs < 0 ||
			currentStartMs < audioCells[currentSentence].audioStartMs) {
			audioCells[currentSentence].audioStartMs = currentStartMs;
		}

		// Обновляем время конца
		if (currentEndMs > audioCells[currentSentence].audioEndMs) {
			audioCells[currentSentence].audioEndMs = currentEndMs;
		}

		audioCells[currentSentence].isExcluded = false;
	};

	// ОСНОВНОЙ ЦИКЛ
	while (i < sourceEnd && j < audioEnd) {
		int sentIdx = m_enWordsCache[i].sentenceIndex;

		if (sentIdx != currentSentence) {
			// Сохраняем накопленное предложение
			flushCurrentSentence();

			// Начинаем новое предложение
			currentSentence = sentIdx;
			currentAudioText.clear();

			if (j < audioEnd) {
				currentStartMs = audioEntries[j].startMs;
				currentEndMs = audioEntries[j].endMs;
			}
		}

		// Добавляем слово
		if (j < audioEnd) {
			currentAudioText.append(audioEntries[j].text);
			currentEndMs = audioEntries[j].endMs;
		}

		i++;
		j++;
	}

	// Оставшиеся аудио слова (если audioCount > sourceCount)
	while (j < audioEnd) {
		currentAudioText.append(audioEntries[j].text);
		currentEndMs = audioEntries[j].endMs;
		j++;
	}

	// Сохраняем последнее предложение (используем ту же лямбду)
	flushCurrentSentence();
}

void Aligner::flushPendingGroup2(int sourceIndex, int audioStart, int audioCount)
{
	int pendingStartMs = -1, pendingEndMs;
	QStringList pendingAudio;

	// формируем предложение во временном списке и рассчитываем время начала и конца
	for (int i = 0; i < audioCount; ++i) {
		pendingAudio.append(audioEntries[audioStart + i].text);
		if (pendingStartMs == -1)
			pendingStartMs = audioEntries[audioStart + i].startMs;
		pendingEndMs = audioEntries[audioStart + i].endMs;
	}

	// Вычисляем позицию вставки: в конец массива предложений или в конкретную позицию 
	int insertPosition = sourceIndex >= m_enWordsCache.size()
		? enCells.size()
		: m_enWordsCache[sourceIndex].sentenceIndex;

	// ставим группу в очередь вставки
	PendingInsert insert;
	insert.index = insertPosition;
	insert.audioCell.text = pendingAudio.join(" ");
	insert.audioCell.isExcluded = false;
	insert.audioCell.isError = false;
	insert.audioCell.audioStartMs = pendingStartMs;
	insert.audioCell.audioEndMs = pendingEndMs;
	m_pendingInserts.append(insert);

	qDebug() << "PENDING S=" << insertPosition << insert.audioCell.text;

}


Aligner::Aligner()
	: modified(false)
{
	// Инициализируем массив указателей
	columnCells.append(&enCells);   // индекс 0
	columnCells.append(&ruCells);   // индекс 1
	columnCells.append(&audioCells); // индекс 2

	loadDictionary("my_initial_dict.dic");
}

int Aligner::rowCount() const
{
	// Максимальное количество ячеек среди всех столбцов
	return qMax(enCells.size(), qMax(ruCells.size(), audioCells.size()));
}

void Aligner::normalizeRowCount()
{
	int targetRows = rowCount();

	// Добиваем каждый столбец до targetRows пустыми ячейками
	while (enCells.size() < targetRows) {
		enCells.append(CellData());
	}
	while (ruCells.size() < targetRows) {
		ruCells.append(CellData());
	}
	while (audioCells.size() < targetRows) {
		audioCells.append(CellData());
	}

	// 2. Удаляем пустые строки с конца
	for (int i = enCells.size() - 1; i >= 0; --i) {
		bool enEmpty = enCells[i].text.isEmpty();
		bool ruEmpty = ruCells[i].text.isEmpty();
		bool audioEmpty = audioCells[i].text.isEmpty();

		if (enEmpty && ruEmpty && audioEmpty) {
			enCells.removeAt(i);
			ruCells.removeAt(i);
			audioCells.removeAt(i);
		}
	}
}

void Aligner::splitCell(int row, int cursorPos, int column)
{
	if (column < 0 || column >= columnCells.size()) return;
	QVector<CellData>* cells = columnCells[column];
	if (row >= cells->size()) return;

	QString text = (*cells)[row].text;
	if (cursorPos <= 0 || cursorPos >= text.length()) return;

	// Находим границу слова ВЛЕВО от курсора
	int splitPos = -1;
	for (int i = cursorPos - 1; i >= 0; --i) {
		QChar ch = text[i];
		if (ch.isSpace() || ch == '.' || ch == ',' || ch == '!' || ch == '?' ||
			ch == ';' || ch == ':' || ch == '-' || ch == '(' || ch == ')' ||
			ch == '"' || ch == '\'' || ch == '[' || ch == ']' || ch == '{' || ch == '}') {
			splitPos = i + 1;
			break;
		}
	}

	if (splitPos == -1) return;
	if (splitPos <= 0 || splitPos >= text.length()) return;

	QString firstPart = text.left(splitPos).trimmed();
	QString secondPart = text.mid(splitPos).trimmed();

	if (secondPart.isEmpty()) return;

	// Обновляем текущую ячейку
	(*cells)[row].text = firstPart;

	// Вставляем новую ячейку ниже
	CellData newCell;
	newCell.text = secondPart;
	newCell.isExcluded = false;
	newCell.audioStartMs = -1;
	newCell.audioEndMs = -1;

	cells->insert(row + 1, newCell);

	// Нормализуем количество строк
	normalizeRowCount();
	modified = true;
}

void Aligner::mergeCells(int row1, int row2, int column)
{
	if (row1 == row2) return;
	if (row1 > row2) std::swap(row1, row2);
	if (column < 0 || column >= columnCells.size()) return;
	QVector<CellData>* cells = columnCells[column];


	// Убеждаемся, что индексы валидны
	if (row2 >= cells->size()) {
		// Если второй ячейки нет, просто выходим
		return;
	}

	// Объединяем текст
	QString merged = (*cells)[row1].text;
	if (!merged.isEmpty() && !(*cells)[row2].text.isEmpty()) {
		merged += " ";
	}
	merged += (*cells)[row2].text;

	(*cells)[row1].text = merged;

	// Удаляем вторую ячейку
	cells->removeAt(row2);

	// Если ячейка в конце и пуста, можно удалить, но оставим для нормализации
	normalizeRowCount();
	modified = true;
}

void Aligner::excludeCell(int row, int column)
{
	if (column < 0 || column >= columnCells.size()) return;
	QVector<CellData>* cells = columnCells[column];

	if (row < cells->size()) {
		(*cells)[row].isExcluded = !(*cells)[row].isExcluded;
		modified = true;
	}
	normalizeRowCount();
}

void Aligner::setCellText(int row, int column, const QString& text)
{
	if (column < 0 || column >= columnCells.size()) return;
	QVector<CellData>* cells = columnCells[column];

	// Расширяем массив если нужно
	while (cells->size() <= row) {
		cells->append(CellData());
	}

	(*cells)[row].text = text;
	normalizeRowCount();
	modified = true;
}


void Aligner::clear()
{
	projectPath.clear();
	enCells.clear();
	ruCells.clear();
	audioCells.clear();
	audioEntries.clear();
	currentEnFile.clear();
	currentRuFile.clear();
	currentAudioTextFile.clear();
	currentAudioFile.clear();
	modified = false;
}

QVector<QString> Aligner::splitIntoSentences(const QString& text)
{
	QVector<QString> sentences;
	QTextBoundaryFinder finder(QTextBoundaryFinder::Sentence, text);

	int start = 0;
	while (true) {
		int end = finder.toNextBoundary();
		if (end < 0) break;

		QString sentence = text.mid(start, end - start).trimmed();
		if (!sentence.isEmpty()) {
			sentences.append(sentence);
		}
		start = end;
	}

	return sentences;

	// Merge short sentences (less than 5 words)
	QVector<QString> merged;
	for (int i = 0; i < sentences.size(); ++i) {
		int wordCount = sentences[i].split(' ', Qt::SkipEmptyParts).size();
		if (wordCount < 5 && !merged.isEmpty() && i > 0) {
			merged.last() += " " + sentences[i];
		}
		else {
			merged.append(sentences[i]);
		}
	}

	return merged;
}

void Aligner::loadSourceText(const QString& filename)
{
	QFile file(filename);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		qWarning() << "Cannot open file:" << filename;
		return;
	}

	QTextStream stream(&file);
	stream.setCodec("UTF-8");
	QString content = stream.readAll();
	file.close();

	QVector<QString> sentences = splitIntoSentences(content);

	enCells.clear();
	for (const QString& sent : sentences) {
		CellData cell;
		cell.text = sent;
		cell.isExcluded = false;
		cell.audioStartMs = -1;
		cell.audioEndMs = -1;
		enCells.append(cell);
	}

	currentEnFile = filename;
	normalizeRowCount();
	modified = true;
}

void Aligner::loadTargetText(const QString& filename)
{
	currentRuFile = filename;
	modified = true;
}

void Aligner::loadAudioEntries(const QVector<AudioEntry>& entries, const QString& filename)
{
	audioEntries = entries;
	currentAudioTextFile = filename;
	modified = true;
}

void Aligner::loadAudioFile(const QString& filename)
{
	currentAudioFile = filename;
	modified = true;
}


void Aligner::alignTargetToSource()
{
	// разбить файл второго языка по предложениям 
	// (а в перспективе сопоставить его с текстовым файлом первого языка по словарю)

	if (enCells.isEmpty() || currentRuFile.isEmpty()) return;

	// загружаем текстовый файл второго языка
	QFile file(currentRuFile);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		qWarning() << "Cannot open Russian file:" << currentRuFile;
		return;
	}

	QTextStream stream(&file);
	stream.setCodec("UTF-8");
	QString ruContent = stream.readAll();
	file.close();

	QVector<QString> ruSentences = splitIntoSentences(ruContent);

	ruCells.clear();
	for (const QString& sent : ruSentences) {
		CellData cell;
		cell.text = sent;
		cell.isExcluded = false;
		cell.audioStartMs = -1;
		cell.audioEndMs = -1;
		ruCells.append(cell);
	}

	normalizeRowCount();
	modified = true;

	calcLexicalSimilarity();
}

void Aligner::calcLexicalSimilarity()
{
	// 5. ЭКСПЕРИМЕНТ: считаем метрику для каждой пары
	// Очищаем аудио столбец
	audioCells.clear();

	for (int i = 0; i < enCells.size(); ++i) {
		QString enText = enCells[i].text;
		QString ruText = (i < ruCells.size()) ? ruCells[i].text : "";

		double score = 0.0;

		if (!enText.isEmpty() && !ruText.isEmpty() && !m_dictionary.isEmpty()) {
			score = lexicalSimilarity(enText, ruText);
		}

		// Сохраняем результат в аудио столбец
		CellData scoreCell;
		if (m_dictionary.isEmpty()) {
			scoreCell.text = "no dict";
		}
		else if (ruText.isEmpty()) {
			scoreCell.text = "no ru";
		}
		else {
			scoreCell.text = QString("sim: %1%").arg(qRound(score * 100));
		}
		scoreCell.isExcluded = false;
		audioCells.append(scoreCell);
	}
}

bool Aligner::splitAudioToMp3()
{
	// Проверяем входной файл
	QFileInfo inputInfo(currentAudioFile);
	if (!inputInfo.exists()) {
		qDebug() << "Input audio file not found:" << currentAudioFile;
		return false;
	}

	QString outputDirectory = QFileInfo(currentAudioFile).absolutePath();

	// Создаем выходную директорию, если её нет
	QDir dir;
	if (!dir.mkpath(outputDirectory)) {
		qDebug() << "Failed to create output directory:" << outputDirectory;
		return false;
	}

	int successCount = 0;
	int totalValidCells = 0;

	// Перебираем все ячейки
	for (int i = 0; i < audioCells.size(); ++i) {
		const CellData& cell = audioCells[i];

		// Пропускаем исключенные или пустые ячейки
		if (cell.isExcluded) {
			qDebug() << "Skipping excluded cell" << i;
			continue;
		}

		// Проверяем валидность таймкодов
		if (cell.audioStartMs < 0 || cell.audioEndMs < 0 ||
			cell.audioEndMs <= cell.audioStartMs) {
			qDebug() << "Invalid timestamps for cell" << i
				<< "start:" << cell.audioStartMs
				<< "end:" << cell.audioEndMs;
			continue;
		}

		totalValidCells++;

		// Формируем имя выходного файла
		QString outputFileName = QString("%1_%2.mp3")
			.arg(QFileInfo(currentAudioFile).baseName())
			.arg(i + 1, 4, 10, QChar('0')); // нумерация с 0001

		QString outputFilePath = QDir(outputDirectory)
			.filePath(outputFileName);

		// Конвертируем миллисекунды в формат времени для FFmpeg (HH:MM:SS.xxx)
		QString startTime = msToTimeFormat(cell.audioStartMs);
		QString duration = msToTimeFormat(cell.audioEndMs - cell.audioStartMs);

		// Формируем команду FFmpeg
		QStringList ffmpegArgs;
		ffmpegArgs << "-i" << currentAudioFile;
		ffmpegArgs << "-ss" << startTime;      // время начала
		ffmpegArgs << "-t" << duration;        // длительность
		ffmpegArgs << "-acodec" << "libmp3lame"; // кодек MP3
		ffmpegArgs << "-ab" << "192k";          // битрейт (можно настроить)
		ffmpegArgs << "-ar" << "44100";         // частота дискретизации
		ffmpegArgs << "-ac" << "2";             // стерео
		ffmpegArgs << "-y";                     // перезаписывать файлы
		ffmpegArgs << outputFilePath;

		// Выполняем FFmpeg
		QProcess ffmpeg;
		ffmpeg.start("c:/MySoft/ffmpeg/bin/ffmpeg.exe", ffmpegArgs);

		if (!ffmpeg.waitForStarted(3000)) {
			qDebug() << "Failed to start ffmpeg for cell" << i;
			continue;
		}

		if (!ffmpeg.waitForFinished(60000)) { // таймаут 60 секунд
			qDebug() << "FFmpeg timeout for cell" << i;
			ffmpeg.kill();
			continue;
		}

		// Проверяем результат
		if (ffmpeg.exitCode() == 0 && QFile::exists(outputFilePath)) {
			qDebug() << "Successfully created:" << outputFileName;
			successCount++;

			// Можно добавить теги ID3
		//@	addMetadataToMp3(outputFilePath, cell.text, i);
		}
		else {
			qDebug() << "Failed to create:" << outputFileName;
			qDebug() << "FFmpeg error:" << ffmpeg.readAllStandardError();
		}
	}

	qDebug() << "Split completed. Success:" << successCount
		<< "of" << totalValidCells;

	return successCount == totalValidCells;
}

QString Aligner::exportToCsv() const
{
	QString result;
	result += "\"#\",\"Source\",\"Target\",\"Audio Text\",\"Start (ms)\",\"End (ms)\",\"Excluded Source\",\"Excluded Target\",\"Excluded Audio\"\n";

	int rows = rowCount();
	for (int i = 0; i < rows; ++i) {
		QString en = (i < enCells.size()) ? enCells[i].text : "";
		QString ru = (i < ruCells.size()) ? ruCells[i].text : "";
		QString audio = (i < audioCells.size()) ? audioCells[i].text : "";
		int startMs = (i < audioCells.size()) ? audioCells[i].audioStartMs : -1;
		int endMs = (i < audioCells.size()) ? audioCells[i].audioEndMs : -1;
		bool enExcl = (i < enCells.size()) && enCells[i].isExcluded;
		bool ruExcl = (i < ruCells.size()) && ruCells[i].isExcluded;
		bool audioExcl = (i < audioCells.size()) && audioCells[i].isExcluded;

		// Экранирование кавычек
		QString enEscaped = en;
		enEscaped.replace('"', "\"\"");
		QString ruEscaped = ru;
		ruEscaped.replace('"', "\"\"");
		QString audioEscaped = audio;
		audioEscaped.replace('"', "\"\"");

		result += QString("\"%1\",\"%2\",\"%3\",\"%4\",%5,%6,\"%7\",\"%8\",\"%9\"\n")
			.arg(i + 1)
			.arg(enEscaped)
			.arg(ruEscaped)
			.arg(audioEscaped)
			.arg(startMs)
			.arg(endMs)
			.arg(enExcl ? "yes" : "no")
			.arg(ruExcl ? "yes" : "no")
			.arg(audioExcl ? "yes" : "no");
	}

	return result;
}

QString Aligner::prepareForHunalign(const QVector<CellData>& cells, const QString& lang)
{
	QString result;
	for (const CellData& cell : cells) {
		if (cell.isExcluded) continue;

		// Создаём КОПИЮ строки для изменений
		QString sentence = cell.text;

		// Токенизация: добавляем пробелы вокруг пунктуации
		sentence.replace('.', " . ");
		sentence.replace(',', " , ");
		sentence.replace('!', " ! ");
		sentence.replace('?', " ? ");
		sentence.replace(';', " ; ");
		sentence.replace(':', " : ");
		sentence.replace('(', " ( ");
		sentence.replace(')', " ) ");
		sentence.replace('"', " \" ");
		sentence.replace('\'', " ' ");

		// Специальные символы через QChar или Unicode код
		sentence.replace(QChar(0x2026), " ... ");      // … (горизонтальное многоточие)
		sentence.replace(QChar(0x201C), " \" ");       // “ (левая двойная кавычка)
		sentence.replace(QChar(0x201D), " \" ");       // ” (правая двойная кавычка)
		sentence.replace(QChar(0x201E), " \" ");       // „ (нижняя двойная кавычка)
		sentence.replace(QChar(0x00AB), " \" ");       // « (левая кавычка-ёлочка)
		sentence.replace(QChar(0x00BB), " \" ");       // » (правая кавычка-ёлочка)
		sentence.replace(QChar(0x2014), " - ");       // — (длинное тире)
		sentence.replace(QChar(0x2013), " - ");       // – (короткое тире)
		sentence.replace('-', " - ");

		// Убираем лишние пробелы
		sentence = sentence.simplified();

		result += sentence + "\n";
	}
	return result;
}

QVector<AlignmentPair> Aligner::parseHunalignOutput(const QString& output)
{
	QVector<AlignmentPair> pairs;
	QStringList lines = output.split('\n', Qt::SkipEmptyParts);

	int ruIndex = 0;
	int enIndex = 0;

	for (const QString& line : lines) {
		QStringList parts = line.split('\t');
		if (parts.size() < 3) continue;

		AlignmentPair pair;
		pair.targetText = parts[0].trimmed();
		pair.sourceText = parts[1].trimmed();
		pair.confidence = parts[2].toDouble() / 100.0;

		// Подсчитываем количество предложений в каждом тексте
		pair.targetIndices = countSentences(pair.targetText, ruIndex);
		pair.sourceIndices = countSentences(pair.sourceText, enIndex);

		pairs.append(pair);

		ruIndex += pair.targetIndices.size();
		enIndex += pair.sourceIndices.size();
	}

	return pairs;
}

QVector<int> countSentences(const QString& text, int startIndex)
{
	QVector<int> indices;
	if (text.contains(" ~~~ ")) {
		// Множественные предложения
		int count = text.count(" ~~~ ") + 1;
		for (int i = 0; i < count; ++i) {
			indices.append(startIndex + i);
		}
	}
	else if (!text.isEmpty()) {
		indices.append(startIndex);
	}
	return indices;
}

void Aligner::runHunalignAlignment()
{
	// 1. Подготовка временных файлов
	QString ruFile = "hunalign_ru.txt";// QDir::temp().absoluteFilePath("hunalign_ru.txt");
	QString enFile = "hunalign_en.txt";// QDir::temp().absoluteFilePath("hunalign_en.txt");
	QString outFile = "hunalign_out.txt";// QDir::temp().absoluteFilePath("hunalign_out.txt");
	
	// Путь к словарю (используем готовый или создаём пустой)
	QString dictFile;
	if (QFile::exists("my_combined_dict.dic")) {
		dictFile = "my_combined_dict.dic";
	}
	else if (QFile::exists("my_initial_dict.dic")) {
		dictFile = "my_initial_dict.dic";
	}
	else {
		// Создаём пустой словарь
		dictFile = QDir::temp().absoluteFilePath("null.dic");
		saveToFile(dictFile, "");
	}

	saveToFile(ruFile, prepareForHunalign(ruCells, "ru"));
	saveToFile(enFile, prepareForHunalign(enCells, "en"));

	// 2. Запуск Hunalign
	QProcess process;
	QStringList args;

	// Сначала все ключи
	args << "-utf"
		<< "-realign"
		<< "-text"
	//	<< "-caut=2"  // более консервативный режим (1-3, где 3 — самый строгий)
	//	<< "-ppthresh=30"
	//	<< "-headerthresh=100"
	//	<< "-topothresh=30"
		;

	// Затем словарь
	args << dictFile;

	// Затем входные файлы: source (русский), target (английский)
	args << ruFile << enFile;

	process.start("hunalign.exe", args);
	process.waitForFinished();

	QString output = process.readAllStandardOutput();

	// 3. Парсинг результата
	QVector<AlignmentPair> pairs = parseHunalignOutput(output);

	// 4. Обновление данных
	applyAlignment(pairs);

	// 5. Очистка временных файлов
//	QFile::remove(ruFile);
//	QFile::remove(enFile);
//	QFile::remove(outFile);
}

void Aligner::applyAlignment(const QVector<AlignmentPair>& pairs)
{
	QVector<CellData> newEnCells;
	QVector<CellData> newRuCells;

	for (const AlignmentPair& pair : pairs) {
		// Создаём КОПИИ строк для изменений
		QString enText = pair.sourceText;
		QString ruText = pair.targetText;

		// Заменяем разделители на пробелы
		enText.replace(" ~~~ ", " ");
		ruText.replace(" ~~~ ", " ");

		// Объединяем английские предложения
		CellData enCell;
		enCell.text = enText;
		enCell.isExcluded = false;
		enCell.audioStartMs = -1;
		enCell.audioEndMs = -1;
		newEnCells.append(enCell);

		// Объединяем русские предложения
		CellData ruCell;
		ruCell.text = ruText;
		ruCell.isExcluded = false;
		ruCell.audioStartMs = -1;
		ruCell.audioEndMs = -1;
		newRuCells.append(ruCell);
	}

	enCells = newEnCells;
	ruCells = newRuCells;
	normalizeRowCount();
	modified = true;
}

void Aligner::saveToFile(const QString& filename, const QString& content)
{
	QFile file(filename);
	if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
		QTextStream stream(&file);
		stream.setCodec("UTF-8");
		stream << content;
		file.close();
	}
	else {
		qWarning() << "Cannot write to file:" << filename;
	}
}

void Aligner::loadDictionary(const QString& filename)
{
	QFile file(filename);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		qWarning() << "Cannot open dictionary:" << filename;
		return;
	}

	QTextStream stream(&file);
	stream.setCodec("UTF-8");

	int lineCount = 0;
	while (!stream.atEnd()) {
		QString line = stream.readLine().trimmed();
		if (line.isEmpty()) continue;

		// Формат: английское @ русское
		QStringList parts = line.split('@');
		if (parts.size() >= 2) {
			QString source = parts[0].trimmed();
			QString target = parts[1].trimmed();

			if (!source.isEmpty() && !target.isEmpty()) {
				m_dictionary[target] = source;
				m_dictionaryReverse[source] = target;
				lineCount++;
			}
		}
	}

	file.close();
	qDebug() << "Loaded" << lineCount << "dictionary entries";
}




double Aligner::lexicalSimilarity(const QString& enSentence, const QString& ruSentence)
{
	// Токенизация английского предложения
	QStringList enWords = tokenizeWords(enSentence);
	QStringList ruWords = tokenizeWords(ruSentence);

	if (enWords.isEmpty() || ruWords.isEmpty()) return 0.0;

	// Множество английских слов
	QSet<QString> enSet;
	for (const QString& w : enWords) {
		enSet.insert(w);
	}

	// Множество "эквивалентных" русских слов
	QSet<QString> ruEquivalent;

	for (const QString& ruWord : ruWords) {
		// Если слово совпадает с английским напрямую
		if (enSet.contains(ruWord)) {
			ruEquivalent.insert(ruWord);
		}
		// Иначе пробуем словарь
		else if (m_dictionary.contains(ruWord)) {
			ruEquivalent.insert(m_dictionary[ruWord]);
		}
		// иначе пробуем лемматизированную форму и словарь
		else if (QString stem = stemRussian(ruWord);  m_dictionary.contains(stem)) {
			ruEquivalent.insert(m_dictionary[stem]);
		}
	}

	if (ruEquivalent.isEmpty()) 
		return 0.0;

	// Считаем совпадения
	int score = 0;
	for (const QString& enWord : enWords) {
		if (ruEquivalent.contains(enWord)) {
			score++;
		}
		else if (QString stem = stemEnglish(enWord);  ruEquivalent.contains(stem)) {
			score++;
		}
	}

	// Нормализуем по длине английского предложения
	double dscore = (double)score / enWords.size();

	return dscore;
}

// aligner.cpp
bool Aligner::saveProjectTxt(const QString& filename)
{
	QFile file(filename);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
		return false;
	}

	QTextStream stream(&file);
	stream.setCodec("UTF-8");

	// Заголовок
	stream << "# Alignment Project File\n";
	stream << "# Created: " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
	stream << "# Program: Text Alignment Tool\n";

	if (!currentEnFile.isEmpty())
		stream << "# Source: " << currentEnFile << "\n";
	if (!currentRuFile.isEmpty())
		stream << "# Target: " << currentRuFile << "\n";
	if (!currentAudioTextFile.isEmpty())
		stream << "# AudioText: " << currentAudioTextFile << "\n";
	if (!currentAudioFile.isEmpty())
		stream << "# AudioFile: " << currentAudioFile << "\n";

	stream << "\n";

	// Данные
	int rows = rowCount();
	for (int i = 0; i < rows; ++i) {
		QString en = (i < enCells.size()) ? enCells[i].text : "";
		QString ru = (i < ruCells.size()) ? ruCells[i].text : "";
		QString audio = (i < audioCells.size()) ? audioCells[i].text : "";
		bool enExcl = (i < enCells.size()) && enCells[i].isExcluded;
		bool ruExcl = (i < ruCells.size()) && ruCells[i].isExcluded;
		bool audioExcl = (i < audioCells.size()) && audioCells[i].isExcluded;
		int startMs = (i < audioCells.size()) ? audioCells[i].audioStartMs : -1;
		int endMs = (i < audioCells.size()) ? audioCells[i].audioEndMs : -1;

		// Экранирование спецсимволов? Можно не делать, если доверяем данным
		stream << "source: " << en << "\n";
		stream << "target: " << ru << "\n";
		stream << "audio: " << audio << "\n";
		stream << "src_excl: " << (enExcl ? "true" : "false") << "\n";
		stream << "tgt_excl: " << (ruExcl ? "true" : "false") << "\n";
		stream << "audio_excl: " << (audioExcl ? "true" : "false") << "\n";
		stream << "start_ms: " << startMs << "\n";
		stream << "end_ms: " << endMs << "\n";
		stream << "\n";
	}

	file.close();
	modified = false;
	return true;
}

bool Aligner::loadProjectTxt(const QString& filename)
{
	QFile file(filename);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		return false;
	}

	QTextStream stream(&file);
	stream.setCodec("UTF-8");

	// Очищаем текущие данные
	enCells.clear();
	ruCells.clear();
	audioCells.clear();

	// Временные переменные для текущего блока
	QString currentEn, currentRu, currentAudio;
	bool currentEnExcl = false, currentRuExcl = false, currentAudioExcl = false;
	int currentStartMs = -1, currentEndMs = -1;

	while (!stream.atEnd()) {
		QString line = stream.readLine();

		// Пропускаем пустые строки
		if (line.trimmed().isEmpty()) {
			if (!currentEn.isEmpty() || !currentRu.isEmpty() || !currentAudio.isEmpty()) {
				// Сохраняем текущий блок
				CellData enCell;
				enCell.text = currentEn;
				enCell.isExcluded = currentEnExcl;
				enCells.append(enCell);

				CellData ruCell;
				ruCell.text = currentRu;
				ruCell.isExcluded = currentRuExcl;
				ruCells.append(ruCell);

				CellData audioCell;
				audioCell.text = currentAudio;
				audioCell.isExcluded = currentAudioExcl;
				audioCell.audioStartMs = currentStartMs;
				audioCell.audioEndMs = currentEndMs;
				audioCells.append(audioCell);

				// Сбрасываем для следующего блока
				currentEn.clear();
				currentRu.clear();
				currentAudio.clear();
				currentEnExcl = false;
				currentRuExcl = false;
				currentAudioExcl = false;
				currentStartMs = -1;
				currentEndMs = -1;
			}
			continue;
		}

		// Пропускаем комментарии
		if (line.startsWith("#")) {
			// Парсим пути к файлам из заголовка
			if (line.startsWith("# Source: ")) {
				currentEnFile = line.mid(10).trimmed();
			}
			else if (line.startsWith("# Target: ")) {
				currentRuFile = line.mid(10).trimmed();
			}
			else if (line.startsWith("# AudioText: ")) {
				currentAudioTextFile = line.mid(12).trimmed();
			}
			else if (line.startsWith("# AudioFile: ")) {
				currentAudioFile = line.mid(12).trimmed();
			}
			continue;
		}

		// Парсим ключ: значение
		int colonPos = line.indexOf(':');
		if (colonPos == -1) continue;

		QString key = line.left(colonPos).trimmed();
		QString value = line.mid(colonPos + 1).trimmed();

		if (key == "source") currentEn = value;
		else if (key == "target") currentRu = value;
		else if (key == "audio") currentAudio = value;
		else if (key == "src_excl") currentEnExcl = (value == "true");
		else if (key == "tgt_excl") currentRuExcl = (value == "true");
		else if (key == "audio_excl") currentAudioExcl = (value == "true");
		else if (key == "start_ms") currentStartMs = value.toInt();
		else if (key == "end_ms") currentEndMs = value.toInt();
	}

	// Сохраняем последний блок, если есть
	if (!currentEn.isEmpty() || !currentRu.isEmpty() || !currentAudio.isEmpty()) {
		CellData enCell;
		enCell.text = currentEn;
		enCell.isExcluded = currentEnExcl;
		enCells.append(enCell);

		CellData ruCell;
		ruCell.text = currentRu;
		ruCell.isExcluded = currentRuExcl;
		ruCells.append(ruCell);

		CellData audioCell;
		audioCell.text = currentAudio;
		audioCell.isExcluded = currentAudioExcl;
		audioCell.audioStartMs = currentStartMs;
		audioCell.audioEndMs = currentEndMs;
		audioCells.append(audioCell);
	}

	file.close();
	normalizeRowCount();
	modified = false;
	return true;
}

double Aligner::calculateWordMatchScore(const QStringList& enWords, const QStringList& audioWords)
{
	if (enWords.isEmpty() || audioWords.isEmpty()) return 0.0;

	QSet<QString> enSet;
	for (const QString& w : enWords) {
		enSet.insert(w.toLower());
	}

	int score = 0;
	int total = enWords.size();

	// Проходим по аудио словам, ищем совпадения
	for (const QString& audioWord : audioWords) {
		QString word = audioWord.toLower();
		// Удаляем знаки пунктуации из аудио слова
		word.remove(QRegularExpression("[\\p{P}]"));

		if (enSet.contains(word)) {
			score++;
		}
	}

	// Штраф за слишком длинное аудио
	double lengthPenalty = 1.0;
	if (audioWords.size() > enWords.size() * 1.5) {
		lengthPenalty = 0.7;
	}
	else if (audioWords.size() > enWords.size() * 2) {
		lengthPenalty = 0.5;
	}

	double dscore = (double)score / total;
	return dscore * lengthPenalty;
}

// aligner.cpp
void Aligner::rebuildSourceWordsCache()
{
	m_enWordsCache.clear();

	for (int sentIdx = 0; sentIdx < enCells.size(); ++sentIdx) {
		if (enCells[sentIdx].isExcluded) continue;

		QStringList words = tokenizeWords(enCells[sentIdx].text);

		for (int wordIdx = 0; wordIdx < words.size(); ++wordIdx) {
			SourceWord w;
			w.text = words[wordIdx].toLower();
			w.sentenceIndex = sentIdx;
			w.wordIndex = wordIdx;
			m_enWordsCache.append(w);
		}
	}
}

bool Aligner::checkAudioAlignment()
{
	struct AudioWord {
		QString word;
		int sentIdx;
	};
	QVector<AudioWord> audioCache;
	for (int sentIdx = 0; sentIdx < audioCells.size(); ++sentIdx) {
		QStringList words = tokenizeWords(audioCells[sentIdx].text);
		for (int wordIdx = 0; wordIdx < words.size(); ++wordIdx) {
			AudioWord aw;
			aw.word = words[wordIdx].toLower();
			aw.sentIdx = sentIdx;
			audioCache.append(aw);
		}
	}
	
	bool r = true;
	for (int i = 0, n = qMin(audioCache.size(), audioEntries.size()); i < n; i++)
		if (!equ(audioCache[i].word, audioEntries[i].text)) {
			int si = audioCache[i].sentIdx;
			audioCells[si].isError = true;
			r = false;
		}			
	if (audioCache.size() != audioEntries.size())
		return false;
	return r;
}

void Aligner::rebuildAudioSentences()
{
	// 1. Инициализация: audioCells такого же размера, как enCells
	audioCells.clear();
	audioCells.resize(enCells.size());
	for (int i = 0; i < audioCells.size(); ++i) {
		audioCells[i].isExcluded = true;
		audioCells[i].text = "";
	}

	if (audioEntries.isEmpty()) {
		return;
	}

	// 2. Проход по аудиословам (снизу вверх)
	QStringList currentWords;
	int currentStartMs = -1;
	int currentEndMs = -1;
	int currentSentenceIdx = -1;
	bool currentIsIns = false;

	for (int i = audioEntries.size() - 1; i >= 0; i--) {
		const AudioEntry& entry = audioEntries[i];

		if (currentWords.isEmpty() ||
			entry.sentenceIdx != currentSentenceIdx ||
			entry.ins != currentIsIns) {

			// Сохраняем предыдущее накопленное предложение
			if (!currentWords.isEmpty() && currentSentenceIdx >= 0) {
				if (currentIsIns) {
					// Вставка: вставляем новые ячейки во все три массива
					CellData emptyCell;
					emptyCell.isExcluded = true;

					enCells.insert(currentSentenceIdx, emptyCell);
					ruCells.insert(currentSentenceIdx, emptyCell);
					audioCells.insert(currentSentenceIdx, emptyCell);

					// Теперь нужно заполнить вставленную ячейку audioCells
					// (она только что вставлена, индекс currentSentenceIdx)
					audioCells[currentSentenceIdx].text = currentWords.join(" ");
					audioCells[currentSentenceIdx].audioStartMs = currentStartMs;
					audioCells[currentSentenceIdx].audioEndMs = currentEndMs;
					audioCells[currentSentenceIdx].isExcluded = false;
				}
				else {
					// Обычное предложение: размещаем в существующей ячейке
					// Индекс должен быть в пределах массива
					if (currentSentenceIdx < audioCells.size()) {
						audioCells[currentSentenceIdx].text = currentWords.join(" ");
						audioCells[currentSentenceIdx].audioStartMs = currentStartMs;
						audioCells[currentSentenceIdx].audioEndMs = currentEndMs;
						audioCells[currentSentenceIdx].isExcluded = false;
					}
				}
			}

			// Начинаем новое предложение
			currentWords.clear();
			currentWords.append(entry.text);
			currentStartMs = entry.startMs;
			currentEndMs = entry.endMs;
			currentSentenceIdx = entry.sentenceIdx;
			currentIsIns = entry.ins;
		}
		else {
			currentWords.prepend(entry.text);
			currentStartMs = entry.startMs;
		}
	}

	// Сохраняем последнее предложение
	if (!currentWords.isEmpty()) {
		if (currentIsIns) {
			CellData emptyCell;
			emptyCell.isExcluded = true;

			enCells.insert(currentSentenceIdx, emptyCell);
			ruCells.insert(currentSentenceIdx, emptyCell);
			audioCells.insert(currentSentenceIdx, emptyCell);

			audioCells[currentSentenceIdx].text = currentWords.join(" ");
			audioCells[currentSentenceIdx].audioStartMs = currentStartMs;
			audioCells[currentSentenceIdx].audioEndMs = currentEndMs;
			audioCells[currentSentenceIdx].isExcluded = false;
		}
		else {
			if (currentSentenceIdx < audioCells.size()) {
				audioCells[currentSentenceIdx].text = currentWords.join(" ");
				audioCells[currentSentenceIdx].audioStartMs = currentStartMs;
				audioCells[currentSentenceIdx].audioEndMs = currentEndMs;
				audioCells[currentSentenceIdx].isExcluded = false;
			}
		}
	}
}

void Aligner::assignAudioGroup(int enStart, int audioStart, int nWords)
{
	// Собираем аудио слова по индексам предложений
	QMap<int, QStringList> audioBySentence;
	QMap<int, int> startMsBySentence;
	QMap<int, int> endMsBySentence;

	for (int i = 0; i < nWords; ++i) {
		int sentIdx = m_enWordsCache[enStart + i].sentenceIndex;
		int audioPos = audioStart + i;

		audioBySentence[sentIdx].append(audioEntries[audioPos].text);

		if (!startMsBySentence.contains(sentIdx)) {
			startMsBySentence[sentIdx] = audioEntries[audioPos].startMs;
		}
		endMsBySentence[sentIdx] = audioEntries[audioPos].endMs;
	}

	// Записываем в audioCells
	for (auto it = audioBySentence.begin(); it != audioBySentence.end(); ++it) {
		int sentIdx = it.key();

		// Расширяем audioCells если нужно
		while (audioCells.size() <= sentIdx) {
			audioCells.append(CellData());
		}

		// ОБЪЕДИНЯЕМ с существующим текстом, а не заменяем!
		if (!audioCells[sentIdx].text.isEmpty()) {
			audioCells[sentIdx].text += " ";
		}
		audioCells[sentIdx].text += it.value().join(" ");
		audioCells[sentIdx].isExcluded = false;

		// Обновляем время начала (берём минимальное)
		if (audioCells[sentIdx].audioStartMs == -1 ||
			startMsBySentence[sentIdx] < audioCells[sentIdx].audioStartMs) {
			audioCells[sentIdx].audioStartMs = startMsBySentence[sentIdx];
		}
		// Обновляем время окончания (берём максимальное)
		if (endMsBySentence[sentIdx] > audioCells[sentIdx].audioEndMs) {
			audioCells[sentIdx].audioEndMs = endMsBySentence[sentIdx];
		}
	}
}


void Aligner::applyPendingInserts()
{
	// Идём с конца, чтобы вставки не влияли на индексы предыдущих
	for (int i = m_pendingInserts.size() - 1; i >= 0; --i) {
		const PendingInsert& insert = m_pendingInserts[i];
		int pos = insert.index;

		// Вставляем пустое английское предложение
		CellData emptyEn;
		emptyEn.text = "";
		emptyEn.isExcluded = true;
		if (pos > enCells.size()) 
			enCells.resize(pos);
		enCells.insert(pos, emptyEn);

		// Вставляем пустое русское предложение
		CellData emptyRu;
		emptyRu.text = "";
		emptyRu.isExcluded = true;
		if (pos > ruCells.size())
			ruCells.resize(pos);
		ruCells.insert(pos, emptyRu);

		// вставляем аудио предложение
		CellData emptyAudio = insert.audioCell;
		emptyAudio.isExcluded = true;
		if (pos > audioCells.size())
			audioCells.resize(pos);
		audioCells.insert(pos, emptyAudio);
	}

	m_pendingInserts.clear();
}


void Aligner::alignAudioToSource()
{
	// вызов одного из алгоритмических Aligner'ов

	if (enCells.isEmpty() || audioEntries.isEmpty()) return;

	rebuildSourceWordsCache();
	m_pendingInserts.clear();

	audioCells.clear();
	normalizeRowCount();
	
	MyAligner aligner;
	aligner.align(this);	

//	AdvancedAligner aligner;
//	aligner.setMatchScore(2);
//	aligner.setMismatchScore(-1);
//	aligner.setGapScore(-1);
//	aligner.setRareWordThreshold(2);
//	aligner.setMaxContextSize(3);

//	StreamingAligner aligner;
//	aligner.setMaxLookahead(10);	// ищем совпадения в пределах 10 слов
//	aligner.setMinMatchGroup(1);    // даже одиночные совпадения считаем группой
//	aligner.align(this);

	// Синхронизация: вставляем пустые en/ru
	normalizeRowCount();
	applyPendingInserts(); // to remove
	rebuildAudioSentences();

	modified = true;
}

QString Aligner::sourceWordsBySentence(int sentIdx)
{
	// отладочная - список слов этого предложения
	QString s;
	for (int i = 0; i < m_enWordsCache.size(); i++) {
		if (m_enWordsCache[i].sentenceIndex == sentIdx) {
			s += m_enWordsCache[i].text;
			s += "\r\n";
		}
	}
	return s;
}


