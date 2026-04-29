#include "audio_parser.h"
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDebug>

QVector<AudioEntry> AudioParser::parseFile(const QString& filename)
{
	if (filename.endsWith(".srt", Qt::CaseInsensitive)) {
		return parseSrt(filename);
	}
	else if (filename.endsWith(".json", Qt::CaseInsensitive)) {
		return parseJson(filename);
	}
	else {
		qWarning() << "Unsupported file format:" << filename;
		return QVector<AudioEntry>();
	}
}

QVector<AudioEntry> AudioParser::parseSrt(const QString& filename)
{
	QVector<AudioEntry> entries;
	QFile file(filename);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		return entries;
	}

	QTextStream stream(&file);
	stream.setCodec("UTF-8");

	while (!stream.atEnd()) {
		AudioEntry entry;
		entry.startMs = -1;
		entry.endMs = -1;

		QString line = stream.readLine().trimmed();
		if (line.isEmpty()) continue;

		bool ok;
	//	entry.index = line.toInt(&ok);
		if (!ok) continue;

		QString timeLine = stream.readLine();
		QRegularExpression reTime(R"((\d{2}):(\d{2}):(\d{2}),(\d{3})\s*-->\s*(\d{2}):(\d{2}):(\d{2}),(\d{3}))");
		QRegularExpressionMatch match = reTime.match(timeLine);

		if (match.hasMatch()) {
			entry.startMs = match.captured(1).toInt() * 3600000 +
				match.captured(2).toInt() * 60000 +
				match.captured(3).toInt() * 1000 +
				match.captured(4).toInt();
			entry.endMs = match.captured(5).toInt() * 3600000 +
				match.captured(6).toInt() * 60000 +
				match.captured(7).toInt() * 1000 +
				match.captured(8).toInt();
		}

		QString text;
		while (!stream.atEnd()) {
			QString nextLine = stream.readLine();
			if (nextLine.trimmed().isEmpty()) break;
			if (!text.isEmpty()) text += " ";
			text += nextLine.trimmed();
		}
		entry.text = text;
		entries.append(entry);
	}

	file.close();
	return entries;
}

QVector<AudioEntry> AudioParser::parseJson(const QString& filename)
{
	QVector<AudioEntry> entries;
	QFile file(filename);
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		qWarning() << "Cannot open JSON file:" << filename;
		return entries;
	}

	QByteArray jsonData = file.readAll();
	file.close();

	QJsonDocument doc = QJsonDocument::fromJson(jsonData);
	if (doc.isNull()) {
		qWarning() << "Invalid JSON:" << filename;
		return entries;
	}

	if (!doc.isObject()) {
		qWarning() << "JSON root is not an object";
		return entries;
	}

	QJsonObject root = doc.object();

	// Ищем массив segments
	if (!root.contains("segments")) {
		qWarning() << "JSON does not contain 'segments' array";
		return entries;
	}

	QJsonArray segments = root["segments"].toArray();

	// Проходим по всем сегментам
	for (const QJsonValue& segVal : segments) {
		if (!segVal.isObject()) continue;

		QJsonObject segment = segVal.toObject();

		// Проверяем наличие массива words
		if (!segment.contains("words")) continue;

		QJsonArray words = segment["words"].toArray();

		// Проходим по всем словам в сегменте
		for (const QJsonValue& wordVal : words) {
			if (!wordVal.isObject()) continue;

			QJsonObject wordObj = wordVal.toObject();

			AudioEntry entry;
		//	entry.index = entries.size() + 1;

			// Извлекаем слово
			entry.text = wordObj["word"].toString().trimmed();
			if (entry.text.isEmpty()) continue;
			
			//entry.text.remove(QRegularExpression("[\\p{P}]"));  // удаляем всю пунктуацию
			entry.text.remove(QRegularExpression("[^\\w\\s']"));
			entry.text = entry.text.toLower();
			
			// debug
			entry.text += QString::asprintf("~%d", entries.size());

			// Извлекаем время (Whisper даёт в секундах с плавающей точкой)
			double startSec = wordObj["start"].toDouble(-1.0);
			double endSec = wordObj["end"].toDouble(-1.0);

			if (startSec >= 0 && endSec >= 0) {
				entry.startMs = qRound(startSec * 1000.0);
				entry.endMs = qRound(endSec * 1000.0);
			}
			else {
				entry.startMs = -1;
				entry.endMs = -1;
			}

			// probability пока игнорируем

			entries.append(entry);
		}
	}

	qDebug() << "Parsed" << entries.size() << "words from Whisper JSON";
	return entries;
}



