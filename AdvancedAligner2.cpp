#include "AdvancedAligner2.h"

#include "alignment.h"
#include <QDebug>
#include <algorithm>

AdvancedAligner::AdvancedAligner()
	: m_engine(nullptr)
	, m_matchScore(2)
	, m_mismatchScore(-1)
	, m_gapScore(-1)
	, m_maxContextSize(3)
	, m_rareWordThreshold(2)
	, m_minSequenceLength(3)
	, m_windowSize(50)
	, m_stepSize(25)
{
}

AdvancedAligner::~AdvancedAligner()
{
}

void AdvancedAligner::setMatchScore(int score)
{
	m_matchScore = score;
}

void AdvancedAligner::setMismatchScore(int score)
{
	m_mismatchScore = score;
}

void AdvancedAligner::setGapScore(int score)
{
	m_gapScore = score;
}

void AdvancedAligner::setMaxContextSize(int size)
{
	m_maxContextSize = size;
}

void AdvancedAligner::setRareWordThreshold(int threshold)
{
	m_rareWordThreshold = threshold;
}

void AdvancedAligner::setMinSequenceLength(int length)
{
	m_minSequenceLength = length;
}

void AdvancedAligner::align(IAlignmentEngine* engine)
{
	if (!engine) {
		qWarning() << "AdvancedAligner::align - engine is null";
		return;
	}

	m_engine = engine;

	int n = m_engine->getSourceWordsCount();
	int m = m_engine->getAudioWordsCount();

	if (n == 0 || m == 0) {
		qWarning() << "AdvancedAligner::align - empty word arrays";
		return;
	}

	// Находим якоря (только те, что являются частью последовательностей)
	QVector<MatchAnchor> anchors = findAnchors();
	if (anchors.isEmpty()) {
		// Нет надежных якорей - используем глобальное выравнивание
		globalAlignment(0, n, 0, m);
		return;
	}

	// Находим первый валидный якорь (начало последовательности)
	int firstValidIdx = findFirstValidAnchor(anchors);
	if (firstValidIdx == -1) {
		globalAlignment(0, n, 0, m);
		return;
	}

	// Берем якоря начиная с первого валидного
	QVector<MatchAnchor> validAnchors = anchors.mid(firstValidIdx);

	// Обрабатываем начало (все, что до первого валидного якоря - преамбула)
	int firstSourceIdx = validAnchors.first().sourceIndex;
	int firstAudioIdx = validAnchors.first().audioIndex;

	if (firstAudioIdx > 0) {
		// Все слова до первого надежного якоря - это преамбула
		m_engine->flushPendingGroup(0, 0, firstAudioIdx);
	}

	// Выравниваем между якорями
	int prevSource = 0;
	int prevAudio = 0;

	for (const auto& anchor : validAnchors) {
		int sourceIdx = anchor.sourceIndex;
		int audioIdx = anchor.audioIndex;

		// Выравниваем участок до якоря
		if (sourceIdx > prevSource || audioIdx > prevAudio) {
			alignSegment(prevSource, sourceIdx - prevSource,
				prevAudio, audioIdx - prevAudio);
		}

		// Добавляем сам якорь как группу из 1 слова
		m_engine->assignMatchedGroup(sourceIdx, 1, audioIdx, 1);

		prevSource = sourceIdx + 1;
		prevAudio = audioIdx + 1;
	}

	// Обрабатываем хвост
	if (prevSource < n || prevAudio < m) {
		if (prevSource < n && prevAudio < m) {
			alignSegment(prevSource, n - prevSource,
				prevAudio, m - prevAudio);
		}
		else if (prevAudio < m) {
			m_engine->flushPendingGroup(n, prevAudio, m - prevAudio);
		}
	}
}

