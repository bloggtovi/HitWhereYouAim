#pragma once

#include "TrueHUDAPI.h"

#include <algorithm>
#include <cstdint>
#include <vector>

class CandidateBoundsDebug
{
public:
	static bool Install()
	{
		trueHUD = static_cast<TRUEHUD_API::IVTrueHUD3*>(
			TRUEHUD_API::RequestPluginAPI( TRUEHUD_API::InterfaceVersion::V3 ) );

		if( !trueHUD )
		{
			REX::DEBUG( "Failed to request TrueHUD API V3 for candidate bounds debug" );
			return false;
		}

		return true;
	}

	static void SetEnabled( bool a_enabled )
	{
		enabled = a_enabled;

		if( !enabled )
			Clear();
	}

	static bool IsEnabled()
	{
		return enabled;
	}

	static void Clear()
	{
		debugInfo.candidates.clear();
	}

	static void Update( float a_duration = 0.0f )
	{
		if( !enabled || !trueHUD )
			return;

		auto ui = RE::UI::GetSingleton();
		auto camera = RE::Main::WorldRootCamera();
		auto renderer = RE::BSGraphics::Renderer::GetSingleton();

		if( !ui || ui->GameIsPaused() || !camera || !renderer )
			return;

		auto screenSize = renderer->GetScreenSize();

		if( !screenSize.width || !screenSize.height )
			return;

		bool fixedPitchMode =
			Settings::bEnabledPitchCorrection &&
			Settings::iPitchCorrectionMode == (uint32_t)PitchCorrectionMode::kFixedAngle;

		GetPotentialPlayerTarget(
			Settings::fMaximumTargetDistance * ( 1.0f / 0.0142875f ),
			nullptr,
			Settings::fPredictionWindowWidth,
			Settings::fPredictionWindowHeight,
			&debugInfo,
			fixedPitchMode );

		const auto& a_debugInfo = debugInfo;

		DrawRectangle(
			camera,
			screenSize.width,
			screenSize.height,
			-a_debugInfo.xLimit,
			a_debugInfo.xLimit,
			-a_debugInfo.yLimitDown,
			a_debugInfo.yLimitUp,
			a_duration,
			selectionWindowColor,
			1.0f );

		DrawCross( camera, screenSize.width, screenSize.height, a_duration );

		auto player = RE::PlayerCharacter::GetSingleton();
		float projectileSpeed = GetPlayerProjectileSpeed( player );

		for( const auto& candidate : a_debugInfo.candidates )
		{
#ifndef _DEBUG
			if( !candidate.hasLineOfSight )
				continue;
#endif
			auto reference = candidate.handle.get();
			if( !reference )
				continue;

			auto actor = reference->As<RE::Actor>();
			if( !actor )
				continue;

			float predictionTime = GetPredictionTime( player, actor, projectileSpeed );
			auto currentBounds = ProjectReferenceBoundsToScreen( camera, actor, 0, 0 );
			auto projectedBounds = predictionTime > 0 ?
				ProjectReferenceBoundsToScreen( camera, actor, predictionTime, 0 ) :
				currentBounds;
			auto sweptBounds = GetSweptProjectedBounds( currentBounds, projectedBounds );

			if( !projectedBounds.valid )
				continue;

			auto& projectedMin = projectedBounds.min;
			auto& projectedMax = projectedBounds.max;

			std::uint32_t color = GetCandidateColor( candidate );
			float thickness = candidate.selected ? 2.0f : 1.0f;

			if( predictionTime > 0 && sweptBounds.valid )
			{
				DrawRectangle(
					camera,
					screenSize.width,
					screenSize.height,
					sweptBounds.min.x,
					sweptBounds.max.x,
					sweptBounds.min.y,
					sweptBounds.max.y,
					a_duration,
					sweepColor,
					1.0f );
			}

			DrawRectangle(
				camera,
				screenSize.width,
				screenSize.height,
				projectedMin.x,
				projectedMax.x,
				projectedMin.y,
				projectedMax.y,
				a_duration,
				color,
				thickness );

			if( !candidate.isHostile )
			{
				float xInset = (projectedMax.x - projectedMin.x) * friendlyInset;

				DrawRectangle(
					camera,
					screenSize.width,
					screenSize.height,
					projectedMin.x + xInset,
					projectedMax.x - xInset,
					projectedMin.y,
					projectedMax.y,
					a_duration,
					friendlySelectionColor,
					1.0f );
			}
		}
	}

private:
	static RE::BGSProjectile* GetMagicProjectile( const RE::MagicItem* a_magicItem )
	{
		if( !a_magicItem )
			return nullptr;

		for( const auto effect : a_magicItem->effects )
		{
			if( effect && effect->baseEffect && effect->baseEffect->data.projectileBase )
				return effect->baseEffect->data.projectileBase;
		}

		return nullptr;
	}

