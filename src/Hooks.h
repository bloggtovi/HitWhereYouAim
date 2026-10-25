#pragma once

#include "Utils.h"
#include "BallisticSolver.h"
#include "Settings.h"
#include "TrueHUDArrowTrail.h"
#include "CandidateBoundsDebug.h"
#include "TargetLock.h"

struct CrosshairRaycastCache
{
	RE::NiPoint3		position;
	RE::bhkPickData		pickData;
	RE::TESObjectREFR*	target;
	RE::COL_LAYER		collisionLayer;
	uint32_t			collisionGroup;
	uint32_t			frameCreated;

	CrosshairRaycastCache()
	{
		memset( this, 0, sizeof(CrosshairRaycastCache) );
	}
};

static constexpr float degToRad = std::numbers::pi_v<float> / 180.0f;
static constexpr float meterToGameUnit = 1.0f / 0.0142875f;
static RE::NiPoint3 lastRayCoord = {0,0,0};
static uint32_t rayImpactExpire = 0;
namespace Hooks
{
	static RE::TESObjectREFR* GetCrosshairRaycastTarget(
		uint32_t a_collisionGroup,
		RE::COL_LAYER a_collisionLayer,
		RE::NiPoint3* out_hitLocation = nullptr,
		RE::bhkPickData* out_pickData = nullptr);

	struct PlayerProjectileCalculateLaunchAngleHook
	{
		static float AngleDifference( float a_corrected, float a_original )
		{
			return std::remainder( a_corrected - a_original, 2.0F * std::numbers::pi_v<float> );
		}

		static void CalculatePlayerProjectileLaunchAngle( RE::PlayerCharacter* a_player, RE::Projectile* a_projectile, RE::NiAVObject* a_launchNode,
			float* out_yaw, float* out_pitch, RE::NiPoint3* a_origin, float a_unk7, float a_unk8 );

		static void thunk( RE::PlayerCharacter* a_player, RE::Projectile* a_projectile, RE::NiAVObject* a_launchNode,
			float* out_yaw, float* out_pitch, RE::NiPoint3* a_origin, float a_unk7, float a_unk8 )
		{
			func( a_player, a_projectile, a_launchNode, out_yaw, out_pitch, a_origin, a_unk7, a_unk8 ); // SkyrimSE.exe+0x764F10

			CalculatePlayerProjectileLaunchAngle( a_player, a_projectile, a_launchNode, out_yaw, out_pitch, a_origin, a_unk7, a_unk8 );
		}
		static inline REL::Relocation<decltype(thunk)> func;
		static void Install()
		{
			auto addr = util::GetInjectionAddress( RELOCATION_RAW(43009,44200), 
				"F3 0F 11 ?? 24 38 F3 0F 11 ?? 24 30 48 89 ?? 24 28 48 89 ?? 24 20 4C 8B C5 48 8B D7 48 8B 0D ?? ?? ?? ?? E8", 0x23, "E8" );

			// Another plugin may already have patched this site. Skip the hook instead of
			// aborting the game; the rest of the plugin keeps working.
			if( !addr )
			{
				REX::WARN( "PlayerProjectileCalculateLaunchAngleHook skipped: target instruction not found (patched by another plugin?)" );
				return;
			}

			stl::write_thunk_call<PlayerProjectileCalculateLaunchAngleHook>( addr );
		}
	};

	struct ConcentrationSpellAngleHook
	{
		using func_t = float( RE::Projectile*, float );
		static inline float playerYaw = FLT_MAX;
		static inline float playerPitch = FLT_MAX;
		static float SetAimPitch( RE::Projectile* a_projectile, float a_angle, func_t* func );
		static float SetAimYaw( RE::Projectile* a_projectile, float a_angle, func_t* func );

