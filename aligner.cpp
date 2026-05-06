#include "aligner.h"
#include <QFile>
#include <QTextStream>
#include <QTextBoundaryFinder>
#include <QDebug>
#include <QDir>
#include <QProcess>
#include <QRegularExpression>
#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

#include "tools.h"
#include "MyAligner.h"

void Aligner::clearSource()
{
	for (auto &cell : sourceCells)
		cell.clear();
	modified = true;
}

void Aligner::clearTarget()
{
	for (auto &cell : targetCells)
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
	return sourceWordsCache.size();
}

int Aligner::getAudioWordsCount() const
{
	return audioEntries.size();
}

const QString& Aligner::getSourceWord(int index) const
{
	static const QString EMPTY;
	if (index < 0 || index >= sourceWordsCache.size()) {
		qWarning() << "getSourceWord: invalid index" << index << "size:" << sourceWordsCache.size();
		assert(false);
		return EMPTY;
	}
	return sourceWordsCache[index].text;
}

int Aligner::getSourceSentence(int index) const
{
	if (index < 0 || index >= sourceWordsCache.size()) {
		return -1;
	}
	return sourceWordsCache[index].sentenceIndex;
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
	// индексы следующих за конечными "[start, end)", в стиле итераторов С++
	int sourceEnd = qMin(sourceStart + sourceCount, sourceWordsCache.size());
	int audioEnd = qMin(audioStart + audioCount, audioEntries.size());
	

	int i = sourceStart;
	int j = audioStart;
	int currentSentence = -1;
	
	// ОСНОВНОЙ ЦИКЛ - пока просто сопоставляем попарно, игнорируя возможные различия
	// в будущем переделать, используя более умное сопоставление
	while (i < sourceEnd && j < audioEnd) {
		currentSentence = getSourceSentence(i);

		// Нормальное сопоставление (не вставка)
		setAudioSentence(j, currentSentence, false);

		i++;
		j++;
	}

	// Оставшиеся аудио слова (если audioCount > sourceCount) добавляем к последнему предложению
	while (j < audioEnd) {
		setAudioSentence(j, currentSentence, false);
		j++;
	}

	// Примечание: если sourceCount > audioCount (удаления),
	// они просто игнорируются — для них не вызывается setAudioSentence
}

void Aligner::flushPendingGroup(int sourceIndex, int audioStart, int audioCount)
{
	// Определяем, к какому предложению привязать вставку
	int targetSentence = -1;

	if (sourceIndex < sourceWordsCache.size()) {
		// Если есть привязка к исходному тексту, берём предложение этого слова
		targetSentence = sourceWordsCache[sourceIndex].sentenceIndex;
	}
	else {
		// Если sourceIndex за пределами исходного текста, вставляем в конец
		// Здесь нужно определить максимальный индекс предложения
		// Вариант: взять последнее существующее предложение или создать новое
		targetSentence = sourceCells.size() - 1; // или audioCells.size() - 1
	}

	// Проходим по всем аудиословам в группе
	for (int i = 0; i < audioCount; ++i) {
		int audioWordIndex = audioStart + i;

		// Устанавливаем слово как вставку (ins=true) с указанным предложением
		setAudioSentence(audioWordIndex, targetSentence, true);
	}
}

Aligner::Aligner()
	: modified(false)
{
	
	loadDictionary("my_initial_dict.dic");
}

int Aligner::rowCount() const
{
	// Максимальное количество ячеек среди всех столбцов
	return qMax(sourceCells.size(), qMax(targetCells.size(), audioCells.size()));
}

