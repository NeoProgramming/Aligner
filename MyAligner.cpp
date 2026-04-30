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
	
	int enCount = 0;// отладочный счетчик
	while (enIdx < engine->getSourceWordsCount()) {
		
		// индекс предложения для отладки; здесь 
		int sentIdx = engine->getSourceSentence(enIdx);
		QString sentStr = QString::asprintf("#%d S=%d", enCount, sentIdx);
		enCount++;

		// размер исходного окна
		int maxSrc = sourceWindowSize(enIdx);
		
		// выводим "EN" строку
		qDebug() << sentStr << "MS=" << maxSrc << debugEnWords(engine, enIdx, maxSrc);

		int bestOffset = -1;
		double bestScore = 0.0;
		MatchResult bestMR;
		int maxSearch = qMin(500, engine->getAudioWordsCount() - audioIdx - WINDOW_SIZE);

		if (sentIdx == 43)
			sentIdx = sentIdx;
		
		// двигаем окно в некоторых пределах
		for (int offset = 0; offset <= maxSearch; ++offset) {
		
			// размер аудио окна
			int maxAud = audioWindowSize(audioIdx + offset, maxSrc);

			MatchResult mr = similarityCG(enIdx, maxSrc, audioIdx + offset, maxAud);

			// отладка
			qDebug() << sentStr << QString::asprintf("W=%03d: %.5f", offset, mr.score) << debugAudioWords(engine, audioIdx + offset, maxAud);

			// если текущее значение лучше - делаем его "наилучшим"
			if (mr.score > bestScore) {
				bestScore = mr.score;
				bestMR = mr;
				bestOffset = offset;				
			}
			// иначе, если текущее хуже_или_такое_же и наилучшее выше порога - считаем что нашли локальный оптимум
			else if (bestScore > THRESHOLD)
				break;
			// если  текущее строго 1 - идеальное совпадение
			if (mr.score == 1.0)
				break;
		}// конец цикла поиска наилучшего совпадения окна

		// если удалось найти какое-то совпадение групп слов
		if (bestOffset >= 0 && bestScore >= THRESHOLD) {
						
			qDebug() << "MATCH! BO=" << bestOffset << "UE=" << bestMR.usedSource << "UA=" << bestMR.usedAudio;

			// Есть неподошедшие аудио слова перед совпадением? (из-за движения окна)
			if (bestOffset > 0) {
				engine->flushPendingGroup(enIdx, audioIdx, bestOffset);
				audioIdx += bestOffset;
			}

			// Привязываем совпавшую группу
			engine->assignMatchedGroup(enIdx, bestMR.usedSource, audioIdx, bestMR.usedAudio);
			
			enIdx += bestMR.usedSource;
			audioIdx += bestMR.usedAudio;
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

int MyAligner::sourceWindowSize(int srcStart)
{
	// определить размер исходного окна, опираясь на предложения
	int s0 = engine->getSourceSentence(srcStart);
	int n = engine->getSourceWordsCount();
	for (int i = srcStart; i < n; i++) {
		int s = engine->getSourceSentence(i);
		// разрыв только в точке перехода между предложениями
		if (s != s0 && i - srcStart >= WINDOW_SIZE)
			return i - srcStart;
		s0 = s;
	}
	return n - srcStart;
}

int MyAligner::audioWindowSize(int audioStart, int sourceWindow)
{
	// определить размер аудио окна, "с запасом" на то что в аудио есть вставки
	int s = qMin((int)(sourceWindow * 1.3), engine->getAudioWordsCount() - audioStart);
	return s;
}

MatchResult MyAligner::similarityDP(int enStart, int audioStart)
{
	MatchResult result;

	// 1. Проверяем границы
	int totalEn = engine->getSourceWordsCount();
	int totalAudio = engine->getAudioWordsCount();

	if (enStart >= totalEn || audioStart >= totalAudio) {
		result.score = 0.0;
		result.usedSource = 0;
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
			//int cost = (enWord == audioWord) ? 0 : 1;
			int cost = equ(enWord, audioWord) ? 0 : 1;

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
		result.score = 1.0 - (double)minDistance / maxLength;
	}
	else {
		result.score = 1.0;  // обе группы пустые
	}

	result.usedSource = enSize;
	result.usedAudio = audioSize;

	return result;
}

MatchResult MyAligner::similarityDPB(int enStart, int audioStart)
{
	MatchResult result;

	int totalEn = engine->getSourceWordsCount();
	int totalAudio = engine->getAudioWordsCount();

	if (enStart >= totalEn || audioStart >= totalAudio) {
		result.score = 0.0;
		result.usedSource = 0;
		result.usedAudio = 0;
		return result;
	}

	int enSize = qMin(WINDOW_SIZE, totalEn - enStart);
	int audioSize = qMin(WINDOW_SIZE, totalAudio - audioStart);

	// Максимальная допустимая разница в длинах (band width)
	const int MAX_DIFF = 5;

	// Если разница слишком большая, группы не могут хорошо соответствовать
	if (abs(enSize - audioSize) > MAX_DIFF) {
		result.score = 0.0;
		result.usedSource = enSize;
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
		result.score = 0.0;
		result.usedSource = enSize;
		result.usedAudio = audioSize;
		return result;
	}

	int minDistance = DP[enSize][audioSize];

	int maxLength = qMax(enSize, audioSize);
	if (maxLength > 0) {
		result.score = 1.0 - (double)minDistance / maxLength;
	}
	else {
		result.score = 1.0;
	}

	result.usedSource = enSize;
	result.usedAudio = audioSize;

	return result;
}

inline double wordSimilarity(const QString& a, const QString& b) {

	return equ(a, b);

	if (a == b) return 1.0;
	if (a.size() < 3 || b.size() < 3) return 0.0;

	// простая эвристика — общий префикс
	int common = 0;
	int len = std::min(a.size(), b.size());
	for (int i = 0; i < len; ++i) {
		if (a[i] != b[i]) break;
		common++;
	}
	return double(common) / std::max(a.size(), b.size());
}

MatchResult MyAligner::similarityCG(int enStart, int maxSrc, int audioStart, int maxAud)
{
	int srcCount = engine->getSourceWordsCount();
	int audCount = engine->getAudioWordsCount();
	
	struct Cell {
		double score = 0.0;
		int prev_i = -1;
		int prev_j = -1;
	};

	QVector<QVector<Cell>> dp(maxSrc + 1, QVector<Cell>(maxAud + 1));

	const double GAP_PENALTY = -0.4;

	// заполнение DP
	for (int i = 0; i <= maxSrc; ++i) {
		for (int j = 0; j <= maxAud; ++j) {

			if (i == 0 && j == 0) continue;

			double best = -1e9;
			int pi = -1, pj = -1;

			// match
			if (i > 0 && j > 0) {
				double sim = wordSimilarity(
					engine->getSourceWord(enStart + i - 1),
					engine->getAudioWord(audioStart + j - 1)
				);

				double val = dp[i - 1][j - 1].score + sim;
				if (val > best) {
					best = val;
					pi = i - 1; pj = j - 1;
				}
			}

			// skip source
			if (i > 0) {
				double val = dp[i - 1][j].score + GAP_PENALTY;
				if (val > best) {
					best = val;
					pi = i - 1; pj = j;
				}
			}

			// skip audio
			if (j > 0) {
				double val = dp[i][j - 1].score + GAP_PENALTY;
				if (val > best) {
					best = val;
					pi = i; pj = j - 1;
				}
			}

			dp[i][j] = { best, pi, pj };
		}
	}

	int bestI = maxSrc;
	int bestJ = WINDOW_SIZE;

	double bestScore = -1e9;
	int minAud = std::min(WINDOW_SIZE, maxAud);
	for (int j = minAud; j <= maxAud; ++j) {
		double norm = dp[maxSrc][j].score / std::max(maxSrc, j);

		if (norm > bestScore) {
			bestScore = norm;
			bestJ = j;
		}
	}
	
	/*
	// выбираем лучший конец (но не меньше MIN_W)
	double bestScore = -1e9;
	int bestI = 0, bestJ = 0;

	int minSrc = std::min(WINDOW_SIZE, maxSrc);
	int minAud = std::min(WINDOW_SIZE, maxAud);

	for (int i = minSrc; i <= maxSrc; ++i) {
		for (int j = minAud; j <= maxAud; ++j) {

			double norm = dp[i][j].score / std::max(i, j);

			if (norm > bestScore ||
				(qFuzzyCompare(norm, bestScore) && std::max(i, j) > std::max(bestI, bestJ)))
			{
				bestScore = norm;
				bestI = i;
				bestJ = j;
			}
		}
	}
	*/

	// backtrack
	int i = bestI, j = bestJ;
	int usedSrc = 0;
	int usedAud = 0;

	while (i > 0 || j > 0) {
		Cell c = dp[i][j];
		if (c.prev_i == i - 1 && c.prev_j == j - 1) {
			usedSrc++;
			usedAud++;
		}
		else if (c.prev_i == i - 1) {
			usedSrc++;
		}
		else {
			usedAud++;
		}

		int ni = c.prev_i;
		int nj = c.prev_j;
		i = ni;
		j = nj;

		if (i < 0 || j < 0) break;
	}

	MatchResult res;
	res.score = std::max(0.0, bestScore);
	res.usedSource = usedSrc;
	res.usedAudio = usedAud;

	return res;
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
		return { 1.0 + next.score, 1 + next.usedSource, 1 + next.usedAudio };
	}

	// Вариант 2: пропускаем английское слово
	MatchResult skipEn = similarityRecursive(enStart + 1, audioStart, currDepth - 1, minDepth);
	skipEn.score += 0;
	skipEn.usedSource += 1;
	skipEn.usedAudio += 0;

	// Вариант 3: пропускаем аудио слово
	MatchResult skipAudio = similarityRecursive(enStart, audioStart + 1, currDepth - 1, minDepth);
	skipAudio.score += 0;
	skipAudio.usedSource += 0;
	skipAudio.usedAudio += 1;

	// Вариант 4: пропускаем оба слова
	MatchResult skipBoth = similarityRecursive(enStart + 1, audioStart + 1, currDepth - 1, minDepth);
	skipBoth.score += 0;
	skipBoth.usedSource += 1;
	skipBoth.usedAudio += 1;


	// Выбираем лучший вариант (по score)
	if (skipEn.score >= skipAudio.score  && skipEn.score >= skipBoth.score) {
		return { skipEn.score , skipEn.usedSource, skipEn.usedAudio };
	}
	else if (skipAudio.score >= skipBoth.score) {
		return { skipAudio.score , skipAudio.usedSource, skipAudio.usedAudio };
	}
	else {
		return { skipBoth.score , skipBoth.usedSource, skipBoth.usedAudio };
	}
}

MatchResult MyAligner::similarity3(int enStart, int audioStart, int N)
{
	int M = N - 2;// *2 / 3;
	MatchResult result = similarityRecursive(enStart, audioStart, N, M);

	if (result.usedSource < M || result.usedAudio < M) {
		return { 0.0, 0, 0 };
	}

	// нормализуем
	int m = qMax(result.usedSource, result.usedAudio);
	if (m == 0)
		result.score = 0;
	result.score /= m;

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

	int score = 0;

	for (int i = 0; i < N; ++i) {
		const QString& enWord = engine->getSourceWord(enStart + i);

		// Очищаем аудио слово от знаков препинания
		QString audioWord = engine->getAudioWord(audioStart + i).toLower();

		if (enWord == audioWord) {
			score++;
		}
	}

	return (double)score / N;
}