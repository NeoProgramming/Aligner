#include "AdvancedAligner1.h"
#include <QHash>
#include <QSet>

void AdvancedAligner1::align(IAlignmentEngine* alignerEngine) 
{
	engine = alignerEngine;

	int n = engine->getSourceWordsCount();
	int m = engine->getAudioWordsCount();

	// Используем локальное выравнивание с поиском якорей
	QVector<QPair<int, int>> anchors = findAnchors();

	if (anchors.isEmpty()) {
		// Нет якорей - используем глобальное выравнивание
		globalAlignment(0, n, 0, m);
		return;
	}

	// Выравниваем между якорями
	int prevSource = 0;
	int prevAudio = 0;

	for (const auto& anchor : anchors) {
		int sourceIdx = anchor.first;
		int audioIdx = anchor.second;

		// Выравниваем участок до якоря
		if (sourceIdx > prevSource || audioIdx > prevAudio) {
			alignSegment(prevSource, sourceIdx - prevSource,
				prevAudio, audioIdx - prevAudio);
		}

		// Добавляем сам якорь как группу из 1 слова
		engine->assignMatchedGroup(sourceIdx, 1, audioIdx, 1);

		prevSource = sourceIdx + 1;
		prevAudio = audioIdx + 1;
	}

	// Выравниваем хвост
	if (prevSource < n || prevAudio < m) {
		alignSegment(prevSource, n - prevSource,
			prevAudio, m - prevAudio);
	}
}

QVector<QPair<int, int>> AdvancedAligner1::findAnchors() 
{
	QVector<QPair<int, int>> anchors;

	int n = engine->getSourceWordsCount();
	int m = engine->getAudioWordsCount();

	// Строим частотный словарь исходных слов
	QHash<QString, int> sourceFreq;
	for (int i = 0; i < n; i++) {
		sourceFreq[engine->getSourceWord(i)]++;
	}

	// Ищем редкие слова как потенциальные якоря
	QSet<QString> rareWords;
	for (auto it = sourceFreq.begin(); it != sourceFreq.end(); ++it) {
		if (it.value() <= 2 && it.key().length() > 3) {
			rareWords.insert(it.key());
		}
	}

	// Ищем совпадения
	for (int i = 0; i < m; i++) {
		const QString& audioWord = engine->getAudioWord(i);

		if (rareWords.contains(audioWord)) {
			// Ищем позицию в исходном тексте
			for (int j = 0; j < n; j++) {
				if (engine->getSourceWord(j) == audioWord) {
					// Проверяем контекст
					if (verifyAnchor(j, i)) {
						anchors.append({ j, i });
						break;
					}
				}
			}
		}
	}

	// Сортируем и удаляем конфликтующие якоря
	std::sort(anchors.begin(), anchors.end());

	QVector<QPair<int, int>> filtered;
	for (const auto& anchor : anchors) {
		if (filtered.isEmpty() ||
			(anchor.first > filtered.last().first &&
				anchor.second > filtered.last().second)) {
			filtered.append(anchor);
		}
	}

	return filtered;
}

bool AdvancedAligner1::verifyAnchor(int sourceIdx, int audioIdx) 
{
	// Проверяем контекст из 3 слов до и после
	int contextSize = 3;
	int matches = 0;
	int checks = 0;

	for (int i = -contextSize; i <= contextSize; i++) {
		if (i == 0) continue;

		int s = sourceIdx + i;
		int a = audioIdx + i;

		if (s >= 0 && s < engine->getSourceWordsCount() &&
			a >= 0 && a < engine->getAudioWordsCount()) {
			if (engine->getSourceWord(s) == engine->getAudioWord(a)) {
				matches++;
			}
			checks++;
		}
	}

	return checks > 0 && (double)matches / checks > 0.5;
}

void AdvancedAligner1::alignSegment(int sourceStart, int sourceCount,
	int audioStart, int audioCount) 
{
	if (sourceCount == 0 && audioCount == 0) return;

	// Если одна из сторон пуста - это отложенные группы
	if (sourceCount == 0) {
		engine->flushPendingGroup(-1, audioStart, audioCount);
		return;
	}

	if (audioCount == 0) {
		// Пропускаем исходные слова без аудио
		return;
	}

	// Для небольших сегментов используем точное ДП
	if (sourceCount <= 100 && audioCount <= 200) {
		preciseAlignment(sourceStart, sourceCount, audioStart, audioCount);
	}
	else {
		// Для больших - используем приближенный алгоритм
		approximateAlignment(sourceStart, sourceCount, audioStart, audioCount);
	}
}

