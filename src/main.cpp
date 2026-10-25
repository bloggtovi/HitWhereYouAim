#include "PCH.h"
#include "Version.h"
#include "Hooks.h"
#include "Settings.h"
#include "HitWhereYouAimAPI.h"
#include "TrueHUDArrowTrail.h"
#include "CandidateBoundsDebug.h"
#include "TargetLock.h"

float crosshairOriginalX = FLT_MAX;
float crosshairOriginalY = FLT_MAX;

extern "C" __declspec(dllexport) bool SKSEPlugin_Query( const SKSE::QueryInterface* a_skse, SKSE::PluginInfo* a_info )
{
	a_info->infoVersion = SKSE::PluginInfo::kVersion;
	a_info->name = Version::NAME.data();
	a_info->version = REL::Version{
		Version::MAJOR,
		Version::MINOR,
		Version::PATCH
	}.pack();

	if( a_skse->IsEditor() )
		return false;

	return true;
}

SKSE_PLUGIN_VERSION = []() {
	SKSE::PluginVersionData v;
	v.PluginVersion(REL::Version{ Version::MAJOR, Version::MINOR, Version::PATCH });
	v.PluginName( Version::NAME.data() );
	v.AuthorName( Version::AUTHOR.data() );
	v.UsesAddressLibrary();
	v.UsesUpdatedStructs();
	v.UsesSigScanning();

	return v;
}();

namespace
{
	class CellChangeHandler final : public RE::BSTEventSink<RE::BGSActorCellEvent>
	{
	public:
		static CellChangeHandler* GetSingleton()
		{
			static CellChangeHandler singleton;
			return &singleton;
		}
		RE::BSEventNotifyControl ProcessEvent( const RE::BGSActorCellEvent* a_event, RE::BSTEventSource<RE::BGSActorCellEvent>* ) override
		{
			switch( a_event->flags.get() )
			{
			case RE::BGSActorCellEvent::CellFlag::kEnter:
				TrueHUDArrowTrail::Stop();
				TargetLock::Unlock();
				break;
			}

			return RE::BSEventNotifyControl::kContinue;
		}
	};

	static void SKSEMessageHandler( SKSE::MessagingInterface::Message* a_message ) 
	{
		switch( a_message->type )
		{
		case SKSE::MessagingInterface::kDataLoaded:
			REX::INFO( "{} {} loaded", Version::PROJECT.data(), Version::NAME.data());
			RE::PlayerCharacter::GetSingleton()->AsBGSActorCellEventSource()->AddEventSink( CellChangeHandler::GetSingleton() );
			break;
		case SKSE::MessagingInterface::kInputLoaded:
			TargetLock::RegisterInputSink();
			break;
		case SKSE::MessagingInterface::kPreLoadGame:
			TrueHUDArrowTrail::Stop();
			TargetLock::Unlock();
			ProjectileDiagnostics::Clear();
			ProjectileGuidance::Clear();
			break;
		case SKSE::MessagingInterface::kPostLoad:
			TrueHUDArrowTrail::Install();
			CandidateBoundsDebug::Install();
			TargetLock::Install();
			break;
		case SKSE::MessagingInterface::kPostPostLoad:
			// TDM Compatibility hook
			Hooks::ProjectileInitHook::Install();
			break;
		}
	}

	static void LogToConsole( RE::StaticFunctionTag*, std::string a_str )
	{
		RE::ConsoleLog::GetSingleton()->Print( a_str.c_str() );
	}

	static void ReloadSettings( RE::StaticFunctionTag* )
	{
		Settings::Load();
		ResetCrosshairPosition();
		TrueHUDArrowTrail::SetEnabled( Settings::bShowLastProjectilePath );
		CandidateBoundsDebug::SetEnabled( Settings::bEnabledTargetPrediction );
		REX::DEBUG( "Settings reloaded by MCM" );
	}

	static bool RegisterFunction( RE::BSScript::IVirtualMachine* a_vm )
	{
		a_vm->RegisterFunction( "ReloadSettings", "HitWhereYouAim", ReloadSettings );
		a_vm->RegisterFunction( "LogToConsole", "HitWhereYouAim", LogToConsole );

		return true;
	}
}

SKSEPluginLoad( const SKSE::LoadInterface* a_skse )
{
#ifdef _DEBUG
	//while( !IsDebuggerPresent() ) Sleep(100);
#endif
	SKSE::Init(a_skse, {.log = true,
		.logName = Version::PROJECT.data(),
		.trampoline = true,
		.trampolineSize = 450 } );

	Runtime::version = a_skse->RuntimeVersion();

	if( auto* messaging = SKSE::GetMessagingInterface() )
		messaging->RegisterListener( SKSEMessageHandler );
	else
		return false;

	if( auto papyrusInterface = SKSE::GetPapyrusInterface() )
		papyrusInterface->Register( RegisterFunction );
	else
		return false;

	Hooks::Install();
	ReloadSettings( nullptr );

	return true;
}
