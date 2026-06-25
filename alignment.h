#pragma once
#include <QString>
#include <QVector>

// шаг alignment path для восстановления
struct PathStep {
	int sourceIdx;   // индекс исходного слова (-1 если вставка)
	int audioIdx;    // индекс аудиослова (-1 если удаление)
};

// результат сравнения двух групп
struct MatchResult {
	double score = 0.0;		// коэффициент похожести от 0 до 1
	int usedSource = 0;		// сколько слов из исходного окна использовано
	int usedAudio = 0;		// сколько слов из аудио окна использовано
	QVector<PathStep> path; // alignment path для восстановления
};

// интерфейс
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
	virtual void assignMatchedGroup(int sourceStart, int sourceCount, int audioStart, int audioCount, QVector<PathStep> &path) = 0;
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
	
	void clear() {
		text = "";
		isExcluded = false;		
	}

	TextSentence() {
		clear();
	}
};

// Данные для одной аудио ячейки
struct AudioSentence : public TextSentence {

	int audioStartMs;	// время начала в миллисекундах
	int audioEndMs;		// время конца в миллисекундах
	int firstWordIndex = -1;   // индекс первого слова предложения в audioEntries
	int lastWordIndex = -1;    // индекс последнего слова предложения в audioEntries
	double audioSim;	// похожесть аудио текста на исходный текст
	double transSim;	// похожесть переведенного текста на исходный текст
	bool isHighlighted;  // Новая отметка для всей строки

	void clear() {
		TextSentence::clear();
		audioStartMs = audioEndMs = -1;
		audioSim = -1.0;
		transSim = -1.0;
		firstWordIndex = -1;
		lastWordIndex = -1;
		isHighlighted = false;
	}

	AudioSentence() {
		clear();
	}
};

