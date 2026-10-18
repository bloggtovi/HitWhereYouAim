/*
* Hit Where You Aim
* Ballistic trajectory solver
*
* This file is based on the ballistic trajectory solver by Forrest Smith:
* https://github.com/forrestthewoods/lib_fts/blob/master/code/fts_ballistic_trajectory.cs
*
* The original solver is dual-licensed to the public domain and under the
* following license:
*
*     You are granted a perpetual, irrevocable license to copy, modify,
*     publish, and distribute this file as you see fit.
*
* The quadratic, cubic, and quartic root-solving functions are based on
* Roots3And4.c by Jochen Schwarze from Graphics Gems I:
* https://github.com/erich666/GraphicsGems/blob/master/gems/Roots3And4.c
*
* Rewritten into generic C++ and modified for Hit Where You Aim.
*
* HWYA modifications are licensed under GPL-3.0-or-later with the
* Modding Exception and GPL-3.0 Linking Exception
* (with Corresponding Source).
*
* See COPYING and EXCEPTIONS for the complete license terms.
*/

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace BallisticSolver
{
	constexpr double EPSILON = 1.0e-9;
	constexpr double ROOT_EPSILON = 1.0e-6;

	template<typename T>
	constexpr const T& _min( const T& a, const T& b ) { return a < b ? a : b; }
	template<typename T>
	constexpr const T& _max( const T& a, const T& b ) { return a > b ? a : b; }

	struct Vector3
	{
		double x{ 0.0 };
		double y{ 0.0 };
		double z{ 0.0 };

		Vector3 operator+( const Vector3& a_other ) const
		{
			return { x + a_other.x, y + a_other.y, z + a_other.z };
		}

		Vector3 operator-( const Vector3& a_other ) const
		{
			return { x - a_other.x, y - a_other.y, z - a_other.z };
		}

		Vector3 operator*( double a_scalar ) const
		{
			return { x * a_scalar, y * a_scalar, z * a_scalar };
		}
	};

	struct Result
	{
		Vector3 launchVelocity;
		Vector3 interceptPosition;
		double pitch{ 0.0 };
		double yaw{ 0.0 };
		double flightTime{ 0.0 };
	};

	inline bool IsZero( double a_value )
	{
		return std::abs( a_value ) <= EPSILON;
	}

	inline bool IsFinite( double a_value )
	{
		return std::isfinite( a_value );
	}

	inline bool IsFinite( const Vector3& a_value )
	{
		return IsFinite( a_value.x ) && IsFinite( a_value.y ) &&
			IsFinite( a_value.z );
	}

	inline double Dot( const Vector3& a_left, const Vector3& a_right )
	{
		return a_left.x * a_right.x + a_left.y * a_right.y +
			a_left.z * a_right.z;
	}

	inline double Length( const Vector3& a_value )
	{
		return std::sqrt( Dot( a_value, a_value ) );
	}

	inline int SolveQuadratic( double a_c0, double a_c1, double a_c2,
		double& a_s0, double& a_s1 )
	{
		const double nan = std::numeric_limits<double>::quiet_NaN();
		a_s0 = nan;
		a_s1 = nan;

		if( IsZero( a_c0 ) )
		{
			if( IsZero( a_c1 ) )
			{
				return 0;
			}

			a_s0 = -a_c2 / a_c1;
			return 1;
		}

		const double p = a_c1 / (2.0 * a_c0);
		const double q = a_c2 / a_c0;
		const double discriminant = p * p - q;

		if( IsZero( discriminant ) )
		{
			a_s0 = -p;
			return 1;
		}

		if( discriminant < 0.0 )
		{
			return 0;
		}

		const double squareRoot = std::sqrt( discriminant );
		a_s0 = squareRoot - p;
		a_s1 = -squareRoot - p;
		return 2;
	}

	inline int SolveCubic( double a_c0, double a_c1, double a_c2,
		double a_c3, double& a_s0, double& a_s1, double& a_s2 )
	{
		const double nan = std::numeric_limits<double>::quiet_NaN();
		a_s0 = nan;
		a_s1 = nan;
		a_s2 = nan;

		if( IsZero( a_c0 ) )
		{
			return SolveQuadratic( a_c1, a_c2, a_c3, a_s0, a_s1 );
		}

		const double A = a_c1 / a_c0;
		const double B = a_c2 / a_c0;
		const double C = a_c3 / a_c0;
		const double squareA = A * A;
		const double p = (-squareA / 3.0 + B) / 3.0;
		const double q =
			(2.0 * A * squareA / 27.0 - A * B / 3.0 + C) / 2.0;
		const double cubicP = p * p * p;
		double discriminant = q * q + cubicP;

		if( IsZero( discriminant ) )
		{
			discriminant = 0.0;
		}

		int rootCount = 0;

		if( discriminant == 0.0 )
		{
			if( IsZero( q ) )
			{
				a_s0 = 0.0;
				rootCount = 1;
			}
			else
			{
				const double u = std::cbrt( -q );
				a_s0 = 2.0 * u;
				a_s1 = -u;
				rootCount = 2;
			}
		}
		else if( discriminant < 0.0 )
		{
			double argument = -q / std::sqrt( -cubicP );
			argument = std::clamp( argument, -1.0, 1.0 );

			const double pi = std::acos( -1.0 );
			const double phi = std::acos( argument ) / 3.0;
			const double t = 2.0 * std::sqrt( -p );
			a_s0 = t * std::cos( phi );
			a_s1 = -t * std::cos( phi + pi / 3.0 );
			a_s2 = -t * std::cos( phi - pi / 3.0 );
			rootCount = 3;
		}
		else
		{
			const double squareRoot = std::sqrt( discriminant );
			const double u = std::cbrt( squareRoot - q );
			const double v = -std::cbrt( squareRoot + q );
			a_s0 = u + v;
			rootCount = 1;
		}

		const double substitution = A / 3.0;
		if( rootCount > 0 )
		{
			a_s0 -= substitution;
		}
		if( rootCount > 1 )
		{
			a_s1 -= substitution;
		}
		if( rootCount > 2 )
		{
			a_s2 -= substitution;
		}
		return rootCount;
	}

	inline int SolveQuartic( double a_c0, double a_c1, double a_c2,
		double a_c3, double a_c4, double& a_s0, double& a_s1,
		double& a_s2, double& a_s3 )
	{
		const double nan = std::numeric_limits<double>::quiet_NaN();
		a_s0 = nan;
		a_s1 = nan;
		a_s2 = nan;
		a_s3 = nan;

		if( IsZero( a_c0 ) )
		{
			return SolveCubic( a_c1, a_c2, a_c3, a_c4, a_s0, a_s1,
				a_s2 );
		}

		const double A = a_c1 / a_c0;
		const double B = a_c2 / a_c0;
		const double C = a_c3 / a_c0;
		const double D = a_c4 / a_c0;
		const double squareA = A * A;
		const double p = -3.0 * squareA / 8.0 + B;
		const double q = squareA * A / 8.0 - A * B / 2.0 + C;
		const double r = -3.0 * squareA * squareA / 256.0 +
			squareA * B / 16.0 - A * C / 4.0 + D;

		std::array<double, 4> roots{ nan, nan, nan, nan };
		int rootCount = 0;

		if( IsZero( r ) )
		{
			rootCount = SolveCubic( 1.0, 0.0, p, q, roots[0], roots[1],
				roots[2] );
			bool containsZero = false;

			for( int i = 0; i < rootCount; i++ )
			{
				if( IsZero( roots[i] ) )
				{
					containsZero = true;
					break;
				}
			}

			if( !containsZero && rootCount < 4 )
			{
				roots[rootCount++] = 0.0;
			}
		}
		else
		{
			double z0;
			double z1;
			double z2;
			const int resolventRootCount = SolveCubic( 1.0, -p / 2.0, -r,
				r * p / 2.0 - q * q / 8.0, z0, z1, z2 );

			if( resolventRootCount <= 0 )
			{
				return 0;
			}

			const std::array<double, 3> resolventRoots{ z0, z1, z2 };
			double z = z0;
			double bestMargin = -std::numeric_limits<double>::infinity();

			for( int i = 0; i < resolventRootCount; i++ )
			{
				const double candidate = resolventRoots[i];
				const double uSquared = candidate * candidate - r;
				const double vSquared = 2.0 * candidate - p;
				const double margin = _min( uSquared, vSquared );

				if( margin > bestMargin )
				{
					bestMargin = margin;
					z = candidate;
				}
			}

			double uSquared = z * z - r;
			double vSquared = 2.0 * z - p;

			if( IsZero( uSquared ) )
			{
				uSquared = 0.0;
			}
			if( IsZero( vSquared ) )
			{
				vSquared = 0.0;
			}
			if( uSquared < 0.0 || vSquared < 0.0 )
			{
				return 0;
			}

			const double u = std::sqrt( uSquared );
			const double v = std::sqrt( vSquared );
			const double signedV = q < 0.0 ? -v : v;
			double quadraticRoot0;
			double quadraticRoot1;
			int count = SolveQuadratic( 1.0, signedV, z - u,
				quadraticRoot0, quadraticRoot1 );

			if( count > 0 )
			{
				roots[rootCount++] = quadraticRoot0;
			}
			if( count > 1 )
			{
				roots[rootCount++] = quadraticRoot1;
			}

			count = SolveQuadratic( 1.0, -signedV, z + u,
				quadraticRoot0, quadraticRoot1 );
			if( count > 0 )
			{
				roots[rootCount++] = quadraticRoot0;
			}
			if( count > 1 )
			{
				roots[rootCount++] = quadraticRoot1;
			}
		}

		const double substitution = A / 4.0;
		for( int i = 0; i < rootCount; i++ )
		{
			roots[i] -= substitution;
		}

		std::array<double, 4> uniqueRoots{};
		int uniqueCount = 0;

		for( int i = 0; i < rootCount; i++ )
		{
			if( !IsFinite( roots[i] ) )
			{
				continue;
			}

			bool duplicate = false;
			for( int j = 0; j < uniqueCount; j++ )
			{
				const double scale = _max( 1.0, std::abs( uniqueRoots[j] ) );
				if( std::abs( roots[i] - uniqueRoots[j] ) <= EPSILON * scale )
				{
					duplicate = true;
					break;
				}
			}

			if( !duplicate && uniqueCount < 4 )
			{
				uniqueRoots[uniqueCount++] = roots[i];
			}
		}

		std::sort( uniqueRoots.begin(), uniqueRoots.begin() + uniqueCount );
		if( uniqueCount > 0 )
		{
			a_s0 = uniqueRoots[0];
		}
		if( uniqueCount > 1 )
		{
			a_s1 = uniqueRoots[1];
		}
		if( uniqueCount > 2 )
		{
			a_s2 = uniqueRoots[2];
		}
		if( uniqueCount > 3 )
		{
			a_s3 = uniqueRoots[3];
		}
		return uniqueCount;
	}

	inline bool BuildResult( const Vector3& a_origin,
		const Vector3& a_targetPosition, const Vector3& a_targetVelocity,
		double a_projectileSpeed, double a_gravity, double a_time,
		Result& a_result )
	{
		if( !IsFinite( a_time ) || a_time <= ROOT_EPSILON )
		{
			return false;
		}

		const Vector3 displacement = a_targetPosition - a_origin;
		const Vector3 launchVelocity{
			displacement.x / a_time + a_targetVelocity.x,
			displacement.y / a_time + a_targetVelocity.y +
			0.5 * a_gravity * a_time,
			displacement.z / a_time + a_targetVelocity.z
		};

		if( !IsFinite( launchVelocity ) )
		{
			return false;
		}

		const double calculatedSpeed = Length( launchVelocity );
		const double speedTolerance =
			_max( 0.01, a_projectileSpeed * 0.001 );

		if( std::abs( calculatedSpeed - a_projectileSpeed ) >
			speedTolerance )
		{
			return false;
		}

		const double horizontalSpeed =
			std::hypot( launchVelocity.x, launchVelocity.z );
		if( horizontalSpeed <= EPSILON )
		{
			return false;
		}

		a_result.launchVelocity = launchVelocity;
		a_result.interceptPosition =
			a_targetPosition + a_targetVelocity * a_time;
		a_result.pitch = std::atan2( launchVelocity.y, horizontalSpeed );
		a_result.yaw = std::atan2( launchVelocity.x, launchVelocity.z );
		a_result.flightTime = a_time;

		return IsFinite( a_result.launchVelocity ) &&
			IsFinite( a_result.interceptPosition ) &&
			IsFinite( a_result.pitch ) && IsFinite( a_result.yaw );
	}

	inline bool SolveLinear( const Vector3& a_origin,
		const Vector3& a_targetPosition, const Vector3& a_targetVelocity,
		double a_projectileSpeed, Result& a_result )
	{
		const Vector3 displacement = a_targetPosition - a_origin;
		const double c0 = Dot( a_targetVelocity, a_targetVelocity ) -
			a_projectileSpeed * a_projectileSpeed;
		const double c1 = 2.0 * Dot( displacement, a_targetVelocity );
		const double c2 = Dot( displacement, displacement );

		double roots[2]{
			std::numeric_limits<double>::quiet_NaN(),
			std::numeric_limits<double>::quiet_NaN()
		};

		const int rootCount =
			SolveQuadratic( c0, c1, c2, roots[0], roots[1] );

		if( rootCount <= 0 )
		{
			return false;
		}

		double selectedTime = std::numeric_limits<double>::infinity();

		for( int i = 0; i < rootCount; i++ )
		{
			const double time = roots[i];

			if( IsFinite( time ) && time > ROOT_EPSILON &&
				time < selectedTime )
			{
				selectedTime = time;
			}
		}

		if( !IsFinite( selectedTime ) )
		{
			return false;
		}

		return BuildResult( a_origin, a_targetPosition, a_targetVelocity,
			a_projectileSpeed, 0.0, selectedTime, a_result );
	}

	inline bool Solve( const Vector3& a_origin,
		const Vector3& a_targetPosition, const Vector3& a_targetVelocity,
		double a_projectileSpeed, double a_gravity, Result& a_result,
		bool a_highArc = false )
	{
		if( !IsFinite( a_origin ) || !IsFinite( a_targetPosition ) ||
			!IsFinite( a_targetVelocity ) || !IsFinite( a_projectileSpeed ) ||
			!IsFinite( a_gravity ) || a_projectileSpeed <= EPSILON )
		{
			return false;
		}

		if( IsZero( a_gravity ) )
		{
			return SolveLinear( a_origin, a_targetPosition,
				a_targetVelocity, a_projectileSpeed, a_result );
		}

		if( a_gravity < 0.0 )
		{
			return false;
		}

		const Vector3 displacement = a_targetPosition - a_origin;
		const double H = displacement.x;
		const double K = displacement.y;
		const double J = displacement.z;
		const double P = a_targetVelocity.x;
		const double Q = a_targetVelocity.y;
		const double R = a_targetVelocity.z;
		const double S = a_projectileSpeed;
		const double L = -0.5 * a_gravity;
		const double c0 = L * L;
		const double c1 = -2.0 * Q * L;
		const double c2 =
			Q * Q - 2.0 * K * L - S * S + P * P + R * R;
		const double c3 = 2.0 * K * Q + 2.0 * H * P + 2.0 * J * R;
		const double c4 = K * K + H * H + J * J;
		const double nan = std::numeric_limits<double>::quiet_NaN();
		std::array<double, 4> roots{ nan, nan, nan, nan };

		const int rootCount = SolveQuartic( c0, c1, c2, c3, c4,
			roots[0], roots[1], roots[2], roots[3] );

		if( rootCount <= 0 )
		{
			return false;
		}

		bool found = false;
		double selectedTime = 0.0;
		Result selectedResult{};

		for( int i = 0; i < rootCount && i < 4; i++ )
		{
			const double time = roots[i];

			if( !IsFinite( time ) || time <= ROOT_EPSILON )
			{
				continue;
			}

			Result candidate{};

			if( !BuildResult( a_origin, a_targetPosition, a_targetVelocity,
				a_projectileSpeed, a_gravity, time, candidate ) )
			{
				continue;
			}

			const bool betterSolution = !found ||
				(!a_highArc && time < selectedTime) ||
				(a_highArc && time > selectedTime);

			if( betterSolution )
			{
				found = true;
				selectedTime = time;
				selectedResult = candidate;
			}
		}

		if( !found )
		{
			return false;
		}

		a_result = selectedResult;
		return true;
	}
}
