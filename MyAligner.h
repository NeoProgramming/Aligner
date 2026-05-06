#pragma once
#include "alignment.h"

struct PathStep {
	int sourceIdx;   // индекс исходного слова (-1 если вставка)
	int audioIdx;    // индекс аудиослова (-1 если удаление)
};

struct MatchResult {
	double score=0.0;		// коэффициент похожести от 0 до 1
	int usedSource = 0;		// сколько слов из исходного окна использовано
	int usedAudio = 0;		// сколько слов из аудио окна использовано
	QVector<PathStep> path; // alignment path для восстановления
};

class MyAligner
{
private:
	IAlignmentEngine* engine;
public:
	void align(IAlignmentEngine* alignerEngine);
private:
	MatchResult similarity(int enStart, int maxSrc, int audioStart, int maxAud);
	int sourceWindowSize(int srcStart);
	int audioWindowSize(int audioStart, int sourceWindow);
private:
	static const int WINDOW_SIZE = 16;
	
};


