#pragma once
#include <QString>

struct Settings
{
	
	void loadSettings();
	void saveSettings();

	QString recentProjectPath;
	QString dictPath;
	QString ffmpegPath;
	QString balconPath;
	QString endingsPath;
		
	QByteArray windowGeometry;
	QByteArray windowState;
};

