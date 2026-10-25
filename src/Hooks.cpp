#include "Hooks.h"

bool Hooks::Install()
{
	MH_Initialize();

	PlayerProjectileCalculateLaunchAngleHook::Install();
	ConcentrationSpellAngleHook::Install();
	PlayerSetAimPitchHook::Install();
	PlayerUpdateHook::Install();

	//Test::SetGraphVarFloatHook::Install();

	return true;
}

// Crosshair raycast position cache
CrosshairRaycastCache g_crosshairCache;
static float pitchVelocity = 0.0f;

RE::TESObjectREFR* Hooks::GetCrosshairRaycastTarget( uint32_t a_collisionGroup, RE::COL_LAYER a_collisionLayer, RE::NiPoint3* out_hitLocation, RE::bhkPickData* out_pickData )
{
	auto currentFrame		= RE::BSGraphics::State::GetSingleton()->GetFrameCount();

	// Invalidate cache
	if( g_crosshairCache.frameCreated != currentFrame ||
		g_crosshairCache.collisionGroup != a_collisionGroup ||
		g_crosshairCache.collisionLayer != a_collisionLayer )
	{
		auto screenSize			= RE::BSGraphics::Renderer::GetSingleton()->GetScreenSize();
		auto screenHalfWidth	= screenSize.width  / 2;
		auto screenHalfHeight	= screenSize.height / 2;
		int32_t crosshairX		= (int32_t)(screenHalfWidth  + ( Settings::fCrosshairPosX * screenHalfWidth ));
		int32_t crosshairY		= (int32_t)(screenHalfHeight + ( Settings::fCrosshairPosY * screenHalfHeight ));

		g_crosshairCache.target = GetScreenRaycastTarget(
			crosshairX,
			crosshairY,
			a_collisionGroup,
			a_collisionLayer,
			&g_crosshairCache.position,
			&g_crosshairCache.pickData,
			Settings::fMaximumTargetDistance * meterToGameUnit );

		g_crosshairCache.frameCreated = currentFrame;
		g_crosshairCache.collisionGroup = a_collisionGroup;
		g_crosshairCache.collisionLayer = a_collisionLayer;
	}

	if( out_hitLocation ) *out_hitLocation = g_crosshairCache.position;
	if( out_pickData ) *out_pickData = g_crosshairCache.pickData;
	return g_crosshairCache.target;
}

template <class... Args>
static void PrintDebugInfo( std::format_string<Args...> a_format, Args&&... a_args )
{
	if( !Settings::bEnableLogging )
		return;

	std::string message = fmt::vformat( a_format.get(), fmt::make_format_args( a_args... ) );
	REX::DEBUG( message.c_str() );

	if( auto* console = RE::ConsoleLog::GetSingleton() )
		console->Print( message.c_str() );
}

