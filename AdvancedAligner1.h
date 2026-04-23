#pragma once
#include "alignment.h"

#include <QVector>
#include <QPair>

class AdvancedAligner1
{
private:
	IAlignmentEngine* engine;

	struct MatchResult {
		double score;
		int sourceLen;
		int audioLen;
	};

public:
	void align(IAlignmentEngine* alignerEngine);
private:
	QVector<QPair<int, int>> findAnchors();
	bool verifyAnchor(int sourceIdx, int audioIdx);
	void preciseAlignment(int sourceStart, int sourceCount,
		int audioStart, int audioCount);
	void alignSegment(int sourceStart, int sourceCount,
		int audioStart, int audioCount);
	void approximateAlignment(int sourceStart, int sourceCount,
		int audioStart, int audioCount);
	void globalAlignment(int sourceStart, int sourceCount,
		int audioStart, int audioCount);
};

