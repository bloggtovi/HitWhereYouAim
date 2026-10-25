#pragma once

#include "GuidanceMath.h"

#include <numbers>
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

		Flight flight;
		flight.projectile = a_projectile->GetHandle();
		flight.target = a_target->GetHandle();
		flights.push_back( flight );
	}

	static void Update( float a_delta )
	{
		bool inFlightAtLock = false;
		auto lockedTarget = TargetLock::GetTargetHandle();

		for( auto it = flights.begin(); it != flights.end(); )
		{
			const char* stopReason = Step( *it, a_delta );
			if( !stopReason )
			{
				if( lockedTarget && it->target == lockedTarget )
					inFlightAtLock = true;
				++it;
			}
			else
			{
				if( Settings::bProjectileDiagnostics )
				{
					REX::INFO( "[Guidance] stopped after {} guided frames ({} solved, {:.3f}s, max turn {:.2f} deg/frame): {}",
						it->guidedFrames, it->solvedFrames, it->time, it->maxTurnDeg, stopReason );
				}
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
		float time = 0.0f;
		uint32_t guidedFrames = 0;
		uint32_t solvedFrames = 0;
		float maxTurnDeg = 0.0f;
	};

	// Skyrim is Z-up, the solver is Y-up
	static GuidanceMath::Vector3 ToSolver( const RE::NiPoint3& a_value )
	{
		return { a_value.x, a_value.z, a_value.y };
	}

	// Returns nullptr while guiding, or why guidance for this projectile finished
	static const char* Step( Flight& a_flight, float a_delta )
	{
		a_flight.time += a_delta;
		if( a_flight.time > maxFlightTime )
			return "timeout";

		auto ref = a_flight.projectile.get();
		auto projectile = ref ? ref->As<RE::Projectile>() : nullptr;
		if( !projectile )
			return "projectile gone";

		auto& runtimeData = projectile->GetProjectileRuntimeData();
		if( !runtimeData.impacts.empty() )
			return "impact";
		if( runtimeData.flags.any( RE::Projectile::Flags::kProcessedImpacts ) )
			return "impact processed flag";
		if( runtimeData.flags.any( RE::Projectile::Flags::kDestroyed ) )
			return "destroyed flag";

		auto targetPtr = a_flight.target.get();
		auto target = targetPtr.get();
		if( !TargetLock::IsValidTarget( target ) )
			return "target down or invalid";

		RE::NiPoint3 targetVelocity{ 0, 0, 0 };
		target->GetLinearVelocity( targetVelocity );

		float speed = runtimeData.linearVelocity.Length();
		if( speed < 1.0f )
			return "projectile stopped";

		auto step = GuidanceMath::Step(
			ToSolver( projectile->GetPosition() ),
			ToSolver( runtimeData.linearVelocity ),
			ToSolver( TargetLock::GetAimPoint( target ) ),
			ToSolver( targetVelocity ),
			projectile->GetGravity() * GetWorldGravity(),
			Settings::fTargetLockGuidanceTurnRate * ( std::numbers::pi / 180.0 ) * a_delta );

		if( step.passedTarget )
			return "passed target";

		auto before = GuidanceMath::Normalize( ToSolver( runtimeData.linearVelocity ) );

		// Back to Skyrim space and to the angle convention SetProjectileAngle() uses:
		// direction = (sinZ cosX, cosZ cosX, -sinX), which also re-orients the arrow model
		auto direction = GuidanceMath::Normalize( step.velocity );
		float angleX = (float)std::asin( std::clamp( -direction.y, -1.0, 1.0 ) );
		float angleZ = (float)std::atan2( direction.x, direction.z );
		SetProjectileAngle( projectile, angleX, angleZ );

		double turn = std::acos( std::clamp( BallisticSolver::Dot( before, direction ), -1.0, 1.0 ) ) * 180.0 / std::numbers::pi;
		a_flight.maxTurnDeg = std::max( a_flight.maxTurnDeg, (float)turn );
		++a_flight.guidedFrames;
		if( step.solved )
			++a_flight.solvedFrames;

		return nullptr;
	}

	static constexpr float maxFlightTime = 6.0f;
	static constexpr size_t maxFlights = 16;

	static inline std::vector<Flight> flights;
};