void Hooks::PlayerProjectileCalculateLaunchAngleHook::CalculatePlayerProjectileLaunchAngle(
	[[maybe_unused]] RE::PlayerCharacter* a_player,
	RE::Projectile* a_projectile,
	RE::NiAVObject* a_launchNode,
	float* out_yaw,
	float* out_pitch,
	RE::NiPoint3* a_origin,
	[[maybe_unused]] float a_unk7,
	[[maybe_unused]] float a_unk8 )
{
	if( a_projectile == nullptr )
		return;

	if( a_launchNode == nullptr && a_origin == nullptr )
		return;

	auto playerCamera		= RE::PlayerCamera::GetSingleton();
	auto cameraState		= playerCamera ? playerCamera->currentState.get() : nullptr;
	if( !cameraState || !RE::Main::WorldRootCamera() )
		return;

	auto thirdPersonState	= skyrim_cast<RE::ThirdPersonState*>( cameraState );
	bool isFreeRotation		= thirdPersonState && thirdPersonState->GetFreeRotationMode();
	if( Settings::bEnabled &&
		(
			// Disable if free rotation is enabled for FPS and TPS
			(
				( !isFreeRotation || thirdPersonState->freeRotation.x < 0.1f )
				&&
				(
					cameraState->id == RE::CameraState::kFirstPerson ||
					cameraState->id == RE::CameraState::kThirdPerson
					)
				) ||
			// Allow free rotation for mounted combat
			cameraState->id == RE::CameraState::kMount
			)
		)
	{
		auto camera				= RE::Main::WorldRootCamera();
		float aimZeroDistance	= Settings::fMaximumTargetDistance * meterToGameUnit;
		RE::NiPoint3 fireOrigin = a_origin ? *a_origin : a_launchNode->world.translate;

		RE::NiPoint3 aimLocation;
		RE::bhkPickData pickData;
		auto target = GetCrosshairRaycastTarget(
			a_projectile->GetCollisionGroup(),
			a_projectile->GetProjectileRuntimeData().spell ? RE::COL_LAYER::kSpell : RE::COL_LAYER::kProjectile,
			&aimLocation,
			&pickData );

		RE::NiPoint3 rayEnd = ((RE::NiPoint3&)(pickData.rayInput.to.quad.m128_f32) / havokScaling);
		auto impactDepth = GetImpactDistanceAheadOfProjectileOrigin( camera->world.translate, rayEnd, aimLocation, fireOrigin );
		PrintDebugInfo( "Crosshair impact depth = {}", impactDepth );

		// Cancel launch angle modification if the ray impact is behind the projectile origin
		if( impactDepth > 0 )
		{
			if( Settings::bTargetDepthPrediction )
			{
				// Clamp depth to potential target if there is one
				RE::NiPoint3 rayDelta = camera->world.translate - aimLocation;
				auto [potentialTarget,predictPos] = GetPotentialPlayerTarget(
					rayDelta.Length(),
					a_projectile->GetSpeed(),
					a_projectile->GetGravity(),
					&fireOrigin,
					Settings::fPredictionWindowWidth,
					Settings::fPredictionWindowHeight,
					nullptr, Settings::IsFixedAngleEnabled() );

				if( potentialTarget )
				{
					auto predictedDist = (predictPos - camera->world.translate).Length();
					aimZeroDistance = predictedDist;

					if( Settings::bShowTargetPredictionInfo )
						PrintDebugInfo( "Predicted aim target: {}, distance: {}", potentialTarget->GetDisplayFullName(), aimZeroDistance );
				}
			}

			aimLocation = ClampVectorFromOrigin( RE::Main::WorldRootCamera()->world.translate, aimLocation, aimZeroDistance );

			// Add ray penetration
			aimLocation = ExtendVectorFromOrigin( camera->world.translate, aimLocation, Settings::fRayPenetrationDepth * meterToGameUnit );

			RE::NiPoint3 targetVelocity{ 0, 0, 0 };

			// Get target velocity to calculate intercept
			if( target )
			{
				target->GetLinearVelocity( targetVelocity );
				auto targetName =  target->GetDisplayFullName();
				PrintDebugInfo( "Project aim correction ray target: {}", targetName ? targetName : "" );
			}

			lastRayCoord = aimLocation;

			bool firingAngleCalculated = false;
			RE::NiPoint3 predicted;
			float originalPitch = *out_pitch;
			float originalYaw = *out_yaw;
			// Use ballistic solver to solve the aim pitch
			if( Settings::IsAutomaticPitchEnabled() )
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
					PrintDebugInfo( "Aim assist mode: Skyrim" );
					aimLocation = predicted;
				}
				else // Use custom solver if the Skyrim solver cannot be used
				{
					PrintDebugInfo( "Aim assist mode: Ballistic Solver" );
					firingAngleCalculated = SolveBallisticIntercept( 
						fireOrigin, aimLocation, &targetVelocity, 
						a_projectile->GetSpeed(), a_projectile->GetGravity() * GetWorldGravity(), 
						nullptr, out_pitch, out_yaw );
				}
			}

			if( !firingAngleCalculated )
			{
				// Override angle
				PrintDebugInfo( "Aim assist mode: Default" );
				CalculateProjectileAngles( fireOrigin, aimLocation,	*out_pitch, *out_yaw );

				// Pitch offset for projectile with gravity in fixed angle mode
				if( Settings::IsFixedAngleEnabled() && a_projectile->GetGravity() > 0.0f )
				{
					*out_pitch -= Settings::fPitchOffsetDegree * degToRad;
				}
			}

			// Ignore angle correction greater than limit when using target leading mode
			if( Settings::IsTargetLeadingEnabled() )
			{

				float angleDeltaLimit = Settings::fMaxLeadingAngle * degToRad;
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

		auto equippedObject = a_player->GetEquippedObject( false );
		if( equippedObject && equippedObject->IsWeapon() )
		{
			auto weapon = equippedObject->As<RE::TESObjectWEAP>();
			if(  weapon &&
				(weapon->IsBow() || weapon->IsCrossbow()) )
			{
				*out_pitch -= Settings::fArcheryPitchCalibration * degToRad;
			}
		}

		rayImpactExpire = GetCurrentGameRuntimeMS() + 5000;
	}
}

