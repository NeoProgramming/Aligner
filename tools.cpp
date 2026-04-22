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