QVector<AdvancedAligner::MatchAnchor> AdvancedAligner::findAnchors()
{
	QVector<MatchAnchor> candidates;

	int n = m_engine->getSourceWordsCount();
	int m = m_engine->getAudioWordsCount();

	// Строим частотный словарь исходных слов
	QHash<QString, int> sourceFreq;
	for (int i = 0; i < n; i++) {
		sourceFreq[m_engine->getSourceWord(i)]++;
	}

	// Проходим по аудио словам
	for (int audioIdx = 0; audioIdx < m; audioIdx++) {
		const QString& audioWord = m_engine->getAudioWord(audioIdx);

		// Проверяем только редкие слова (частота <= порога и длина > 3)
		if (sourceFreq.value(audioWord, 0) > m_rareWordThreshold) {
			continue;
		}

		if (audioWord.length() <= 3) {
			continue;
		}

		// Ищем это слово в исходном тексте
		for (int sourceIdx = 0; sourceIdx < n; sourceIdx++) {
			if (m_engine->getSourceWord(sourceIdx) == audioWord) {
				// Проверяем, является ли это совпадение частью последовательности
				if (isPartOfSequence(sourceIdx, audioIdx)) {
					MatchAnchor anchor;
					anchor.sourceIndex = sourceIdx;
					anchor.audioIndex = audioIdx;
					candidates.append(anchor);
					break; // берем первое вхождение
				}
			}
		}
	}

	// Сортируем по аудио индексу
	std::sort(candidates.begin(), candidates.end(),
		[](const MatchAnchor& a, const MatchAnchor& b) {
		return a.audioIndex < b.audioIndex;
	});

	// Фильтруем - оставляем только монотонно возрастающие
	QVector<MatchAnchor> anchors;
	for (const auto& candidate : candidates) {
		if (anchors.isEmpty()) {
			anchors.append(candidate);
			continue;
		}

		const auto& last = anchors.last();
		if (candidate.sourceIndex > last.sourceIndex &&
			candidate.audioIndex > last.audioIndex) {
			anchors.append(candidate);
		}
	}

	return anchors;
}

bool AdvancedAligner::isPartOfSequence(int sourceIdx, int audioIdx)
{
	// Проверяем, есть ли m_minSequenceLength совпадающих слов подряд
	int forwardMatch = 0;

	// Считаем совпадения вперед
	for (int k = 0; k < m_minSequenceLength; k++) {
		int s = sourceIdx + k;
		int a = audioIdx + k;

		if (s >= m_engine->getSourceWordsCount() ||
			a >= m_engine->getAudioWordsCount()) {
			break;
		}

		if (m_engine->getSourceWord(s) == m_engine->getAudioWord(a)) {
			forwardMatch++;
		}
		else {
			break;
		}
	}

	if (forwardMatch >= m_minSequenceLength) {
		return true;
	}

	// Проверяем, не является ли это слово частью последовательности,
	// которая началась раньше (смотрим назад)
	int backwardMatch = 1; // текущее слово считаем совпавшим
	for (int k = 1; k < m_minSequenceLength; k++) {
		int s = sourceIdx - k;
		int a = audioIdx - k;

		if (s < 0 || a < 0) {
			break;
		}

		if (m_engine->getSourceWord(s) == m_engine->getAudioWord(a)) {
			backwardMatch++;
		}
		else {
			break;
		}
	}

	// Если сзади есть достаточно совпадений, и текущее + сзади дают нужную длину
	if (backwardMatch >= m_minSequenceLength) {
		return true;
	}

	// Комбинированная проверка: часть спереди + текущее + часть сзади
	if (backwardMatch + forwardMatch - 1 >= m_minSequenceLength) {
		return true;
	}

	return false;
}

int AdvancedAligner::findFirstValidAnchor(const QVector<MatchAnchor>& anchors)
{
	if (anchors.isEmpty()) {
		return -1;
	}

	// Ищем первый якорь, после которого есть последовательность якорей
	for (int i = 0; i <= anchors.size() - m_minSequenceLength; i++) {
		bool isSequence = true;

		// Проверяем, что следующие якоря идут с примерно постоянным интервалом
		for (int k = i + 1; k < i + m_minSequenceLength; k++) {
			int sourceGap = anchors[k].sourceIndex - anchors[k - 1].sourceIndex;
			int audioGap = anchors[k].audioIndex - anchors[k - 1].audioIndex;

			// Интервалы должны быть примерно равны (разница не более 2)
			if (abs(sourceGap - audioGap) > 2) {
				isSequence = false;
				break;
			}
		}

		if (isSequence) {
			return i;
		}
	}

	return -1;
}

void AdvancedAligner::alignSegment(int sourceStart, int sourceCount,
	int audioStart, int audioCount)
{
	if (sourceCount == 0 && audioCount == 0) return;

	// Если нет исходных слов, но есть аудио - это вставка
	if (sourceCount == 0) {
		m_engine->flushPendingGroup(sourceStart, audioStart, audioCount);
		return;
	}

	// Если нет аудио слов, но есть исходные - просто пропускаем
	if (audioCount == 0) {
		return;
	}

	// Для небольших сегментов используем точное ДП
	if (sourceCount <= 100 && audioCount <= 200) {
		preciseAlignment(sourceStart, sourceCount, audioStart, audioCount);
	}
	else {
		approximateAlignment(sourceStart, sourceCount, audioStart, audioCount);
	}
}

