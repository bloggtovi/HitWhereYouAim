#pragma once

#include <cmath>
#include <vector>

// Flight recorder for the player's projectiles, written to HitWhereYouAim.log.
//
// Every frame it samples the projectile position (and the locked target's aim point) until impact,
// then fits the real trajectory: x and y linearly, z as a parabola. That yields the projectile's
// true launch speed and gravity, which are compared with the values the aim solver used, plus the
// closest approach to the target and what the projectile actually hit. This is the data needed to
// calibrate aiming against the engine instead of assuming its physics.
class ProjectileDiagnostics
{
public:
	static void Begin( RE::Projectile* a_projectile, RE::Actor* a_target, const RE::NiPoint3& a_aimPoint,
		const RE::NiPoint3& a_targetVelocity, float a_solverSpeed, float a_solverGravity, float a_drawPower, bool a_solved )
	{
		if( !Settings::bProjectileDiagnostics || !a_projectile )
			return;

		// Keep the recorder bounded even if impacts are never reported
		if( flights.size() >= maxFlights )
			flights.erase( flights.begin() );

		Flight flight;
		flight.projectile = a_projectile->GetHandle();
		flight.target = a_target ? a_target->GetHandle() : RE::ActorHandle();
		flight.solverSpeed = a_solverSpeed;
		flight.solverGravity = a_solverGravity;
		flight.solved = a_solved;
		flight.drawPower = a_drawPower;
		flight.targetVelocity = a_targetVelocity;
		if( a_target )
		{
			if( auto root = a_target->Get3D2() )
				flight.targetRadius = root->worldBound.radius;
		}
		flight.startPosition = a_projectile->GetPosition();
		flight.startAimPoint = a_aimPoint;
		flight.samples.push_back( { 0.0, a_projectile->GetPosition() } );
		flights.push_back( flight );
	}

	static void Update( float a_delta )
	{
		if( flights.empty() )
			return;

		for( auto it = flights.begin(); it != flights.end(); )
		{
			if( Step( *it, a_delta ) )
				++it;
			else
				it = flights.erase( it );
		}
	}

	static void Clear()
	{
		flights.clear();
	}

private:
	struct Sample
	{
		double time;
		RE::NiPoint3 position;
	};

	struct Flight
	{
		RE::ObjectRefHandle projectile;
		RE::ActorHandle target;
		float solverSpeed = 0.0f;
		float solverGravity = 0.0f;
		bool solved = false;
		float drawPower = 1.0f;
		float targetRadius = 0.0f;
		RE::NiPoint3 targetVelocity;
		RE::NiPoint3 startPosition;
		RE::NiPoint3 startAimPoint;
		std::vector<Sample> samples;
		double time = 0.0;
		float closestApproach = FLT_MAX;
		float closestApproachTime = 0.0f;

		// At the closest approach: where the arrow was, where the target really was, and where the
		// solver predicted the target would be (start aim point + velocity * time). The arrow vs the
		// prediction is the ballistic error; the target vs the prediction is how far it deviated.
		RE::NiPoint3 arrowAtClosest;
		RE::NiPoint3 targetAtClosest;
		RE::NiPoint3 predictedAtClosest;
	};

	// Returns false when the flight is finished and logged
	static bool Step( Flight& a_flight, float a_delta )
	{
		a_flight.time += a_delta;

		auto ref = a_flight.projectile.get();
		auto projectile = ref ? ref->As<RE::Projectile>() : nullptr;
		if( !projectile )
		{
			Finish( a_flight, nullptr, "projectile gone" );
			return false;
		}

		auto& runtimeData = projectile->GetProjectileRuntimeData();
		RE::NiPoint3 position = projectile->GetPosition();

		// Closest approach of the segment travelled this frame to the target's current aim point
		if( auto targetPtr = a_flight.target.get() )
		{
			if( auto root = targetPtr->Get3D2() )
			{
				RE::NiPoint3 aim = root->worldBound.center;
				static RE::BSFixedString spineName{ "NPC Spine2 [Spn2]" };
				if( auto spine = root->GetObjectByName( spineName ) )
					aim = spine->world.translate;

				RE::NiPoint3 closestPoint;
				float distance = SegmentPointDistance( a_flight.samples.back().position, position, aim, &closestPoint );
				if( distance < a_flight.closestApproach )
				{
					a_flight.closestApproach = distance;
					a_flight.closestApproachTime = (float)a_flight.time;
					a_flight.arrowAtClosest = closestPoint;
					a_flight.targetAtClosest = aim;
					a_flight.predictedAtClosest = a_flight.startAimPoint + a_flight.targetVelocity * (float)a_flight.time;
				}
			}
		}

		bool impacted = !runtimeData.impacts.empty();
		if( !impacted )
			a_flight.samples.push_back( { a_flight.time, position } );

		if( impacted )
		{
			Finish( a_flight, projectile, "impact" );
			return false;
		}

		if( a_flight.time > maxFlightTime || a_flight.samples.size() > maxSamples )
		{
			Finish( a_flight, projectile, "timeout" );
			return false;
		}

		return true;
	}