float Hooks::ConcentrationSpellAngleHook::SetAimPitch( RE::Projectile* a_projectile, float a_angle, func_t* func )
{
	auto shooter = a_projectile->GetProjectileRuntimeData().shooter;
	if( Settings::bEnabled && shooter && shooter.native_handle() == 0x100000 && playerPitch != FLT_MAX )
	{
		auto cameraState = RE::PlayerCamera::GetSingleton()->currentState.get();
		if( cameraState && (
			cameraState->id == RE::CameraState::kFirstPerson ||
			cameraState->id == RE::CameraState::kThirdPerson ||
			cameraState->id == RE::CameraState::kMount ) )
		{
			return func( a_projectile, playerPitch );
		}
	}

	return func( a_projectile, a_angle );
}

float Hooks::ConcentrationSpellAngleHook::SetAimYaw( RE::Projectile* a_projectile, float a_angle, func_t* func )
{
	auto& runtimeData = a_projectile->GetProjectileRuntimeData();
	auto shooter = runtimeData.shooter;
	if( Settings::bEnabled && shooter && shooter.native_handle() == 0x100000 )
	{
		auto cameraState = RE::PlayerCamera::GetSingleton()->currentState.get();
		auto shooterRef = shooter.get();
		auto actor = shooterRef ? shooterRef->As<RE::Actor>() : nullptr;
		auto caster = actor ? actor->GetMagicCaster( runtimeData.castingSource ) : nullptr;
		auto magicNode = caster ? caster->GetMagicNode() : nullptr;
		auto camera = RE::Main::WorldRootCamera();

		// Invalidate so SetAimPitch never reuses an angle computed for an earlier frame
		playerPitch = playerYaw = FLT_MAX;

		if( cameraState && magicNode && camera && (
			cameraState->id == RE::CameraState::kFirstPerson ||
			cameraState->id == RE::CameraState::kThirdPerson ||
			cameraState->id == RE::CameraState::kMount ) )
		{
			RE::NiPoint3 hitLocation;
			RE::bhkPickData pickData;
			GetCrosshairRaycastTarget(
				a_projectile->GetCollisionGroup(),
				RE::COL_LAYER::kSpell,
				&hitLocation,
				&pickData
			);
			auto origin = magicNode->world.translate;
			auto rayEnd = ((RE::NiPoint3&)(pickData.rayInput.to.quad.m128_f32) / havokScaling);
			auto impactDepth = GetImpactDistanceAheadOfProjectileOrigin( camera->world.translate, rayEnd, hitLocation, origin );

			if( impactDepth > 0 && CalculateProjectileAngles( origin, hitLocation, playerPitch, playerYaw ) &&
				playerYaw != FLT_MAX )
			{
				return func( a_projectile, playerYaw );
			}

			playerPitch = playerYaw = FLT_MAX;
		}
	}

	return func( a_projectile, a_angle );
}

