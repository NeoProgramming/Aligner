#pragma once

#include <QVector>
#include <QString>
#include "alignment.h"

class IAlignmentEngine;


class StreamingAligner {
public:
	StreamingAligner();
	~StreamingAligner();

	// ќсновной метод выравнивани€
	void align(IAlignmentEngine* engine);

	// Ќастройка параметров
	void setMaxLookahead(int words);        // максимальна€ дистанци€ поиска вперед
	void setMinMatchGroup(int length);      // минимальна€ длина группы совпадений
	void setMaxSourceSkip(int words);       // максимальное количество пропускаемых исходных слов
	void setMaxAudioInsertion(int words);   // максимальное количество аудио вставок подр€д
	
private:
	void fallbackAlignment();
private:
	IAlignmentEngine* m_engine;

	// ѕараметры
	int m_maxLookahead;         // по умолчанию 10
	int m_minMatchGroup;        // по умолчанию 1
	int m_maxSourceSkip;        // по умолчанию 5 (не пропускаем много исходных слов)
	int m_maxAudioInsertion;    // по умолчанию 200 (максимум вставок до сброса)

	// —труктура дл€ найденного совпадени€
	struct Match {
		int sourceOffset;
		int audioOffset;
		int length;
	};

	// ѕоиск лучшего совпадени€ в окне
	Match findBestMatch(int sourcePos, int audioPos);

	// ѕодсчет длины непрерывной группы совпадений
	int countMatchLength(int sourcePos, int audioPos);
};