	static void Finish( const Flight& a_flight, RE::Projectile* a_projectile, const char* a_reason )
	{
		// The spawn sample is taken during projectile init, part way through a frame, so its time stamp
		// is not on the same clock as the per-frame samples. Fit the per-frame samples only.
		double vx = 0, vy = 0, vz = 0, g = 0;
		bool fitted = FitTrajectory( a_flight.samples, 1, vx, vy, vz, g );
		double measuredSpeed = std::sqrt( vx * vx + vy * vy + vz * vz );

		std::string hitName = "nothing";
		bool hitTarget = false;
		float impactToAim = -1.0f;
		if( a_projectile )
		{
			for( auto impact : a_projectile->GetProjectileRuntimeData().impacts )
			{
				if( !impact )
					continue;

				auto collidee = impact->collidee.get();
				if( collidee )
				{
					auto name = collidee->GetDisplayFullName();
					hitName = name && *name ? name : "object";
					hitTarget = a_flight.target && collidee.get() == a_flight.target.get().get();
				}
				else
				{
					hitName = "world";
				}

				if( auto targetPtr = a_flight.target.get() )
				{
					if( auto root = targetPtr->Get3D2() )
						impactToAim = impact->desiredTargetLoc.GetDistance( root->worldBound.center );
				}
				break;
			}
		}

		auto targetPtr = a_flight.target.get();
		float targetDistance = targetPtr ? a_flight.startPosition.GetDistance( a_flight.startAimPoint ) * unitToMeter : 0.0f;
		auto targetName = targetPtr ? targetPtr->GetDisplayFullName() : nullptr;

		REX::INFO(
			"[ArrowDiag] end={} flight={:.3f}s samples={} drawPower={:.2f} | speed solver={:.1f} measured={:.1f} ({:+.2f}%) | gravity solver={:.1f} measured={:.1f} ({:+.2f}%){}",
			a_reason, a_flight.time, a_flight.samples.size(), a_flight.drawPower,
			a_flight.solverSpeed, measuredSpeed, Percent( measuredSpeed, a_flight.solverSpeed ),
			a_flight.solverGravity, g, Percent( g, a_flight.solverGravity ),
			fitted ? "" : " (not enough samples to fit)" );

		if( targetPtr )
		{
			// A projectile that strikes an actor is removed in the same frame, so its impact record is
			// often never seen. Passing within the target's body while disappearing counts as a hit.
			float hitRadius = std::max( a_flight.targetRadius * 0.6f, 12.0f );
			bool vanished = std::string_view( a_reason ) == "projectile gone";
			const char* verdict = hitTarget ? "HIT" :
				( vanished && a_flight.closestApproach <= hitRadius ) ? "PROBABLE HIT" :
				a_flight.closestApproach <= hitRadius ? "GRAZE" : "MISS";

			RE::NiPoint3 ballistic = a_flight.arrowAtClosest - a_flight.predictedAtClosest;
			RE::NiPoint3 deviation = a_flight.targetAtClosest - a_flight.predictedAtClosest;

			REX::INFO(
				"[ArrowDiag]   {} target='{}' distance={:.1f}m velocity=({:.0f},{:.0f},{:.0f}) solved={} | closest approach={:.1f}u ({:.2f}m) at {:.3f}s, hit radius {:.0f}u | hit='{}' impactToTargetCenter={:.1f}u",
				verdict, targetName ? targetName : "", targetDistance,
				a_flight.targetVelocity.x, a_flight.targetVelocity.y, a_flight.targetVelocity.z, a_flight.solved,
				a_flight.closestApproach, a_flight.closestApproach * unitToMeter, a_flight.closestApproachTime, hitRadius,
				hitName, impactToAim );

			REX::INFO(
				"[ArrowDiag]   error split: ballistic (arrow vs predicted point)={:.1f}u [dz={:+.1f}] | target deviation (actual vs predicted)={:.1f}u [dz={:+.1f}]",
				ballistic.Length(), ballistic.z, deviation.Length(), deviation.z );
		}
		else
		{
			REX::INFO( "[ArrowDiag]   no target lock | hit='{}'", hitName );
		}
	}