float Hooks::PlayerSetAimPitchHook::PlayerGetAimPitch( RE::AIProcess* a_process, const float a_pitch )
{
	float result = a_pitch;

	RE::PlayerCharacter* player = RE::PlayerCharacter::GetSingleton();
	if( !player || !RE::Main::WorldRootCamera() )
		return result;

	if( Settings::IsCrosshairImpactDebugEnabled() )
	{
		auto currentTimeMS = RE::BSTimer::GetSingleton()->smoothedRunTimeMS;
		if( rayImpactExpire > currentTimeMS && lastRayCoord != RE::NiPoint3::Zero() )
			SetCrosshairToWorldPosition( lastRayCoord );
		else
			ResetCrosshairPosition();
	}

	// Try to make the aiming pose align with the target. Skip this for VATS
	auto playerCamera = RE::PlayerCamera::GetSingleton();
	auto currentCameraState = playerCamera->currentState ? playerCamera->currentState.get() : nullptr;
	bool isVATSPlaying = currentCameraState && currentCameraState->id == RE::CameraState::kVATS;
	if( Settings::bEnabled && a_process == player->GetActorRuntimeData().currentProcess )
	{
		// Initialize
		if( currentPitch == FLT_MAX )
			currentPitch = a_pitch;

		if( player->AsActorState()->IsWeaponDrawn() &&
			player->AsActorState()->GetAttackState() >= RE::ATTACK_STATE_ENUM::kBowDraw &&
			player->AsActorState()->GetAttackState() <= RE::ATTACK_STATE_ENUM::kBowFollowThrough &&
			!isVATSPlaying )
		{
			//auto camera				= RE::Main::WorldRootCamera();
			auto screenSize			= RE::BSGraphics::Renderer::GetSingleton()->GetScreenSize();
			auto screenHalfWidth	= screenSize.width  / 2;
			auto screenHalfHeight	= screenSize.height / 2;
			int32_t crosshairX		= (int32_t)(screenHalfWidth  + ( Settings::fCrosshairPosX * screenHalfWidth ));
			int32_t crosshairY		= (int32_t)(screenHalfHeight + ( Settings::fCrosshairPosY * screenHalfHeight ));

			RE::NiPoint3 lookTarget{0,0,0};
			RE::CFilter filter;
			player->GetCollisionFilterInfo( filter );
			GetScreenRaycastTarget(
				crosshairX,
				crosshairY,
				filter.GetSystemGroup(),
				RE::COL_LAYER::kProjectile,
				&lookTarget,
				nullptr,
				Settings::fMaximumTargetDistance * meterToGameUnit );

			float lookPitch = 0, lookYaw;
			auto aimPos = player->GetPosition();

			if( Settings::bEnabledPitchCorrection )
				aimPos.z += player->GetInfoRuntimeData().eyeHeight;

			// Add penetration
			lookTarget = ExtendVectorFromOrigin( aimPos, lookTarget, Settings::fRayPenetrationDepth * meterToGameUnit );

			// Add z-bias
			lookTarget.z += Settings::fHeightBias;

			// 3rd person model firing angle adjustment
			bool isAngleCalculated = false;
			if( Settings::IsAutomaticPitchEnabled() )
			{
				auto ammo				= player->GetCurrentAmmo();
				auto projectile			= ammo ? ammo->GetRuntimeData().data.projectile : nullptr;
				auto projectileSpeed	= projectile ? projectile->data.speed : 0;
				auto projectileGravity	= projectile ? projectile->data.gravity : 0;
				isAngleCalculated = SolveBallisticIntercept( 
					aimPos, lookTarget, nullptr,
					projectileSpeed, projectileGravity * GetWorldGravity(), 
					&lookTarget, &lookPitch, &lookYaw );
			}
			
			if( !isAngleCalculated )
			{
				CalculateProjectileAngles( aimPos, lookTarget, lookPitch, lookYaw );
			}

			// Fixed pitch adjustment
			if( Settings::IsFixedAngleEnabled() )
			{
				lookPitch -= Settings::fPitchOffsetDegree * degToRad;
			}

			// 3rd person pitch bias
			float pitchBias = Settings::fPitchBias * degToRad;

			// Reduce bias for extremes because they're already correct.
			float halfPi = std::numbers::pi_v<float> * 0.5f;
			float fadeStart = std::clamp( Settings::fBiasFadingStart * degToRad, 0.0f, halfPi );
			float t = std::clamp(( abs( lookPitch ) - fadeStart ) / ( halfPi - fadeStart ), 0.0f, 1.0f);
			float biasFactor = 1.0f - t * t * ( 3.0f - 2.0f * t );
			pitchBias *= biasFactor;
			lookPitch -= pitchBias;

			result = -lookPitch;

			// Don't snap into place. Do it smoothly.
			float deltaTime = RE::BSTimer::GetSingleton()->delta;
			float maxSpeed = Settings::fMaxPitchAdjustmentSpeed * degToRad;
			float acceleration = maxSpeed * 4.0f; // Reach max speed in 0.25 sec

			float delta = result - currentPitch;
			float distance = abs( delta );

			if( distance < 0.00001f )
			{
				currentPitch = result;
				pitchVelocity = 0.0f;
			}
			else
			{
				float direction = delta > 0 ? 1.0f : -1.0f;
				float brakingSpeed = sqrt( 2.0f * acceleration * distance );
				float targetVelocity = direction * std::min( maxSpeed, brakingSpeed );

				pitchVelocity += std::clamp(
					targetVelocity - pitchVelocity,
					-acceleration * deltaTime,
					acceleration * deltaTime );

				float angleStep = pitchVelocity * deltaTime;

				if( abs( angleStep ) >= distance && angleStep * delta > 0 )
				{
					currentPitch = result;
					pitchVelocity = 0.0f;
				}
				else
				{
					currentPitch += angleStep;
				}
			}

			result = currentPitch;
		}
		else
		{
			// Reset when not firing. Start from the game's pitch so the next draw eases in from the
			// current pose instead of snapping to level first.
			currentPitch = a_pitch;
			pitchVelocity = 0;
		}
	}

	return result;
}

