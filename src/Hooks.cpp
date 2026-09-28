#include "Hooks.h"

bool Hooks::Install()
{
	MH_Initialize();

	//Unused::ProjectileConstructorHook::Install();
	PlayerProjectileCalculateLaunchAngleHook::Install();
	PlayerSetAimPitchHook::Install();

	return true;
}
