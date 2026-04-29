#pragma once

#include <QVector>
#include <QString>
#include <QHash>
#include "alignment.h"

// элемент массива слов из json
struct AudioEntry {
	QString text;	// слово
	int startMs;	// время начала слова в миллисекундах
	int endMs;		// время конца слова в миллисекундах
};

struct SourceWord {
	QString text;           // слово в нижнем регистре (для сравнения)
	int sentenceIndex;      // индекс в массиве предложений
	int wordIndex;          // позиция в предложении
};

// Данные для одной ячейки
struct CellData {
	QString text;
	bool isExcluded;  // можно исключить отдельную ячейку
	bool isError;
	int audioStartMs; // время начала в миллисекундах
	int audioEndMs;	  // время конца в миллисекундах

	void clear() {
		text = "";
		isExcluded = false;
		isError = false;
		audioStartMs = audioEndMs = -1;
	}

	CellData() {
		clear();
	}
};

struct PendingInsert {
	int index;           // позиция для вставки
	CellData audioCell;  // данные для вставки
};

struct AlignmentPair {
	QString targetText;
	QString sourceText;
	double confidence;
	QVector<int> targetIndices;
	QVector<int> sourceIndices;
};



QVector<int> countSentences(const QString& text, int startIndex);

class Aligner : public IAlignmentEngine
{
public:
	// Реализация IAlignmentEngine
	int getSourceWordsCount() const override;
	int getAudioWordsCount() const override;
	const QString& getSourceWord(int index) const override;
	const QString& getAudioWord(int index) const override;
	int getSourceSentence(int index) const override;

	void assignMatchedGroup(int sourceStart, int sourceCount, int audioStart, int audioCount) override;
	void flushPendingGroup(int sourceIndex, int audioStart, int audioCount) override;
public:
	Aligner();

	bool saveProjectTxt(const QString& filename);
	bool loadProjectTxt(const QString& filename);

	void clearSource();
	void clearTarget();
	void clearAudio();
	
	// Данные (публичные поля для простоты)

	QString projectPath;

	 // Независимые массивы для каждого столбца
	QVector<CellData> enCells;      // английский текст
	QVector<CellData> ruCells;      // русский текст
	QVector<CellData> audioCells;   // аудио текст (с таймкодами)

	// Массив указателей для удобного доступа по индексу столбца
	QVector<QVector<CellData>*> columnCells;

	QVector<AudioEntry> audioEntries;

	QVector<SourceWord> m_enWordsCache;
	QVector<PendingInsert> m_pendingInserts;  // отложенные вставки

	

	QString currentEnFile;
	QString currentRuFile;
	QString currentAudioTextFile;
	QString currentAudioFile;

	QHash<QString, QString> m_dictionary;  // русское слово -> английское
	QHash<QString, QString> m_dictionaryReverse; // английское -> русское (опционально)

	bool modified;

	// Основные операции
	void loadSourceText(const QString& filename);
	void loadTargetText(const QString& filename);
	void loadAudioEntries(const QVector<AudioEntry>& entries, const QString& filename);
	void loadAudioFile(const QString& filename);

	void loadDictionary(const QString& filename);
	
	double lexicalSimilarity(const QString& enSentence, const QString& ruSentence);

	void calcLexicalSimilarity();

	// Обновить кэш после изменений
	void rebuildSourceWordsCache();
	bool checkAudioAlignment();

	// Разбивка текста
	QVector<QString> splitIntoSentences(const QString& text);

	// Операции с ячейками (независимо для каждого столбца)
	void splitCell(int row, int cursorPos, int column);
	void mergeCells(int row1, int row2, int column);
	void excludeCell(int row, int column);
	void setCellText(int row, int column, const QString& text);

	// Выравнивание
	void alignTargetToSource();
	void alignAudioToSource();

	bool splitAudioToMp3();
	
	QString prepareForHunalign(const QVector<CellData>& cells, const QString& lang);
	QVector<AlignmentPair> parseHunalignOutput(const QString& output);
	void runHunalignAlignment();
	void applyAlignment(const QVector<AlignmentPair>& pairs);
	void saveToFile(const QString& filename, const QString& content);

	// Экспорт
	QString exportToCsv() const;
	
	// Нормализация количества строк
	void normalizeRowCount();
	
	// Вспомогательные
	int rowCount() const;
	void clear();

	QString sourceWordsBySentence(int i);

private:
	
	double calculateWordMatchScore(const QStringList& enWords, const QStringList& audioWords);

	// Привязка группы аудио слов к английским предложениям
	void assignAudioGroup(int enStart, int audioStart, int nWords);

	void applyPendingInserts();  // Синхронизация после выравнивания
};

