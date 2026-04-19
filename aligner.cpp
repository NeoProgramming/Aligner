#include "aligner.h"
#include <QFile>
#include <QTextStream>
#include <QTextBoundaryFinder>
#include <QDebug>
#include <QDir>
#include <QProcess>
#include <QRegularExpression>
#include <QDateTime>

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
		else {
			break;  // Как только встретили непустую строку, дальше не идём
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


void Aligner::autoAlignTarget()
{
	if (enCells.isEmpty() || currentRuFile.isEmpty()) return;

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

void Aligner::assignAudioToRows()
{
	if (audioEntries.isEmpty()) return;

	audioCells.clear();
	for (const AudioEntry& entry : audioEntries) {
		CellData cell;
		cell.text = entry.text;
		cell.isExcluded = false;
		cell.audioStartMs = entry.startMs;
		cell.audioEndMs = entry.endMs;
		audioCells.append(cell);
	}

	normalizeRowCount();
	modified = true;
}

void Aligner::updateRussianColumn()
{
	// This is called from UI to sync, but we don't need it now
}

void Aligner::updateAudioColumn()
{
	// This is called from UI to sync, but we don't need it now
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

QStringList Aligner::tokenizeWords(const QString& text)
{
	QStringList result;
	QString currentToken;

	for (int i = 0; i < text.length(); ++i) {
		QChar ch = text[i];

		// Буква (любого алфавита) или цифра
		if (ch.isLetter() || ch.isDigit()) {
			currentToken += ch;
		}
		else {
			if (!currentToken.isEmpty()) {
				QString lwr = currentToken.toLower();
				result.append(lwr);
				currentToken.clear();
			}
		}
	}

	if (!currentToken.isEmpty()) {
		QString lwr = currentToken.toLower();
		result.append(lwr);
		currentToken.clear();
	}

	return result;
}

QString stemRussian(const QString& word)
{
	QString result = word.toLower();

	// Простейшие правила (нужно расширять)
	if (result.endsWith("ами")) result.chop(3);
	else if (result.endsWith("ями")) result.chop(3);
	else if (result.endsWith("ов")) result.chop(2);
	else if (result.endsWith("ев")) result.chop(2);
	else if (result.endsWith("ем")) result.chop(2);
	else if (result.endsWith("ом")) result.chop(2);
	else if (result.endsWith("ой")) result.chop(2);
	else if (result.endsWith("ей")) result.chop(2);
	else if (result.endsWith("ах")) result.chop(2);
	else if (result.endsWith("ях")) result.chop(2);
	else if (result.endsWith("а")) result.chop(1);
	else if (result.endsWith("у")) result.chop(1);
	else if (result.endsWith("е")) result.chop(1);
	else if (result.endsWith("и")) result.chop(1);
	else if (result.endsWith("ы")) result.chop(1);
	else if (result.endsWith("ь")) result.chop(1);

	return result;
}

QString stemEnglish(const QString& word)
{
	QString result = word.toLower();

	// Специальные случаи (множественное число с изменением формы)
	if (result == "shelves") return "shelf";
	if (result == "wives") return "wife";
	if (result == "leaves") return "leaf";
	if (result == "knives") return "knife";
	if (result == "children") return "child";
	if (result == "men") return "man";
	if (result == "women") return "woman";
	if (result == "mice") return "mouse";
	if (result == "feet") return "foot";
	if (result == "teeth") return "tooth";
	if (result == "geese") return "goose";

	// -ing
	if (result.endsWith("ing") && result.length() > 4) {
		result.chop(3);
		// try: running -> run (нужно отрезать двойную букву)
		if (result.endsWith("nn")) result.chop(1);
		else if (result.endsWith("mm")) result.chop(1);
		else if (result.endsWith("pp")) result.chop(1);
		else if (result.endsWith("tt")) result.chop(1);
		return result;
	}

	// -ed
	if (result.endsWith("ed") && result.length() > 3) {
		result.chop(2);
		if (result.endsWith("nn")) result.chop(1);
		else if (result.endsWith("mm")) result.chop(1);
		else if (result.endsWith("pp")) result.chop(1);
		else if (result.endsWith("tt")) result.chop(1);
		return result;
	}

	// -es (после sh, ch, s, x, z)
	if (result.endsWith("es") && result.length() > 3) {
		result.chop(2);
		return result;
	}

	// -s (обычное множественное число)
	if (result.endsWith("s") && result.length() > 2) {
		// Не отрезаем 's' у слов, оканчивающихся на 'us' (cactus, status)
		if (!result.endsWith("us")) {
			result.chop(1);
		}
		return result;
	}

	return result;
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
	int matches = 0;
	for (const QString& enWord : enWords) {
		if (ruEquivalent.contains(enWord)) {
			matches++;
		}
		else if (QString stem = stemEnglish(enWord);  ruEquivalent.contains(stem)) {
			matches++;
		}
	}

	// Нормализуем по длине английского предложения
	double score = (double)matches / enWords.size();

	return score;
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
		stream << "srcexcl: " << (enExcl ? "true" : "false") << "\n";
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

	int matches = 0;
	int total = enWords.size();

	// Проходим по аудио словам, ищем совпадения
	for (const QString& audioWord : audioWords) {
		QString word = audioWord.toLower();
		// Удаляем знаки пунктуации из аудио слова
		word.remove(QRegularExpression("[\\p{P}]"));

		if (enSet.contains(word)) {
			matches++;
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

	double score = (double)matches / total;
	return score * lengthPenalty;
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

double Aligner::similarity1(int enStart, int audioStart, int N)
{
	// Проверка границ
	if (enStart + N > m_enWordsCache.size() || audioStart + N > audioEntries.size()) {
		return 0.0;
	}

	double totalScore = 0.0;

	// Для каждого английского слова ищем наилучшее совпадение в аудио фрагменте
	// с учётом позиции (штраф за расстояние)
	for (int enIdx = 0; enIdx < N; ++enIdx) {
		const QString& enWord = m_enWordsCache[enStart + enIdx].text;
		if (enWord.isEmpty()) continue;

		double bestWordScore = 0.0;
		int bestPos = -1;

		// Ищем это слово среди аудио слов в диапазоне
		for (int audioIdx = 0; audioIdx < N; ++audioIdx) {
			const QString& audioWord = audioEntries[audioStart + audioIdx].text.toLower();

			if (enWord == audioWord) {
				// Расстояние в позициях (0 = идеальное совпадение позиции)
				int distance = abs(enIdx - audioIdx);
				// Вес: 1.0 для совпадающей позиции, уменьшается с расстоянием
				double positionWeight = 1.0 / (distance + 1.0);
				double wordScore = positionWeight;

				if (wordScore > bestWordScore) {
					bestWordScore = wordScore;
					bestPos = audioIdx;
				}
			}
		}

		totalScore += bestWordScore;
	}

	// Нормализуем по длине (максимальный возможный score = N)
	return totalScore / N;
}

double Aligner::similarity3(int enStart, int audioStart, int N)
{
	// Проверка границ
	if (enStart + N > m_enWordsCache.size() || audioStart + N > audioEntries.size()) {
		return 0.0;
	}

	int matches = 0;

	for (int i = 0; i < N; ++i) {
		const QString& enWord = m_enWordsCache[enStart + i].text;
		
		// Очищаем аудио слово от знаков препинания
		QString audioWord = audioEntries[audioStart + i].text.toLower();
		
		if (enWord == audioWord) {
			matches++;
		}
	}

	return (double)matches / N;
}

QString Aligner::cleanAudioWord(const QString& word)
{
	QString result = word.toLower();
	result.remove(QRegularExpression("[\\p{P}]"));
	return result;
}

void Aligner::flushPendingAudio(QStringList& pendingAudio, int& pendingStartMs, int& pendingEndMs, int insertPosition)
{
	if (pendingAudio.isEmpty()) return;
	
	PendingInsert insert;
	insert.index = insertPosition;
	insert.audioCell.text = pendingAudio.join(" ");
	insert.audioCell.isExcluded = false;
	insert.audioCell.audioStartMs = pendingStartMs;
	insert.audioCell.audioEndMs = pendingEndMs;
	m_pendingInserts.append(insert);

	pendingAudio.clear();
	pendingStartMs = -1;
	pendingEndMs = -1;

	qDebug() << "[PENDING] " << insert.audioCell.text;
}

void Aligner::assignAudioGroup(int enStart, int audioStart, int windowSize)
{
	// Собираем аудио слова по индексам предложений
	QMap<int, QStringList> audioBySentence;
	QMap<int, int> startMsBySentence;
	QMap<int, int> endMsBySentence;

	for (int i = 0; i < windowSize; ++i) {
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

// aligner.cpp
QString Aligner::debugEnWords(int start, int count)
{
	if (start < 0 || start >= m_enWordsCache.size()) {
		return QString("<EN invalid start: %1>").arg(start);
	}

	int end = qMin(start + count, m_enWordsCache.size());
	QStringList result;

	result.append(QString("EN[%1]").arg(start));

	for (int i = start; i < end; ++i) {
		result.append(m_enWordsCache[i].text);
	}

	return result.join(" ");
}

QString Aligner::debugAudioWords(int start, int count)
{
	if (start < 0 || start >= audioEntries.size()) {
		return QString("<AU invalid start: %1>").arg(start);
	}

	int end = qMin(start + count, audioEntries.size());
	QStringList result;

	result.append(QString("AU [%1]").arg(start));

	for (int i = start; i < end; ++i) {
		result.append(audioEntries[i].text);
	}

	return result.join(" ");
}

void Aligner::syncCellsAfterAlignment()
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

MatchResult Aligner::similarityRecursive(int enStart, int audioStart, int currDepth, int minDepth)
{
	// Базовый случай: вышли за границы
	if (enStart >= m_enWordsCache.size() || audioStart >= audioEntries.size()) {
		return { 0.0, 0, 0 };
	}

	const QString& enWord = m_enWordsCache[enStart].text;
	const QString& audioWord = audioEntries[audioStart].text;
	if (audioWord == "1")
		enStart += 0;

	// Вариант 1: слова совпадают
	if (enWord == audioWord) {
		// рекурсивно вызываем для следующей пары слов
		MatchResult next = similarityRecursive(enStart + 1, audioStart + 1, currDepth - 1, minDepth);
		// возвращаем результат
		return { 1.0 + next.matches, 1 + next.enTotal, 1 + next.audioTotal };
	}

	// правильно ли я перенес сюда это???
	// Базовый случай: достигли максимальной глубины
	if (currDepth <= minDepth) {
		return { 0.0, 0, 0 };
	}

	// Вариант 2: пропускаем английское слово
	MatchResult skipEn = similarityRecursive(enStart + 1, audioStart, currDepth - 1, minDepth);
	skipEn.matches += 0;
	skipEn.enTotal += 1;
	skipEn.audioTotal += 0;

	// Вариант 3: пропускаем аудио слово
	MatchResult skipAudio = similarityRecursive(enStart, audioStart + 1, currDepth - 1, minDepth);
	skipAudio.matches += 0;
	skipAudio.enTotal += 0;
	skipAudio.audioTotal += 1;

	// Вариант 4: пропускаем оба слова
	MatchResult skipBoth = similarityRecursive(enStart + 1, audioStart + 1, currDepth - 1, minDepth);
	skipBoth.matches += 0;
	skipBoth.enTotal += 1;
	skipBoth.audioTotal += 1;


	// Выбираем лучший вариант (по score)
	if (skipEn.matches >= skipAudio.matches  && skipEn.matches >= skipBoth.matches) {
		return { skipEn.matches , skipEn.enTotal, skipEn.audioTotal };
	}
	else if (skipAudio.matches >= skipBoth.matches) {
		return { skipAudio.matches , skipAudio.enTotal, skipAudio.audioTotal };
	}
	else {
		return { skipBoth.matches , skipBoth.enTotal, skipBoth.audioTotal };
	}
}

double Aligner::similarity(int enStart, int audioStart, int N, int& enUsed, int& audioUsed)
{
	int M = N * 2 / 3;
	MatchResult result = similarityRecursive(enStart, audioStart, N, M);

	if (result.enTotal < M || result.audioTotal < M) {
		enUsed = 0;
		audioUsed = 0;
		return 0.0;
	}
	
	enUsed = result.enTotal;
	audioUsed = result.audioTotal;
	int m = qMax(enUsed, audioUsed);
	if (m == 0)
		return 0.0;
	return result.matches / m;  // нормализуем
}

void Aligner::alignAudioToSource()
{
	if (enCells.isEmpty() || audioEntries.isEmpty()) return;

	rebuildSourceWordsCache();

	// Сохраняем оригинальные размеры для отслеживания вставок
	int originalEnSize = enCells.size();

	audioCells.clear();
	m_pendingInserts.clear();
//	normalizeRowCount();

	// audioCells нужно подготовить к вставкам, но мы будем вставлять по мере необходимости

	int enIdx = 0;
	int audioIdx = 0;

	const int WINDOW_SIZE = 12;
	const double THRESHOLD = 0.8;

	QStringList pendingAudio;
	int pendingStartMs = -1;
	int pendingEndMs = -1;

	while (enIdx < m_enWordsCache.size()) {
		int currentWindow = qMin(WINDOW_SIZE, m_enWordsCache.size() - enIdx);

		// отладка
		qDebug() << debugEnWords(enIdx, currentWindow);

		int bestOffset = -1;
		double bestScore = 0.0;
		int maxSearch = qMin(500, audioEntries.size() - audioIdx - currentWindow);

		int enUsed = 0;
		int audioUsed = 0;

		for (int offset = 0; offset <= maxSearch; ++offset) {
			
			double score = similarity(enIdx, audioIdx + offset, currentWindow, enUsed, audioUsed);

			// отладка
			qDebug() << QString::asprintf("#%03d: %.5f", offset, score) << debugAudioWords(audioIdx + offset, currentWindow);

			if (score > bestScore) {
				bestScore = score;
				bestOffset = offset;
			}
			else if (bestScore > THRESHOLD)
				break;
			
			if (score == 1.0)
				break;
		}

		if (bestOffset >= 0 && bestScore >= THRESHOLD) {
			// Есть неподошедшие аудио слова перед совпадением?
			if (bestOffset > 0) {
				for (int i = 0; i < bestOffset; ++i) {
					pendingAudio.append(audioEntries[audioIdx + i].text);
					if (pendingStartMs == -1) 
						pendingStartMs = audioEntries[audioIdx + i].startMs;
					pendingEndMs = audioEntries[audioIdx + i].endMs;
				}

				// Вычисляем позицию вставки: текущий enIdx 
				int insertPos = m_enWordsCache[enIdx].sentenceIndex;
				flushPendingAudio(pendingAudio, pendingStartMs, pendingEndMs, insertPos);

				audioIdx += bestOffset;
			}

			// Привязываем совпавшую группу
			assignAudioGroup(enIdx, audioIdx, currentWindow);

			enIdx += enUsed;
			audioIdx += audioUsed;
		}
		else {
			// Не нашли совпадение — пропускаем английское слово
			enIdx++;
		}
	}

	// Оставшиеся аудио слова в конце
	if (audioIdx < audioEntries.size()) {
		for (int i = audioIdx; i < audioEntries.size(); ++i) {
			pendingAudio.append(audioEntries[i].text);
			if (pendingStartMs == -1) pendingStartMs = audioEntries[i].startMs;
			pendingEndMs = audioEntries[i].endMs;
		}
		// Вставляем в конец
		flushPendingAudio(pendingAudio, pendingStartMs, pendingEndMs, enCells.size());
	}

	// Синхронизация: вставляем пустые en/ru для ячеек с isExcluded = true
	normalizeRowCount();
	syncCellsAfterAlignment();

	modified = true;
}



