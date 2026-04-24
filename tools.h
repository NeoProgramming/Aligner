#pragma once
#include <QString>
#include "alignment.h"

QString debugEnWords(IAlignmentEngine *engine, int start, int count);
QString debugAudioWords(IAlignmentEngine *engine, int start, int count);

QString msToTimeFormat(int ms);

QString stemRussian(const QString& word);
QString stemEnglish(const QString& word);

