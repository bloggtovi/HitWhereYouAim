#pragma once

#include "GuidanceMath.h"

#include <vector>

// Steers the player's projectiles fired at a locked target until they hit it. Each projectile keeps
// its own target, so a new lock does not redirect arrows already in the air. Guidance stops on
// impact, when the target goes down (the arrow then flies on ballistically), or once the arrow has
// passed the target.
class ProjectileGuidance
{
public:
	static void Begin( RE::Projectile* a_projectile, RE::Actor* a_target )
	{
		if( !Settings::bTargetLockGuidance || !a_projectile || !a_target )
			return;

		if( flights.size() >= maxFlights )
			flights.erase( flights.begin() );

		flights.push_back( { a_projectile->GetHandle(), a_target->GetHandle(), 0.0f } );
	}

	static void Update( float a_delta )
	{
		bool inFlightAtLock = false;
		auto lockedTarget = TargetLock::GetTargetHandle();

		for( auto it = flights.begin(); it != flights.end(); )
		{
			if( Step( *it, a_delta ) )
			{
				if( lockedTarget && it->target == lockedTarget )
					inFlightAtLock = true;
				++it;
			}
			else
			{
				it = flights.erase( it );
			}
		}

		TargetLock::SetProjectileInFlight( inFlightAtLock );
	}

	static void Clear()
	{
		flights.clear();
		TargetLock::SetProjectileInFlight( false );
	}

private:
	struct Flight
	{
		RE::ObjectRefHandle projectile;
		RE::ActorHandle target;
		float time;
	};

	// Skyrim is Z-up, the solver is Y-up
	static GuidanceMath::Vector3 ToSolver( const RE::NiPoint3& a_value )
	{
		return { a_value.x, a_value.z, a_value.y };
	}

	// Returns false when guidance for this projectile is finished
	static bool Step( Flight& a_flight, float a_delta )
	{
		a_flight.time += a_delta;
		if( a_flight.time > maxFlightTime )
			return false;

		auto ref = a_flight.projectile.get();
		auto projectile = ref ? ref->As<RE::Projectile>() : nullptr;
		if( !projectile )
			return false;

		auto& runtimeData = projectile->GetProjectileRuntimeData();
		if( !runtimeData.impacts.empty() ||
			runtimeData.flags.any( RE::Projectile::Flags::kProcessedImpacts ) ||
			runtimeData.flags.any( RE::Projectile::Flags::kDestroyed ) )
			return false;

		auto targetPtr = a_flight.target.get();
		auto target = targetPtr.get();
		if( !TargetLock::IsValidTarget( target ) )
			return false;

		RE::NiPoint3 targetVelocity{ 0, 0, 0 };
		target->GetLinearVelocity( targetVelocity );

		float speed = runtimeData.linearVelocity.Length();
		if( speed < 1.0f )
			return false;

		auto step = GuidanceMath::Step(
			ToSolver( projectile->GetPosition() ),
			ToSolver( runtimeData.linearVelocity ),
			ToSolver( TargetLock::GetAimPoint( target ) ),
			ToSolver( targetVelocity ),
			projectile->GetGravity() * GetWorldGravity(),
			Settings::fTargetLockGuidanceTurnRate * ( std::numbers::pi / 180.0 ) * a_delta );

		if( step.passedTarget )
			return false;

		// Back to Skyrim space and to the angle convention SetProjectileAngle() uses:
		// direction = (sinZ cosX, cosZ cosX, -sinX), which also re-orients the arrow model
		auto direction = GuidanceMath::Normalize( step.velocity );
		float angleX = (float)std::asin( std::clamp( -direction.y, -1.0, 1.0 ) );
		float angleZ = (float)std::atan2( direction.x, direction.z );
		SetProjectileAngle( projectile, angleX, angleZ );

		return true;
	}

	static constexpr float maxFlightTime = 6.0f;
	static constexpr size_t maxFlights = 16;

	static inline std::vector<Flight> flights;
};