void Hooks::ProjectileInitHook::OverrideProjectileInit( RE::Projectile* a_this, RE::NiPoint3& a_angle )
{
	auto& shooter = a_this->GetProjectileRuntimeData().shooter;
	if( shooter.native_handle() == 0x100000 ) // Player handle
	{
		if( Settings::bEnabled )
		{
			// Replace with HWYA angle
			auto player = RE::PlayerCharacter::GetSingleton();
			auto pcCamera = RE::PlayerCamera::GetSingleton();
			auto cameraState = pcCamera ? pcCamera->currentState.get() : nullptr;
			if( cameraState && cameraState->id == RE::CameraState::kMount )
			{
				auto biped = player->GetCurrentBiped();
				if( biped &&
					player->AsActorState()->GetAttackState() >= RE::ATTACK_STATE_ENUM::kBowReleasing &&
					player->AsActorState()->GetAttackState() <= RE::ATTACK_STATE_ENUM::kBowReleased )
				{
					auto process = player->GetActorRuntimeData().currentProcess;
					auto fireNode = process ? process->GetWeaponNode( biped ) : nullptr;

					RE::NiPoint3 fireDirection{ 0, 0, 0 };
					if( fireNode )
					{
						fireDirection = fireNode->world.rotate * RE::NiPoint3( 0, 1, 0 );
						fireDirection.Unitize();
					}

					float horizontalSquared =
						fireDirection.x * fireDirection.x +
						fireDirection.y * fireDirection.y;

					float lengthSquared = horizontalSquared + fireDirection.z * fireDirection.z;

					if( lengthSquared > 1.e-6f && horizontalSquared > 1.e-6f )
					{
						// Only clamp yaw. Leave pitch as is.
						//float angleX = atan2( -fireDirection.z, sqrt( horizontalSquared ) );
						float angleZ = atan2( fireDirection.x, fireDirection.y );

						float maxVisualDifference = Settings::fMountedYawTolerance * degToRad;
						float yawDifference = std::remainder(a_angle.z - angleZ, 2.0f * std::numbers::pi_v<float>);

						//a_angle.x = angleX;
						a_angle.z = angleZ + std::clamp( yawDifference, -maxVisualDifference, maxVisualDifference );
					}
				}
			}

			SetProjectileAngle( a_this, a_angle.x, a_angle.z );
			if( Settings::bInheritVelocity && !Settings::IsTargetLeadingEnabled() )
			{
				RE::NiPoint3 playerVelocity{ 0, 0, 0 };
				player->GetLinearVelocity( playerVelocity );

				auto& projectileRuntimeData = a_this->GetProjectileRuntimeData();
				projectileRuntimeData.linearVelocity.x += playerVelocity.x;
				projectileRuntimeData.linearVelocity.y += playerVelocity.y;
				// Skyrim default behavior leaves z-velocity out.
				// projectileRuntimeData.linearVelocity.z += playerVelocity.z;
			}
		}

		if( !a_this->IsFlameProjectile() && !a_this->IsBeamProjectile() )
			TrueHUDArrowTrail::BeginArrow( a_this );
	}
}
