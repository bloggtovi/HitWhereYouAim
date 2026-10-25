#pragma once

// In-flight projectile guidance, independent of the game so it can be unit tested.
//
// Every frame the intercept is solved again from the projectile's current position and velocity to
// the target's current aim point and velocity, with gravity, and the velocity is turned toward that
// solution at a bounded rate. Re-solving from the live state corrects launch point offsets, model
// error and target manoeuvres as they happen, instead of relying on one prediction at release.
//
// Solver space is Y-up with gravity along -Y (same as BallisticSolver).

#include "BallisticSolver.h"

#include <cmath>

namespace GuidanceMath
{
	using BallisticSolver::Vector3;

	struct StepResult
	{
		Vector3 velocity;
		bool passedTarget = false;  // moving away from the target: guidance should stop
		bool solved = false;        // true when the ballistic intercept was used, false for direct pursuit
	};

	inline Vector3 Normalize( const Vector3& a_value )
	{
		double length = BallisticSolver::Length( a_value );
		return length > 1e-12 ? a_value * ( 1.0 / length ) : Vector3{ 0.0, 0.0, 0.0 };
	}

	// Rotates a_from toward a_to (both unit vectors) by at most a_maxAngle radians
	inline Vector3 RotateToward( const Vector3& a_from, const Vector3& a_to, double a_maxAngle )
	{
		double cosAngle = std::clamp( BallisticSolver::Dot( a_from, a_to ), -1.0, 1.0 );
		double angle = std::acos( cosAngle );
		if( angle <= a_maxAngle || angle < 1e-9 )
			return a_to;

		double sinAngle = std::sin( angle );
		if( sinAngle < 1e-9 )
		{
			// Opposite directions: no unique plane, keep the current heading
			return a_from;
		}

		Vector3 blended = a_from * ( std::sin( angle - a_maxAngle ) / sinAngle ) + a_to * ( std::sin( a_maxAngle ) / sinAngle );
		return Normalize( blended );
	}

	inline StepResult Step( const Vector3& a_position, const Vector3& a_velocity, const Vector3& a_targetPosition,
		const Vector3& a_targetVelocity, double a_gravity, double a_maxTurnAngle )
	{
		StepResult result;
		result.velocity = a_velocity;

		double speed = BallisticSolver::Length( a_velocity );
		if( speed < 1e-6 )
			return result;

		Vector3 toTarget = a_targetPosition - a_position;
		if( BallisticSolver::Dot( toTarget, a_velocity ) <= 0.0 )
		{
			// Already past the target. Turning back would make the projectile boomerang.
			result.passedTarget = true;
			return result;
		}

		Vector3 desired;
		BallisticSolver::Result solution;
		if( BallisticSolver::Solve( a_position, a_targetPosition, a_targetVelocity, speed, a_gravity, solution, false ) )
		{
			desired = Normalize( solution.launchVelocity );
			result.solved = true;
		}
		else
		{
			desired = Normalize( toTarget );
		}

		Vector3 current = a_velocity * ( 1.0 / speed );
		result.velocity = RotateToward( current, desired, a_maxTurnAngle ) * speed;
		return result;
	}
}
