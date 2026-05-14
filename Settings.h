#pragma once
#include <QString>

struct Settings
{
	
	void loadSettings();
	void saveSettings();

	QString recentProjectPath;
	QString dictPath;
	QString ffmpegPath;
		
	QByteArray windowGeometry;
	QByteArray windowState;
};

