#pragma once

#include "Utils.h"
#include "BallisticSolver.h"

namespace Hooks
{
	struct PlayerProjectileCalculateLaunchAngleHook
	{
		static float AngleDifference( float a_corrected, float a_original )
		{
			return std::remainder( a_corrected - a_original, 2.0F * std::numbers::pi_v<float> );
		}

		static void thunk( RE::PlayerCharacter* a_player, RE::Projectile* a_projectile, RE::NiAVObject* a_launchNode,
			float* out_yaw, float* out_pitch, RE::NiPoint3* a_origin, float a_unk7, float a_unk8 )
		{
			func( a_player, a_projectile, a_launchNode, out_yaw, out_pitch, a_origin, a_unk7, a_unk8 ); // SkyrimSE.exe+0x764F10
			
			auto playerCamera = RE::PlayerCamera::GetSingleton();
			auto cameraState = playerCamera->currentState.get();
			if( cameraState->id == RE::CameraState::kFirstPerson ||
				cameraState->id == RE::CameraState::kThirdPerson ||
				cameraState->id == RE::CameraState::kMount )
			{
				auto screenSize = RE::BSGraphics::Renderer::GetSingleton()->GetScreenSize();

				float aimZeroDistance = 10000.0f;

				RE::NiPoint3 aimLocation;
				RE::bhkPickData pickData;
				auto target = GetScreenRaycastTarget(
					screenSize.width / 2,
					screenSize.height / 2,
					a_projectile->GetCollisionGroup(),
					a_projectile->GetProjectileRuntimeData().spell ? RE::COL_LAYER::kSpell : RE::COL_LAYER::kProjectile,
					&aimLocation,
					&pickData );

				// Clamp depth to potential target if there is one
				auto potentialTarget = GetPotentialPlayerTarget( 10000.0f );
				if( potentialTarget )
				{
					auto shooterPos = a_player->GetPosition();
					auto potentialPos = potentialTarget->GetPosition();
					auto projectileSpeed = a_projectile->GetSpeed();
					float flightTime = CalculateProjectileFlightTimeToTarget( shooterPos, potentialPos, projectileSpeed );

					RE::NiPoint3 potentialTargetVelocity;
					potentialTarget->GetLinearVelocity( potentialTargetVelocity );

					auto predictedPos = potentialPos + potentialTargetVelocity * flightTime;
					auto predictedDist = (predictedPos - shooterPos).Length();
					aimZeroDistance = predictedDist;

					REX::DEBUG( "Predicted aim target: {}, distance: {}", potentialTarget->GetDisplayFullName(), aimZeroDistance );
				}

				aimLocation = ClampVectorFromOrigin( RE::Main::WorldRootCamera()->world.translate, aimLocation, aimZeroDistance );

				// Add ray penetration
				auto camera = RE::Main::WorldRootCamera();
				aimLocation = ExtendVectorFromOrigin( camera->world.translate, aimLocation, 30.0f );

				RE::NiPoint3 targetVelocity;
				RE::NiPoint3 fireOrigin = a_launchNode ? a_launchNode->world.translate : *a_origin;

				// Get target velocity to calculate intercept
				if( target )
				{
					target->GetLinearVelocity( targetVelocity );
					auto targetName =  target->GetDisplayFullName();
					REX::DEBUG( "Project aim correction ray target: {}", targetName ? targetName : "" );
				}

				bool firingAngleCalculated = false;
				RE::NiPoint3 predicted;
				float originalPitch = *out_pitch;
				float originalYaw = *out_yaw;
				if( CalculatePredictedProjectileAimPoint(
					&predicted, &fireOrigin,
					&aimLocation,
					&targetVelocity,
					a_projectile->GetSpeed(),
					a_projectile->GetGravity() * GetWorldGravity(),
					false ) )
				{
					REX::DEBUG( "Aim assist mode: Skyrim" );
					aimLocation = predicted;
				}
				else // Use custom solver if the Skyrim solver cannot be used
				{
					REX::DEBUG( "Aim assist mode: Ballistic Solver" );
					firingAngleCalculated = SolveBallisticIntercept( 
						fireOrigin, aimLocation, &targetVelocity, 
						a_projectile->GetSpeed(), a_projectile->GetGravity() * GetWorldGravity(), 
						aimLocation, out_pitch, out_yaw );
				}

				if( !firingAngleCalculated )
				{
					// Override angle
					REX::DEBUG( "Aim assist mode: Default" );
					CalculateProjectileAngles( fireOrigin, aimLocation,	*out_pitch, *out_yaw );
				}

				// Ignore angle correction greater than limit
				float constexpr angleDeltaLimit = 45.0f * ( std::numbers::pi_v<float> / 180.0f );
				float pitchDiff = AngleDifference( *out_pitch, originalPitch );
				float yawDiff = AngleDifference( *out_yaw, originalYaw );
				if( pitchDiff >  angleDeltaLimit || yawDiff >  angleDeltaLimit ||
					pitchDiff < -angleDeltaLimit || yawDiff < -angleDeltaLimit )
				{
					*out_pitch = originalPitch;
					*out_yaw = originalYaw;
				}
			}
		}
		static inline REL::Relocation<decltype(thunk)> func;
		static void Install()
		{
			auto addr = util::GetInjectionAddress( RELOCATION_RAW(44200,44200), 
				"F3 0F 11 ?? 24 38 F3 0F 11 ?? 24 30 48 89 ?? 24 28 48 89 ?? 24 20 4C 8B C5 48 8B D7 48 8B 0D ?? ?? ?? ?? E8", 0x23, "E8" );

			if( !addr )
				REX::FAIL( "Cannot find target instruction for PlayerProjectileCalculateLaunchAngleHook" );

			stl::write_thunk_call<PlayerProjectileCalculateLaunchAngleHook>( addr );
		}
	};


