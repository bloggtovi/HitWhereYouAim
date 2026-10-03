#include "Utils.h"
#include "Settings.h"

void Settings::Load()
{
	CSimpleIniA iniDefault;
	iniDefault.SetUnicode();

	if( iniDefault.LoadFile( L"Data/MCM/Config/HitWhereYouAim/settings.ini" ) != SI_OK )
		REX::FAIL( "Data/MCM/Config/HitWhereYouAim/settings.ini is missing!" );

	auto settings = GetSingleton();
	settings->RegisterBinding( { &bEnabled,					Settings::BindingType::kBoolean,	"Main", "bEnabled",					true } );
	settings->RegisterBinding( { &bEnabledPitchCorrection,	Settings::BindingType::kBoolean,	"Main", "bEnabledPitchCorrection",	true } );
	settings->RegisterBinding( { &iPitchCorrectionMode,		Settings::BindingType::kInteger,	"Main", "iPitchCorrectionMode",		1 } );
	settings->RegisterBinding( { &fPitchOffsetDegree,		Settings::BindingType::kFloat,		"Main", "fPitchOffsetDegree",		7.0f } );
	settings->RegisterBinding( { &bTargetDepthPrediction,	Settings::BindingType::kBoolean,	"Main", "bTargetDepthPrediction",	true } );
	settings->RegisterBinding( { &fPredictionWindowWidth,	Settings::BindingType::kFloat,		"Main", "fPredictionWindowWidth",	0.5f } );
	settings->RegisterBinding( { &fPredictionWindowHeight,	Settings::BindingType::kFloat,		"Main", "fPredictionWindowHeight",	0.5f } );
	settings->RegisterBinding( { &bEnabledTargetLeading,	Settings::BindingType::kBoolean,	"Main", "bEnabledTargetLeading",	true } );
	settings->RegisterBinding( { &fMaxLeadingAngle,			Settings::BindingType::kFloat,		"Main", "fMaxLeadingAngle",			45.0f } );

	settings->RegisterBinding( { &fMaximumTargetDistance,	Settings::BindingType::kFloat,		"Calibration", "fMaximumTargetDistance",	100.0f } );
	settings->RegisterBinding( { &fCrosshairPosX,			Settings::BindingType::kFloat,		"Calibration", "fCrosshairPosX",			0.0f } );
	settings->RegisterBinding( { &fCrosshairPosY,			Settings::BindingType::kFloat,		"Calibration", "fCrosshairPosY",			0.0f } );
	settings->RegisterBinding( { &fRayPenetrationDepth,		Settings::BindingType::kFloat,		"Calibration", "fRayPenetrationDepth",		0.2f } );

	settings->RegisterBinding( { &fPitchBias,				Settings::BindingType::kFloat,		"AngleCorrection", "fPitchBias",				0.0f } );
	settings->RegisterBinding( { &fHeightBias,				Settings::BindingType::kFloat,		"AngleCorrection", "fHeightBias",				0.0f } );
	settings->RegisterBinding( { &fBiasFadingStart,			Settings::BindingType::kFloat,		"AngleCorrection", "fBiasFadingStart",			45.0f } );
	settings->RegisterBinding( { &fMaxPitchAdjustmentSpeed,	Settings::BindingType::kFloat,		"AngleCorrection", "fMaxPitchAdjustmentSpeed",	360.0f } );
	settings->RegisterBinding( { &fMountedYawTolerance,		Settings::BindingType::kFloat,		"AngleCorrection", "fMountedYawTolerance",		15.0f } );

	settings->RegisterBinding( { &bEnableLogging,			Settings::BindingType::kBoolean,	"Debug", "bEnableLogging",				false } );
	settings->RegisterBinding( { &bShowTargetPredictionInfo,Settings::BindingType::kBoolean,	"Debug", "bShowTargetPredictionInfo",	false } );
	settings->RegisterBinding( { &bShowCrosshairRayImpact,	Settings::BindingType::kBoolean,	"Debug", "bShowCrosshairRayImpact",		false } );
	
	settings->Load( iniDefault );

	CSimpleIniA iniUser;
	iniUser.SetUnicode();
	if( iniUser.LoadFile( L"Data/MCM/Settings/HitWhereYouAim.ini" ) == SI_OK )
	{
		settings->Load( iniUser );
	}
}
