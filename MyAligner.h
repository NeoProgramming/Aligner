#pragma once
#include "alignment.h"

struct MatchResult {
	double similarity=0.0;	// от 0 до 1
	int usedEn = 0;			// сколько слов из английского (исходного) окна использовано
	int usedAudio = 0;		// сколько слов из аудио окна использовано
};

class MyAligner
{
private:
	IAlignmentEngine* engine;
public:
	void align(IAlignmentEngine* alignerEngine);
private:
	double similarity1(int enStart, int audioStart, int N);
	double similarity2(int enStart, int audioStart, int N);
	MatchResult similarity3(int enStart, int audioStart, int N);
	MatchResult similarityRecursive(int enStart, int audioStart, int currDepth, int minDepth);	
	MatchResult similarityDP(int enStart, int audioStart);
	MatchResult similarityDPB(int enStart, int audioStart);
private:
	static const int WINDOW_SIZE = 16;
	int DP[WINDOW_SIZE + 1][WINDOW_SIZE + 1];
};


