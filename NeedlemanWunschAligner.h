#pragma once
#include "alignment.h"

class NeedlemanWunschAligner
{
private:
	IAlignmentEngine* engine;

	// Параметры выравнивания
	const int MATCH_SCORE = 2;
	const int MISMATCH_SCORE = -1;
	const int GAP_SCORE = -1;
	const int MAX_GAP_PENALTY = -5;  // Максимальный штраф за длинные пропуски

public:
	void align(IAlignmentEngine* alignerEngine);
};

