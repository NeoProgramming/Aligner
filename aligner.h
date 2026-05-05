#pragma once

#include <QVector>
#include <QString>
#include <QHash>
#include "alignment.h"



struct AlignmentPair {
	QString targetText;
	QString sourceText;
	double confidence;
	QVector<int> targetIndices;
	QVector<int> sourceIndices;
};


class Aligner : public IAlignmentEngine
{
public:
	// Реализация IAlignmentEngine
	int getSourceWordsCount() const override;
	int getAudioWordsCount() const override;
	const QString& getSourceWord(int index) const override;
	const QString& getAudioWord(int index) const override;
	
	int  getSourceSentence(int index) const override;
	void setAudioSentence(int index, int sentidx, bool ins) override;

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
	QVector<TextSentence> sourceCells;      // оригинальный текст
	QVector<TextSentence> targetCells;      // переведенный текст
	QVector<AudioSentence> audioCells;		// аудио текст (с таймкодами и отладочной информацией)
	
	QVector<AudioEntry> audioEntries;
	QVector<SourceWord> sourceWordsCache;
	
	QString currentSourceFile;
	QString currentTargetFile;
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
	void rebuildAudioSentences();
	void insertSentence(const QStringList &currentWords, int currentSentenceIdx, bool currentIsIns, int currentStartMs, int currentEndMs);

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

	bool splitAudioToMp3(const QString &ffmpegPath);
		
	// Нормализация количества строк
	void normalizeRowCount();
	
	// Вспомогательные
	int rowCount() const;
	void clear();

	QString sourceWordsBySentence(int i);

private:
	double calculateWordMatchScore(const QStringList& enWords, const QStringList& audioWords);
};

