#pragma once

#include <QVector>
#include <QString>
#include <QHash>
#include "alignment.h"
#include "Settings.h"

class Aligner : public IAlignmentEngine
{
	static const int WINDOW_SIZE = 16;
public:
	// Реализация IAlignmentEngine
	int getSourceWordsCount() const override;
	int getAudioWordsCount() const override;
	const QString& getSourceWord(int index) const override;
	const QString& getAudioWord(int index) const override;
	
	int  getSourceSentence(int index) const override;
	void setAudioSentence(int index, int sentidx, bool ins) override;

	void assignMatchedGroup(int sourceStart, int sourceCount, int audioStart, int audioCount, QVector<PathStep> &path) override;
	void flushPendingGroup(int sourceIndex, int audioStart, int audioCount) override;

	void assignMatchedGroupNaive(int sourceStart, int sourceCount, int audioStart, int audioCount);

	void alignAudio();
	int sourceWindowSize(int srcStart);
	int audioWindowSize(int audioStart, int sourceWindow);
	MatchResult similarity(int enStart, int maxSrc, int audioStart, int maxAud);

	Aligner();

	bool saveProjectTxt(const QString& filename);
	bool loadProjectTxt(const QString& filename);

	void clearSource();
	void clearTranslated();
	void clearAudio();

	bool removeAudioSentence(int row, bool force = false);

	void moveCellUp(int row, int column);
	void moveCellDown(int row, int column);
	
	// Данные (публичные поля для простоты)
	Settings cfg;

	 // Независимые массивы для каждого столбца
	QVector<TextSentence> sourceCells;      // оригинальный текст
	QVector<TextSentence> translatedCells;  // переведенный текст
	QVector<AudioSentence> audioCells;		// аудио текст (с таймкодами и отладочной информацией)
	
	QVector<AudioEntry> audioEntries;
	QVector<SourceWord> sourceWordsCache;
	
	QString currentSourceFile;
	QString currentTranslatedFile;
	QString currentAudioTextFile;
	QString currentAudioFile;
	QString currentOutputDir;

	QHash<QString, QString> m_dictionary;  // русское слово -> английское
	

	bool modified;
	double totalAudioSim = 0;

	// Основные операции
	void loadSourceText(const QString& filename);
	void loadTranslatedText(const QString& filename);
	void loadAudioEntries(const QString& filename);
	void loadAudioFile(const QString& filename);

	void loadDictionary(const QString& filename);
	
	double lexicalSimilarity(const QString& enSentence, const QString& ruSentence);

	void calcTranslatedSimilarity();
	void calcAudioSimilarity();

	// Обновить кэш после изменений
	void rebuildSourceWordsCache();
	void rebuildAudioSentences();
	void insertSentence(const QStringList &currentWords, int currentSentenceIdx, bool currentIsIns, 
		int currentStartMs, int currentEndMs, int firstWordIdx, int lastWordIdx);
	void rebuildAudioEntires();

	// Разбивка текста
	QVector<QString> splitIntoSentences(const QString& text);

	void highlightRow(int row, bool highlight);
	bool isHighlightedRow(int row);

	// Операции с ячейками (независимо для каждого столбца)
	void splitCell(int row, int cursorPos, int column);
	void mergeCells(int row1, int row2, int column);
	void mergeCells2(int row1, int row2, int column); // old
	void excludeRow(int row);
	void excludeCell(int row, int column);
	void setCellText(int row, int column, const QString& text);

	// Выравнивание
	void alignTranslatedToSource();
	void alignAudioToSource();

	// Разрезка и Генерация
	bool prepareFilePath(bool gen, int i, QString &outputFilePath);
	bool splitAudio();
	bool splitAudioSentence(int i);	
	bool generateAudio();
	bool generateAudioSentence(int i);
			
	// Нормализация количества строк
	void normalizeRowCount();
	
	// Вспомогательные
	int rowCount() const;
	void clear();
	void clearAudioAlignment();

	bool moveAudioWordsToPrev(int sentenceIndex, int wordOffset);
	bool moveAudioWordsToNext(int sentenceIndex, int wordOffset);
	void updateAudioSentenceFromEntries(int sentenceIndex);
private:
	bool generateMp3(const QString &outputFilePath, const QString &text);
	bool generateMp3v2(const QString &outputFilePath, const QString &text);
	bool generateWav(const QString &outputFilePath, const QString &text);

	void updateAudioIndicesAfterSwap(int row1, int row2);
};