void Aligner::normalizeRowCount()
{
	int targetRows = rowCount();

	// Добиваем каждый столбец до targetRows пустыми ячейками
	while (sourceCells.size() < targetRows) {
		sourceCells.append(TextSentence());
	}
	while (targetCells.size() < targetRows) {
		targetCells.append(TextSentence());
	}
	while (audioCells.size() < targetRows) {
		audioCells.append(AudioSentence());
	}

	// 2. Удаляем пустые строки с конца
	for (int i = sourceCells.size() - 1; i >= 0; --i) {
		bool enEmpty = sourceCells[i].text.isEmpty();
		bool ruEmpty = targetCells[i].text.isEmpty();
		bool audioEmpty = audioCells[i].text.isEmpty();

		if (enEmpty && ruEmpty && audioEmpty) {
			sourceCells.removeAt(i);
			targetCells.removeAt(i);
			audioCells.removeAt(i);
		}
	}
}

void Aligner::splitCell(int row, int cursorPos, int column)
{
	if (column < 0 || column >= 2) return;
	QVector<TextSentence>* cells = column==1 ? &targetCells : &sourceCells;
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
	TextSentence newCell;
	newCell.text = secondPart;
	newCell.isExcluded = false;
	
	cells->insert(row + 1, newCell);

	// Нормализуем количество строк
	normalizeRowCount();
	modified = true;
}

void Aligner::mergeCells(int row1, int row2, int column)
{
	if (row1 == row2) return;
	if (row1 > row2) std::swap(row1, row2);
	if (column < 0 || column >= 2) return;
	QVector<TextSentence>* cells = column == 1 ? &targetCells : &sourceCells;


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
	if (column < 0 || column >= 2) return;
	QVector<TextSentence>* cells = column == 1 ? &targetCells : &sourceCells;

	if (row < cells->size()) {
		(*cells)[row].isExcluded = !(*cells)[row].isExcluded;
		modified = true;
	}
	normalizeRowCount();
}

void Aligner::setCellText(int row, int column, const QString& text)
{
	if (column < 0 || column >= 2) return;
	QVector<TextSentence>* cells = column == 1 ? &targetCells : &sourceCells;

	// Расширяем массив если нужно
	while (cells->size() <= row) {
		cells->append(TextSentence());
	}

	(*cells)[row].text = text;
	normalizeRowCount();
	modified = true;
}


void Aligner::clear()
{
	projectPath.clear();
	sourceCells.clear();
	targetCells.clear();
	audioCells.clear();
	audioEntries.clear();
	currentSourceFile.clear();
	currentTargetFile.clear();
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

	sourceCells.clear();
	for (const QString& sent : sentences) {
		TextSentence cell;
		cell.text = sent;
		cell.isExcluded = false;
		sourceCells.append(cell);
	}

	currentSourceFile = filename;
	normalizeRowCount();
	modified = true;
}

void Aligner::loadTargetText(const QString& filename)
{
	currentTargetFile = filename;
	modified = true;
}

