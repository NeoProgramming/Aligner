#include "NeedlemanWunschAligner.h"
#include <QVector>
#include <QPair>

void NeedlemanWunschAligner::align(IAlignmentEngine* alignerEngine) 
{
	engine = alignerEngine;

	int n = engine->getSourceWordsCount();
	int m = engine->getAudioWordsCount();

	// DP матрица
	struct Cell {
		int score;
		int sourceLen;   // длина группы исходных слов
		int audioLen;    // длина группы аудио слов
		int prevI, prevJ;
	};

	QVector<QVector<Cell>> dp(n + 1, QVector<Cell>(m + 1));

	// Инициализация
	dp[0][0] = { 0, 0, 0, -1, -1 };
	for (int i = 1; i <= n; i++) {
		dp[i][0] = { dp[i - 1][0].score + GAP_SCORE, 1, 0, i - 1, 0 };
	}
	for (int j = 1; j <= m; j++) {
		dp[0][j] = { dp[0][j - 1].score + GAP_SCORE, 0, 1, 0, j - 1 };
	}

	// Заполняем матрицу
	for (int i = 1; i <= n; i++) {
		for (int j = 1; j <= m; j++) {
			const QString& sourceWord = engine->getSourceWord(i - 1);
			const QString& audioWord = engine->getAudioWord(j - 1);

			bool match = (sourceWord == audioWord);
			int diagScore = dp[i - 1][j - 1].score + (match ? MATCH_SCORE : MISMATCH_SCORE);
			int upScore = dp[i - 1][j].score + GAP_SCORE;
			int leftScore = dp[i][j - 1].score + GAP_SCORE;

			// Выбираем лучший вариант
			if (diagScore >= upScore && diagScore >= leftScore) {
				dp[i][j].score = diagScore;
				dp[i][j].sourceLen = dp[i - 1][j - 1].sourceLen + 1;
				dp[i][j].audioLen = dp[i - 1][j - 1].audioLen + 1;
				dp[i][j].prevI = i - 1;
				dp[i][j].prevJ = j - 1;
			}
			else if (upScore >= leftScore) {
				dp[i][j].score = upScore;
				dp[i][j].sourceLen = dp[i - 1][j].sourceLen + 1;
				dp[i][j].audioLen = dp[i - 1][j].audioLen;
				dp[i][j].prevI = i - 1;
				dp[i][j].prevJ = j;
			}
			else {
				dp[i][j].score = leftScore;
				dp[i][j].sourceLen = dp[i][j - 1].sourceLen;
				dp[i][j].audioLen = dp[i][j - 1].audioLen + 1;
				dp[i][j].prevI = i;
				dp[i][j].prevJ = j - 1;
			}
		}
	}

	// Восстанавливаем группы
	QVector<QPair<QPair<int, int>, QPair<int, int>>> groups; // (sourceStart,sourceCount), (audioStart,audioCount)

	int i = n, j = m;
	while (i > 0 || j > 0) {
		const Cell& cell = dp[i][j];

		if (cell.sourceLen > 0 && cell.audioLen > 0) {
			// Нашли группу
			int sourceStart = i - cell.sourceLen;
			int audioStart = j - cell.audioLen;

			groups.prepend({ {sourceStart, cell.sourceLen},
						   {audioStart, cell.audioLen} });
		}

		i = cell.prevI;
		j = cell.prevJ;
	}

	// Вызываем callback'и
	for (const auto& group : groups) {
		int sourceStart = group.first.first;
		int sourceCount = group.first.second;
		int audioStart = group.second.first;
		int audioCount = group.second.second;

		engine->assignMatchedGroup(sourceStart, sourceCount, audioStart, audioCount);
	}
}
