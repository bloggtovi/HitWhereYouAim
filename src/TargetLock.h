#pragma once

#include "TrueHUDAPI.h"

#include <bit>
#include <cmath>
#include <numbers>
#include <optional>

enum class TargetLockMode
{
	kToggle,
	kHold,
};

// Camera target lock: the lock key picks the actor nearest the crosshair and keeps the screen
// center on it every frame, whether the player or the target moves.
// Hold mode (default, left mouse button): snaps on press, tracks while held, releases on release.
// Toggle mode: one press locks, the next press releases.
class TargetLock
{
public:
	static void Install()
	{
		trueHUD = (TRUEHUD_API::IVTrueHUD3*)TRUEHUD_API::RequestPluginAPI( TRUEHUD_API::InterfaceVersion::V3 );
	}

	static void RegisterInputSink()
	{
		if( auto inputManager = RE::BSInputDeviceManager::GetSingleton() )
		{
			inputManager->AddEventSink( InputHandler::GetSingleton() );
			REX::DEBUG( "Target lock input handler registered" );
		}
	}

	static bool IsLocked()
	{
		return target.get() != nullptr;
	}

	static bool IsHoldMode()
	{
		return Settings::iTargetLockMode == (uint32_t)TargetLockMode::kHold;
	}

	// Called every frame. Reads the key straight from the game's input device, so presses and
	// releases are still seen when another plugin's input sink consumes the button event first.
	static void Poll()
	{
		if( !Settings::bTargetLockEnabled )
			return;

		auto heldState = IsKeyHeld();
		if( IsInMenu() )
		{
			// Clicks inside menus are not aim input
			if( IsHoldMode() )
				Unlock();
			if( heldState )
				keyWasHeld = *heldState;
			return;
		}

		if( !heldState )
			return;

		// The event path normally handles both edges first. Polling only fills in edges that never
		// arrived as events, and requires two consistent frames so a one-frame lag in the raw
		// buffer can never fake a press right after a release or a release in the middle of a hold.
		if( *heldState )
		{
			++polledHeldFrames;
			polledReleasedFrames = 0;
		}
		else
		{
			++polledReleasedFrames;
			polledHeldFrames = 0;
		}

		if( polledHeldFrames >= 2 )
			polledSawHeld = true;

		if( !keyWasHeld && polledHeldFrames == 2 )
			OnKeyPressed();
		else if( keyWasHeld && polledSawHeld && polledReleasedFrames == 2 )
			OnKeyReleased();
	}

	static void OnKeyPressed()
	{
		keyWasHeld = true;

		if( IsHoldMode() )
		{
			// Every new click picks the adversary nearest the crosshair at that moment. The choice
			// then stays fixed for the whole hold, so the crosshair never hops between targets.
			if( releaseTimer > 0.0f )
				Unlock();

			if( !IsLocked() )
				Lock( false );

			// Turn right away, in the same frame as the click, so even a quick click that
			// fires immediately goes at the target
			if( IsLocked() )
				Update( 0.0f, true );
		}
		else if( IsLocked() )
		{
			Unlock( true );
		}
		else
		{
			Lock( true );
		}
	}

	static void OnKeyReleased()
	{
		keyWasHeld = false;
		polledSawHeld = false;

		// Bows and crossbows fire on release, a few frames after the button goes up. Keep tracking
		// briefly so that shot still goes at the target.
		if( IsHoldMode() && IsLocked() )
		{
			releaseTimer = std::max( Settings::fTargetLockReleaseDelay, 0.0f );
			if( releaseTimer <= 0.0f )
				Unlock( true );
		}
	}

	// Set every frame by projectile guidance: an arrow is still flying at the locked target, so the
	// crosshair stays on it until the impact even after the release delay
	static void SetProjectileInFlight( bool a_inFlight )
	{
		projectileInFlight = a_inFlight;
	}

	static RE::ActorHandle GetTargetHandle()
	{
		return target;
	}

