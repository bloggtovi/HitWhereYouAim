#pragma once

#include "TrueHUDAPI.h"

#include <bit>
#include <cmath>
#include <numbers>

// Camera target lock: a key press picks the actor nearest the crosshair and keeps the screen
// center on it every frame, whether the player or the target moves.
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

	static void Toggle()
	{
		if( IsLocked() )
		{
			Unlock( true );
			return;
		}

		auto newTarget = FindTarget();
		if( !newTarget )
		{
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

	static void Update( float a_delta )
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

		// Frame rate independent exponential smoothing
		float blend = 1.0f - std::exp( -std::max( Settings::fTargetLockRotationSpeed, 0.1f ) * a_delta );

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

			auto ui = RE::UI::GetSingleton();
			if( !ui || ui->GameIsPaused() || ui->IsMenuOpen( RE::Console::MENU_NAME ) )
				return RE::BSEventNotifyControl::kContinue;

			for( auto event = *a_event; event; event = event->next )
			{
				if( event->GetEventType() != RE::INPUT_EVENT_TYPE::kButton )
					continue;

				auto button = event->AsButtonEvent();
				if( !button || !button->IsDown() )
					continue;

				if( ToKeyCode( event->GetDevice(), button->GetIDCode() ) == Settings::iTargetLockKey )
				{
					Toggle();
					break;
				}
			}

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
};
