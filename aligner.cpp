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
#include "FuzzyLemmatizer.h"

void Aligner::clearSource()
{
	for (auto &cell : sourceCells)
		cell.clear();
	modified = true;
}

void Aligner::clearTranslated()
{
	for (auto &cell : translatedCells)
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

void Aligner::assignMatchedGroup(int sourceStart, int sourceCount, int audioStart, int audioCount, QVector<PathStep> &path)
{
	// Переменные для группировки по предложениям
	int currentSentence = -1;
	QVector<int> pendingAudioIndices;  // накапливаем аудиоиндексы для текущего предложения

	 // Проходим по пути выравнивания
	for (const auto& step : path) {
		if (step.audioIdx == -1) {
			// Удаление (слово есть в исходном, нет в аудио) — игнорируем
			continue;
		}

		// Определяем предложение для этого аудиослова
		int sentenceForWord;
		if (step.sourceIdx == -1) {
			// Вставка — используем текущее предложение
			sentenceForWord = currentSentence;
		}
		else {
			sentenceForWord = getSourceSentence(step.sourceIdx);
		}

		// Если предложение изменилось, обрабатываем накопленные аудиослова
		if (sentenceForWord != currentSentence && currentSentence != -1) {
			// Все накопленные аудиослова относятся к предыдущему предложению
			for (int audIdx : pendingAudioIndices) {
				setAudioSentence(audIdx, currentSentence, false);
			}
			pendingAudioIndices.clear();
		}

		// Обновляем текущее предложение
		if (sentenceForWord != -1) {
			currentSentence = sentenceForWord;
		}

		// Добавляем аудиослово в накопление
		pendingAudioIndices.append(step.audioIdx);
	}

	// Обрабатываем последнее накопленное предложение
	if (!pendingAudioIndices.isEmpty() && currentSentence != -1) {
		for (int audIdx : pendingAudioIndices) {
			// Проверяем, было ли это слово вставкой
			// Для этого нужно знать, был ли step.sourceIdx == -1
			// Упрощённо: считаем все слова в конце не вставками
			setAudioSentence(audIdx, currentSentence, false);
		}
	}

	// Обработка оставшихся аудиослов (если path их не покрыл)
	// Это может случиться, если audioCount > result.usedAudio
	int lastProcessedAudio = path.isEmpty() ? audioStart :
		path.last().audioIdx;

	if (lastProcessedAudio >= 0 && lastProcessedAudio + 1 < audioStart + audioCount) {
		// Оставшиеся аудиослова прикрепляем к последнему предложению
		for (int j = lastProcessedAudio + 1; j < audioStart + audioCount; ++j) {
			setAudioSentence(j, currentSentence, false);
		}
	}
}

void Aligner::assignMatchedGroupNaive(int sourceStart, int sourceCount, int audioStart, int audioCount)
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
}

int Aligner::rowCount() const
{
	// Максимальное количество ячеек среди всех столбцов
	return qMax(sourceCells.size(), qMax(translatedCells.size(), audioCells.size()));
}

void Aligner::normalizeRowCount()
{
	int targetRows = rowCount();

	// Добиваем каждый столбец до targetRows пустыми ячейками
	while (sourceCells.size() < targetRows) {
		sourceCells.append(TextSentence());
	}
	while (translatedCells.size() < targetRows) {
		translatedCells.append(TextSentence());
	}
	while (audioCells.size() < targetRows) {
		audioCells.append(AudioSentence());
	}

	// 2. Удаляем пустые строки, полный проход с конца к началу
	for (int i = sourceCells.size() - 1; i >= 0; --i) {
		bool enEmpty = sourceCells[i].text.isEmpty();
		bool ruEmpty = translatedCells[i].text.isEmpty();
		bool audioEmpty = audioCells[i].text.isEmpty();

		if (enEmpty && ruEmpty && audioEmpty) {
			// удаляем строку
			sourceCells.removeAt(i);
			translatedCells.removeAt(i);
			audioCells.removeAt(i);
			// уменьшаем все индексы большие i и обнуляем равные i в audioEntires
			for (auto &e : audioEntries)
				if (e.sentenceIdx == i)
					e.sentenceIdx = -1;
				else if (e.sentenceIdx > i)
					e.sentenceIdx--;
		}
	}

	// 3. Дополнительная валидация для аудио индексов 
	// по идее никакого влияния не оказывает
	for (int i = 0; i < audioCells.size(); ++i) {
		AudioSentence& sent = audioCells[i];
		if (!sent.isExcluded && !sent.text.isEmpty()) {
			// Проверяем, что индексы соответствуют тексту
			if (sent.firstWordIndex >= 0 && sent.lastWordIndex >= 0) {
				if (sent.firstWordIndex > sent.lastWordIndex ||
					sent.lastWordIndex >= audioEntries.size()) {
					// Некорректные индексы - сбрасываем
					sent.firstWordIndex = -1;
					sent.lastWordIndex = -1;
				}
			}
		}
	}
}