	// Standing, active combatants only. Dead, bleeding out, knocked down or getting up actors are
	// excluded so a corpse lying under a live adversary is never chosen.
	static bool IsValidTarget( RE::Actor* a_actor )
	{
		auto player = RE::PlayerCharacter::GetSingleton();
		if( !a_actor || a_actor == player || a_actor->IsDead() || a_actor->IsDisabled() )
			return false;

		// Never lock onto or guide shots at other people. Multiplayer clients (SkyMP) spawn remote
		// players as actors built on Skyrim.esm's "Player" base record (0x7), so any actor other
		// than the local player that uses it is a human player and is excluded.
		auto base = a_actor->GetActorBase();
		if( base && base->GetFormID() == playerBaseFormID )
			return false;

		auto actor3D = a_actor->Get3D2();
		if( !actor3D || actor3D->flags.any( RE::NiAVObject::Flag::kNotVisible ) )
			return false;

		auto state = a_actor->AsActorState();
		if( state->IsBleedingOut() || state->IsUnconscious() || state->GetKnockState() != RE::KNOCK_STATE_ENUM::kNormal )
			return false;

		return true;
	}

	// Upper torso of humanoids; creatures fall back to the center of their bounds
	static RE::NiPoint3 GetAimPoint( RE::Actor* a_actor )
	{
		auto root = a_actor->Get3D2();

		static RE::BSFixedString spineName{ "NPC Spine2 [Spn2]" };
		if( auto spine = root->GetObjectByName( spineName ) )
			return spine->world.translate;

		return root->worldBound.center;
	}

	// Aim point and actor of the current lock, used to send projectiles straight at the locked
	// target instead of at whatever the crosshair ray hits first (a corpse lying in front, say)
	static RE::Actor* GetLockedTarget( RE::NiPoint3& out_aimPoint )
	{
		auto actor = target.get();
		if( !actor || !IsValidTarget( actor.get() ) )
			return nullptr;

		out_aimPoint = GetAimPoint( actor.get() );
		return actor.get();
	}

	static void Lock( bool a_notifyNoTarget )
	{
		auto newTarget = FindTarget();
		if( !newTarget )
		{
			if( a_notifyNoTarget )
				Notify( "No hay objetivo para fijar" );
			return;
		}

		target = newTarget->GetHandle();
		timeOutOfSight = 0.0f;

		if( trueHUD && trueHUD->RequestTargetControl( SKSE::GetPluginHandle() ) != TRUEHUD_API::APIResult::AlreadyTaken )
		{
			hasTrueHUDControl = true;
			trueHUD->SetTarget( SKSE::GetPluginHandle(), target );
		}

		auto name = newTarget->GetDisplayFullName();
		Notify( std::format( "Objetivo fijado: {}", name ? name : "" ).c_str() );
	}

	static void Unlock( bool a_notify = false )
	{
		if( !target && !hasTrueHUDControl )
			return;

		target.reset();
		timeOutOfSight = 0.0f;
		releaseTimer = 0.0f;

		if( hasTrueHUDControl && trueHUD )
		{
			trueHUD->SetTarget( SKSE::GetPluginHandle(), RE::ActorHandle() );
			(void)trueHUD->ReleaseTargetControl( SKSE::GetPluginHandle() );
		}
		hasTrueHUDControl = false;

		if( a_notify )
			Notify( "Objetivo liberado" );
	}