void AdvancedAligner::preciseAlignment(int sourceStart, int sourceCount,
	int audioStart, int audioCount)
{
	int n = sourceCount;
	int m = audioCount;

	// Создаем матрицы
	QVector<QVector<int>> dp(n + 1, QVector<int>(m + 1, 0));
	QVector<QVector<char>> backtrack(n + 1, QVector<char>(m + 1, 0));

	// Инициализация
	for (int i = 1; i <= n; i++) {
		dp[i][0] = dp[i - 1][0] + m_gapScore;
		backtrack[i][0] = 'U';
	}
	for (int j = 1; j <= m; j++) {
		dp[0][j] = dp[0][j - 1] + m_gapScore;
		backtrack[0][j] = 'L';
	}

	// Заполняем матрицу
	for (int i = 1; i <= n; i++) {
		for (int j = 1; j <= m; j++) {
			const QString& sWord = m_engine->getSourceWord(sourceStart + i - 1);
			const QString& aWord = m_engine->getAudioWord(audioStart + j - 1);

			int match = (sWord == aWord) ? m_matchScore : m_mismatchScore;
			int diag = dp[i - 1][j - 1] + match;
			int up = dp[i - 1][j] + m_gapScore;
			int left = dp[i][j - 1] + m_gapScore;

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
			// Ищем непрерывную группу совпадений
			int groupLen = 1;
			while (i - groupLen > 0 && j - groupLen > 0 &&
				backtrack[i - groupLen][j - groupLen] == 'D') {
				const QString& sWord = m_engine->getSourceWord(sourceStart + i - groupLen - 1);
				const QString& aWord = m_engine->getAudioWord(audioStart + j - groupLen - 1);
				if (sWord == aWord) {
					groupLen++;
				}
				else {
					break;
				}
			}

			m_engine->assignMatchedGroup(
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
			// Пропуск аудио слова - это вставка
			int gapStart = j - 1;
			int gapLen = 1;

			while (j - gapLen > 0 && (i == 0 || backtrack[i][j - gapLen] == 'L')) {
				gapLen++;
			}

			int insertPosition = sourceStart + i;
			m_engine->flushPendingGroup(insertPosition, audioStart + gapStart - gapLen + 1, gapLen);

			j -= gapLen;
		}
	}
}

void AdvancedAligner::approximateAlignment(int sourceStart, int sourceCount,
	int audioStart, int audioCount)
{
	int processedAudio = 0;

	for (int s = sourceStart; s < sourceStart + sourceCount; s += m_stepSize) {
		int sEnd = qMin(s + m_windowSize, sourceStart + sourceCount);

		int bestAudioPos = -1;
		int bestMatches = 0;

		for (int a = audioStart + processedAudio; a <= audioStart + audioCount - m_windowSize; a += m_stepSize) {
			int score = 0;
			for (int k = 0; k < m_windowSize && s + k < sEnd && a + k < audioStart + audioCount; k++) {
				if (m_engine->getSourceWord(s + k) == m_engine->getAudioWord(a + k)) {
					score++;
				}
			}

			if (score > bestMatches) {
				bestMatches = score;
				bestAudioPos = a;
			}
		}

		if (bestMatches > m_windowSize * 0.6 && bestAudioPos > processedAudio) {
			if (bestAudioPos > processedAudio) {
				m_engine->flushPendingGroup(s, audioStart + processedAudio,
					bestAudioPos - processedAudio);
				processedAudio = bestAudioPos;
			}

			alignSegment(s, sEnd - s, audioStart + processedAudio, m_windowSize);
			processedAudio += m_windowSize;
		}
	}

	if (processedAudio < audioCount) {
		m_engine->flushPendingGroup(sourceStart + sourceCount,
			audioStart + processedAudio,
			audioCount - processedAudio);
	}
}

void AdvancedAligner::globalAlignment(int sourceStart, int sourceCount,
	int audioStart, int audioCount)
{
	int s = sourceStart;
	int a = audioStart;
	int sourceEnd = sourceStart + sourceCount;
	int audioEnd = audioStart + audioCount;

	while (s < sourceEnd && a < audioEnd) {
		if (m_engine->getSourceWord(s) == m_engine->getAudioWord(a)) {
			int groupLen = 1;
			while (s + groupLen < sourceEnd && a + groupLen < audioEnd &&
				m_engine->getSourceWord(s + groupLen) == m_engine->getAudioWord(a + groupLen)) {
				groupLen++;
			}

			m_engine->assignMatchedGroup(s, groupLen, a, groupLen);
			s += groupLen;
			a += groupLen;
		}
		else {
			m_engine->flushPendingGroup(s, a, 1);
			a++;
		}
	}

	if (a < audioEnd) {
		m_engine->flushPendingGroup(sourceEnd, a, audioEnd - a);
	}
}
