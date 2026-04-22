#include "MyAligner.h"
#include "tools.h"
#include <QDebug>

void MyAligner::align(IAlignmentEngine* alignerEngine)
{
	int enIdx = 0;
	int audioIdx = 0;

	const int WINDOW_SIZE = 12;
	const double THRESHOLD = 0.8;

	int pendingStartMs = -1;
	int pendingEndMs = -1;

	engine = alignerEngine;
	
	while (enIdx < engine->getSourceWordsCount()) {
		int currentWindow = qMin(WINDOW_SIZE, engine->getSourceWordsCount() - enIdx);

		// отладка
		qDebug() << debugEnWords(engine, enIdx, currentWindow);

		int bestOffset = -1;
		double bestScore = 0.0;
		int maxSearch = qMin(500, engine->getAudioWordsCount() - audioIdx - currentWindow);

		int enUsed = 0;
		int audioUsed = 0;

		for (int offset = 0; offset <= maxSearch; ++offset) {

			double score = 
				//similarity(enIdx, audioIdx + offset, currentWindow, enUsed, audioUsed);
				similarity1(enIdx, audioIdx + offset, currentWindow);
			enUsed = audioUsed = currentWindow;

			// отладка
			qDebug() << QString::asprintf("#%03d: %.5f", offset, score) << debugAudioWords(engine, audioIdx + offset, currentWindow);

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
				engine->flushPendingGroup(enIdx, audioIdx, bestOffset);
				audioIdx += bestOffset;
			}

			// Привязываем совпавшую группу
			engine->assignMatchedGroup(enIdx, audioIdx, currentWindow, currentWindow);
			
			enIdx += enUsed;
			audioIdx += audioUsed;
		}
		else {
			// Не нашли совпадение — пропускаем английское слово
			enIdx++;
		}
	}

	// Оставшиеся аудио слова в конце
	if (audioIdx < engine->getAudioWordsCount()) {
		engine->flushPendingGroup(enIdx, audioIdx, engine->getAudioWordsCount() - audioIdx);
	}
}

MatchResult MyAligner::similarityRecursive(int enStart, int audioStart, int currDepth, int minDepth)
{
	// Базовый случай: вышли за границы
	if (enStart >= engine->getSourceWordsCount() || audioStart >= engine->getAudioWordsCount()) {
		return { 0.0, 0, 0 };
	}

	// Базовый случай: достигли максимальной глубины
	if (currDepth <= minDepth) {
		return { 0.0, 0, 0 };
	}

	const QString& enWord = engine->getSourceWord(enStart);
	const QString& audioWord = engine->getAudioWord(audioStart);

	// debug
	if (audioWord == "1")
		enStart += 0;

	// Вариант 1: слова совпадают
	if (enWord == audioWord) {
		// рекурсивно вызываем для следующей пары слов
		MatchResult next = similarityRecursive(enStart + 1, audioStart + 1, currDepth - 1, minDepth);
		// возвращаем результат
		return { 1.0 + next.matches, 1 + next.enTotal, 1 + next.audioTotal };
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

double MyAligner::similarity(int enStart, int audioStart, int N, int& enUsed, int& audioUsed)
{
	int M = N - 2;// *2 / 3;
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

double MyAligner::similarity1(int enStart, int audioStart, int N)
{
	// Проверка границ
	if (enStart + N > engine->getSourceWordsCount() || audioStart + N > engine->getAudioWordsCount()) {
		return 0.0;
	}

	double totalScore = 0.0;

	// Для каждого английского слова ищем наилучшее совпадение в аудио фрагменте
	// с учётом позиции (штраф за расстояние)
	for (int enIdx = 0; enIdx < N; ++enIdx) {
		const QString& enWord = engine->getSourceWord(enStart + enIdx);
		if (enWord.isEmpty()) continue;

		double bestWordScore = 0.0;
		int bestPos = -1;

		// Ищем это слово среди аудио слов в диапазоне
		for (int audioIdx = 0; audioIdx < N; ++audioIdx) {
			QString audioWord = engine->getAudioWord(audioStart + audioIdx).toLower();

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

double MyAligner::similarity3(int enStart, int audioStart, int N)
{
	// Проверка границ
	if (enStart + N > engine->getSourceWordsCount() || audioStart + N > engine->getAudioWordsCount()) {
		return 0.0;
	}

	int matches = 0;

	for (int i = 0; i < N; ++i) {
		const QString& enWord = engine->getSourceWord(enStart + i);

		// Очищаем аудио слово от знаков препинания
		QString audioWord = engine->getAudioWord(audioStart + i).toLower();

		if (enWord == audioWord) {
			matches++;
		}
	}

	return (double)matches / N;
}