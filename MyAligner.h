#pragma once
#include "alignment.h"


struct MatchResult {
	double matches;			// количество совпавших слов
	int enTotal;			// сколько английских слов рассмотрено
	int audioTotal;			// сколько аудио слов рассмотрено

	double getScore() const {
		int maxWords = qMax(enTotal, audioTotal);
		if (maxWords == 0) return 0.0;
		return matches / maxWords;
	}
};


class MyAligner
{
private:
	IAlignmentEngine* engine;
public:
	void align(IAlignmentEngine* alignerEngine);
private:
	double similarity1(int enStart, int audioStart, int N);
	double similarity3(int enStart, int audioStart, int N);
	MatchResult similarityRecursive(int enStart, int audioStart, int currDepth, int minDepth);
	double similarity(int enStart, int audioStart, int N, int& enUsed, int& audioUsed);
	
};


