#pragma once
#include <QString>

class IAlignmentEngine {
public:
	virtual ~IAlignmentEngine() = default;

	virtual int getSourceWordsCount() const = 0;
	virtual int getAudioWordsCount() const = 0;

	virtual const QString& getSourceWord(int index) const = 0;
	virtual const QString& getAudioWord(int index) const = 0;

	virtual void assignMatchedGroup(int sourceStart, int sourceCount, int audioStart, int audioCount) = 0;
	virtual void flushPendingGroup(int sourceIndex, int audioStart, int audioCount) = 0;
};


