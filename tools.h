#pragma once
#include <QString>
#include <QVector>
#include "alignment.h"

QString debugEnWords(IAlignmentEngine *engine, int start, int count);
QString debugAudioWords(IAlignmentEngine *engine, int start, int count);

QString msToTimeFormat(int ms);

QString stemRussian(const QString& word);
QString stemEnglish(const QString& word);

bool equ(const QString& word1, const QString& word2);
bool equ2(const QString& word1, const QString& word2);

QStringList tokenizeWords(const QString& text);
void saveToFile(const QString& filename, const QString& content);

double evaluateSentenceSimilaritySimple(const QString& sourceSentence, const QString& targetSentence);