void Aligner::splitCell(int row, int cursorPos, int column)
{
	if (column < 0 || column >= 2) return;
	QVector<TextSentence>* cells = column==1 ? &translatedCells : &sourceCells;
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

	// Для текстовых столбцов (0 - source, 1 - translated)
	if (column >= 0 && column <= 1) {
		QVector<TextSentence>* cells = (column == 1) ? &translatedCells : &sourceCells;

		// Убеждаемся, что индексы валидны
		if (row2 >= cells->size()) {
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

		normalizeRowCount();
		modified = true;
		return;
	}

	// Для аудио столбца (column == 2)
	if (column == 2) {
		// Убеждаемся, что индексы валидны
		if (row2 >= audioCells.size()) {
			return;
		}

		AudioSentence& cell1 = audioCells[row1];
		AudioSentence& cell2 = audioCells[row2];

		// Объединяем текст
		QString mergedText = cell1.text;
		if (!mergedText.isEmpty() && !cell2.text.isEmpty()) {
			mergedText += " ";
		}
		mergedText += cell2.text;

		// Объединяем времена
		int mergedStartMs = cell1.audioStartMs;
		int mergedEndMs = cell2.audioEndMs;

		// Если в первой ячейке нет времени, используем время второй
		if (mergedStartMs < 0 && cell2.audioStartMs >= 0) {
			mergedStartMs = cell2.audioStartMs;
		}

		// Если во второй ячейке нет времени, используем время первой
		if (mergedEndMs < 0 && cell1.audioEndMs >= 0) {
			mergedEndMs = cell1.audioEndMs;
		}

		// Объединяем индексы слов в audioEntries
		int mergedFirstIdx = cell1.firstWordIndex;
		int mergedLastIdx = cell2.lastWordIndex;

		// Корректируем индексы, если одна из ячеек пустая
		if (mergedFirstIdx < 0 && cell2.firstWordIndex >= 0) {
			mergedFirstIdx = cell2.firstWordIndex;
		}
		if (mergedLastIdx < 0 && cell1.lastWordIndex >= 0) {
			mergedLastIdx = cell1.lastWordIndex;
		}

		// Обновляем audioEntries: все слова из второй ячейки теперь принадлежат первой
		if (mergedFirstIdx >= 0 && mergedLastIdx >= 0 &&
			mergedFirstIdx <= mergedLastIdx &&
			mergedLastIdx < audioEntries.size()) {

			// Обновляем sentenceIdx для всех слов из второй ячейки
			for (int i = cell2.firstWordIndex; i <= cell2.lastWordIndex; ++i) {
				if (i >= 0 && i < audioEntries.size()) {
					audioEntries[i].sentenceIdx = row1;
					audioEntries[i].ins = false;  // При слиянии вставки становятся обычными словами
				}
			}
		}

		// Обновляем первую ячейку
		cell1.text = mergedText;
		cell1.audioStartMs = mergedStartMs;
		cell1.audioEndMs = mergedEndMs;
		cell1.firstWordIndex = mergedFirstIdx;
		cell1.lastWordIndex = mergedLastIdx;

		// Объединяем флаги excluded: ячейка исключена, только если обе исключены
		cell1.isExcluded = cell1.isExcluded && cell2.isExcluded;

		// Удаляем вторую ячейку
		audioCells.removeAt(row2);

		// Обновляем индексы предложений для всех последующих аудио ячеек
		for (int i = row1 + 1; i < audioCells.size(); ++i) {
			AudioSentence& sent = audioCells[i];
			if (sent.firstWordIndex >= 0 && sent.lastWordIndex >= 0) {
				// Обновляем sentenceIdx в audioEntries для слов этой ячейки
				for (int j = sent.firstWordIndex; j <= sent.lastWordIndex; ++j) {
					if (j >= 0 && j < audioEntries.size()) {
						audioEntries[j].sentenceIdx = i;
					}
				}
			}
		}

		normalizeRowCount();
		modified = true;

		// Пересчитываем похожесть для объединённой ячейки
		if (row1 < sourceCells.size()) {
			audioCells[row1].audioSim = evaluateSentenceSimilaritySimple(
				audioCells[row1].text,
				sourceCells[row1].text
			);
		}

		return;
	}
}

void Aligner::mergeCells2(int row1, int row2, int column)
{
	if (row1 == row2) return;
	if (row1 > row2) std::swap(row1, row2);
	if (column < 0 || column >= 2) return;
	QVector<TextSentence>* cells = column == 1 ? &translatedCells : &sourceCells;


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

bool Aligner::isHighlightedRow(int row)
{
	if (row >= 0 && row < audioCells.size())
		return audioCells[row].isHighlighted;
	return false;
}

void Aligner::highlightCell(int row, bool highlight)
{
	if (row >= 0 && row < audioCells.size())
		audioCells[row].isHighlighted = highlight;
}

void Aligner::excludeCell(int row, int column)
{
	if (column < 0 || column >= 2) return;
	QVector<TextSentence>* cells = column == 1 ? &translatedCells : &sourceCells;

	if (row < cells->size()) {
		(*cells)[row].isExcluded = !(*cells)[row].isExcluded;
		modified = true;
	}
	normalizeRowCount();
}

void Aligner::setCellText(int row, int column, const QString& text)
{
	if (column < 0 || column >= 2) return;
	QVector<TextSentence>* cells = column == 1 ? &translatedCells : &sourceCells;

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
	sourceCells.clear();
	translatedCells.clear();
	audioCells.clear();
	audioEntries.clear();
	sourceWordsCache.clear();
	currentSourceFile.clear();
	currentTranslatedFile.clear();
	currentAudioTextFile.clear();
	currentAudioFile.clear();
	totalAudioSim = 0;
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

void Aligner::loadTranslatedText(const QString& filename)
{
	currentTranslatedFile = filename;
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


void Aligner::alignTranslatedToSource()
{
	// разбить файл второго языка по предложениям 
	// (а в перспективе сопоставить его с текстовым файлом первого языка по словарю)

	if (sourceCells.isEmpty() || currentTranslatedFile.isEmpty()) return;

	// загружаем текстовый файл второго языка
	QFile file(currentTranslatedFile);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		qWarning() << "Cannot open Russian file:" << currentTranslatedFile;
		return;
	}

	QTextStream stream(&file);
	stream.setCodec("UTF-8");
	QString ruContent = stream.readAll();
	file.close();

	QVector<QString> ruSentences = splitIntoSentences(ruContent);

	translatedCells.clear();
	for (const QString& sent : ruSentences) {
		TextSentence cell;
		cell.text = sent;
		cell.isExcluded = false;
		translatedCells.append(cell);
	}

	normalizeRowCount();
	modified = true;

	calcTranslatedSimilarity();
}

void Aligner::calcTranslatedSimilarity()
{
	// статистика: оценка выравнивания переведеного текста
	// ЭКСПЕРИМЕНТ: считаем метрику для каждой пары
	int n = rowCount();
	for (int i = 0; i < n; ++i) {
		QString srcText = sourceCells[i].text;
		QString transText = translatedCells[i].text;

		double similarity = 0.0;

		if (!srcText.isEmpty() && !transText.isEmpty() && !m_dictionary.isEmpty()) {
			similarity = lexicalSimilarity(srcText, transText);
		}

		audioCells[i].transSim = similarity;
	}
}

void Aligner::calcAudioSimilarity()
{
	// статистика: оценка выравнивания аудиотекста
	// коэффциенты похожести для предложений
	int n = rowCount();
	for (int i = 0; i < n; i++) {
		audioCells[i].audioSim = evaluateSentenceSimilaritySimple(audioCells[i].text, sourceCells[i].text);
	}

	// сумма всех положительных коэффициентов
	totalAudioSim = 0;
	n = 0;
	for (const AudioSentence& a : audioCells) {
		if (a.audioSim >= 0) {
			totalAudioSim += a.audioSim;
			n++;
		}
	}
	if (n > 0)
		totalAudioSim /= n;
}

bool Aligner::prepareFilePath(bool gen, int i, QString &outputFilePath)
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

	// Формируем имя выходного файла
	QString outputFileName = QString(gen ? "%1_%2t.wav" : "%1_%2.mp3")
		.arg(QFileInfo(currentAudioFile).baseName())
		.arg(i, 4, 10, QChar('0')); // нумерация с 0001

	outputFilePath = QDir(outputDirectory)
		.filePath(outputFileName);

	return true;
}

bool Aligner::splitAudioSentence(int i)
{
	QString outputFilePath;
	if (!prepareFilePath(false, i, outputFilePath))
		return false;

	if (i < 0 || i >= audioCells.size())
		return false;
	const AudioSentence& cell = audioCells[i];

	// Пропускаем исключенные или пустые ячейки
	if (cell.isExcluded) {
		qDebug() << "Skipping excluded cell" << i;
		return true;
	}

	// Проверяем валидность таймкодов
	if (cell.audioStartMs < 0 || cell.audioEndMs < 0 ||
		cell.audioEndMs <= cell.audioStartMs) {
		qDebug() << "Invalid timestamps for cell" << i
			<< "start:" << cell.audioStartMs
			<< "end:" << cell.audioEndMs;
		return false;
	}	

	// Конвертируем миллисекунды в формат времени для FFmpeg (HH:MM:SS.xxx)
	QString startTime = msToTimeFormat(cell.audioStartMs + 200);	// делаем небольшой запас т.к. иначе обрезка по самому концу слова
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
	ffmpeg.start(cfg.ffmpegPath, ffmpegArgs);

	if (!ffmpeg.waitForStarted(3000)) {
		qDebug() << "Failed to start ffmpeg for cell" << i;
		return false;
	}

	if (!ffmpeg.waitForFinished(60000)) { // таймаут 60 секунд
		qDebug() << "FFmpeg timeout for cell" << i;
		ffmpeg.kill();
		return false;
	}

	// Проверяем результат
	if (ffmpeg.exitCode() == 0 && QFile::exists(outputFilePath)) {
		qDebug() << "Successfully created:" << outputFilePath;
		
		// Можно добавить теги ID3
	//@	addMetadataToMp3(outputFilePath, cell.text, i);
	}
	else {
		qDebug() << "Failed to create:" << outputFilePath;
		qDebug() << "FFmpeg error:" << ffmpeg.readAllStandardError();
	}

	return true;
}

bool Aligner::generateAudioSentence(int i)
{
	QString outputFilePath;
	if (!prepareFilePath(true, i, outputFilePath))
		return false;

	if (i < 0 || i >= translatedCells.size())
		return false;
	const TextSentence& cell = translatedCells[i];

	// Пропускаем исключенные или пустые ячейки
	if (cell.isExcluded) {
		qDebug() << "Skipping excluded cell" << i;
		return true;
	}
	

	// Формируем команду balcon
	QStringList balconArgs;
	// Выбираем текст (можно передать и из файла, и как строку)
	balconArgs << "-t" << cell.text;
	// Задаем выходной файл
	balconArgs << "-w" << outputFilePath;
	// (Необязательно) Выбираем голос
	balconArgs << "-n" << "Irina";
	// (Необязательно) Настраиваем качество
	balconArgs << "-fr" << "44";

	QProcess balcon;
	balcon.start(cfg.balconPath, balconArgs);

	if (!balcon.waitForStarted(3000)) {
		qDebug() << "Failed to start balcon for cell" << i;
		return false;
	}

	if (!balcon.waitForFinished(60000)) { // таймаут 60 секунд
		qDebug() << "balcon timeout for cell" << i;
		balcon.kill();
		return false;
	}

	// Проверяем результат
	if (balcon.exitCode() != 0 || !QFile::exists(outputFilePath)) {
		qDebug() << "Failed to create:" << outputFilePath;
		qDebug() << "FFmpeg error:" << balcon.readAllStandardError();
		return false;
	}

	qDebug() << "Successfully created:" << outputFilePath;
	return false;
}

bool Aligner::splitAudio()
{
	int successCount = 0;

	// Перебираем все ячейки
	for (int i = 0; i < audioCells.size(); ++i) {
		if (splitAudioSentence(i))
			successCount++;
	}

	qDebug() << "Split completed. Success:" << successCount;
	return  true;
}

bool Aligner::generateAudio()
{
	int successCount = 0;

	// Перебираем все ячейки
	for (int i = 0; i < audioCells.size(); ++i) {
		if (generateAudioSentence(i))
			successCount++;
	}

	qDebug() << "Generation completed. Success:" << successCount;
	return  true;
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

		// Формат: исходное = переведенное
		QStringList parts = line.split('=');
		if (parts.size() >= 2) {
			QString source = parts[0].trimmed();
			QString trans = parts[1].trimmed();

			if (!source.isEmpty() && !trans.isEmpty()) {
				m_dictionary[source] = trans;
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
		
	// лемматизация
	QStringList rwWords;
	for (const QString &rw : ruWords) {
		rwWords.append(FuzzyLemmatizer::lemmatize(rw));
	}


	// для каждого английского и английского_токенизированного слова
	// переводим его в список русских слов и определяем есть ли такие русские слова в ruWords

	// число совпадений
	int count = 0;
	
	qDebug() << "======= RECALC STATS FOR: " << enSentence;
	// у нас есть множество английских слов; цикл по ним
	for (QString enWord : enWords) {

		qDebug() << enWord;
		if (enWord == "inch")
			count = count;

		// если слово напрямую есть среди русских - ок, это какое-то имя собственное
		if (ruWords.indexOf(enWord) >= 0) {
			count++;
			continue;
		}

		// если перевод слова есть в словаре
		if (m_dictionary.contains(enWord)) {
			QString trs = m_dictionary[enWord];
			qDebug() << trs;
			// выбираем список слов, являющихся переводом данного
			QStringList trWords = tokenizeWords(trs);
			if (intersect(ruWords, trWords)) {
				count++;
				qDebug() << "@ok2";
				continue;
			}

			// если лемматизированный перевод присутствует в русском предложении
			if (intersect(rwWords, trWords)) {
				count++;
				qDebug() << "@ok3";
				continue;
			}
		}
		// если перевод лемматизированного слова есть в словаре
		enWord = stemEnglish(enWord);
		if (m_dictionary.contains(enWord)) {
			// выбираем список слов, являющихся переводом данного
			QStringList trWords = tokenizeWords(m_dictionary[enWord]);
			if (intersect(ruWords, trWords)) {
				count++;
				qDebug() << "@ok4";
				continue;
			}

			// если лемматизированный перевод присутствует в русском предложении
			if (intersect(rwWords, trWords)) {
				count++;
				qDebug() << "@ok5";
				continue;
			}
		}
	}
	   	
	// Нормализуем по длине английского предложения
	double dscore = (double)count / enWords.size();

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
	if (!currentTranslatedFile.isEmpty())
		stream << "# Translated: " << currentTranslatedFile << "\n";
	if (!currentAudioTextFile.isEmpty())
		stream << "# AudioText: " << currentAudioTextFile << "\n";
	if (!currentAudioFile.isEmpty())
		stream << "# AudioFile: " << currentAudioFile << "\n";
	stream << "\n";

	// Сохраняем аудио энтрисы, если они есть
	if (!audioEntries.isEmpty()) {
		stream << "# Audio entries count: " << audioEntries.size() << "\n";
		for (int i = 0; i < audioEntries.size(); ++i) {
			const AudioEntry& entry = audioEntries[i];
			stream << "aentry: "
				<< entry.text << " | "
				<< entry.startMs << " | "
				<< entry.endMs << " | "
				<< entry.sentenceIdx << " | "
				<< (entry.ins ? "1" : "0") << "\n";
		}
		stream << "\n";
	}

	// Данные
	int rows = rowCount();
	for (int i = 0; i < rows; ++i) {
		QString src = (i < sourceCells.size()) ? sourceCells[i].text : "";
		QString trans = (i < translatedCells.size()) ? translatedCells[i].text : "";
		QString audio = (i < audioCells.size()) ? audioCells[i].text : "";
		bool srcExcl = (i < sourceCells.size()) && sourceCells[i].isExcluded;
		bool transExcl = (i < translatedCells.size()) && translatedCells[i].isExcluded;
		bool audioExcl = (i < audioCells.size()) && audioCells[i].isExcluded;
		int startMs = (i < audioCells.size()) ? audioCells[i].audioStartMs : -1;
		int endMs = (i < audioCells.size()) ? audioCells[i].audioEndMs : -1;
		int firstIdx = (i < audioCells.size()) ? audioCells[i].firstWordIndex : -1;
		int lastIdx = (i < audioCells.size()) ? audioCells[i].lastWordIndex : -1;
		bool isHighlighted = (i < audioCells.size()) ? audioCells[i].isHighlighted : false;

		// Экранирование спецсимволов? Можно не делать, если доверяем данным
		stream << "source: " << src << "\n";
		stream << "trans: " << trans << "\n";
		stream << "audio: " << audio << "\n";
		stream << "src_excl: " << (srcExcl ? "true" : "false") << "\n";
		stream << "trans_excl: " << (transExcl ? "true" : "false") << "\n";
		stream << "audio_excl: " << (audioExcl ? "true" : "false") << "\n";
		stream << "start_ms: " << startMs << "\n";
		stream << "end_ms: " << endMs << "\n";
		stream << "first: " << firstIdx << "\n";
		stream << "last: " << lastIdx << "\n";
		stream << "highlight: " << (isHighlighted ? "true" : "false") << "\n";
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
	translatedCells.clear();
	audioCells.clear();

	// Временные переменные для текущего блока
	QString currentEn, currentRu, currentAudio;
	bool currentEnExcl = false, currentRuExcl = false, currentAudioExcl = false;
	int currentStartMs = -1, currentEndMs = -1;
	int currentFirstIdx = -1, currentLastIdx = -1;
	bool currentHighlighted = false;

	// Флаг, что мы в секции аудио энтрисов
	bool inAudioEntriesSection = false;

	while (!stream.atEnd()) {
		QString line = stream.readLine();

		// Пропускаем пустые строки
		if (line.trimmed().isEmpty()) {
			if (!currentEn.isEmpty() || !currentRu.isEmpty() || !currentAudio.isEmpty()) {
				// Сохраняем текущий блок
				TextSentence sourceCell;
				sourceCell.text = currentEn;
				sourceCell.isExcluded = currentEnExcl;
				sourceCells.append(sourceCell);

				TextSentence transCell;
				transCell.text = currentRu;
				transCell.isExcluded = currentRuExcl;
				translatedCells.append(transCell);

				AudioSentence audioCell;
				audioCell.text = currentAudio;
				audioCell.isExcluded = currentAudioExcl;
				audioCell.audioStartMs = currentStartMs;
				audioCell.audioEndMs = currentEndMs;
				audioCell.firstWordIndex = currentFirstIdx;
				audioCell.lastWordIndex = currentLastIdx;
				audioCell.isHighlighted = currentHighlighted;
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
				currentFirstIdx = -1;
				currentLastIdx = -1;
			}
			continue;
		}

		// Пропускаем комментарии
		if (line.startsWith("#")) {
			int colonPos = line.indexOf(':');
			if (colonPos == -1) continue;
			QString value = line.mid(colonPos + 1).trimmed();

			// Парсим пути к файлам из заголовка
			if (line.startsWith("# Source: ")) {
				currentSourceFile = value;
			}
			else if (line.startsWith("# Translated: ")) {
				currentTranslatedFile = value;
			}
			else if (line.startsWith("# AudioText: ")) {
				currentAudioTextFile = value;
			}
			else if (line.startsWith("# AudioFile: ")) {
				currentAudioFile = value;
			}
			else if (line.startsWith("# Audio entries count: ")) {
				// Начинаем секцию аудио энтрисов
				inAudioEntriesSection = true;
				int count = line.mid(22).trimmed().toInt();
				audioEntries.reserve(count);
			}
			continue;
		}

		// Парсим аудио энтрисы (специальный формат)
		if (inAudioEntriesSection && line.startsWith("aentry: ")) {
			QString data = line.mid(8).trimmed();
			QStringList parts = data.split(" | ");
			if (parts.size() >= 5) {
				AudioEntry entry;
				entry.text = parts[0];
				entry.startMs = parts[1].toInt();
				entry.endMs = parts[2].toInt();
				entry.sentenceIdx = parts[3].toInt();
				entry.ins = (parts[4] == "1");
				audioEntries.append(entry);
			}
			continue;
		}

		// Парсим обычные ключи

		// Парсим ключ: значение
		int colonPos = line.indexOf(':');
		if (colonPos == -1) continue;

		QString key = line.left(colonPos).trimmed();
		QString value = line.mid(colonPos + 1).trimmed();

		if (key == "source") currentEn = value;
		else if (key == "trans") currentRu = value;
		else if (key == "audio") currentAudio = value;
		else if (key == "src_excl") currentEnExcl = (value == "true");
		else if (key == "trans_excl") currentRuExcl = (value == "true");
		else if (key == "audio_excl") currentAudioExcl = (value == "true");
		else if (key == "start_ms") currentStartMs = value.toInt();
		else if (key == "end_ms") currentEndMs = value.toInt();
		else if (key == "first") currentFirstIdx = value.toInt();
		else if (key == "last") currentLastIdx = value.toInt();
		else if (key == "highlight") currentHighlighted = (value == "true");
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
		translatedCells.append(ruCell);

		AudioSentence audioCell;
		audioCell.text = currentAudio;
		audioCell.isExcluded = currentAudioExcl;
		audioCell.audioStartMs = currentStartMs;
		audioCell.audioEndMs = currentEndMs;
		audioCell.firstWordIndex = currentFirstIdx;
		audioCell.lastWordIndex = currentLastIdx;
		audioCell.isHighlighted = currentHighlighted;
		audioCells.append(audioCell);
	}

	file.close();
	normalizeRowCount();

	// Валидация индексов (на всякий случай)
	for (int i = 0; i < audioCells.size(); ++i) {
		AudioSentence& sent = audioCells[i];
		if (sent.firstWordIndex >= 0 && sent.lastWordIndex >= 0) {
			// Проверяем, что индексы в пределах массива
			if (sent.firstWordIndex < 0 || sent.lastWordIndex >= audioEntries.size() ||
				sent.firstWordIndex > sent.lastWordIndex) {
				// Сбрасываем некорректные индексы
				sent.firstWordIndex = -1;
				sent.lastWordIndex = -1;
			}
		}
	}

	modified = false;
	return true;
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

void Aligner::insertSentence(const QStringList &currentWords, int currentSentenceIdx, 
	bool currentIsIns, int currentStartMs, int currentEndMs, int firstWordIdx, int lastWordIdx)
{
	if (currentIsIns) {
		TextSentence emptyTextCell;
		emptyTextCell.isExcluded = true;
		emptyTextCell.text = "";

	//	sourceCells.insert(currentSentenceIdx, emptyTextCell);
	//	translatedCells.insert(currentSentenceIdx, emptyTextCell);
	//	AudioSentence emptyAudioCell;
	//	emptyAudioCell.isExcluded = true;
	//	audioCells.insert(currentSentenceIdx, emptyAudioCell);

		// Вставляем в середину
		if (currentSentenceIdx < sourceCells.size()) {
			sourceCells.insert(currentSentenceIdx, emptyTextCell);
			translatedCells.insert(currentSentenceIdx, emptyTextCell);

			AudioSentence emptyAudioCell;
			emptyAudioCell.isExcluded = true;
			audioCells.insert(currentSentenceIdx, emptyAudioCell);
		}
		else {
			// Добавляем в конец
			sourceCells.append(emptyTextCell);
			translatedCells.append(emptyTextCell);

			AudioSentence emptyAudioCell;
			emptyAudioCell.isExcluded = true;
			audioCells.append(emptyAudioCell);
		}		

		// Теперь нужно заполнить вставленную ячейку audioCells
		// (она только что вставлена, индекс currentSentenceIdx)
		int targetIdx = (currentSentenceIdx < sourceCells.size()) ? currentSentenceIdx : audioCells.size() - 1;
		audioCells[targetIdx].text = currentWords.join(" ");
		audioCells[targetIdx].audioStartMs = currentStartMs;
		audioCells[targetIdx].audioEndMs = currentEndMs;
		audioCells[targetIdx].isExcluded = false;
		audioCells[targetIdx].firstWordIndex = firstWordIdx;
		audioCells[targetIdx].lastWordIndex = lastWordIdx;
	}
	else {
		// Обычное предложение: размещаем в существующей ячейке
		// Индекс должен быть в пределах массива
		if (currentSentenceIdx >= 0 && currentSentenceIdx < audioCells.size()) {
			audioCells[currentSentenceIdx].text = currentWords.join(" ");
			audioCells[currentSentenceIdx].audioStartMs = currentStartMs;
			audioCells[currentSentenceIdx].audioEndMs = currentEndMs;
			audioCells[currentSentenceIdx].isExcluded = false;
			audioCells[currentSentenceIdx].firstWordIndex = firstWordIdx;
			audioCells[currentSentenceIdx].lastWordIndex = lastWordIdx;
		}
		else if (currentSentenceIdx >= audioCells.size()) {
			// Если индекс выходит за пределы, добавляем новую ячейку
			AudioSentence newCell;
			newCell.text = currentWords.join(" ");
			newCell.audioStartMs = currentStartMs;
			newCell.audioEndMs = currentEndMs;
			newCell.isExcluded = false;
			newCell.firstWordIndex = firstWordIdx;
			newCell.lastWordIndex = lastWordIdx;
			audioCells.append(newCell);
		}
	}	
}

void Aligner::rebuildAudioEntires()
{
	// если в audioCells стоят корректные индексы слов, а в audioEntires некорректные индексы предложений
	int an = audioEntries.size();
	for (int i = 0; i < audioCells.size(); ++i) {
		int fi = audioCells[i].firstWordIndex;
		int ei = audioCells[i].lastWordIndex;
		for (int j = fi; j>=0 && j<an && j <= ei; j++)			
			audioEntries[j].sentenceIdx = i;
	}
	modified = true;
}

void Aligner::rebuildAudioSentences()
{
	// 1. Инициализация: audioCells такого же размера, как sourceCells
	audioCells.clear();
	audioCells.resize(sourceCells.size());
	for (int i = 0; i < audioCells.size(); ++i) {
		audioCells[i].isExcluded = true;
		audioCells[i].text = "";
		audioCells[i].audioStartMs = -1;
		audioCells[i].audioEndMs = -1;
		audioCells[i].firstWordIndex = -1;
		audioCells[i].lastWordIndex = -1;
		audioCells[i].audioSim = -1.0;
		audioCells[i].transSim = -1.0;
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
	int currentFirstWordIdx = -1;
	int currentLastWordIdx = -1;


	for (int i = audioEntries.size() - 1; i >= 0; i--) {
		const AudioEntry& entry = audioEntries[i];

		if (currentWords.isEmpty() ||
			entry.sentenceIdx != currentSentenceIdx ||
			entry.ins != currentIsIns) {

			// Сохраняем предыдущее накопленное предложение
			if (!currentWords.isEmpty() && currentSentenceIdx >= 0) {
				insertSentence(currentWords, currentSentenceIdx, currentIsIns, currentStartMs, currentEndMs, currentFirstWordIdx, currentLastWordIdx);
			}

			// Начинаем новое предложение
			currentWords.clear();
			currentWords.append(entry.text);
			currentStartMs = entry.startMs;
			currentEndMs = entry.endMs;
			currentSentenceIdx = entry.sentenceIdx;
			currentIsIns = entry.ins;
			currentFirstWordIdx = i;
			currentLastWordIdx = i;
		}
		else {
			currentWords.prepend(entry.text);
			currentStartMs = entry.startMs;
			currentFirstWordIdx = i;
		}
	}

	// Сохраняем последнее предложение
	if (!currentWords.isEmpty()) {
		insertSentence(currentWords, currentSentenceIdx, currentIsIns, currentStartMs, currentEndMs, currentFirstWordIdx, currentLastWordIdx);
	}	
}

void Aligner::alignAudio()
{
	int enIdx = 0;
	int audioIdx = 0;
	const double THRESHOLD = 0.8;
	
	while (enIdx < getSourceWordsCount()) {

		// индекс предложения для отладки; здесь 
		int sentIdx = getSourceSentence(enIdx);
		QString sentStr = QString::asprintf("S=%d", sentIdx);

		// размер исходного окна
		int maxSrc = sourceWindowSize(enIdx);

		// выводим "SOURCE" строку
		qDebug() << sentStr << "MS=" << maxSrc << debugEnWords(this, enIdx, maxSrc);


		int bestOffset = -1;
		double bestScore = 0.0;
		MatchResult bestMR;
		int maxSearch = qMin(500, getAudioWordsCount() - audioIdx - WINDOW_SIZE);

		if (sentIdx == 43)
			sentIdx = sentIdx;

		// двигаем окно в некоторых пределах
		for (int offset = 0; offset <= maxSearch; ++offset) {

			// размер аудио окна
			int maxAud = audioWindowSize(audioIdx + offset, maxSrc);

			MatchResult mr = similarity(enIdx, maxSrc, audioIdx + offset, maxAud);

			// отладка
			qDebug() << sentStr << QString::asprintf("OFFS=%03d: %.5f", offset, mr.score) << debugAudioWords(this, audioIdx + offset, maxAud);

			// если текущее значение лучше - делаем его "наилучшим"
			if (mr.score > bestScore) {
				bestScore = mr.score;
				bestMR = mr;
				bestOffset = offset;
			}
			// иначе, если текущее хуже_или_такое_же и наилучшее выше порога - считаем что нашли локальный оптимум
			else if (bestScore > THRESHOLD)
				break;
			// если  текущее строго 1 - идеальное совпадение
			if (mr.score == 1.0)
				break;
		}// конец цикла поиска наилучшего совпадения окна

		// если удалось найти какое-то совпадение групп слов
		if (bestOffset >= 0 && bestScore >= THRESHOLD) {

			qDebug() << "MATCH! BestOffset=" << bestOffset << "UsedSrc=" << bestMR.usedSource << "UsedAudio=" << bestMR.usedAudio;

			// Есть неподошедшие аудио слова перед совпадением? (из-за движения окна)
			if (bestOffset > 0) {
				flushPendingGroup(enIdx, audioIdx, bestOffset);
				audioIdx += bestOffset;
			}

			// Привязываем совпавшую группу
			assignMatchedGroup(enIdx, bestMR.usedSource, audioIdx, bestMR.usedAudio, bestMR.path);

			enIdx += bestMR.usedSource;
			audioIdx += bestMR.usedAudio;
		}
		else {
			// Не нашли совпадение — пропускаем английское слово
			enIdx++;
		}
	}

	// Оставшиеся аудио слова в конце
	if (audioIdx < getAudioWordsCount()) {
		flushPendingGroup(enIdx, audioIdx, getAudioWordsCount() - audioIdx);
	}
}

int Aligner::sourceWindowSize(int srcStart)
{
	// определить размер исходного окна, опираясь на предложения
	int s0 = getSourceSentence(srcStart);
	int n = getSourceWordsCount();
	for (int i = srcStart; i < n; i++) {
		int s = getSourceSentence(i);
		// разрыв только в точке перехода между предложениями
		if (s != s0 && i - srcStart >= WINDOW_SIZE)
			return i - srcStart;
		s0 = s;
	}
	return n - srcStart;
}

int Aligner::audioWindowSize(int audioStart, int sourceWindow)
{
	// определить размер аудио окна, "с запасом" на то что в аудио есть вставки
	int s = qMin((int)(sourceWindow * 1.3), getAudioWordsCount() - audioStart);
	return s;
}

MatchResult Aligner::similarity(int enStart, int maxSrc, int audioStart, int maxAud)
{
	int srcCount = getSourceWordsCount();
	int audCount = getAudioWordsCount();

	struct Cell {
		double score = 0.0;
		int prev_i = -1;
		int prev_j = -1;
	};

	QVector<QVector<Cell>> dp(maxSrc + 1, QVector<Cell>(maxAud + 1));

	const double GAP_PENALTY = -0.4;

	// заполнение DP
	for (int i = 0; i <= maxSrc; ++i) {
		for (int j = 0; j <= maxAud; ++j) {

			if (i == 0 && j == 0) continue;

			double best = -1e9;
			int pi = -1, pj = -1;

			// match
			if (i > 0 && j > 0) {
				double sim = wordSimilarity(
					getSourceWord(enStart + i - 1),
					getAudioWord(audioStart + j - 1)
				);

				double val = dp[i - 1][j - 1].score + sim;
				if (val > best) {
					best = val;
					pi = i - 1;
					pj = j - 1;
				}
			}

			// skip source
			if (i > 0) {
				double val = dp[i - 1][j].score + GAP_PENALTY;
				if (val > best) {
					best = val;
					pi = i - 1;
					pj = j;
				}
			}

			// skip audio
			if (j > 0) {
				double val = dp[i][j - 1].score + GAP_PENALTY;
				if (val > best) {
					best = val;
					pi = i;
					pj = j - 1;
				}
			}

			dp[i][j] = { best, pi, pj };
		}
	}

	// выбор bestI, bestJ
	int bestI = maxSrc;
	int bestJ = maxAud;
	double bestScore = -1e9;
	for (int j = 0; j <= maxAud; ++j) {
		double norm = dp[maxSrc][j].score / std::max(maxSrc, j);

		if (norm > bestScore) {
			bestScore = norm;
			bestJ = j;
		}
	}

	// Восстанавливаем путь (alignment path) (backtrack)
	QVector<PathStep> path;
	int i = bestI, j = bestJ;
	int usedSrc = 0;
	int usedAud = 0;
	while (i > 0 || j > 0) {
		Cell c = dp[i][j];
		if (c.prev_i == i - 1 && c.prev_j == j - 1) {
			// Совпадение или замена
			path.prepend({ enStart + i - 1, audioStart + j - 1 });
			usedSrc++;
			usedAud++;
			i--;  // перемещаемся в prev_i
			j--;  // перемещаемся в prev_j
		}
		else if (c.prev_i == i - 1) {
			// Удаление (слово есть в исходном, нет в аудио)
			path.prepend({ enStart + i - 1, -1 });
			usedSrc++;
			i--;  // перемещаемся в prev_i
			// j не меняется
		}
		else {
			// Вставка (слово есть в аудио, нет в исходном)
			path.prepend({ -1, audioStart + j - 1 });
			usedAud++;
			j--;  // перемещаемся в prev_j
			// i не меняется
		}
	}

	MatchResult res;
	res.score = std::max(0.0, bestScore);
	res.usedSource = usedSrc;
	res.usedAudio = usedAud;
	res.path = path;

	return res;
}

void Aligner::clearAudioAlignment()
{
	// Очищаем аудио ячейки
	audioCells.clear();

	// Сбрасываем индексы предложений в audioEntries
	for (int i = 0; i < audioEntries.size(); ++i) {
		audioEntries[i].sentenceIdx = -1;
		audioEntries[i].ins = false;
	}

	// Очищаем кэш исходных слов
	sourceWordsCache.clear();

	// Сбрасываем статистику
	totalAudioSim = 0;

	// Не очищаем sourceCells и translatedCells!
	// Они должны сохраниться
}

void Aligner::alignAudioToSource()
{
	if (sourceCells.isEmpty() || audioEntries.isEmpty()) return;

	clearAudioAlignment();
	normalizeRowCount();
	rebuildSourceWordsCache();	
			
	alignAudio();	

	normalizeRowCount();
	rebuildAudioSentences();

	calcAudioSimilarity();

	modified = true;
}

bool Aligner::moveAudioWordsToPrev(int sentenceIndex, int wordOffset)
{
	if (sentenceIndex <= 0 || sentenceIndex >= audioCells.size()) return false;
	if (audioCells[sentenceIndex].firstWordIndex < 0) return false;

	AudioSentence& curr = audioCells[sentenceIndex];
	AudioSentence& prev = audioCells[sentenceIndex - 1];

	int firstWord = curr.firstWordIndex;
	int lastWord = curr.lastWordIndex;

	if (firstWord < 0 || lastWord < 0 || firstWord > lastWord) return false;
	if (wordOffset <= 0 || wordOffset > (lastWord - firstWord + 1)) return false;

	// Определяем новые границы
	int newFirstWord = firstWord + wordOffset;
	int movedEnd = firstWord + wordOffset - 1;

	// Обновляем previous предложение
	if (prev.firstWordIndex < 0) {
		prev.firstWordIndex = firstWord;
	}
	prev.lastWordIndex = movedEnd;

	// Обновляем текущее предложение
	curr.firstWordIndex = newFirstWord;
	if (curr.firstWordIndex > curr.lastWordIndex) {
		// Все слова ушли - помечаем как исключенное/пустое
		curr.isExcluded = true;
		curr.text = "";
	}

	// Обновляем тексты предложений
	updateAudioSentenceFromEntries(sentenceIndex - 1);
	updateAudioSentenceFromEntries(sentenceIndex);

	// Обновляем индексы в audioEntries
	for (int i = firstWord; i <= movedEnd; ++i) {
		audioEntries[i].sentenceIdx = sentenceIndex - 1;
		audioEntries[i].ins = false;
	}
	for (int i = newFirstWord; i <= lastWord; ++i) {
		audioEntries[i].sentenceIdx = sentenceIndex;
		audioEntries[i].ins = false;
	}

	modified = true;
	return true;
}

bool Aligner::moveAudioWordsToNext(int sentenceIndex, int wordOffset)
{
	if (sentenceIndex < 0 || sentenceIndex >= audioCells.size() - 1) return false;
	if (audioCells[sentenceIndex].firstWordIndex < 0) return false;

	AudioSentence& curr = audioCells[sentenceIndex];
	AudioSentence& next = audioCells[sentenceIndex + 1];

	int firstWord = curr.firstWordIndex;
	int lastWord = curr.lastWordIndex;

	if (firstWord < 0 || lastWord < 0 || firstWord > lastWord) return false;
	if (wordOffset <= 0 || wordOffset > (lastWord - firstWord + 1)) return false;

	// Определяем новые границы
	int movedStart = lastWord - wordOffset + 1;
	int newLastWord = movedStart - 1;

	// Обновляем следующее предложение
	if (next.lastWordIndex < 0) {
		next.lastWordIndex = lastWord;
	}
	next.firstWordIndex = movedStart;

	// Обновляем текущее предложение
	curr.lastWordIndex = newLastWord;
	if (curr.firstWordIndex > curr.lastWordIndex) {
		curr.isExcluded = true;
		curr.text = "";
	}

	// Обновляем тексты предложений
	updateAudioSentenceFromEntries(sentenceIndex);
	updateAudioSentenceFromEntries(sentenceIndex + 1);

	// Обновляем индексы в audioEntries
	for (int i = movedStart; i <= lastWord; ++i) {
		audioEntries[i].sentenceIdx = sentenceIndex + 1;
		audioEntries[i].ins = false;
	}

	modified = true;
	return true;
}

void Aligner::updateAudioSentenceFromEntries(int sentenceIndex)
{
	if (sentenceIndex < 0 || sentenceIndex >= audioCells.size()) return;

	AudioSentence& sent = audioCells[sentenceIndex];
	if (sent.firstWordIndex < 0 || sent.lastWordIndex < 0) return;

	QStringList words;
	sent.audioStartMs = audioEntries[sent.firstWordIndex].startMs;
	sent.audioEndMs = audioEntries[sent.lastWordIndex].endMs;

	for (int i = sent.firstWordIndex; i <= sent.lastWordIndex; ++i) {
		words.append(audioEntries[i].text);
	}

	sent.text = words.join(" ");
	sent.isExcluded = false;
}


bool Aligner::removeAudioSentence(int row, bool force)
{
	// Проверяем границы
	if (row < 0 || row >= audioCells.size()) {
		qWarning() << "removeAudioSentence: invalid row" << row;
		return false;
	}

	AudioSentence& sent = audioCells[row];

	// Проверяем, есть ли валидные индексы
	bool hasValidIndices = (sent.firstWordIndex >= 0 && sent.lastWordIndex >= 0 &&
		sent.firstWordIndex <= sent.lastWordIndex &&
		sent.lastWordIndex < audioEntries.size());

	// Если есть валидные индексы и нет принудительного удаления - спрашиваем
	if (hasValidIndices && !force) {
		// Возвращаем false, UI сам спросит пользователя
		return false;
	}

	// Удаляем привязку слов к этому предложению из audioEntries
	if (hasValidIndices) {
		for (int i = sent.firstWordIndex; i <= sent.lastWordIndex; ++i) {
			if (i >= 0 && i < audioEntries.size()) {
				audioEntries[i].sentenceIdx = -1;
				audioEntries[i].ins = false;
			}
		}
	}

	// Удаляем ячейку
	audioCells.removeAt(row);

	// Корректируем индексы в audioEntries для всех последующих предложений
	for (auto& entry : audioEntries) {
		if (entry.sentenceIdx > row) {
			entry.sentenceIdx--;
		}
	}

	// Нормализуем количество строк (синхронизируем с sourceCells и translatedCells)
	normalizeRowCount();

	modified = true;
	return true;
}
