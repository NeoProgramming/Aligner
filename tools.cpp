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

	result.append(QString("EN[%1]").arg(start));

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

	result.append(QString("AU [%1]").arg(start));

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
