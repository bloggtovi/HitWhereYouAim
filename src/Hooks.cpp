#include "Hooks.h"

bool Hooks::Install()
{
	MH_Initialize();

	PlayerProjectileCalculateLaunchAngleHook::Install();
	PlayerSetAimPitchHook::Install();

	return true;
}