	static void Update( float a_delta, bool a_instant = false )
	{
		if( !target )
			return;

		if( !Settings::bTargetLockEnabled )
		{
			Unlock();
			return;
		}

		auto player = RE::PlayerCharacter::GetSingleton();
		auto camera = RE::Main::WorldRootCamera();
		auto playerCamera = RE::PlayerCamera::GetSingleton();
		auto cameraState = playerCamera ? playerCamera->currentState.get() : nullptr;
		if( !player || !camera || !cameraState )
			return;

		if( releaseTimer > 0.0f )
		{
			releaseTimer -= a_delta;
			if( releaseTimer <= 0.0f )
			{
				// Keep the crosshair on the target while a guided arrow is still flying at it. The timer
				// stays positive so a new click still drops this lock and picks the nearest adversary.
				if( projectileInFlight )
				{
					releaseTimer = 0.05f;
				}
				else
				{
					Unlock( true );
					return;
				}
			}
		}

		// A downed target is dropped at once and never picked again while it stays down
		auto targetActor = target.get();
		if( !targetActor || !IsValidTarget( targetActor.get() ) )
		{
			Unlock( true );
			return;
		}

		float maxDistance = Settings::fTargetLockMaxDistance * meterToGameUnitLock;
		if( targetActor->GetPosition().GetSquaredDistance( player->GetPosition() ) > maxDistance * maxDistance )
		{
			Unlock( true );
			return;
		}

		auto aimPoint = GetAimPoint( targetActor.get() );
		if( IsVisible( player, camera, targetActor.get(), aimPoint ) )
		{
			timeOutOfSight = 0.0f;
		}
		else
		{
			timeOutOfSight += a_delta;
			if( timeOutOfSight > Settings::fTargetLockLoseSightTime )
			{
				Unlock( true );
				return;
			}
		}

		// Only steer gameplay cameras. Kill cams, VATS, furniture and so on keep their own control.
		bool isFirstPerson = cameraState->id == RE::CameraState::kFirstPerson;
		bool isThirdPerson = cameraState->id == RE::CameraState::kThirdPerson;
		if( !isFirstPerson && !isThirdPerson )
			return;

		float desiredPitch = 0.0f;
		float desiredYaw = player->data.angle.z;
		if( !CalculateProjectileAngles( camera->world.translate, aimPoint, desiredPitch, desiredYaw ) )
			return;

		desiredPitch = std::clamp( desiredPitch, -maxPitch, maxPitch );

		// Rotation speed 0 means snap. Otherwise use frame rate independent exponential smoothing.
		float blend = 1.0f;
		if( !a_instant && Settings::fTargetLockRotationSpeed > 0.0f )
			blend = 1.0f - std::exp( -Settings::fTargetLockRotationSpeed * a_delta );

		// The third person camera looks along player angle + freeRotation on both axes (the same
		// relation True Directional Movement uses to read the camera rotation). First person has no
		// offset. Correcting the error of the full camera angle keeps the crosshair exact even when
		// freeRotation is not zero.
		auto thirdPersonState = isThirdPerson ? skyrim_cast<RE::ThirdPersonState*>( cameraState ) : nullptr;
		float yawOffset = thirdPersonState ? thirdPersonState->freeRotation.x : 0.0f;
		float pitchOffset = thirdPersonState ? thirdPersonState->freeRotation.y : 0.0f;
		float yawError = WrapAngle( desiredYaw - ( player->data.angle.z + yawOffset ) );
		float pitchError = desiredPitch - ( player->data.angle.x + pitchOffset );

		if( thirdPersonState && thirdPersonState->GetFreeRotationMode() )
		{
			// Orbiting camera: steer the offset and leave the character's heading to the movement controls
			thirdPersonState->freeRotation.x += yawError * blend;
		}
		else
		{
			player->data.angle.z = NormalizeYaw( player->data.angle.z + yawError * blend );
		}

		player->data.angle.x = std::clamp( player->data.angle.x + pitchError * blend, -maxPitch, maxPitch );
	}

private:
	class InputHandler final : public RE::BSTEventSink<RE::InputEvent*>
	{
	public:
		static InputHandler* GetSingleton()
		{
			static InputHandler singleton;
			return &singleton;
		}

		RE::BSEventNotifyControl ProcessEvent( RE::InputEvent* const* a_event, RE::BSTEventSource<RE::InputEvent*>* ) override
		{
			if( !a_event || !Settings::bTargetLockEnabled )
				return RE::BSEventNotifyControl::kContinue;

			// Menus are handled by Poll()
			if( IsInMenu() )
				return RE::BSEventNotifyControl::kContinue;

			// The event path reacts in the same frame as the click, before the attack is processed.
			// keyWasHeld keeps it and Poll() from handling the same press twice.
			for( auto event = *a_event; event; event = event->next )
			{
				if( event->GetEventType() != RE::INPUT_EVENT_TYPE::kButton )
					continue;

				auto button = event->AsButtonEvent();
				if( !button || ToKeyCode( event->GetDevice(), button->GetIDCode() ) != Settings::iTargetLockKey )
					continue;

				if( button->IsDown() && !keyWasHeld )
					OnKeyPressed();
				else if( button->IsUp() && keyWasHeld )
					OnKeyReleased();
			}

			// Never consume the event: the same button still attacks, draws the bow or casts
			return RE::BSEventNotifyControl::kContinue;
		}

	private:
		// Same numbering as SKSE / MCM Helper keymaps: keyboard scan codes, mouse 256+, gamepad 266+
		static uint32_t ToKeyCode( RE::INPUT_DEVICE a_device, uint32_t a_idCode )
		{
			switch( a_device )
			{
			case RE::INPUT_DEVICE::kKeyboard:
				return a_idCode;
			case RE::INPUT_DEVICE::kMouse:
				return 256 + a_idCode;
			case RE::INPUT_DEVICE::kGamepad:
				if( a_idCode == 0x9 ) return 280;	// Left trigger
				if( a_idCode == 0xA ) return 281;	// Right trigger
				if( std::has_single_bit( a_idCode ) ) return 266 + (uint32_t)std::countr_zero( a_idCode );
				return 0;
			default:
				return 0;
			}
		}
	};

