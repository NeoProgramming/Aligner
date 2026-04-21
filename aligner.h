#pragma once

#include <QVector>
#include <QString>
#include <QHash>
#include "alignment.h"

struct AudioEntry {
	QString text;	// слово
	int startMs;	// время начала слова в миллисекундах
	int endMs;		// время конца слова в миллисекундах

	//	int index;		// индекс в json
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
	int audioStartMs; // только для аудио столбца
	int audioEndMs;
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

QVector<int> countSentences(const QString& text, int startIndex);

class Aligner : public IAlignmentEngine
{
public:
	// Реализация IAlignmentEngine
	int getSourceWordsCount() const override;
	int getAudioWordsCount() const override;
	const QString& getSourceWord(int index) const override;
	const QString& getAudioWord(int index) const override;

	void assignMatchedGroup(int sourceStart, int sourceCount, int audioStart, int audioCount) override;
	void flushPendingGroup(int sourceIndex, int audioStart, int audioCount) override;
public:
	Aligner();

	bool Aligner::saveProjectTxt(const QString& filename);
	bool Aligner::loadProjectTxt(const QString& filename);

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
	QStringList tokenizeWords(const QString& text);
	double lexicalSimilarity(const QString& enSentence, const QString& ruSentence);

	void calcLexicalSimilarity();

	// Обновить кэш после изменений
	void rebuildSourceWordsCache();

	// Разбивка текста
	QVector<QString> splitIntoSentences(const QString& text);

	// Операции с ячейками (независимо для каждого столбца)
	void splitCell(int row, int cursorPos, int column);
	void mergeCells(int row1, int row2, int column);
	void excludeCell(int row, int column);
	void setCellText(int row, int column, const QString& text);

	// Выравнивание
	void autoAlignTarget();
	void alignAudioToSource();
	void assignAudioToRows();
	
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

	QString debugEnWords(int start, int count);
	QString debugAudioWords(int start, int count);

private:
	void updateRussianColumn();
	void updateAudioColumn();
	double similarity1(int enStart, int audioStart, int N);
	double similarity3(int enStart, int audioStart, int N);
	MatchResult similarityRecursive(int enStart, int audioStart, int currDepth, int minDepth);
	double similarity(int enStart, int audioStart, int N, int& enUsed, int& audioUsed);
	double similaritySimple(int enStart, int audioStart, int N);

	double calculateWordMatchScore(const QStringList& enWords, const QStringList& audioWords);

	// Очистка аудио слова от пунктуации
	QString cleanAudioWord(const QString& word);

	// Добавление неподошедших аудио слов как отдельной строки
	void flushPendingAudio(QStringList& pendingAudio, int& pendingStartMs, int& pendingEndMs, int insertPosition);

	// Привязка группы аудио слов к английским предложениям
	void assignAudioGroup(int enStart, int audioStart, int nWords);

	void syncCellsAfterAlignment();  // Синхронизация после выравнивания
};

