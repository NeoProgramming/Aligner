#pragma once
#include <QString>

struct Settings
{
	
	void loadSettings();
	void saveSettings();

	QString recentProjectPath;
		
	QByteArray windowGeometry;
	QByteArray windowState;
};

