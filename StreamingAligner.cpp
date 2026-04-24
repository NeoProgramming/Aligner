#include "StreamingAligner.h"
#include "alignment.h"
#include <QDebug>
#include <algorithm>

StreamingAligner::StreamingAligner()
	: m_engine(nullptr)
	, m_maxLookahead(10)
	, m_minMatchGroup(1)
	, m_maxSourceSkip(5)
	, m_maxAudioInsertion(200)
{
}

StreamingAligner::~StreamingAligner()
{
}

void StreamingAligner::setMaxLookahead(int words)
{
	m_maxLookahead = qMax(1, words);
}

void StreamingAligner::setMinMatchGroup(int length)
{
	m_minMatchGroup = qMax(1, length);
}

void StreamingAligner::setMaxSourceSkip(int words)
{
	m_maxSourceSkip = qMax(0, words);
}

void StreamingAligner::setMaxAudioInsertion(int words)
{
	m_maxAudioInsertion = qMax(1, words);
}

void StreamingAligner::align(IAlignmentEngine* engine)
{
	if (!engine) {
		qWarning() << "StreamingAligner::align - engine is null";
		return;
	}

	m_engine = engine;

	int n = m_engine->getSourceWordsCount();
	int m = m_engine->getAudioWordsCount();

	if (n == 0 || m == 0) {
		qWarning() << "StreamingAligner::align - empty word arrays";
		return;
	}

	// ФАЗА 1: Находим первую точку синхронизации
	int sourcePos = 0;
	int audioPos = 0;

	// Ищем первое надежное совпадение в начале текста
	bool syncFound = false;
	int maxPreambleSearch = qMin(500, m); // ищем преамбулу в первых 500 словах

	for (int audioSearch = 0; audioSearch < maxPreambleSearch && !syncFound; audioSearch++) {
		const QString& audioWord = m_engine->getAudioWord(audioSearch);

		// Ищем это слово в первых 50 словах текста
		for (int sourceSearch = 0; sourceSearch < 50 && sourceSearch < n; sourceSearch++) {
			if (m_engine->getSourceWord(sourceSearch) == audioWord) {
				// Проверяем, что это не случайное совпадение - смотрим контекст
				int matchLen = countMatchLength(sourceSearch, audioSearch);
				if (matchLen >= 2) {
					// Нашли надежную синхронизацию
					sourcePos = sourceSearch;
					audioPos = audioSearch;
					syncFound = true;

					// Преамбула - все аудио до этой позиции
					if (audioSearch > 0) {
						m_engine->flushPendingGroup(0, 0, audioSearch);
					}
					break;
				}
			}
		}
	}

	if (!syncFound) {
		// Не нашли синхронизацию - используем глобальное выравнивание
		qWarning() << "StreamingAligner: sync not found, using fallback";
		fallbackAlignment();
		return;
	}

	// ФАЗА 2: Последовательное выравнивание от найденной синхроточки
	int pendingStart = -1;
	int pendingCount = 0;

	auto flushPending = [&](int insertPosition) {
		if (pendingCount > 0) {
			m_engine->flushPendingGroup(insertPosition, pendingStart, pendingCount);
			pendingCount = 0;
			pendingStart = -1;
		}
	};

	while (sourcePos < n && audioPos < m) {
		// Проверяем текущие слова
		if (m_engine->getSourceWord(sourcePos) == m_engine->getAudioWord(audioPos)) {
			// Совпадают - сбрасываем накопленные вставки и сопоставляем группу
			flushPending(sourcePos);

			int matchLen = countMatchLength(sourcePos, audioPos);
			m_engine->assignMatchedGroup(sourcePos, matchLen, audioPos, matchLen);

			sourcePos += matchLen;
			audioPos += matchLen;
		}
		else {
			// Не совпадают - пробуем найти совпадение с небольшим смещением
			bool found = false;

			// Пробуем пропустить до 3 исходных слов
			for (int skipSource = 1; skipSource <= 3 && sourcePos + skipSource < n; skipSource++) {
				if (m_engine->getSourceWord(sourcePos + skipSource) == m_engine->getAudioWord(audioPos)) {
					// Пропускаем исходные слова (нет в аудио)
					sourcePos += skipSource;
					found = true;
					break;
				}
			}

			// Пробуем пропустить до 5 аудио слов (вставки)
			if (!found) {
				for (int skipAudio = 1; skipAudio <= 5 && audioPos + skipAudio < m; skipAudio++) {
					if (m_engine->getSourceWord(sourcePos) == m_engine->getAudioWord(audioPos + skipAudio)) {
						// Аудио вставка
						m_engine->flushPendingGroup(sourcePos, audioPos, skipAudio);
						audioPos += skipAudio;
						found = true;
						break;
					}
				}
			}

			if (!found) {
				// Не нашли совпадения - накапливаем аудио слово как вставку
				if (pendingStart == -1) {
					pendingStart = audioPos;
				}
				pendingCount++;
				audioPos++;

				// Если накопили слишком много - сбрасываем
				if (pendingCount > 50) {
					flushPending(sourcePos);
				}
			}
		}
	}

	// Хвост
	flushPending(sourcePos);

	if (audioPos < m) {
		m_engine->flushPendingGroup(sourcePos, audioPos, m - audioPos);
	}
}