	// Least squares: x(t) and y(t) linear, z(t) = z0 + vz t - g/2 t^2
	static bool FitTrajectory( const std::vector<Sample>& a_samples, size_t a_first, double& out_vx, double& out_vy, double& out_vz, double& out_g )
	{
		if( a_samples.size() < a_first + 4 )
			return false;

		double s0 = 0, s1 = 0, s2 = 0, s3 = 0, s4 = 0;
		double sx = 0, sxt = 0, sy = 0, syt = 0, sz = 0, szt = 0, szt2 = 0;
		for( size_t i = a_first; i < a_samples.size(); ++i )
		{
			const auto& s = a_samples[i];
			double t = s.time;
			double t2 = t * t;
			s0 += 1; s1 += t; s2 += t2; s3 += t2 * t; s4 += t2 * t2;
			sx += s.position.x; sxt += s.position.x * t;
			sy += s.position.y; syt += s.position.y * t;
			sz += s.position.z; szt += s.position.z * t; szt2 += s.position.z * t2;
		}

		double linearDet = s0 * s2 - s1 * s1;
		if( std::abs( linearDet ) < 1e-12 )
			return false;

		out_vx = ( s0 * sxt - s1 * sx ) / linearDet;
		out_vy = ( s0 * syt - s1 * sy ) / linearDet;

		// Normal equations for z = a + b t + c t^2
		double m[3][4] = {
			{ s0, s1, s2, sz },
			{ s1, s2, s3, szt },
			{ s2, s3, s4, szt2 }
		};
		for( int col = 0; col < 3; ++col )
		{
			int pivot = col;
			for( int row = col + 1; row < 3; ++row )
				if( std::abs( m[row][col] ) > std::abs( m[pivot][col] ) )
					pivot = row;
			if( std::abs( m[pivot][col] ) < 1e-12 )
				return false;
			for( int k = 0; k < 4; ++k )
				std::swap( m[col][k], m[pivot][k] );
			for( int row = 0; row < 3; ++row )
			{
				if( row == col )
					continue;
				double f = m[row][col] / m[col][col];
				for( int k = 0; k < 4; ++k )
					m[row][k] -= f * m[col][k];
			}
		}

		double b = m[1][3] / m[1][1];
		double c = m[2][3] / m[2][2];
		out_vz = b;
		out_g = -2.0 * c;
		return true;
	}

	static float SegmentPointDistance( const RE::NiPoint3& a_start, const RE::NiPoint3& a_end, const RE::NiPoint3& a_point, RE::NiPoint3* out_closest )
	{
		RE::NiPoint3 segment = a_end - a_start;
		float lengthSquared = segment.SqrLength();
		float t = 0.0f;
		if( lengthSquared > 1e-6f )
			t = std::clamp( ( a_point - a_start ).Dot( segment ) / lengthSquared, 0.0f, 1.0f );

		RE::NiPoint3 closest = a_start + segment * t;
		if( out_closest )
			*out_closest = closest;
		return a_point.GetDistance( closest );
	}

	static double Percent( double a_measured, double a_expected )
	{
		return std::abs( a_expected ) > 1e-6 ? ( a_measured - a_expected ) / a_expected * 100.0 : 0.0;
	}

	static constexpr float unitToMeter = 0.0142875f;
	static constexpr double maxFlightTime = 6.0;
	static constexpr size_t maxSamples = 1000;
	static constexpr size_t maxFlights = 8;

	static inline std::vector<Flight> flights;
};
