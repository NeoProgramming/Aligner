#pragma once

#include <QVector>
#include <QPair>
#include <QString>
#include <QHash>
#include <QSet>

class IAlignmentEngine;

class AdvancedAligner {
public:
	AdvancedAligner();
	~AdvancedAligner();

	// Основной метод выравнивания
	void align(IAlignmentEngine* engine);

	// Настройка параметров
	void setMatchScore(int score);
	void setMismatchScore(int score);
	void setGapScore(int score);
	void setMaxContextSize(int size);
	void setRareWordThreshold(int threshold);
	void setMinSequenceLength(int length);

private:
	IAlignmentEngine* m_engine;

	// Параметры выравнивания
	int m_matchScore;
	int m_mismatchScore;
	int m_gapScore;
	int m_maxContextSize;
	int m_rareWordThreshold;
	int m_minSequenceLength;      // минимальная длина последовательности для валидного якоря
	int m_windowSize;
	int m_stepSize;

	// Структура для якоря
	struct MatchAnchor {
		int sourceIndex;
		int audioIndex;
	};

	// Основные методы
	QVector<MatchAnchor> findAnchors();
	bool isPartOfSequence(int sourceIdx, int audioIdx);
	void alignSegment(int sourceStart, int sourceCount,
		int audioStart, int audioCount);
	void preciseAlignment(int sourceStart, int sourceCount,
		int audioStart, int audioCount);
	void approximateAlignment(int sourceStart, int sourceCount,
		int audioStart, int audioCount);
	void globalAlignment(int sourceStart, int sourceCount,
		int audioStart, int audioCount);

	// Утилиты
	int findFirstValidAnchor(const QVector<MatchAnchor>& anchors);
};