	static float currentPitch = FLT_MAX;
	struct PlayerSetAimPitchHook
	{
		static void thunk( RE::AIProcess* a_process, float a_pitch )
		{
			RE::PlayerCharacter* player = RE::PlayerCharacter::GetSingleton();
			if( a_process == player->currentProcess )
			{
				// Initialize
				if( currentPitch == FLT_MAX )
					currentPitch = a_pitch;

				if( player->GetWeaponState() == RE::WEAPON_STATE::kDrawn &&
					player->GetAttackState() >= RE::ATTACK_STATE_ENUM::kBowDraw &&
					player->GetAttackState() <= RE::ATTACK_STATE_ENUM::kBowFollowThrough )
				{
					auto screenSize = RE::BSGraphics::Renderer::GetSingleton()->GetScreenSize();
					
					RE::NiPoint3 lookTarget{0,0,0};
					RE::CFilter filter;
					player->currentProcess->GetCharController()->GetCollisionFilterInfo( filter );
					GetScreenRaycastTarget(
						screenSize.width / 2,
						screenSize.height / 2,
						filter.GetSystemGroup(),
						RE::COL_LAYER::kProjectile,
						&lookTarget );

					float lookPitch = 0, lookYaw;
					auto aimPos = player->GetPosition();
					aimPos.z += player->GetInfoRuntimeData().eyeHeight;

					lookTarget = ExtendVectorFromOrigin( aimPos, lookTarget, 30.0f );

					// Add z-bias
					lookTarget.z += 30;

					CalculateProjectileAngles( aimPos, lookTarget, lookPitch, lookYaw );

					// Pitch bias
					float pitchBias = 0.1f;

					// Reduce bias for extremes because they're already correct.
					const float pi = std::numbers::pi_v<float>;
					float t = std::clamp(( std::abs(lookPitch) - pi * 0.25f) / ( pi * 0.25f ), 0.0f, 1.0f);
					float biasFactor = 1.0f - t * t * ( 3.0f - 2.0f * t );
					pitchBias *= biasFactor;
					lookPitch -= pitchBias;

					a_pitch = -lookPitch;

					// Don't snap into place. Do it smoothly.
					float maxDelta = 10.0f * RE::BSTimer::GetSingleton()->delta;
					float delta = a_pitch - currentPitch;
					if( abs( delta ) < maxDelta )
						currentPitch = a_pitch;
					else
						currentPitch += copysign( maxDelta, delta );

					a_pitch = currentPitch;
				}
				else
				{
					currentPitch = 0; // Reset when not firing
				}
			}

			return func( a_process, a_pitch );
		}
		static inline REL::Relocation<decltype(thunk)> func;

		static void Install()
		{
			auto addr = util::GetInjectionAddress( 37356, 
				"F3 0F 10 4F 48 41 0F 57 C9 48 8B 8F F8 00 00 00 E8 ?? ?? ?? ??", 0x10, "E8" );

			if( !addr )
			{
				REX::FAIL( "Cannot find target instruction for PlayerSetAimPitchHook" );
			}

			stl::write_thunk_call<PlayerSetAimPitchHook>( addr );
		}
	};

	bool Install();

	namespace Test
	{
		struct PlayerSetGraphVarFloatHook
		{
			static bool thunk( RE::PlayerCharacter* a_this, RE::BSFixedString& a_name, float a_value )
			{
				REX::INFO( "Player::SetGraphVariableFloat {} = {}"sv, a_name.c_str(), a_value);
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

		struct PlayerSetGraphVarNiPoint3Hook
		{
			static bool thunk( RE::PlayerCharacter* a_this, RE::BSFixedString& a_name, RE::NiPoint3& a_value )
			{

				REX::INFO( "Player::SetGraphVariableNiPoint3: {} = ({},{},{})", a_name.c_str(), a_value.x, a_value.y, a_value.z);
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

		struct ProjectileUpdateHook
		{
			static uint64_t thunk( RE::Projectile* a_projectile, float a_delta )
			{
				return func( a_projectile, a_delta );
			}

			static inline REL::Relocation<decltype(thunk)> func;
			static const size_t size = 0xB5;
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

		struct ProjectileUpdateHook
		{
			static uint64_t thunk( RE::Projectile* a_projectile, float a_delta )
			{
				return func( a_projectile, a_delta );
			}

			static inline REL::Relocation<decltype(thunk)> func;
			static const size_t size = 0xB5;
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