	static RE::BGSProjectile* GetEquippedMagicProjectile( RE::PlayerCharacter* a_player, bool a_leftHand )
	{
		if( !a_player )
			return nullptr;

		auto equippedObject = a_player->GetEquippedObject( a_leftHand );
		auto spell = equippedObject ? equippedObject->As<RE::SpellItem>() : nullptr;
		return GetMagicProjectile( spell );
	}

	static float GetPlayerProjectileSpeed( RE::PlayerCharacter* a_player )
	{
		if( !a_player )
			return 0.0f;

		auto rightObject = a_player->GetEquippedObject( false );
		auto leftObject = a_player->GetEquippedObject( true );
		auto rightWeapon = rightObject ? rightObject->As<RE::TESObjectWEAP>() : nullptr;
		auto leftWeapon = leftObject ? leftObject->As<RE::TESObjectWEAP>() : nullptr;
		bool hasRangedWeapon = (rightWeapon && rightWeapon->IsRanged()) || (leftWeapon && leftWeapon->IsRanged());

		if( hasRangedWeapon )
		{
			auto ammo = a_player->GetCurrentAmmo();

			if( ammo && ammo->data.projectile )
				return ammo->data.projectile->data.speed;
		}

		if( auto projectile = GetEquippedMagicProjectile( a_player, false ) )
			return projectile->data.speed;

		if( auto projectile = GetEquippedMagicProjectile( a_player, true ) )
			return projectile->data.speed;

		return 0.0f;
	}

	static float GetPredictionTime( RE::PlayerCharacter* a_player, RE::Actor* a_target, float a_projectileSpeed )
	{
		if( !a_player || !a_target || a_projectileSpeed <= 0.0f )
			return 0.0f;

		auto target3D = a_target->Get3D2();
		if( !target3D )
			return 0.0f;

		RE::NiPoint3 targetPosition = target3D->worldBound.center;
		targetPosition.x = a_target->GetPositionX();
		targetPosition.y = a_target->GetPositionY();

		RE::NiPoint3 velocity;
		a_target->GetLinearVelocity( velocity );

		RE::NiPoint3 origin = a_player->GetPosition();
		float predictionTime = (targetPosition - origin).Length() / a_projectileSpeed;
		return (targetPosition + velocity * predictionTime - origin).Length() / a_projectileSpeed;
	}

	static void DrawRectangle(
		RE::NiCamera* a_camera,
		std::uint32_t a_screenWidth,
		std::uint32_t a_screenHeight,
		float a_left,
		float a_right,
		float a_bottom,
		float a_top,
		float a_duration,
		std::uint32_t a_color,
		float a_thickness )
	{
		RE::NiPoint3 topLeft;
		RE::NiPoint3 topRight;
		RE::NiPoint3 bottomRight;
		RE::NiPoint3 bottomLeft;

		if( !ScreenCoordinateToWorld( a_camera, a_screenWidth, a_screenHeight, a_left, a_top, topLeft ) ||
			!ScreenCoordinateToWorld( a_camera, a_screenWidth, a_screenHeight, a_right, a_top, topRight ) ||
			!ScreenCoordinateToWorld( a_camera, a_screenWidth, a_screenHeight, a_right, a_bottom, bottomRight ) ||
			!ScreenCoordinateToWorld( a_camera, a_screenWidth, a_screenHeight, a_left, a_bottom, bottomLeft ) )
		{
			return;
		}

		trueHUD->DrawLine( topLeft, topRight, a_duration, a_color, a_thickness );
		trueHUD->DrawLine( topRight, bottomRight, a_duration, a_color, a_thickness );
		trueHUD->DrawLine( bottomRight, bottomLeft, a_duration, a_color, a_thickness );
		trueHUD->DrawLine( bottomLeft, topLeft, a_duration, a_color, a_thickness );
	}