	// True whenever the click is not gameplay input. The engine's own input context stack is the
	// primary signal: every interactive menu pushes a context, even when a mod such as SkyrimSouls RE
	// keeps it from pausing the game. Cursor menus that are not always-open cover overlays that do
	// not push one, and disabled fighting controls cover scripted scenes.
	static bool IsInMenu()
	{
		auto ui = RE::UI::GetSingleton();
		if( !ui || ui->GameIsPaused() )
			return true;

		if( auto controlMap = RE::ControlMap::GetSingleton() )
		{
			const auto& data = controlMap->GetRuntimeData();
			if( !data.contextPriorityStack.empty() &&
				data.contextPriorityStack.back() != RE::UserEvents::INPUT_CONTEXT_ID::kGameplay )
				return true;

			if( !controlMap->IsFightingControlsEnabled() )
				return true;
		}

		for( const auto& menu : ui->menuStack )
		{
			if( menu && !menu->AlwaysOpen() && ( menu->PausesGame() || menu->UsesCursor() ) )
				return true;
		}

		return false;
	}

	// Physical key state for keyboard and mouse, read from the raw DirectInput buffers the engine
	// polls. BSInputDevice::IsPressed() tests heldDownSecs, which is still 0 on the press frame,
	// so it is avoided for the mouse. Gamepad presses come from input events only.
	static std::optional<bool> IsKeyHeld()
	{
		auto inputManager = RE::BSInputDeviceManager::GetSingleton();
		if( !inputManager )
			return std::nullopt;

		uint32_t key = Settings::iTargetLockKey;
		if( key >= 266 )
			return std::nullopt;

		if( key >= 256 )
		{
			auto mouse = inputManager->GetMouse();
			if( !mouse || key - 256 >= 8 )
				return std::nullopt;

			return ( mouse->GetRuntimeData().dInputNextState.rgbButtons[key - 256] & 0x80 ) != 0;
		}

		// Read curState directly like BSWin32KeyboardDevice::IsPressed() does. Calling that function
		// would link CommonLib's keyboard device object, whose virtuals only exist inside the game.
		auto keyboard = inputManager->GetKeyboard();
		if( !keyboard || key >= 0x100 )
			return std::nullopt;

		return ( keyboard->GetRuntimeData().curState[key] & 0x80 ) != 0;
	}

	static bool IsFollower( RE::Actor* a_actor, RE::PlayerCharacter* a_player )
	{
		if( a_actor->IsPlayerTeammate() )
			return true;

		auto commander = a_actor->GetCommandingActor();
		return commander && commander.get() == a_player;
	}

	// Normalized screen position (-1..1, +y up) of a world point, false when behind the camera
	static bool ProjectToScreen( RE::NiCamera* a_camera, const RE::NiPoint3& a_world, float& out_x, float& out_y )
	{
		float x, y, depth;
		if( !RE::NiCamera::WorldPtToScreenPt3( a_camera->GetRuntimeData().worldToCam, a_camera->GetRuntimeData2().port, a_world, x, y, depth, 1.0e-5f ) )
			return false;

		if( depth <= 0.0f || depth >= 1.0f )
			return false;

		out_x = x * 2.0f - 1.0f;
		out_y = y * 2.0f - 1.0f;
		return true;
	}

