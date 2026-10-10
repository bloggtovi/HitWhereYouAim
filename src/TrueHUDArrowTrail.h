#pragma once

#include "TrueHUDAPI.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

class TrueHUDArrowTrail
{
public:
	static bool Install()
	{
		trueHUD = static_cast<TRUEHUD_API::IVTrueHUD3*>(
			TRUEHUD_API::RequestPluginAPI( TRUEHUD_API::InterfaceVersion::V3 ) );

		if( !trueHUD )
		{
			REX::DEBUG( "Failed to request TrueHUD API V3" );
			return false;
		}

		REX::DEBUG( "TrueHUD arrow trail installed" );
		return true;
	}

	static void BeginArrow( RE::Projectile* a_projectile )
	{
		std::scoped_lock lock( trailMutex );

		projectileHandle.reset();
		worldTrailPoints.clear();
		active = false;

		if( !a_projectile )
			return;

		projectileHandle = a_projectile->GetHandle();

		auto projectile3D = a_projectile->Get3D();

		if( projectile3D )
			AppendPosition( projectile3D->world.translate );
		else
			AppendPosition( a_projectile->GetPosition() );

		active = true;
	}

	static void Update()
	{
		if( !enabled || !trueHUD )
			return;

		auto ui = RE::UI::GetSingleton();

		if( !ui || ui->GameIsPaused() )
			return;

		std::scoped_lock lock( trailMutex );

		UpdateTrail();

		if( worldTrailPoints.size() < 2 )
			return;

		auto camera = RE::Main::WorldRootCamera();

		if( !camera )
			return;

		const RE::NiPoint3& cameraPosition = camera->world.translate;

		for( std::size_t i = 1; i < worldTrailPoints.size(); i++ )
		{
			const RE::NiPoint3& start = worldTrailPoints[i - 1];
			const RE::NiPoint3& end = worldTrailPoints[i];
			float width = GetSegmentWidth( start, end, cameraPosition );

			trueHUD->DrawLine( start, end, 0.0f, color, width );
		}
	}

	static void Stop()
	{
		std::scoped_lock lock( trailMutex );

		projectileHandle.reset();
		worldTrailPoints.clear();
		active = false;
	}

	static void SetWidth( float a_width )
	{
		std::scoped_lock lock( trailMutex );
		trailWidth = std::max( a_width, 0.1f );
	}

	static void SetColor( float a_red, float a_green, float a_blue, float a_alpha )
	{
		std::scoped_lock lock( trailMutex );

		std::uint32_t red = ToColorByte( a_red );
		std::uint32_t green = ToColorByte( a_green );
		std::uint32_t blue = ToColorByte( a_blue );
		std::uint32_t alpha = ToColorByte( a_alpha );

		color = red << 24 | green << 16 | blue << 8 | alpha;
	}

	static void SetMinimumPointDistance( float a_distance )
	{
		std::scoped_lock lock( trailMutex );
		minimumPointDistance = std::max( a_distance, 0.0f );
	}

	static void SetMaximumPointCount( std::size_t a_count )
	{
		std::scoped_lock lock( trailMutex );
		maximumPointCount = std::max<std::size_t>( a_count, 2 );
	}

	static void SetDistanceWidthScaling(
		bool a_enabled,
		float a_referenceDistance,
		float a_minimumWidth,
		float a_maximumWidth )
	{
		std::scoped_lock lock( trailMutex );

		scaleWidthByDistance = a_enabled;
		widthReferenceDistance = std::max( a_referenceDistance, 1.0f );
		minimumTrailWidth = std::max( a_minimumWidth, 0.1f );
		maximumTrailWidth = std::max( a_maximumWidth, minimumTrailWidth );
	}

	static void SetEnabled( bool a_enabled )
	{
		enabled = a_enabled;

		if( !enabled )
			Stop();
	}

private:
	static bool IsVATSPlaying()
	{
		auto pcCamera = RE::PlayerCamera::GetSingleton();

		if( pcCamera )
		{
			auto state = pcCamera->currentState;

			if( state && state.get()->id == RE::CameraState::kVATS )
				return true;
		}

		return false;
	}

	static void UpdateTrail()
	{
		if( !active )
			return;

		auto reference = projectileHandle.get();

		if( !reference )
		{
			active = false;
			projectileHandle.reset();
			return;
		}

		auto projectile = reference->As<RE::Projectile>();

		if( !projectile )
		{
			active = false;
			projectileHandle.reset();
			return;
		}

		if( HasHitSomething( projectile ) )
		{
			active = false;
			projectileHandle.reset();
			return;
		}

		auto projectile3D = projectile->Get3D();

		if( projectile3D )
			AppendPosition( projectile3D->world.translate );
		else
			AppendPosition( projectile->GetPosition() );
	}

	static bool HasHitSomething( const RE::Projectile* a_projectile )
	{
		if( !a_projectile )
			return false;

		auto& runtimeData = a_projectile->GetProjectileRuntimeData();

		return !runtimeData.impacts.empty() ||
			runtimeData.flags.any( RE::Projectile::Flags::kProcessedImpacts ) ||
			runtimeData.flags.any( RE::Projectile::Flags::kDestroyed );
	}

	static void AppendPosition( const RE::NiPoint3& a_position )
	{
		if( !worldTrailPoints.empty() )
		{
			RE::NiPoint3 delta = a_position - worldTrailPoints.back();

			if( delta.SqrLength() < minimumPointDistance * minimumPointDistance )
				return;
		}

		worldTrailPoints.push_back( a_position );

		if( worldTrailPoints.size() > maximumPointCount )
			worldTrailPoints.erase( worldTrailPoints.begin() );
	}

	static float GetSegmentWidth(
		const RE::NiPoint3& a_start,
		const RE::NiPoint3& a_end,
		const RE::NiPoint3& a_cameraPosition )
	{
		if( !scaleWidthByDistance )
			return trailWidth;

		RE::NiPoint3 midpoint{
			(a_start.x + a_end.x) * 0.5f,
			(a_start.y + a_end.y) * 0.5f,
			(a_start.z + a_end.z) * 0.5f
		};

		float distance = (midpoint - a_cameraPosition).Length();

		if( distance <= 0.001f )
			return maximumTrailWidth;

		float width = trailWidth * widthReferenceDistance / distance;
		return std::clamp( width, minimumTrailWidth, maximumTrailWidth );
	}

	static std::uint32_t ToColorByte( float a_value )
	{
		return static_cast<std::uint32_t>( std::clamp( a_value, 0.0f, 1.0f ) * 255.0f + 0.5f );
	}

	static inline TRUEHUD_API::IVTrueHUD3* trueHUD = nullptr;
	static inline RE::ObjectRefHandle projectileHandle;
	static inline std::vector<RE::NiPoint3> worldTrailPoints;
	static inline std::mutex trailMutex;
	static inline std::uint32_t color = 0xFF400DFF;
	static inline float trailWidth = 3.0f;
	static inline float minimumPointDistance = 5.0f;
	static inline float widthReferenceDistance = 1000.0f;
	static inline float minimumTrailWidth = 1.0f;
	static inline float maximumTrailWidth = 6.0f;
	static inline std::size_t maximumPointCount = 256;
	static inline bool scaleWidthByDistance = true;
	static inline bool active = false;
	static inline bool enabled = true;
};