void Aligner::loadAudioEntries(const QString& filename)
{
	currentAudioTextFile = filename;
	//audioEntries = entries;
	audioEntries.clear();

	QFile file(filename);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		qWarning() << "Cannot open JSON file:" << filename;
		return;
	}

	QByteArray jsonData = file.readAll();
	file.close();

	QJsonDocument doc = QJsonDocument::fromJson(jsonData);
	if (doc.isNull()) {
		qWarning() << "Invalid JSON:" << filename;
		return;
	}

	if (!doc.isObject()) {
		qWarning() << "JSON root is not an object";
		return;
	}

	QJsonObject root = doc.object();

	// Ищем массив segments
	if (!root.contains("segments")) {
		qWarning() << "JSON does not contain 'segments' array";
		return;
	}

	QJsonArray segments = root["segments"].toArray();

	// Проходим по всем сегментам
	for (const QJsonValue& segVal : segments) {
		if (!segVal.isObject()) continue;

		QJsonObject segment = segVal.toObject();

		// Проверяем наличие массива words
		if (!segment.contains("words")) continue;

		QJsonArray words = segment["words"].toArray();

		// Проходим по всем словам в сегменте
		for (const QJsonValue& wordVal : words) {
			if (!wordVal.isObject()) continue;

			QJsonObject wordObj = wordVal.toObject();

			AudioEntry entry;

			// Извлекаем слово
			entry.text = wordObj["word"].toString().trimmed();
			if (entry.text.isEmpty()) continue;

			//entry.text.remove(QRegularExpression("[\\p{P}]"));  // удаляем всю пунктуацию
			entry.text.remove(QRegularExpression("[^\\w\\s']"));
			entry.text = entry.text.toLower();

			// Извлекаем время (Whisper даёт в секундах с плавающей точкой)
			double startSec = wordObj["start"].toDouble(-1.0);
			double endSec = wordObj["end"].toDouble(-1.0);

			if (startSec >= 0 && endSec >= 0) {
				entry.startMs = qRound(startSec * 1000.0);
				entry.endMs = qRound(endSec * 1000.0);
			}
			else {
				entry.startMs = -1;
				entry.endMs = -1;
			}

			// probability пока игнорируем

			audioEntries.append(entry);
		}
	}

	qDebug() << "Parsed" << audioEntries.size() << "words from Whisper JSON";
		
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

	if (sourceCells.isEmpty() || currentTargetFile.isEmpty()) return;

	// загружаем текстовый файл второго языка
	QFile file(currentTargetFile);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		qWarning() << "Cannot open Russian file:" << currentTargetFile;
		return;
	}

	QTextStream stream(&file);
	stream.setCodec("UTF-8");
	QString ruContent = stream.readAll();
	file.close();

	QVector<QString> ruSentences = splitIntoSentences(ruContent);

	targetCells.clear();
	for (const QString& sent : ruSentences) {
		TextSentence cell;
		cell.text = sent;
		cell.isExcluded = false;
		targetCells.append(cell);
	}

	normalizeRowCount();
	modified = true;

	calcLexicalSimilarity();
}

void Aligner::calcLexicalSimilarity()
{
	// 5. ЭКСПЕРИМЕНТ: считаем метрику для каждой пары
	// переделать: обесепчиваем чтобы аудио столбец был и заносим туда в служебные поля
/*
	audioCells.clear();

	for (int i = 0; i < sourceCells.size(); ++i) {
		QString enText = sourceCells[i].text;
		QString ruText = (i < targetCells.size()) ? targetCells[i].text : "";

		double similarity = 0.0;

		if (!enText.isEmpty() && !ruText.isEmpty() && !m_dictionary.isEmpty()) {
			similarity = lexicalSimilarity(enText, ruText);
		}

		// Сохраняем результат в аудио столбец
		TextSentence scoreCell;
		if (m_dictionary.isEmpty()) {
			scoreCell.text = "no dict";
		}
		else if (ruText.isEmpty()) {
			scoreCell.text = "no ru";
		}
		else {
			scoreCell.text = QString("sim: %1%").arg(qRound(similarity * 100));
		}
		scoreCell.isExcluded = false;
		audioCells.append(scoreCell);
	}
	*/
}

