#include "tools.h"
#include <QStringList>
#include <QFile>
#include <QTextStream>
#include <QDebug>

// aligner.cpp
QString debugEnWords(IAlignmentEngine *engine, int start, int count)
{
	if (start < 0 || start >= engine->getSourceWordsCount()) {
		return QString("<EN invalid start: %1>").arg(start);
	}

	int end = qMin(start + count, engine->getSourceWordsCount());
	QStringList result;

	result.append(QString("                 EN W=%1").arg(start));

	for (int i = start; i < end; ++i) {
		result.append(engine->getSourceWord(i));
	}

	return result.join(" ");
}

QString debugAudioWords(IAlignmentEngine *engine, int start, int count)
{
	if (start < 0 || start >= engine->getAudioWordsCount()) {
		return QString("<AU invalid start: %1>").arg(start);
	}

	int end = qMin(start + count, engine->getAudioWordsCount());
	QStringList result;

	result.append(QString("AU W=%1").arg(start));

	for (int i = start; i < end; ++i) {
		result.append(engine->getAudioWord(i));
	}

	return result.join(" ");
}

QString msToTimeFormat(int ms)
{
	int hours = ms / 3600000;
	int minutes = (ms % 3600000) / 60000;
	int seconds = (ms % 60000) / 1000;
	int milliseconds = ms % 1000;

	return QString("%1:%2:%3.%4")
		.arg(hours, 2, 10, QChar('0'))
		.arg(minutes, 2, 10, QChar('0'))
		.arg(seconds, 2, 10, QChar('0'))
		.arg(milliseconds, 3, 10, QChar('0'));
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

double wordSimilarity(const QString& a, const QString& b) 
{
	if (a == b) return 1.0;
	if (a.size() < 3 || b.size() < 3) return 0.0;

	// простая эвристика — общий префикс
	int common = 0;
	int len = std::min(a.size(), b.size());
	for (int i = 0; i < len; ++i) {
		if (a[i] != b[i]) break;
		common++;
	}
	return double(common) / std::max(a.size(), b.size());
}


QStringList tokenizeWords(const QString& text)
{
	QStringList result;
	QString currentToken;

	for (int i = 0; i < text.length(); ++i) {
		QChar ch = text[i];

		// Буква (любого алфавита) или цифра
		if (ch.isLetter() || ch.isDigit() || ch == '~') {
			currentToken += ch;
		}
		else if (ch == QChar(8217) || ch == QChar(39))
			currentToken += QChar(39);
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

void saveToFile(const QString& filename, const QString& content)
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

double evaluateSentenceSimilaritySimple(const QString& sourceSentence, const QString& targetSentence)
{
	// проверочная функция - оценка похожести предложений

	if (sourceSentence.isEmpty() && targetSentence.isEmpty()) {
		return 1.0;
	}

	if (sourceSentence.isEmpty() || targetSentence.isEmpty()) {
		return 0.0;
	}

	QStringList sourceWords = tokenizeWords(sourceSentence);
	QStringList targetWords = tokenizeWords(targetSentence);

	// Считаем совпадения слов (позиция не важна)
	QMap<QString, int> sourceCount;
	for (const QString& w : sourceWords) {
		sourceCount[w.toLower()]++;
	}

	int matches = 0;
	for (const QString& w : targetWords) {
		QString lower = w.toLower();
		if (sourceCount.contains(lower) && sourceCount[lower] > 0) {
			matches++;
			sourceCount[lower]--;
		}
	}

	double recall = (double)matches / sourceWords.size();      // сколько слов исходника нашли
	double precision = (double)matches / targetWords.size();   // сколько слов аудио совпало

	if (precision + recall == 0) return 0.0;

	// F-мера (среднее гармоническое)
	double f1 = 2 * (precision * recall) / (precision + recall);

	return f1;
}