		static float SetAimPitchFlame( RE::Projectile* a_projectile, float a_angle ) { return SetAimPitch( a_projectile, a_angle, (func_t*)funcPitchFlame ); }
		static float SetAimYawFlame( RE::Projectile* a_projectile, float a_angle ) { return SetAimYaw( a_projectile, a_angle, (func_t*)funcYawFlame ); }
		static float SetAimPitchBeam( RE::Projectile* a_projectile, float a_angle ) { return SetAimPitch( a_projectile, a_angle, (func_t*)funcPitchBeam ); }
		static float SetAimYawBeam( RE::Projectile* a_projectile, float a_angle ) { return SetAimYaw( a_projectile, a_angle, (func_t*)funcYawBeam ); }
		static inline uintptr_t funcPitchFlame;
		static inline uintptr_t funcYawFlame;
		static inline uintptr_t funcPitchBeam;
		static inline uintptr_t funcYawBeam;
		static void Install()
		{
			auto addr = util::GetInjectionAddress( RELOCATION_RAW(42727,43894), 
				"48 8B CD E8 ?? ?? ?? ?? 0F 28 C8 48 8B CE E8 ?? ?? ?? ?? 48 8B CD E8 ?? ?? ?? ?? 0F 28 C8 48 8B CE E8 ?? ?? ?? ??" );

			auto& trampoline = SKSE::GetTrampoline();
			if( addr )
			{
				funcYawFlame = trampoline.write_call<5>( addr + 0xE, SetAimYawFlame );
				funcPitchFlame = trampoline.write_call<5>( addr + 0x21, SetAimPitchFlame );
			}
			else
			{
				REX::WARN( "ConcentrationSpellAngleHook(Flame) skipped: target instruction not found (patched by another plugin?)" );
			}

			addr = util::GetInjectionAddress( RELOCATION_RAW(42586,43749),
				"49 8B CE E8 ?? ?? ?? ?? 0F 28 C8 48 8B CE E8 ?? ?? ?? ?? 49 8B CE E8 ?? ?? ?? ?? 0F 28 C8 48 8B CE E8 ?? ?? ?? ??" );

			if( addr )
			{
				funcYawBeam = trampoline.write_call<5>( addr + 0xE, SetAimYawBeam );
				funcPitchBeam = trampoline.write_call<5>( addr + 0x21, SetAimPitchBeam );
			}
			else
			{
				REX::WARN( "ConcentrationSpellAngleHook(Beam) skipped: target instruction not found (patched by another plugin?)" );
			}
		}
	};

	static float currentPitch = FLT_MAX;
	struct PlayerSetAimPitchHook
	{
		static float PlayerGetAimPitch( RE::AIProcess* a_process, const float a_pitch );

		static void thunk( RE::AIProcess* a_process, float a_pitch )
		{
			a_pitch = PlayerGetAimPitch( a_process, a_pitch );

			return func( a_process, a_pitch );
		}
		static inline REL::Relocation<decltype(thunk)> func;

		static void Install()
		{
			// SE: 36602 0x5EDD50+0x148
			if( Runtime::version == SKSE::RUNTIME_SSE_1_5_97 )
			{
				auto addr = REL::Relocation( REL::ID( 36602 ), 0x148 ).address();
				stl::write_thunk_call<PlayerSetAimPitchHook>( addr );
			}
			else
			{
				auto addr = util::GetInjectionAddress( 37356, 
					"F3 0F 10 4F 48 41 0F 57 C9 48 8B 8F F8 00 00 00 E8 ?? ?? ?? ??", 0x10, "E8" );

				if( !addr )
				{
					REX::WARN( "PlayerSetAimPitchHook skipped: target instruction not found (patched by another plugin?)" );
					return;
				}

				stl::write_thunk_call<PlayerSetAimPitchHook>( addr );
			}
		}
	};

	struct ProjectileInitHook
	{
		static void OverrideProjectileInit( RE::Projectile* a_this, RE::NiPoint3& a_angle );

		static void thunk( RE::Projectile* a_this )
		{
			// Copy old unmodified angle
			RE::NiPoint3 angle = a_this->data.angle;

			// Run original (TDM overrides the data here)
			func( a_this );

			OverrideProjectileInit( a_this, angle );
		}

		static inline REL::Relocation<decltype(thunk)> func;
		static void Install()
		{
			if( Runtime::version == SKSE::RUNTIME_SSE_1_5_97 )
			{
				auto addr = REL::Relocation( REL::ID( 43030 ), 0x3B8 ).address();
				stl::write_thunk_call<ProjectileInitHook>( addr );
			}
			else
			{
				auto addr = util::GetInjectionAddress( 44222,
					"48 8B 01 FF 50 08 48 8B CF E8 ?? ?? ?? ?? 48 8B D7", 0x9, "E8" );

				if( !addr )
				{
					REX::WARN( "ProjectileInitHook skipped: target instruction not found (patched by another plugin?)" );
					return;
				}

				stl::write_thunk_call<ProjectileInitHook>( addr );
			}
		}
	};

