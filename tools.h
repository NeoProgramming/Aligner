#pragma once
#include <QString>
#include <QVector>
#include "alignment.h"

QString debugEnWords(IAlignmentEngine *engine, int start, int count);
QString debugAudioWords(IAlignmentEngine *engine, int start, int count);

QString msToTimeFormat(int ms);

QString stemRussian(const QString& word);
QString stemEnglish(const QString& word);

double wordSimilarity(const QString& a, const QString& b);

QStringList tokenizeWords(const QString& text);
void saveToFile(const QString& filename, const QString& content);

double evaluateSentenceSimilaritySimple(const QString& sourceSentence, const QString& transSentence);
double calculateWordMatchScore(const QStringList& enWords, const QStringList& audioWords);

bool intersect(const QStringList& list1, const QStringList& list2);
