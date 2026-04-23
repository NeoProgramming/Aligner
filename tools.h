#pragma once
#include <QString>
#include "alignment.h"

QString debugEnWords(IAlignmentEngine *engine, int start, int count);
QString debugAudioWords(IAlignmentEngine *engine, int start, int count);

QString msToTimeFormat(int ms);
