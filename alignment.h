#pragma once
#include <QString>

class IAlignmentEngine {
public:
	virtual ~IAlignmentEngine() = default;

	virtual int getSourceWordsCount() const = 0;
	virtual int getAudioWordsCount() const = 0;

	virtual const QString& getSourceWord(int index) const = 0;
	virtual const QString& getAudioWord(int index) const = 0;

	virtual int  getSourceSentence(int index) const = 0;
	virtual void setAudioSentence(int index, int sentid, bool ins) = 0;

	// возможно от этого откажемся
	virtual void assignMatchedGroup(int sourceStart, int sourceCount, int audioStart, int audioCount) = 0;
	virtual void flushPendingGroup(int sourceIndex, int audioStart, int audioCount) = 0;	
};


// элемент массива слов из json
struct AudioEntry {
	QString text;		// слово
	int startMs;		// время начала слова в миллисекундах
	int endMs;			// время конца слова в миллисекундах
	int sentenceIdx;	// индекс исходного предложения, которому соответствует это слово
	bool ins;			// вставка в новое предложение перед данным
};

// слово с индексом предложения и позицией в предложении
struct SourceWord {
	QString text;           // слово в нижнем регистре (для сравнения)
	int sentenceIndex;      // индекс в массиве предложений
	int wordIndex;          // позиция в предложении
};

// Данные для одной текстовой ячейки
struct TextSentence {
	QString text;
	bool isExcluded;  // можно исключить отдельную ячейку
	bool isError;

	void clear() {
		text = "";
		isExcluded = false;
		isError = false;
	}

	TextSentence() {
		clear();
	}
};

// Данные для одной аудио ячейки
struct AudioSentence : public TextSentence {

	int audioStartMs; // время начала в миллисекундах
	int audioEndMs;	  // время конца в миллисекундах
	double similarity;

	void clear() {
		TextSentence::clear();
		audioStartMs = audioEndMs = -1;
		similarity = -1.0;
	}

	AudioSentence() {
		clear();
	}
};