void StreamingAligner::fallbackAlignment()
{
	int n = m_engine->getSourceWordsCount();
	int m = m_engine->getAudioWordsCount();

	// Простое последовательное сравнение с начала
	int sourcePos = 0;
	int audioPos = 0;
	int pendingStart = -1;
	int pendingCount = 0;

	auto flushPending = [&](int insertPosition) {
		if (pendingCount > 0) {
			m_engine->flushPendingGroup(insertPosition, pendingStart, pendingCount);
			pendingCount = 0;
			pendingStart = -1;
		}
	};

	while (sourcePos < n && audioPos < m) {
		if (m_engine->getSourceWord(sourcePos) == m_engine->getAudioWord(audioPos)) {
			flushPending(sourcePos);

			int matchLen = 1;
			while (sourcePos + matchLen < n && audioPos + matchLen < m &&
				m_engine->getSourceWord(sourcePos + matchLen) ==
				m_engine->getAudioWord(audioPos + matchLen)) {
				matchLen++;
			}

			m_engine->assignMatchedGroup(sourcePos, matchLen, audioPos, matchLen);
			sourcePos += matchLen;
			audioPos += matchLen;
		}
		else {
			if (pendingStart == -1) {
				pendingStart = audioPos;
			}
			pendingCount++;
			audioPos++;
		}
	}

	flushPending(sourcePos);

	if (audioPos < m) {
		m_engine->flushPendingGroup(sourcePos, audioPos, m - audioPos);
	}
}

StreamingAligner::Match StreamingAligner::findBestMatch(int sourcePos, int audioPos)
{
	Match bestMatch;
	bestMatch.sourceOffset = 0;
	bestMatch.audioOffset = 0;
	bestMatch.length = 0;

	int n = m_engine->getSourceWordsCount();
	int m = m_engine->getAudioWordsCount();

	// Ограничиваем поиск: не пропускаем слишком много исходных слов
	int maxSourceOffset = qMin(m_maxSourceSkip, n - sourcePos - 1);
	int maxAudioOffset = qMin(m_maxLookahead, m - audioPos - 1);

	// Проверяем все комбинации смещений в пределах окна
	for (int sourceOffset = 0; sourceOffset <= maxSourceOffset; sourceOffset++) {
		for (int audioOffset = 0; audioOffset <= maxAudioOffset; audioOffset++) {

			// Пропускаем нулевое смещение (будет проверено отдельно)
			if (sourceOffset == 0 && audioOffset == 0) continue;

			int s = sourcePos + sourceOffset;
			int a = audioPos + audioOffset;

			if (s >= n || a >= m) continue;

			// Проверяем, совпадают ли слова при этих смещениях
			if (m_engine->getSourceWord(s) == m_engine->getAudioWord(a)) {

				// Нашли совпадение - считаем длину непрерывной группы
				int matchLen = countMatchLength(s, a);

				// Оцениваем качество совпадения
				// Чем длиннее группа и чем меньше смещения - тем лучше
				int score = matchLen * 100 - (sourceOffset + audioOffset) * 5;

				int bestScore = bestMatch.length * 100 - (bestMatch.sourceOffset + bestMatch.audioOffset) * 5;

				if (score > bestScore) {
					bestMatch.sourceOffset = sourceOffset;
					bestMatch.audioOffset = audioOffset;
					bestMatch.length = matchLen;
				}
			}
		}
	}

	// Проверяем нулевое смещение (текущие слова совпадают)
	if (sourcePos < n && audioPos < m &&
		m_engine->getSourceWord(sourcePos) == m_engine->getAudioWord(audioPos)) {

		int matchLen = countMatchLength(sourcePos, audioPos);

		int score = matchLen * 100;
		int bestScore = bestMatch.length * 100 - (bestMatch.sourceOffset + bestMatch.audioOffset) * 5;

		if (score > bestScore) {
			bestMatch.sourceOffset = 0;
			bestMatch.audioOffset = 0;
			bestMatch.length = matchLen;
		}
	}

	return bestMatch;
}

int StreamingAligner::countMatchLength(int sourcePos, int audioPos)
{
	int n = m_engine->getSourceWordsCount();
	int m = m_engine->getAudioWordsCount();

	int length = 0;
	while (sourcePos + length < n &&
		audioPos + length < m &&
		m_engine->getSourceWord(sourcePos + length) ==
		m_engine->getAudioWord(audioPos + length)) {
		length++;
	}

	return length;
}