void AdvancedAligner1::preciseAlignment(int sourceStart, int sourceCount,
	int audioStart, int audioCount) 
{
	int n = sourceCount;
	int m = audioCount;

	// Создаем матрицу
	QVector<QVector<int>> dp(n + 1, QVector<int>(m + 1, 0));
	QVector<QVector<char>> backtrack(n + 1, QVector<char>(m + 1, 0));

	for (int i = 1; i <= n; i++) {
		for (int j = 1; j <= m; j++) {
			const QString& sWord = engine->getSourceWord(sourceStart + i - 1);
			const QString& aWord = engine->getAudioWord(audioStart + j - 1);

			int diag = dp[i - 1][j - 1] + (sWord == aWord ? 2 : -1);
			int up = dp[i - 1][j] - 1;
			int left = dp[i][j - 1] - 1;

			if (diag >= up && diag >= left) {
				dp[i][j] = diag;
				backtrack[i][j] = 'D';
			}
			else if (up >= left) {
				dp[i][j] = up;
				backtrack[i][j] = 'U';
			}
			else {
				dp[i][j] = left;
				backtrack[i][j] = 'L';
			}
		}
	}

	// Восстанавливаем группы
	int i = n, j = m;
	while (i > 0 || j > 0) {
		if (i > 0 && j > 0 && backtrack[i][j] == 'D') {
			const QString& sWord = engine->getSourceWord(sourceStart + i - 1);
			const QString& aWord = engine->getAudioWord(audioStart + j - 1);

			// Ищем непрерывную группу совпадений
			int groupLen = 1;
			while (i - groupLen > 0 && j - groupLen > 0 &&
				backtrack[i - groupLen][j - groupLen] == 'D' &&
				engine->getSourceWord(sourceStart + i - groupLen - 1) ==
				engine->getAudioWord(audioStart + j - groupLen - 1)) {
				groupLen++;
			}

			engine->assignMatchedGroup(
				sourceStart + i - groupLen, groupLen,
				audioStart + j - groupLen, groupLen
			);

			i -= groupLen;
			j -= groupLen;
		}
		else if (i > 0 && (j == 0 || backtrack[i][j] == 'U')) {
			// Пропуск исходного слова - ничего не делаем
			i--;
		}
		else if (j > 0) {
			// Пропуск аудио слова - сохраняем как отложенное
			int gapStart = j - 1;
			int gapLen = 1;

			while (j - gapLen > 0 && (i == 0 || backtrack[i][j - gapLen] == 'L')) {
				gapLen++;
			}

			engine->flushPendingGroup(-1, audioStart + gapStart - gapLen + 1, gapLen);
			j -= gapLen;
		}
	}
}

void AdvancedAligner1::approximateAlignment(int sourceStart, int sourceCount,
	int audioStart, int audioCount) 
{
	// Для больших сегментов используем скользящее окно
	const int WINDOW_SIZE = 50;
	const int STEP = 25;

	for (int s = sourceStart; s < sourceStart + sourceCount; s += STEP) {
		int sEnd = qMin(s + WINDOW_SIZE, sourceStart + sourceCount);

		// Ищем лучшее совпадение в аудио
		int bestAudioPos = -1;
		int bestMatches = 0;

		for (int a = audioStart; a <= audioStart + audioCount - WINDOW_SIZE; a += STEP) {
			int matches = 0;
			for (int k = 0; k < WINDOW_SIZE && s + k < sEnd && a + k < audioStart + audioCount; k++) {
				if (engine->getSourceWord(s + k) == engine->getAudioWord(a + k)) {
					matches++;
				}
			}

			if (matches > bestMatches) {
				bestMatches = matches;
				bestAudioPos = a;
			}
		}

		if (bestMatches > WINDOW_SIZE * 0.6) {
			// Нашли хорошее совпадение
			alignSegment(s, sEnd - s, bestAudioPos, WINDOW_SIZE);
		}
	}
}

void AdvancedAligner1::globalAlignment(int sourceStart, int sourceCount,
	int audioStart, int audioCount) 
{
	// Fallback: простое последовательное сравнение
	int s = sourceStart;
	int a = audioStart;
	int sourceEnd = sourceStart + sourceCount;
	int audioEnd = audioStart + audioCount;

	while (s < sourceEnd && a < audioEnd) {
		if (engine->getSourceWord(s) == engine->getAudioWord(a)) {
			// Нашли совпадение - ищем группу
			int groupLen = 1;
			while (s + groupLen < sourceEnd && a + groupLen < audioEnd &&
				engine->getSourceWord(s + groupLen) == engine->getAudioWord(a + groupLen)) {
				groupLen++;
			}

			engine->assignMatchedGroup(s, groupLen, a, groupLen);
			s += groupLen;
			a += groupLen;
		}
		else {
			// Пропускаем аудио слово как отложенное
			engine->flushPendingGroup(-1, a, 1);
			a++;
		}
	}

	// Оставшиеся аудио слова
	if (a < audioEnd) {
		engine->flushPendingGroup(-1, a, audioEnd - a);
	}
}
