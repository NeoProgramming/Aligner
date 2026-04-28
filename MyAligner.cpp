#include "MyAligner.h"
#include "tools.h"
#include <QDebug>

void MyAligner::align(IAlignmentEngine* alignerEngine)
{
	int enIdx = 0;
	int audioIdx = 0;
		
	const double THRESHOLD = 0.8;

	int pendingStartMs = -1;
	int pendingEndMs = -1;

	engine = alignerEngine;
	
	while (enIdx < engine->getSourceWordsCount()) {
		int currentWindow = qMin(WINDOW_SIZE, engine->getSourceWordsCount() - enIdx);

		// выводим "EN" строку
		qDebug() << debugEnWords(engine, enIdx, currentWindow);

		int bestOffset = -1;
		double bestScore = 0.0;
		int maxSearch = qMin(500, engine->getAudioWordsCount() - audioIdx - currentWindow);

		int enUsed = 0;
		int audioUsed = 0;

		for (int offset = 0; offset <= maxSearch; ++offset) {

			double score = 0;
			MatchResult mr;
			//score = similarity1(enIdx, audioIdx + offset, currentWindow);
			//score = similarity2(enIdx, audioIdx + offset, currentWindow);
			//mr = similarity3(enIdx, audioIdx + offset);
			mr = similarityDP(enIdx, audioIdx + offset);
				
			score = mr.similarity;
			enUsed = mr.usedEn;
			audioUsed = mr.usedAudio;

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
			engine->assignMatchedGroup(enIdx, currentWindow, audioIdx, currentWindow);
			
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

MatchResult MyAligner::similarityDP(int enStart, int audioStart)
{
	MatchResult result;

	// 1. Проверяем границы
	int totalEn = engine->getSourceWordsCount();
	int totalAudio = engine->getAudioWordsCount();

	if (enStart >= totalEn || audioStart >= totalAudio) {
		result.similarity = 0.0;
		result.usedEn = 0;
		result.usedAudio = 0;
		return result;
	}

	// 2. Определяем реальные размеры окон (не выходим за границы)
	int enSize = qMin(WINDOW_SIZE, totalEn - enStart);
	int audioSize = qMin(WINDOW_SIZE, totalAudio - audioStart);

	// 3. Инициализируем таблицу DP большими числами
	const int INF = 1000;
	for (int i = 0; i <= enSize; ++i) {
		for (int j = 0; j <= audioSize; ++j) {
			DP[i][j] = INF;
		}
	}

	// 4. Базовые случаи (нулевая строка и нулевой столбец)
	DP[0][0] = 0;

	// Превращаем пустое в audio слова (вставки)
	for (int j = 1; j <= audioSize; ++j) {
		DP[0][j] = j;  // нужно j вставок
	}

	// Превращаем en слова в пустое (удаления)
	for (int i = 1; i <= enSize; ++i) {
		DP[i][0] = i;  // нужно i удалений
	}

	// 5. Заполняем таблицу
	for (int i = 1; i <= enSize; ++i) {
		for (int j = 1; j <= audioSize; ++j) {
			// Получаем слова по индексам
			// engine->getSourceWord(enStart + i - 1) — i-1 потому что в DP i=1 соответствует первому слову
			QString enWord = engine->getSourceWord(enStart + i - 1);
			QString audioWord = engine->getAudioWord(audioStart + j - 1);

			// Стоимость замены (0 если слова равны, 1 если разные)
			int cost = (enWord == audioWord) ? 0 : 1;

			// Три варианта:
			// 1. Замена/совпадение
			int replaceCost = DP[i - 1][j - 1] + cost;

			// 2. Удаление (слово в en лишнее)
			int deleteCost = DP[i - 1][j] + 1;

			// 3. Вставка (слово в audio лишнее)
			int insertCost = DP[i][j - 1] + 1;

			// Берём минимум
			DP[i][j] = qMin(replaceCost, qMin(deleteCost, insertCost));
		}
	}

	// 6. Получаем минимальное расстояние
	int minDistance = DP[enSize][audioSize];
	
	// 7. Вычисляем similarity (от 0 до 1)
	int maxLength = qMax(enSize, audioSize);
	if (maxLength > 0) {
		result.similarity = 1.0 - (double)minDistance / maxLength;
	}
	else {
		result.similarity = 1.0;  // обе группы пустые
	}

	result.usedEn = enSize;
	result.usedAudio = audioSize;

	return result;
}

MatchResult MyAligner::similarityDPB(int enStart, int audioStart)
{
	MatchResult result;

	int totalEn = engine->getSourceWordsCount();
	int totalAudio = engine->getAudioWordsCount();

	if (enStart >= totalEn || audioStart >= totalAudio) {
		result.similarity = 0.0;
		result.usedEn = 0;
		result.usedAudio = 0;
		return result;
	}

	int enSize = qMin(WINDOW_SIZE, totalEn - enStart);
	int audioSize = qMin(WINDOW_SIZE, totalAudio - audioStart);

	// Максимальная допустимая разница в длинах (band width)
	const int MAX_DIFF = 5;

	// Если разница слишком большая, группы не могут хорошо соответствовать
	if (abs(enSize - audioSize) > MAX_DIFF) {
		result.similarity = 0.0;
		result.usedEn = enSize;
		result.usedAudio = audioSize;
		return result;
	}

	const int INF = 1000;

	// Инициализируем таблицу
	for (int i = 0; i <= enSize; ++i) {
		for (int j = 0; j <= audioSize; ++j) {
			DP[i][j] = INF;
		}
	}

	DP[0][0] = 0;

	// Инициализация границ внутри полосы
	for (int j = 1; j <= audioSize && j <= MAX_DIFF; ++j) {
		DP[0][j] = j;
	}

	for (int i = 1; i <= enSize && i <= MAX_DIFF; ++i) {
		DP[i][0] = i;
	}

	// Заполняем только внутри полосы |i - j| <= MAX_DIFF
	for (int i = 1; i <= enSize; ++i) {
		int j_start = qMax(1, i - MAX_DIFF);
		int j_end = qMin(audioSize, i + MAX_DIFF);

		for (int j = j_start; j <= j_end; ++j) {
			QString enWord = engine->getSourceWord(enStart + i - 1);
			QString audioWord = engine->getAudioWord(audioStart + j - 1);

			int cost = (enWord == audioWord) ? 0 : 1;

			// Проверяем, что предыдущие клетки существуют
			int replaceCost = (i - 1 >= 0 && j - 1 >= 0) ? DP[i - 1][j - 1] + cost : INF;
			int deleteCost = (i - 1 >= 0) ? DP[i - 1][j] + 1 : INF;
			int insertCost = (j - 1 >= 0) ? DP[i][j - 1] + 1 : INF;

			// Если клетка вне полосы, она INF, так что не повлияет
			DP[i][j] = qMin(replaceCost, qMin(deleteCost, insertCost));
		}
	}

	// Если последняя клетка не заполнена (INF), значит выравнивание невозможно
	if (DP[enSize][audioSize] >= INF / 2) {
		result.similarity = 0.0;
		result.usedEn = enSize;
		result.usedAudio = audioSize;
		return result;
	}

	int minDistance = DP[enSize][audioSize];

	int maxLength = qMax(enSize, audioSize);
	if (maxLength > 0) {
		result.similarity = 1.0 - (double)minDistance / maxLength;
	}
	else {
		result.similarity = 1.0;
	}

	result.usedEn = enSize;
	result.usedAudio = audioSize;

	return result;
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
		return { 1.0 + next.similarity, 1 + next.usedEn, 1 + next.usedAudio };
	}

	// Вариант 2: пропускаем английское слово
	MatchResult skipEn = similarityRecursive(enStart + 1, audioStart, currDepth - 1, minDepth);
	skipEn.similarity += 0;
	skipEn.usedEn += 1;
	skipEn.usedAudio += 0;

	// Вариант 3: пропускаем аудио слово
	MatchResult skipAudio = similarityRecursive(enStart, audioStart + 1, currDepth - 1, minDepth);
	skipAudio.similarity += 0;
	skipAudio.usedEn += 0;
	skipAudio.usedAudio += 1;

	// Вариант 4: пропускаем оба слова
	MatchResult skipBoth = similarityRecursive(enStart + 1, audioStart + 1, currDepth - 1, minDepth);
	skipBoth.similarity += 0;
	skipBoth.usedEn += 1;
	skipBoth.usedAudio += 1;


	// Выбираем лучший вариант (по score)
	if (skipEn.similarity >= skipAudio.similarity  && skipEn.similarity >= skipBoth.similarity) {
		return { skipEn.similarity , skipEn.usedEn, skipEn.usedAudio };
	}
	else if (skipAudio.similarity >= skipBoth.similarity) {
		return { skipAudio.similarity , skipAudio.usedEn, skipAudio.usedAudio };
	}
	else {
		return { skipBoth.similarity , skipBoth.usedEn, skipBoth.usedAudio };
	}
}

MatchResult MyAligner::similarity3(int enStart, int audioStart, int N)
{
	int M = N - 2;// *2 / 3;
	MatchResult result = similarityRecursive(enStart, audioStart, N, M);

	if (result.usedEn < M || result.usedAudio < M) {
		return { 0.0, 0, 0 };
	}

	// нормализуем
	int m = qMax(result.usedEn, result.usedAudio);
	if (m == 0)
		result.similarity = 0;
	result.similarity /= m;

	return result;
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

double MyAligner::similarity2(int enStart, int audioStart, int N)
{
	// Проверка границ
	if (enStart + N > engine->getSourceWordsCount() || audioStart + N > engine->getAudioWordsCount()) {
		return 0.0;
	}

	int similarity = 0;

	for (int i = 0; i < N; ++i) {
		const QString& enWord = engine->getSourceWord(enStart + i);

		// Очищаем аудио слово от знаков препинания
		QString audioWord = engine->getAudioWord(audioStart + i).toLower();

		if (enWord == audioWord) {
			similarity++;
		}
	}

	return (double)similarity / N;
}