#include "FuzzyLemmatizer.h"
#include <QFile>


// Таблица замен 
QHash<QString, QStringList> FuzzyLemmatizer::endingsTable;

// Исключения
QHash<QString, QStringList> FuzzyLemmatizer::exceptions;

void FuzzyLemmatizer::initLemmatizer(const QString& endingsFile, const QString& exceptionsFile) 
{
	loadEndings(endingsFile);
	if (!exceptionsFile.isEmpty()) {
		loadExceptions(exceptionsFile);
	}	
}

QStringList FuzzyLemmatizer::lemmatize(const QString& input) 
{
	QString word = input.toLower();

	// Проверка исключений
	if (exceptions.contains(word)) {
		return exceptions[word];
	}

	QStringList results;

	// Поиск по таблице окончаний
	for (auto it = endingsTable.begin(); it != endingsTable.end(); ++it) {
		const QString& ending = it.key();
		if (word.endsWith(ending)) {
			QString stem = word.left(word.length() - ending.length());
			for (const QString& replacement : it.value()) {
				results << stem + replacement;
			}
		}
	}

	// Удаляем дубликаты
	results.removeDuplicates();
	results.removeAll("");

	// Если ничего не нашли, возвращаем исходное слово
	if (results.isEmpty()) {
		results << word;
	}

	return results;
}

void FuzzyLemmatizer::loadEndings(const QString& filename) 
{
	QFile file(filename);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		qWarning() << "Cannot open endings file:" << filename;
		return;
	}

	QTextStream stream(&file);
	stream.setCodec("UTF-8");

	int lineNum = 0;
	while (!stream.atEnd()) {
		QString line = stream.readLine();
		lineNum++;
		line = line.trimmed();

		// Пропускаем пустые строки и комментарии
		if (line.isEmpty() || line.startsWith("#")) {
			continue;
		}

		// Формат: окончание = замена1, замена2, замена3
		// Пример: ы = а, _
		//         _ означает пустую строку

		QStringList parts = line.split("=");
		if (parts.size() != 2) {
			qWarning() << "Invalid format at line" << lineNum << ":" << line;
			continue;
		}

		QString ending = parts[0].trimmed();
		QString replacementsStr = parts[1].trimmed();

		// Обрабатываем пустое окончание (обозначается подчерком)
		if (ending == "_") {
			ending = "";
		}

		// Разбираем замены
		QStringList replacements = replacementsStr.split(",");
		QStringList processedReplacements;

		for (QString rep : replacements) {
			rep = rep.trimmed();
			if (rep == "_") {
				processedReplacements << "";  // пустая замена
			}
			else {
				processedReplacements << rep;
			}
		}

		if (!endingsTable.contains(ending)) {
			endingsTable[ending] = processedReplacements;
		}
		else {
			// Если окончание уже есть, добавляем новые замены
			endingsTable[ending].append(processedReplacements);
		}
	}

	file.close();
	qDebug() << "Loaded" << endingsTable.size() << "endings from" << filename;
}

void FuzzyLemmatizer::loadExceptions(const QString& filename) 
{
	QFile file(filename);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		qWarning() << "Cannot open exceptions file:" << filename;
		return;
	}

	QTextStream stream(&file);
	stream.setCodec("UTF-8");

	int lineNum = 0;
	while (!stream.atEnd()) {
		QString line = stream.readLine();
		lineNum++;
		line = line.trimmed();

		if (line.isEmpty() || line.startsWith("#")) {
			continue;
		}

		// Формат: исходное_слово = форма1, форма2
		// Пример: человека = человек

		QStringList parts = line.split("=");
		if (parts.size() != 2) {
			qWarning() << "Invalid format at line" << lineNum << ":" << line;
			continue;
		}

		QString word = parts[0].trimmed();
		QStringList forms = parts[1].trimmed().split(",");

		QStringList processedForms;
		for (QString form : forms) {
			form = form.trimmed();
			if (form == "_") {
				processedForms << "";
			}
			else {
				processedForms << form;
			}
		}

		exceptions[word] = processedForms;
	}

	file.close();
	qDebug() << "Loaded" << exceptions.size() << "exceptions from" << filename;
}

