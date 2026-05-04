#include "Settings.h"
#include <QSettings>

static const char INI_FILE[] = "aligner.ini";

#define SETTINGS_LIST	\
	X(recentProjectPath,	"path", "")\
	X(windowGeometry,		"win_geometry", QVariant())\
	X(windowState,			"win_state", QVariant())
	

// Qt does not QVariant.getValue() method
template<typename T>
void getValue(const QVariant &v, T &x)
{
	x = v.value<T>();
}

void Settings::loadSettings()
{
	QSettings settings(INI_FILE, QSettings::IniFormat);
	
#define X(var, id, def)	getValue(settings.value(id, def), var);
	SETTINGS_LIST
#undef X

	if (settings.allKeys().size() != 6)
		saveSettings();
}

void Settings::saveSettings()
{
	QSettings settings(INI_FILE, QSettings::IniFormat);

#define X(var, id, def)	settings.setValue(id, var);
	SETTINGS_LIST
#undef X
}