bool Aligner::splitAudioToMp3(const QString &ffmpegPath)
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
		const AudioSentence& cell = audioCells[i];

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
		ffmpeg.start(ffmpegPath, ffmpegArgs);

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

	if (!currentSourceFile.isEmpty())
		stream << "# Source: " << currentSourceFile << "\n";
	if (!currentTargetFile.isEmpty())
		stream << "# Target: " << currentTargetFile << "\n";
	if (!currentAudioTextFile.isEmpty())
		stream << "# AudioText: " << currentAudioTextFile << "\n";
	if (!currentAudioFile.isEmpty())
		stream << "# AudioFile: " << currentAudioFile << "\n";

	stream << "\n";

	// Данные
	int rows = rowCount();
	for (int i = 0; i < rows; ++i) {
		QString en = (i < sourceCells.size()) ? sourceCells[i].text : "";
		QString ru = (i < targetCells.size()) ? targetCells[i].text : "";
		QString audio = (i < audioCells.size()) ? audioCells[i].text : "";
		bool enExcl = (i < sourceCells.size()) && sourceCells[i].isExcluded;
		bool ruExcl = (i < targetCells.size()) && targetCells[i].isExcluded;
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
	sourceCells.clear();
	targetCells.clear();
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
				TextSentence enCell;
				enCell.text = currentEn;
				enCell.isExcluded = currentEnExcl;
				sourceCells.append(enCell);

				TextSentence ruCell;
				ruCell.text = currentRu;
				ruCell.isExcluded = currentRuExcl;
				targetCells.append(ruCell);

				AudioSentence audioCell;
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
				currentSourceFile = line.mid(10).trimmed();
			}
			else if (line.startsWith("# Target: ")) {
				currentTargetFile = line.mid(10).trimmed();
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
		TextSentence enCell;
		enCell.text = currentEn;
		enCell.isExcluded = currentEnExcl;
		sourceCells.append(enCell);

		TextSentence ruCell;
		ruCell.text = currentRu;
		ruCell.isExcluded = currentRuExcl;
		targetCells.append(ruCell);

		AudioSentence audioCell;
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

void Aligner::rebuildSourceWordsCache()
{
	sourceWordsCache.clear();

	for (int sentIdx = 0; sentIdx < sourceCells.size(); ++sentIdx) {
		if (sourceCells[sentIdx].isExcluded) continue;

		QStringList words = tokenizeWords(sourceCells[sentIdx].text);

		for (int wordIdx = 0; wordIdx < words.size(); ++wordIdx) {
			SourceWord w;
			w.text = words[wordIdx].toLower();
			w.sentenceIndex = sentIdx;
			w.wordIndex = wordIdx;
			sourceWordsCache.append(w);
		}
	}
}

void Aligner::insertSentence(const QStringList &currentWords, int currentSentenceIdx, bool currentIsIns, int currentStartMs, int currentEndMs)
{
	if (currentIsIns) {
		TextSentence emptyTextCell;
		emptyTextCell.isExcluded = true;

		sourceCells.insert(currentSentenceIdx, emptyTextCell);
		targetCells.insert(currentSentenceIdx, emptyTextCell);

		AudioSentence emptyAudioCell;
		emptyAudioCell.isExcluded = true;
		audioCells.insert(currentSentenceIdx, emptyAudioCell);

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

	// оцениваем похожесть
	audioCells[currentSentenceIdx].similarity = evaluateSentenceSimilaritySimple(audioCells[currentSentenceIdx].text, sourceCells[currentSentenceIdx].text);
}

void Aligner::rebuildAudioSentences()
{
	// 1. Инициализация: audioCells такого же размера, как sourceCells
	audioCells.clear();
	audioCells.resize(sourceCells.size());
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
				insertSentence(currentWords, currentSentenceIdx, currentIsIns, currentStartMs, currentEndMs);				
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
		insertSentence(currentWords, currentSentenceIdx, currentIsIns, currentStartMs, currentEndMs);
	}

	// сумма всех пложительных коэффициентов
	totalSim = 0;
	int n = 0;
	for (const AudioSentence& a : audioCells) {
		if (a.similarity >= 0) {
			totalSim += a.similarity;
			n++;
		}
	}
	if (n > 0)
		totalSim /= n;
}




void Aligner::alignAudioToSource()
{
	// вызов одного из алгоритмических Aligner'ов

	if (sourceCells.isEmpty() || audioEntries.isEmpty()) return;

	rebuildSourceWordsCache();
	
	audioCells.clear();
	normalizeRowCount();
	
	MyAligner aligner;
	aligner.align(this);	

	// Синхронизация: вставляем пустые source/target
	normalizeRowCount();
	rebuildAudioSentences();

	modified = true;
}

QString Aligner::sourceWordsBySentence(int sentIdx)
{
	// отладочная - список слов этого предложения
	QString s;
	for (int i = 0; i < sourceWordsCache.size(); i++) {
		if (sourceWordsCache[i].sentenceIndex == sentIdx) {
			s += sourceWordsCache[i].text;
			s += "\r\n";
		}
	}
	return s;
}