	static void DrawCross(
		RE::NiCamera* a_camera,
		std::uint32_t a_screenWidth,
		std::uint32_t a_screenHeight,
		float a_duration )
	{
		RE::NiPoint3 horizontalStart;
		RE::NiPoint3 horizontalEnd;
		RE::NiPoint3 verticalStart;
		RE::NiPoint3 verticalEnd;

		if( !ScreenCoordinateToWorld( a_camera, a_screenWidth, a_screenHeight, -centerMarkSize, 0.0f, horizontalStart ) ||
			!ScreenCoordinateToWorld( a_camera, a_screenWidth, a_screenHeight, centerMarkSize, 0.0f, horizontalEnd ) ||
			!ScreenCoordinateToWorld( a_camera, a_screenWidth, a_screenHeight, 0.0f, -centerMarkSize, verticalStart ) ||
			!ScreenCoordinateToWorld( a_camera, a_screenWidth, a_screenHeight, 0.0f, centerMarkSize, verticalEnd ) )
		{
			return;
		}

		trueHUD->DrawLine( horizontalStart, horizontalEnd, a_duration, centerMarkColor, 1.0f );
		trueHUD->DrawLine( verticalStart, verticalEnd, a_duration, centerMarkColor, 1.0f );
	}

	static bool ScreenCoordinateToWorld(
		RE::NiCamera* a_camera,
		std::uint32_t a_screenWidth,
		std::uint32_t a_screenHeight,
		float a_x,
		float a_y,
		RE::NiPoint3& a_worldPosition )
	{
		float screenX = (a_x * 0.5f + 0.5f) * static_cast<float>( a_screenWidth );
		float screenY = (0.5f - a_y * 0.5f) * static_cast<float>( a_screenHeight );
		RE::NiPoint3 origin;
		RE::NiPoint3 direction;

		if( !a_camera->WindowPointToRay(
			static_cast<std::int32_t>( screenX ),
			static_cast<std::int32_t>( screenY ),
			origin,
			direction,
			static_cast<float>( a_screenWidth ),
			static_cast<float>( a_screenHeight ) ) )
		{
			return false;
		}

		a_worldPosition = origin + direction * projectionPlaneDistance;
		return true;
	}

	static std::uint32_t GetCandidateColor( const TargetCandidateInfo& a_candidate )
	{
		if( a_candidate.selected )
			return selectedColor;

		if( !a_candidate.validProjection || !a_candidate.insideSelectionWindow || !a_candidate.hasLineOfSight )
			return rejectedColor;

		if( !a_candidate.isHostile )
			return a_candidate.passedFriendlySelection ? friendlyAcceptedColor : friendlyRejectedColor;

		return hostileColor;
	}

	static constexpr float friendlyInset = 0.1f;
	static constexpr float centerMarkSize = 0.0125f;
	static constexpr float projectionPlaneDistance = 100.0f;
	static constexpr std::uint32_t selectionWindowColor = 0x00FFFFFF;
	static constexpr std::uint32_t sweepColor = 0xFFFF00FF;
	static constexpr std::uint32_t centerMarkColor = 0xFFFFFFFF;
	static constexpr std::uint32_t hostileColor = 0xFF4000FF;
	static constexpr std::uint32_t friendlyRejectedColor = 0xFF00FFFF;
	static constexpr std::uint32_t friendlyAcceptedColor = 0x00FF00FF;
	static constexpr std::uint32_t friendlySelectionColor = 0x00FFFFFF;
	static constexpr std::uint32_t selectedColor = 0xFFFFFFFF;
	static constexpr std::uint32_t rejectedColor = 0x808080FF;

	static inline TRUEHUD_API::IVTrueHUD3* trueHUD = nullptr;
	static inline CandidateSelectionInfo debugInfo;
	static inline bool enabled = false;
};
