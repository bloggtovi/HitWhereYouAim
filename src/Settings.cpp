#include "Utils.h"
#include "Settings.h"



void Settings::Load()
{
	CSimpleIniA iniFile;
	iniFile.SetUnicode();
	iniFile.SetMultiKey();
	iniFile.LoadFile( L"Data/SKSE/Plugins/HitWhereYouAim.ini" );
}