	struct PlayerUpdateHook
	{
		static void thunk( RE::PlayerCharacter* a_this, float a_delta )
		{
			func( a_this, a_delta );
			TargetLock::Poll();
			TargetLock::Update( a_delta );
			TrueHUDArrowTrail::Update();
			CandidateBoundsDebug::Update();
		}

		static inline REL::Relocation<decltype( thunk )> func;
		static inline size_t size = 0xAD;
		static void Install()
		{
			REL::Relocation<std::uintptr_t> vtable{ RE::VTABLE_PlayerCharacter[0] };
			stl::write_vfunc<RE::PlayerCharacter,PlayerUpdateHook>();

			REX::DEBUG( "PlayerCharacter::Update hook installed" );
		}
	};

	bool Install();

	namespace Test
	{
		struct SetGraphVarFloatHook
		{
			static bool thunk( RE::TESForm* a_this, RE::BSFixedString& a_name, float a_value )
			{
				auto ref = a_this->As<RE::TESObjectREFR>();
				if( ref )
				{
					REX::INFO( "SetGraphVariableFloat {} = {}"sv, a_name.c_str(), a_value);
				}
				
				return func( a_this, a_name, a_value );
			}
			static inline REL::Relocation<decltype(thunk)> func;

			static void Install()
			{
				auto addr = (void*)REL::Relocation<uintptr_t>( REL::ID(32887) ).address();
				MH_CreateHook( addr, thunk,	(void**)&func );
				MH_EnableHook( addr );
			}
		};

		struct SetGraphVarNiPoint3Hook
		{
			static bool thunk( RE::TESForm* a_this, RE::BSFixedString& a_name, RE::NiPoint3& a_value )
			{

				REX::INFO( "SetGraphVariableNiPoint3: {} = ({},{},{})", a_name.c_str(), a_value.x, a_value.y, a_value.z);
				return func( a_this, a_name, a_value );
			}
			static inline REL::Relocation<decltype(thunk)> func;

			static void Install()
			{
				auto addr = (void*)REL::Relocation<uintptr_t>( REL::ID(32888) ).address();
				MH_CreateHook( addr, thunk,	(void**)&func );
				MH_EnableHook( addr );
			}
		};
	}

	namespace Unused
	{
		// 1.6.1170 44100	8273456	0x7E3E30 Projectile ctor -- Unused
		struct ProjectileConstructorHook
		{
			static RE::Projectile* thunk( RE::Projectile* a_this, RE::Projectile::LaunchData* a_launchData )
			{
				//a_launchData->angleX = -1.57;
				return func( a_this, a_launchData );
			}
			static inline REL::Relocation<decltype(thunk)> func;

			static void Install()
			{
				auto addr = (void*)REL::Relocation<uintptr_t>( REL::ID(44100) ).address();
				MH_CreateHook( addr, thunk,	(void**)&func );
				MH_EnableHook( addr );
			}
		};

		struct BeamProjectileUpdateHook
		{
			static void thunk( RE::Projectile* a_projectile, float a_delta )
			{
				return func( a_projectile, a_delta );
			}
			static inline REL::Relocation<decltype(thunk)> func;
			static const size_t size = 0xAB;
			static void Install()
			{
				stl::write_vfunc<RE::BeamProjectile, 0, BeamProjectileUpdateHook>();
			}
		};

		// 38009 SkyrimSE.exe+0x6A1370+0x682
		struct GetLookingAtHook
		{
			static RE::NiPoint3* thunk( const RE::Actor* a_player, RE::NiPoint3* out_result )
			{
				RE::NiPoint3* result = func( a_player, out_result );
				return result;
			}
			static inline REL::Relocation<decltype(thunk)> func;

			static void Install()
			{
				auto addr = util::GetInjectionAddress( 38009, 
					"49 8B 06 48 8D 55 DF 49 8B CE FF 90 ?? ?? ?? ?? F3 0F 10 00", 0xA, "FF 90" );

				if( !addr )
				{
					REX::FAIL( "Cannot find target instruction for GetLookingAtHook" );
				}

				stl::write_thunk_call_virtual<RE::PlayerCharacter,GetLookingAtHook>( addr, 0, 0x5B );
			}
		};
	}
}
