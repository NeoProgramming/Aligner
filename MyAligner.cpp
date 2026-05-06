#include "MyAligner.h"
#include "tools.h"
#include <QDebug>

void MyAligner::align(IAlignmentEngine* alignerEngine)
{
	int enIdx = 0;
	int audioIdx = 0;
		
	const double THRESHOLD = 0.8;

	engine = alignerEngine;
		
	while (enIdx < engine->getSourceWordsCount()) {
		
		// индекс предложения для отладки; здесь 
		int sentIdx = engine->getSourceSentence(enIdx);
		QString sentStr = QString::asprintf("S=%d", sentIdx);
		
		// размер исходного окна
		int maxSrc = sourceWindowSize(enIdx);
		
		// выводим "SOURCE" строку
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

			MatchResult mr = similarity(enIdx, maxSrc, audioIdx + offset, maxAud);

			// отладка
			qDebug() << sentStr << QString::asprintf("OFFS=%03d: %.5f", offset, mr.score) << debugAudioWords(engine, audioIdx + offset, maxAud);

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
						
			qDebug() << "MATCH! BestOffset=" << bestOffset << "UsedSrc=" << bestMR.usedSource << "UsedAudio=" << bestMR.usedAudio;

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

MatchResult MyAligner::similarity(int enStart, int maxSrc, int audioStart, int maxAud)
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
	int bestJ = maxAud;

	double bestScore = -1e9;
	//@int minAud = std::min(WINDOW_SIZE, maxAud);
	//@for (int j = minAud; j <= maxAud; ++j) {
	for (int j = 0; j <= maxAud; ++j) {
		double norm = dp[maxSrc][j].score / std::max(maxSrc, j);

		if (norm > bestScore) {
			bestScore = norm;
			bestJ = j;
		}
	}
		

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



