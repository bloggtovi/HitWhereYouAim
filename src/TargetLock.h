#pragma once

#include "TrueHUDAPI.h"

#include <bit>
#include <cmath>
#include <numbers>

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

		bool held = IsKeyHeld();
		if( IsInMenu() )
		{
			// Clicks inside menus are not aim input
			if( IsHoldMode() )
				Unlock();
			keyWasHeld = held;
			return;
		}

		if( held && !keyWasHeld )
			OnKeyPressed();
		else if( !held && keyWasHeld )
			OnKeyReleased();
	}

	static void OnKeyPressed()
	{
		keyWasHeld = true;

		if( IsHoldMode() )
		{
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
		if( IsHoldMode() )
			Unlock( true );
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

		auto targetActor = target.get();
		if( !targetActor || targetActor->IsDead() || targetActor->IsDisabled() || !targetActor->Get3D2() )
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

		if( HasLineOfSight( player, targetActor.get() ) )
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
		if( !CalculateProjectileAngles( camera->world.translate, GetAimPoint( targetActor.get() ), desiredPitch, desiredYaw ) )
			return;

		desiredPitch = std::clamp( desiredPitch, -maxPitch, maxPitch );

		// Rotation speed 0 means snap. Otherwise use frame rate independent exponential smoothing.
		float blend = 1.0f;
		if( !a_instant && Settings::fTargetLockRotationSpeed > 0.0f )
			blend = 1.0f - std::exp( -Settings::fTargetLockRotationSpeed * a_delta );

		auto thirdPersonState = isThirdPerson ? skyrim_cast<RE::ThirdPersonState*>( cameraState ) : nullptr;
		if( thirdPersonState && thirdPersonState->GetFreeRotationMode() )
		{
			// The orbiting camera looks along player yaw + freeRotation.x, so steer the offset and
			// leave the character's heading to the movement controls.
			float cameraYaw = player->data.angle.z + thirdPersonState->freeRotation.x;
			thirdPersonState->freeRotation.x += WrapAngle( desiredYaw - cameraYaw ) * blend;
		}
		else
		{
			player->data.angle.z = NormalizeYaw( player->data.angle.z + WrapAngle( desiredYaw - player->data.angle.z ) * blend );
		}

		player->data.angle.x += ( desiredPitch - player->data.angle.x ) * blend;
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

	// Any open menu that pauses the game or shows the mouse cursor. Checking the cursor flag also
	// covers mods such as SkyrimSouls RE that keep menus from pausing the game.
	static bool IsInMenu()
	{
		auto ui = RE::UI::GetSingleton();
		if( !ui )
			return true;

		if( ui->GameIsPaused() )
			return true;

		for( const auto& menu : ui->menuStack )
		{
			if( menu && ( menu->PausesGame() || menu->UsesCursor() ) )
				return true;
		}

		return false;
	}

	static bool IsKeyHeld()
	{
		auto inputManager = RE::BSInputDeviceManager::GetSingleton();
		if( !inputManager )
			return false;

		uint32_t key = Settings::iTargetLockKey;
		if( key >= 280 && key <= 281 )
		{
			auto gamepad = inputManager->GetGamepad();
			return gamepad && gamepad->IsPressed( key == 280 ? 0x9 : 0xA );
		}
		if( key >= 266 )
		{
			auto gamepad = inputManager->GetGamepad();
			return gamepad && key - 266 < 16 && gamepad->IsPressed( 1u << ( key - 266 ) );
		}
		if( key >= 256 )
		{
			auto mouse = inputManager->GetMouse();
			return mouse && mouse->IsPressed( key - 256 );
		}

		auto keyboard = inputManager->GetKeyboard();
		return keyboard && keyboard->IsPressed( key );
	}

	static RE::Actor* FindTarget()
	{
		auto candidate = GetPotentialPlayerTarget(
			Settings::fTargetLockMaxDistance * meterToGameUnitLock,
			0.0f, 0.0f, nullptr,
			Settings::fTargetLockWindowWidth,
			Settings::fTargetLockWindowHeight ).first;

		return candidate ? candidate->As<RE::Actor>() : nullptr;
	}

	static RE::NiPoint3 GetAimPoint( RE::Actor* a_actor )
	{
		auto root = a_actor->Get3D2();

		// Upper torso of humanoids; creatures fall back to the center of their bounds
		static RE::BSFixedString spineName{ "NPC Spine2 [Spn2]" };
		if( auto spine = root->GetObjectByName( spineName ) )
			return spine->world.translate;

		return root->worldBound.center;
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

	static constexpr float meterToGameUnitLock = 1.0f / 0.0142875f;
	static constexpr float maxPitch = 80.0f * std::numbers::pi_v<float> / 180.0f;

	static inline TRUEHUD_API::IVTrueHUD3* trueHUD = nullptr;
	static inline RE::ActorHandle target;
	static inline float timeOutOfSight = 0.0f;
	static inline bool hasTrueHUDControl = false;
	static inline bool keyWasHeld = false;
};