	// The adversary whose torso is nearest the crosshair on screen. Hostile actors qualify inside the
	// search window; anything else (a deer while hunting, say) only when the crosshair is right on it.
	static RE::Actor* FindTarget()
	{
		auto processLists = RE::ProcessLists::GetSingleton();
		auto player = RE::PlayerCharacter::GetSingleton();
		auto camera = RE::Main::WorldRootCamera();
		auto renderer = RE::BSGraphics::Renderer::GetSingleton();
		if( !processLists || !player || !camera || !renderer )
			return nullptr;

		auto screenSize = renderer->GetScreenSize();
		float aspect = screenSize.height ? (float)screenSize.width / (float)screenSize.height : 1.0f;

		// Crosshair in the same normalized space (Settings Y offset grows downward)
		float crosshairX = Settings::fCrosshairPosX;
		float crosshairY = -Settings::fCrosshairPosY;

		float maxDistance = Settings::fTargetLockMaxDistance * meterToGameUnitLock;
		float windowX = std::max( Settings::fTargetLockWindowWidth, 0.0001f );
		float windowY = std::max( Settings::fTargetLockWindowHeight, 0.0001f );

		RE::Actor* best = nullptr;
		float bestScore = FLT_MAX;

		auto consider = [&]( RE::ActorHandle& a_handle ) {
			auto actorPtr = a_handle.get();
			auto actor = actorPtr.get();
			if( !IsValidTarget( actor ) || IsFollower( actor, player ) )
				return;

			if( actor->GetPosition().GetSquaredDistance( player->GetPosition() ) > maxDistance * maxDistance )
				return;

			auto aimPoint = GetAimPoint( actor );
			float x, y;
			if( !ProjectToScreen( camera, aimPoint, x, y ) )
				return;

			float dx = x - crosshairX;
			float dy = y - crosshairY;

			if( actor->IsHostileToActor( player ) )
			{
				if( std::abs( dx ) > windowX || std::abs( dy ) > windowY )
					return;
			}
			else if( !IsCrosshairOnActor( camera, actor, crosshairX, crosshairY ) )
			{
				return;
			}

			// Distance as seen on screen, so horizontal and vertical offsets weigh the same
			float score = dx * dx * aspect * aspect + dy * dy;
			if( score >= bestScore )
				return;

			// Visibility is the expensive test, so run it only for a would-be winner
			if( !IsVisible( player, camera, actor, aimPoint ) )
				return;

			best = actor;
			bestScore = score;
		};

		// High process actors are the ones near the player. Distant actors that are still loaded and
		// rendered live in the middle-high list, so both are searched to lock onto far adversaries.
		for( auto& handle : processLists->highActorHandles )
			consider( handle );
		for( auto& handle : processLists->middleHighActorHandles )
			consider( handle );

		return best;
	}

	// A physical ray from the camera to the target's torso, on the engine's line-of-sight layer. It has
	// no distance cap, unlike the GetLineOfSight condition, which is kept as a fallback.
	static bool IsVisible( RE::PlayerCharacter* a_player, RE::NiCamera* a_camera, RE::Actor* a_actor, const RE::NiPoint3& a_aimPoint )
	{
		RE::CFilter filter;
		a_player->GetCollisionFilterInfo( filter );

		RE::NiPoint3 from = a_camera->world.translate;
		RE::NiPoint3 to = a_aimPoint;
		RE::bhkPickData pickData;
		auto hitRef = GetRaycastTarget( from, to, filter.GetSystemGroup(), RE::COL_LAYER::kLOS, nullptr, &pickData );

		if( hitRef == a_actor || !pickData.rayOutput.HasHit() )
			return true;

		return HasLineOfSight( a_player, a_actor );
	}

	static bool IsCrosshairOnActor( RE::NiCamera* a_camera, RE::Actor* a_actor, float a_crosshairX, float a_crosshairY )
	{
		auto bounds = ProjectReferenceBoundsToScreen( a_camera, a_actor, 0.0f, 0.0f );
		return bounds.valid &&
			a_crosshairX >= bounds.min.x && a_crosshairX <= bounds.max.x &&
			a_crosshairY >= bounds.min.y && a_crosshairY <= bounds.max.y;
	}

	static float WrapAngle( float a_angle )
	{
		return std::remainder( a_angle, 2.0f * std::numbers::pi_v<float> );
	}

	static float NormalizeYaw( float a_angle )
	{
		float twoPi = 2.0f * std::numbers::pi_v<float>;
		a_angle = std::fmod( a_angle, twoPi );
		return a_angle < 0.0f ? a_angle + twoPi : a_angle;
	}

	static void Notify( const char* a_message )
	{
		if( Settings::bTargetLockNotification )
			RE::SendHUDMessage::ShowHUDMessage( a_message );
	}

	static constexpr RE::FormID playerBaseFormID = 0x00000007;
	static constexpr float meterToGameUnitLock = 1.0f / 0.0142875f;
	static constexpr float maxPitch = 80.0f * std::numbers::pi_v<float> / 180.0f;

	static inline TRUEHUD_API::IVTrueHUD3* trueHUD = nullptr;
	static inline RE::ActorHandle target;
	static inline float timeOutOfSight = 0.0f;
	static inline bool hasTrueHUDControl = false;
	static inline bool keyWasHeld = false;
	static inline float releaseTimer = 0.0f;
	static inline uint32_t polledHeldFrames = 0;
	static inline uint32_t polledReleasedFrames = 0;
	static inline bool polledSawHeld = false;
	static inline bool projectileInFlight = false;
};
