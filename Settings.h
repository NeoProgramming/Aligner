#pragma once
#include <QString>

struct Settings
{
	
	void loadSettings();
	void saveSettings();

	QString recentProjectPath;
	QString ffmpegPath;
		
	QByteArray windowGeometry;
	QByteArray windowState;
};

