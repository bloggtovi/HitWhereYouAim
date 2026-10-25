#include "Utils.h"
#include "Settings.h"

void Settings::Load()
{
	auto settings = GetSingleton();
	settings->RegisterBinding( { &bEnabled,					Settings::BindingType::kBoolean,	"Main", "bEnabled",					true } );
	settings->RegisterBinding( { &bEnabledPitchCorrection,	Settings::BindingType::kBoolean,	"Main", "bEnabledPitchCorrection",	true } );
	settings->RegisterBinding( { &iPitchCorrectionMode,		Settings::BindingType::kInteger,	"Main", "iPitchCorrectionMode",		1 } );
	settings->RegisterBinding( { &fPitchOffsetDegree,		Settings::BindingType::kFloat,		"Main", "fPitchOffsetDegree",		0.7f } );
	settings->RegisterBinding( { &bTargetDepthPrediction,	Settings::BindingType::kBoolean,	"Main", "bTargetDepthPrediction",	true } );
	settings->RegisterBinding( { &fPredictionWindowWidth,	Settings::BindingType::kFloat,		"Main", "fPredictionWindowWidth",	0.5f } );
	settings->RegisterBinding( { &fPredictionWindowHeight,	Settings::BindingType::kFloat,		"Main", "fPredictionWindowHeight",	0.5f } );
	settings->RegisterBinding( { &bEnabledTargetLeading,	Settings::BindingType::kBoolean,	"Main", "bEnabledTargetLeading",	true } );
	settings->RegisterBinding( { &fMaxLeadingAngle,			Settings::BindingType::kFloat,		"Main", "fMaxLeadingAngle",			45.0f } );
	settings->RegisterBinding( { &bInheritVelocity,			Settings::BindingType::kBoolean,	"Main", "bInheritVelocity",			false } );

	settings->RegisterBinding( { &fMaximumTargetDistance,	Settings::BindingType::kFloat,		"Calibration", "fMaximumTargetDistance",	100.0f } );
	settings->RegisterBinding( { &fArcheryPitchCalibration,	Settings::BindingType::kFloat,		"Calibration", "fArcheryPitchCalibration",	0.02f } );
	settings->RegisterBinding( { &fCrosshairPosX,			Settings::BindingType::kFloat,		"Calibration", "fCrosshairPosX",			0.0f } );
	settings->RegisterBinding( { &fCrosshairPosY,			Settings::BindingType::kFloat,		"Calibration", "fCrosshairPosY",			0.0f } );
	settings->RegisterBinding( { &fRayPenetrationDepth,		Settings::BindingType::kFloat,		"Calibration", "fRayPenetrationDepth",		0.2f } );

	settings->RegisterBinding( { &fPitchBias,				Settings::BindingType::kFloat,		"AngleCorrection", "fPitchBias",				0.0f } );
	settings->RegisterBinding( { &fHeightBias,				Settings::BindingType::kFloat,		"AngleCorrection", "fHeightBias",				0.0f } );
	settings->RegisterBinding( { &fBiasFadingStart,			Settings::BindingType::kFloat,		"AngleCorrection", "fBiasFadingStart",			45.0f } );
	settings->RegisterBinding( { &fMaxPitchAdjustmentSpeed,	Settings::BindingType::kFloat,		"AngleCorrection", "fMaxPitchAdjustmentSpeed",	360.0f } );
	settings->RegisterBinding( { &fMountedYawTolerance,		Settings::BindingType::kFloat,		"AngleCorrection", "fMountedYawTolerance",		15.0f } );

	settings->RegisterBinding( { &bTargetLockEnabled,		Settings::BindingType::kBoolean,	"TargetLock", "bTargetLockEnabled",			true } );
	settings->RegisterBinding( { &iTargetLockKey,			Settings::BindingType::kInteger,	"TargetLock", "iTargetLockKey",				47 } );		// V
	settings->RegisterBinding( { &fTargetLockMaxDistance,	Settings::BindingType::kFloat,		"TargetLock", "fTargetLockMaxDistance",		60.0f } );
	settings->RegisterBinding( { &fTargetLockWindowWidth,	Settings::BindingType::kFloat,		"TargetLock", "fTargetLockWindowWidth",		0.6f } );
	settings->RegisterBinding( { &fTargetLockWindowHeight,	Settings::BindingType::kFloat,		"TargetLock", "fTargetLockWindowHeight",	0.6f } );
	settings->RegisterBinding( { &fTargetLockRotationSpeed,	Settings::BindingType::kFloat,		"TargetLock", "fTargetLockRotationSpeed",	12.0f } );
	settings->RegisterBinding( { &fTargetLockLoseSightTime,	Settings::BindingType::kFloat,		"TargetLock", "fTargetLockLoseSightTime",	2.0f } );
	settings->RegisterBinding( { &bTargetLockNotification,	Settings::BindingType::kBoolean,	"TargetLock", "bTargetLockNotification",	true } );

	settings->RegisterBinding( { &bEnableLogging,			Settings::BindingType::kBoolean,	"Debug", "bEnableLogging",				false } );
	settings->RegisterBinding( { &bShowTargetPredictionInfo,Settings::BindingType::kBoolean,	"Debug", "bShowTargetPredictionInfo",	false } );
	settings->RegisterBinding( { &bShowCrosshairRayImpact,	Settings::BindingType::kBoolean,	"Debug", "bShowCrosshairRayImpact",		false } );
	settings->RegisterBinding( { &bShowLastProjectilePath,	Settings::BindingType::kBoolean,	"Debug", "bShowLastProjectilePath",		false } );
	settings->RegisterBinding( { &bEnabledTargetPrediction,	Settings::BindingType::kBoolean,	"Debug", "bEnabledTargetPrediction",	false } );
	settings->ApplyDefaults();

	// A missing default INI used to abort the game on startup. Fall back to the built-in
	// defaults instead so the plugin still works without the MCM files installed.
	CSimpleIniA iniDefault;
	iniDefault.SetUnicode();
	if( iniDefault.LoadFile( L"Data/MCM/Config/HitWhereYouAim/settings.ini" ) == SI_OK )
		settings->Load( iniDefault );
	else
		REX::WARN( "Data/MCM/Config/HitWhereYouAim/settings.ini is missing, using built-in defaults" );

	CSimpleIniA iniUser;
	iniUser.SetUnicode();
	if( iniUser.LoadFile( L"Data/MCM/Settings/HitWhereYouAim.ini" ) == SI_OK )
	{
		settings->Load( iniUser );
	}
}
