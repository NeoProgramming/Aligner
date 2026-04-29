#include "tools.h"
#include <QStringList>

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

bool equ(const QString& word1, const QString& word2)
{
	int i = 0;
	int len1 = word1.length();
	int len2 = word2.length();

	while (i < len1 && i < len2) {
		QChar c1 = word1[i];
		QChar c2 = word2[i];

		// Если в любой из строк встретили '~' - дальше не сравниваем
		if (c1 == '~' || c2 == '~') {
			break;
		}

		// Если символы не совпадают - строки разные
		if (c1 != c2) {
			return false;
		}

		i++;
	}

	// Если одна строка закончилась, а в другой на этой позиции не '~'
	if (i < len1) {
		return word1[i] == '~';
	}
	if (i < len2) {
		return word2[i] == '~';
	}

	// Обе строки закончились одновременно
	return true;
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

