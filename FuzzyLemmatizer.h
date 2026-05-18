#pragma once
#include <QString>
#include <QStringList>
#include <QHash>
#include <QDebug>
#include <QRegularExpression>

class FuzzyLemmatizer
{
private:
	// Простая таблица: окончание -> список возможных замен
	static QHash<QString, QStringList> endingsTable;
	// Исключения
	static QHash<QString, QStringList> exceptions;
	

	// Проверяет, является ли слово коротким (не более 3 букв)
	static bool isShortWord(const QString& word) {
		return word.length() <= 3;
	}

	// Проверяет, является ли слово иностранным (содержит редкие буквы)
	static bool isForeign(const QString& word) {
		return word.contains(QRegularExpression("[фщхцкджиэю]")) && word.length() > 8;
	}

	static void loadEndings(const QString& filename);
	static void loadExceptions(const QString& filename);

public:
	static void initLemmatizer(const QString& endingsFile, const QString& exceptionsFile = QString());
	static QStringList lemmatize(const QString& input);
}; 


