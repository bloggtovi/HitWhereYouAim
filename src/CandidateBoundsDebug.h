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
		trueHUD = (TRUEHUD_API::IVTrueHUD3*)TRUEHUD_API::RequestPluginAPI( TRUEHUD_API::InterfaceVersion::V3 );

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

		auto player = RE::PlayerCharacter::GetSingleton();
		float projectileSpeed;
		float projectileGravity;
		GetPlayerProjectileData( player, projectileSpeed, projectileGravity );
		RE::NiPoint3 fireOrigin = player ? player->GetPosition() : RE::NiPoint3::Zero();
		bool fixedPitchMode =
			Settings::bEnabledPitchCorrection &&
			Settings::iPitchCorrectionMode == (uint32_t)PitchCorrectionMode::kFixedAngle;

		GetPotentialPlayerTarget(
			Settings::fMaximumTargetDistance * ( 1.0f / 0.0142875f ),
			projectileSpeed,
			projectileGravity,
			&fireOrigin,
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

			auto projectedSweep = GetProjectedSweep( currentBounds, projectedBounds );

			auto actor3D = actor->Get3D2();
			if( !actor3D )
				continue;

			RE::NiPoint3 currentTargetPosition = actor3D->worldBound.center;
			currentTargetPosition.x = actor->GetPositionX();
			currentTargetPosition.y = actor->GetPositionY();

			RE::NiPoint3 velocity;
			actor->GetLinearVelocity( velocity );
			RE::NiPoint3 predictedTargetPosition = currentTargetPosition + velocity * predictionTime;

			auto friendlyCurrentBounds = GetHorizontallyInsetBounds( currentBounds, friendlyInset );
			auto friendlyProjectedBounds = GetHorizontallyInsetBounds( projectedBounds, friendlyInset );

			if( fixedPitchMode )
			{
				friendlyCurrentBounds = ExtendProjectedBoundsUpwardForGravity(
					camera, friendlyCurrentBounds, fireOrigin, currentTargetPosition, projectileSpeed, projectileGravity );
				friendlyProjectedBounds = ExtendProjectedBoundsUpwardForGravity(
					camera, friendlyProjectedBounds, fireOrigin, predictedTargetPosition, projectileSpeed, projectileGravity );
			}

			auto friendlyProjectedSweep = GetProjectedSweep( friendlyCurrentBounds, friendlyProjectedBounds );

			if( !projectedBounds.valid )
				continue;

			auto& projectedMin = projectedBounds.min;
			auto& projectedMax = projectedBounds.max;

			uint32_t color = GetCandidateColor( candidate );
			float thickness = candidate.selected ? 2.0f : 1.0f;

			if( predictionTime > 0 && projectedSweep.valid )
			{
				DrawProjectedSweep(
					camera,
					screenSize.width,
					screenSize.height,
					projectedSweep,
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

			if( !candidate.isHostile && friendlyProjectedSweep.valid )
			{
				DrawProjectedSweep(
					camera,
					screenSize.width,
					screenSize.height,
					friendlyProjectedSweep,
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

	static void GetPlayerProjectileData( RE::PlayerCharacter* a_player, float& out_speed, float& out_gravity )
	{
		out_speed = 0.0f;
		out_gravity = 0.0f;

		if( !a_player )
			return;

		auto rightObject = a_player->GetEquippedObject( false );
		auto leftObject = a_player->GetEquippedObject( true );
		auto rightWeapon = rightObject ? rightObject->As<RE::TESObjectWEAP>() : nullptr;
		auto leftWeapon = leftObject ? leftObject->As<RE::TESObjectWEAP>() : nullptr;
		bool hasRangedWeapon = (rightWeapon && rightWeapon->IsRanged()) || (leftWeapon && leftWeapon->IsRanged());
		RE::BGSProjectile* projectile = nullptr;

		if( hasRangedWeapon )
		{
			auto ammo = a_player->GetCurrentAmmo();
			if( ammo )
				projectile = ammo->data.projectile;
		}

		if( !projectile )
			projectile = GetEquippedMagicProjectile( a_player, false );

		if( !projectile )
			projectile = GetEquippedMagicProjectile( a_player, true );

		if( projectile )
		{
			bool isHitScan = projectile->IsBeam() || projectile->IsFlamethrower();
			out_speed = isHitScan ? 1.e6f : projectile->data.speed;
			out_gravity = isHitScan ? 0 : projectile->data.gravity * GetWorldGravity();
		}
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

	static void DrawScreenLine(
		RE::NiCamera* a_camera,
		uint32_t a_screenWidth,
		uint32_t a_screenHeight,
		const RE::NiPoint2& a_start,
		const RE::NiPoint2& a_end,
		float a_duration,
		uint32_t a_color,
		float a_thickness )
	{
		RE::NiPoint3 start;
		RE::NiPoint3 end;

		if( !ScreenCoordinateToWorld( a_camera, a_screenWidth, a_screenHeight, a_start.x, a_start.y, start ) ||
			!ScreenCoordinateToWorld( a_camera, a_screenWidth, a_screenHeight, a_end.x, a_end.y, end ) )
		{
			return;
		}

		trueHUD->DrawLine( start, end, a_duration, a_color, a_thickness );
	}

	static void DrawProjectedSweep(
		RE::NiCamera* a_camera,
		uint32_t a_screenWidth,
		uint32_t a_screenHeight,
		const ProjectedSweep& a_sweep,
		float a_duration,
		uint32_t a_color,
		float a_thickness )
	{
		for( size_t i = 0; i < a_sweep.points.size(); ++i )
		{
			DrawScreenLine(
				a_camera,
				a_screenWidth,
				a_screenHeight,
				a_sweep.points[i],
				a_sweep.points[(i + 1) % a_sweep.points.size()],
				a_duration,
				a_color,
				a_thickness );
		}
	}

	static void DrawRectangle(
		RE::NiCamera* a_camera,
		uint32_t a_screenWidth,
		uint32_t a_screenHeight,
		float a_left,
		float a_right,
		float a_bottom,
		float a_top,
		float a_duration,
		uint32_t a_color,
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
		uint32_t a_screenWidth,
		uint32_t a_screenHeight,
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
		uint32_t a_screenWidth,
		uint32_t a_screenHeight,
		float a_x,
		float a_y,
		RE::NiPoint3& a_worldPosition )
	{
		float screenX = ( a_x * 0.5f + 0.5f ) * a_screenWidth;
		float screenY = ( 0.5f - a_y * 0.5f ) * a_screenHeight;
		RE::NiPoint3 origin;
		RE::NiPoint3 direction;

		if( !a_camera->WindowPointToRay(
			(int32_t)screenX,
			(int32_t)screenY,
			origin,
			direction,
			(float)a_screenWidth,
			(float)a_screenHeight ) )
		{
			return false;
		}

		a_worldPosition = origin + direction * projectionPlaneDistance;
		return true;
	}

	static uint32_t GetCandidateColor( const TargetCandidateInfo& a_candidate )
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
	static constexpr uint32_t selectionWindowColor = 0x00FFFFFF;
	static constexpr uint32_t sweepColor = 0xFFFF00FF;
	static constexpr uint32_t centerMarkColor = 0xFFFFFFFF;
	static constexpr uint32_t hostileColor = 0xFF4000FF;
	static constexpr uint32_t friendlyRejectedColor = 0xFF00FFFF;
	static constexpr uint32_t friendlyAcceptedColor = 0x00FF00FF;
	static constexpr uint32_t friendlySelectionColor = 0x00FFFFFF;
	static constexpr uint32_t selectedColor = 0xFFFFFFFF;
	static constexpr uint32_t rejectedColor = 0x808080FF;

	static inline TRUEHUD_API::IVTrueHUD3* trueHUD = nullptr;
	static inline CandidateSelectionInfo debugInfo;
	static inline bool enabled = false;
};
