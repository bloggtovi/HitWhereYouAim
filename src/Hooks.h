#pragma once

#include "Utils.h"
#include "BallisticSolver.h"
#include "Settings.h"

enum class PitchCorrectionMode
{
	kAutomatic,
	kFixedAngle,
};

static constexpr float degToRad = std::numbers::pi_v<float> / 180.0f;
static constexpr float meterToGameUnit = 1.0f / 0.0142875f;
static RE::NiPoint3 lastRayCoord = {0,0,0};
static uint32_t rayImpactExpire = 0;
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
			
			auto playerCamera		= RE::PlayerCamera::GetSingleton();
			auto cameraState		= playerCamera->currentState.get();
			auto thirdPersonState	= skyrim_cast<RE::ThirdPersonState*>( cameraState );
			bool isFreeRotation		= thirdPersonState && thirdPersonState->GetFreeRotationMode();
			if( Settings::bEnabled && !isFreeRotation &&
				(
				cameraState->id == RE::CameraState::kFirstPerson ||
				cameraState->id == RE::CameraState::kThirdPerson ||
				cameraState->id == RE::CameraState::kMount
				))
			{
				bool isFixedAngle		= Settings::bEnabledPitchCorrection &&
										 (PitchCorrectionMode)Settings::iPitchCorrectionMode == PitchCorrectionMode::kFixedAngle;
				bool isAutoPitch		= Settings::bEnabledPitchCorrection &&
										 (PitchCorrectionMode)Settings::iPitchCorrectionMode == PitchCorrectionMode::kAutomatic;

				auto camera				= RE::Main::WorldRootCamera();
				auto screenSize			= RE::BSGraphics::Renderer::GetSingleton()->GetScreenSize();
				auto screenHalfWidth	= screenSize.width  / 2;
				auto screenHalfHeight	= screenSize.height / 2;
				int32_t crosshairX		= (int32_t)(screenHalfWidth  + ( Settings::fCrosshairPosX * screenHalfWidth ));
				int32_t crosshairY		= (int32_t)(screenHalfHeight + ( Settings::fCrosshairPosY * screenHalfHeight ));
				
				float aimZeroDistance	= Settings::fMaximumTargetDistance * meterToGameUnit;
				RE::NiPoint3 fireOrigin = a_origin ? *a_origin : a_launchNode->world.translate;

				RE::NiPoint3 aimLocation;
				RE::bhkPickData pickData;
				auto target = GetScreenRaycastTarget(
					crosshairX,
					crosshairY,
					a_projectile->GetCollisionGroup(),
					a_projectile->GetProjectileRuntimeData().spell ? RE::COL_LAYER::kSpell : RE::COL_LAYER::kProjectile,
					&aimLocation,
					&pickData );

				RE::NiPoint3 rayEnd = ((RE::NiPoint3&)(pickData.rayInput.to.quad.m128_f32) / havokScaling);
				auto impactDepth = GetImpactDistanceAheadOfProjectileOrigin( camera->world.translate, rayEnd, aimLocation, fireOrigin );
				REX::DEBUG( "Crosshair impact depth = {}", impactDepth );

				// Cancel launch angle modification if the ray impact is behind the projectile origin
				if( impactDepth > 0 )
				{
					if( Settings::bTargetDepthPrediction )
					{
						// Clamp depth to potential target if there is one
						auto [potentialTarget,predictPos] = GetPotentialPlayerTarget( aimZeroDistance, a_projectile, Settings::fPredictionWindowWidth, Settings::fPredictionWindowHeight );
						if( potentialTarget )
						{
							auto predictedDist = (predictPos - a_player->GetPosition()).Length();
							aimZeroDistance = predictedDist;

							REX::DEBUG( "Predicted aim target: {}, distance: {}", potentialTarget->GetDisplayFullName(), aimZeroDistance );
						}
					}

					aimLocation = ClampVectorFromOrigin( RE::Main::WorldRootCamera()->world.translate, aimLocation, aimZeroDistance );

					// Add ray penetration
					aimLocation = ExtendVectorFromOrigin( camera->world.translate, aimLocation, Settings::fRayPenetrationDepth * meterToGameUnit );

					RE::NiPoint3 targetVelocity;

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
					// Use ballistic solver to solve the aim pitch
					if( isAutoPitch ||	Settings::bEnabledTargetLeading )
					{
						// Zero out the velocity prediction if not needed
						if( !Settings::bEnabledTargetLeading )
							targetVelocity = { 0, 0, 0 };

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
					}

					if( !firingAngleCalculated )
					{
						// Override angle
						REX::DEBUG( "Aim assist mode: Default" );
						CalculateProjectileAngles( fireOrigin, aimLocation,	*out_pitch, *out_yaw );

						// Pitch offset for projectile with gravity in fixed angle mode
						if( isFixedAngle && a_projectile->GetGravity() > 0.0f )
						{
							*out_pitch -= Settings::fPitchOffsetDegree * degToRad;
						}
					}

					// Ignore angle correction greater than limit when using target leading mode
					if( Settings::bEnabledTargetLeading )
					{
						
						float angleDeltaLimit = Settings::fMaxLeadingAngle * ( std::numbers::pi_v<float> / 180.0f );
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

				lastRayCoord = aimLocation;
				rayImpactExpire = RE::BSTimer::GetSingleton()->smoothedRunTimeMS + 5000;
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
			if( Settings::bShowCrosshairRayImpact )
			{
				auto currentTimeMS = RE::BSTimer::GetSingleton()->smoothedRunTimeMS;
				if( rayImpactExpire > currentTimeMS && lastRayCoord != RE::NiPoint3::Zero() )
					SetCrosshairToWorldPosition( lastRayCoord );
				else
					ResetCrosshairPosition();
			}

			// Try to make the aiming pose align with the target
			if( Settings::bEnabled && a_process == player->currentProcess )
			{
				// Initialize
				if( currentPitch == FLT_MAX )
					currentPitch = a_pitch;

				if( player->GetWeaponState() == RE::WEAPON_STATE::kDrawn &&
					player->GetAttackState() >= RE::ATTACK_STATE_ENUM::kBowDraw &&
					player->GetAttackState() <= RE::ATTACK_STATE_ENUM::kBowFollowThrough )
				{
					auto screenSize			= RE::BSGraphics::Renderer::GetSingleton()->GetScreenSize();
					auto screenHalfWidth	= screenSize.width  / 2;
					auto screenHalfHeight	= screenSize.height / 2;
					int32_t crosshairX		= (int32_t)(screenHalfWidth  + ( Settings::fCrosshairPosX * screenHalfWidth ));
					int32_t crosshairY		= (int32_t)(screenHalfHeight + ( Settings::fCrosshairPosY * screenHalfHeight ));

					RE::NiPoint3 lookTarget{0,0,0};
					RE::CFilter filter;
					player->currentProcess->GetCharController()->GetCollisionFilterInfo( filter );
					GetScreenRaycastTarget(
						crosshairX,
						crosshairY,
						filter.GetSystemGroup(),
						RE::COL_LAYER::kProjectile,
						&lookTarget );

					float lookPitch = 0, lookYaw;
					auto aimPos = player->GetPosition();

					if( Settings::bEnabledPitchCorrection )
						aimPos.z += player->GetInfoRuntimeData().eyeHeight;

					// Add penetration
					lookTarget = ExtendVectorFromOrigin( aimPos, lookTarget, Settings::fRayPenetrationDepth );

					// Add z-bias
					lookTarget.z += Settings::fHeightBias;

					// 3rd model firing angle to the crosshair ray
					CalculateProjectileAngles( aimPos, lookTarget, lookPitch, lookYaw );

					// Fixed pitch adjustment
					if( (PitchCorrectionMode)Settings::iPitchCorrectionMode == PitchCorrectionMode::kFixedAngle )
					{
						lookPitch -= Settings::fPitchOffsetDegree * degToRad;
					}

					// 3rd person pitch bias
					float pitchBias = Settings::fPitchBias * degToRad;

					// Reduce bias for extremes because they're already correct.
					const float pi = std::numbers::pi_v<float>;
					float t = std::clamp(( std::abs(lookPitch) - pi * 0.25f) / ( pi * 0.25f ), 0.0f, 1.0f);
					float biasFactor = 1.0f - t * t * ( 3.0f - 2.0f * t );
					pitchBias *= biasFactor;
					lookPitch -= pitchBias;

					a_pitch = -lookPitch;

					// Don't snap into place. Do it smoothly.
					float maxRads = Settings::fMaxPitchAdjustmentSpeed * degToRad;
					float maxDelta = maxRads * RE::BSTimer::GetSingleton()->delta;
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
