#pragma once

#include <QVector>
#include <QString>
#include "aligner.h"

class AudioParser
{
public:
	// Автоматическое определение формата по расширению
	static QVector<AudioEntry> parseFile(const QString& filename);

	// Парсеры для конкретных форматов
	static QVector<AudioEntry> parseSrt(const QString& filename);
	static QVector<AudioEntry> parseJson(const QString& filename);
};



