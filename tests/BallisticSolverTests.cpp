// Standalone tests for the ballistic solver used to aim projectiles (no game or CommonLib needed).
//
// Each case solves an intercept, then integrates the projectile and the moving target forward in
// time and checks that they meet. A second group converts the solution to Skyrim's angle
// convention exactly like SolveBallisticIntercept() and SetProjectileAngle() in src/Utils.h and
// checks the resulting launch direction, which is where sign mistakes would show up.

#include "BallisticSolver.h"

#include <cmath>
#include <cstdio>
#include <numbers>
#include <random>

using BallisticSolver::Vector3;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK( cond, ... )                         \
	do {                                           \
		++g_checks;                                \
		if( !( cond ) ) {                          \
			++g_failures;                          \
			std::printf( "FAIL %s:%d: ", __FILE__, __LINE__ ); \
			std::printf( __VA_ARGS__ );            \
			std::printf( "\n" );                   \
		}                                          \
	} while( 0 )

// Y-up solver space: gravity pulls along -Y
static Vector3 ProjectilePosition( const Vector3& a_origin, const Vector3& a_velocity, double a_gravity, double a_time )
{
	return {
		a_origin.x + a_velocity.x * a_time,
		a_origin.y + a_velocity.y * a_time - 0.5 * a_gravity * a_time * a_time,
		a_origin.z + a_velocity.z * a_time
	};
}

static double Distance( const Vector3& a, const Vector3& b )
{
	return BallisticSolver::Length( a - b );
}

// Closest approach between projectile and target over the flight, sampled finely
static double ClosestApproach( const Vector3& a_origin, const Vector3& a_launchVelocity, double a_gravity,
	const Vector3& a_target, const Vector3& a_targetVelocity, double a_maxTime )
{
	double best = 1e300;
	const int steps = 20000;
	for( int i = 0; i <= steps; ++i )
	{
		double t = a_maxTime * i / steps;
		Vector3 p = ProjectilePosition( a_origin, a_launchVelocity, a_gravity, t );
		Vector3 q = a_target + a_targetVelocity * t;
		best = std::min( best, Distance( p, q ) );
	}
	return best;
}

static void TestHitsTarget( const char* a_name, Vector3 a_origin, Vector3 a_target, Vector3 a_targetVelocity,
	double a_speed, double a_gravity )
{
	BallisticSolver::Result result;
	bool solved = BallisticSolver::Solve( a_origin, a_target, a_targetVelocity, a_speed, a_gravity, result, false );
	CHECK( solved, "%s: no solution", a_name );
	if( !solved )
		return;

	CHECK( std::abs( BallisticSolver::Length( result.launchVelocity ) - a_speed ) < a_speed * 0.002,
		"%s: launch speed %.3f != %.3f", a_name, BallisticSolver::Length( result.launchVelocity ), a_speed );

	// Exact meeting point at the solved flight time
	Vector3 p = ProjectilePosition( a_origin, result.launchVelocity, a_gravity, result.flightTime );
	Vector3 q = a_target + a_targetVelocity * result.flightTime;
	double miss = Distance( p, q );
	CHECK( miss < 1.0, "%s: misses by %.3f units at t=%.4f", a_name, miss, result.flightTime );

	// And the integrated flight really passes through the target
	double approach = ClosestApproach( a_origin, result.launchVelocity, a_gravity, a_target, a_targetVelocity, result.flightTime * 1.5 );
	CHECK( approach < 2.0, "%s: closest approach %.3f units", a_name, approach );

	std::printf( "ok   %-48s t=%.3fs miss=%.4f\n", a_name, result.flightTime, miss );
}

// Skyrim is Z-up. SolveBallisticIntercept() swaps Y/Z into the solver, then returns
// angleX = -pitch (negative is up) and angleZ = yaw. SetProjectileAngle() turns those into the
// direction (sinZ*cosX, cosZ*cosX, -sinX). This mirrors both to check the round trip.
static void TestSkyrimAngleConvention( const char* a_name, Vector3 a_originZUp, Vector3 a_targetZUp, Vector3 a_velZUp,
	double a_speed, double a_gravity )
{
	Vector3 origin{ a_originZUp.x, a_originZUp.z, a_originZUp.y };
	Vector3 target{ a_targetZUp.x, a_targetZUp.z, a_targetZUp.y };
	Vector3 vel{ a_velZUp.x, a_velZUp.z, a_velZUp.y };

	BallisticSolver::Result result;
	bool solved = BallisticSolver::Solve( origin, target, vel, a_speed, a_gravity, result, false );
	CHECK( solved, "%s: no solution", a_name );
	if( !solved )
		return;

	double angleX = -result.pitch;
	double angleZ = result.yaw;
	Vector3 dirZUp{
		std::sin( angleZ ) * std::cos( angleX ),
		std::cos( angleZ ) * std::cos( angleX ),
		-std::sin( angleX )
	};

	// Expected direction: the solver's launch velocity mapped back to Z-up
	Vector3 v = result.launchVelocity;
	Vector3 expected{ v.x, v.z, v.y };
	double len = BallisticSolver::Length( expected );
	expected = expected * ( 1.0 / len );

	double err = Distance( dirZUp, expected );
	CHECK( err < 1e-6, "%s: direction mismatch %.3g (dir %.4f %.4f %.4f vs %.4f %.4f %.4f)", a_name, err,
		dirZUp.x, dirZUp.y, dirZUp.z, expected.x, expected.y, expected.z );

	// Simulate in Z-up with gravity along -Z using the Skyrim direction
	Vector3 launch = dirZUp * a_speed;
	Vector3 p{
		a_originZUp.x + launch.x * result.flightTime,
		a_originZUp.y + launch.y * result.flightTime,
		a_originZUp.z + launch.z * result.flightTime - 0.5 * a_gravity * result.flightTime * result.flightTime
	};
	Vector3 q = a_targetZUp + a_velZUp * result.flightTime;
	double miss = Distance( p, q );
	CHECK( miss < 1.0, "%s: Skyrim-space miss %.3f", a_name, miss );

	std::printf( "ok   %-48s angleX=%+.4f angleZ=%+.4f miss=%.4f\n", a_name, angleX, angleZ, miss );
}

int main()
{
	// Typical Skyrim magnitudes: arrow speed ~ 3000-6000 units/s, gravity ~ 0.2-1 * 686 units/s^2,
	// distances up to ~ 7000 units (100 m)
	const double g = 686.0;

	std::printf( "-- Static targets --\n" );
	TestHitsTarget( "level shot 20 m", { 0, 100, 0 }, { 0, 100, 1400 }, { 0, 0, 0 }, 4500, g * 0.35 );
	TestHitsTarget( "uphill 40 m", { 0, 100, 0 }, { 300, 800, 2700 }, { 0, 0, 0 }, 4500, g * 0.35 );
	TestHitsTarget( "downhill 60 m", { 0, 1500, 0 }, { -900, 100, 4000 }, { 0, 0, 0 }, 4500, g * 0.35 );
	TestHitsTarget( "long range 100 m", { 0, 100, 0 }, { 2000, 100, 6700 }, { 0, 0, 0 }, 4000, g * 0.5 );
	TestHitsTarget( "no gravity (spell)", { 0, 100, 0 }, { 500, 300, 3000 }, { 0, 0, 0 }, 2500, 0.0 );

	std::printf( "-- Moving targets --\n" );
	TestHitsTarget( "strafing runner 30 m", { 0, 100, 0 }, { 0, 100, 2100 }, { 450, 0, 0 }, 4500, g * 0.35 );
	TestHitsTarget( "charging enemy 40 m", { 0, 100, 0 }, { 200, 100, 2800 }, { 0, 0, -500 }, 4500, g * 0.35 );
	TestHitsTarget( "fleeing enemy 50 m", { 0, 100, 0 }, { -300, 100, 3500 }, { -100, 0, 400 }, 4500, g * 0.35 );
	TestHitsTarget( "horse rider 60 m", { 0, 200, 0 }, { 1000, 150, 4000 }, { -900, 0, 300 }, 5000, g * 0.35 );
	TestHitsTarget( "moving, no gravity", { 0, 100, 0 }, { 0, 100, 3000 }, { 600, 0, 0 }, 2500, 0.0 );

	std::printf( "-- Randomized (fixed seed) --\n" );
	std::mt19937 rng( 12345 );
	std::uniform_real_distribution<double> pos( -4000.0, 4000.0 );
	std::uniform_real_distribution<double> height( -600.0, 900.0 );
	std::uniform_real_distribution<double> speedTarget( -500.0, 500.0 );
	int randomSolved = 0;
	for( int i = 0; i < 300; ++i )
	{
		Vector3 origin{ 0, 100, 0 };
		Vector3 target{ pos( rng ), 100 + height( rng ), pos( rng ) };
		if( Distance( origin, target ) < 300 )
			continue;
		Vector3 vel{ speedTarget( rng ), 0, speedTarget( rng ) };
		BallisticSolver::Result r;
		if( !BallisticSolver::Solve( origin, target, vel, 4500, g * 0.35, r, false ) )
			continue;
		++randomSolved;
		Vector3 p = ProjectilePosition( origin, r.launchVelocity, g * 0.35, r.flightTime );
		Vector3 q = target + vel * r.flightTime;
		CHECK( Distance( p, q ) < 1.0, "random case %d misses by %.3f", i, Distance( p, q ) );
	}
	CHECK( randomSolved > 250, "only %d of 300 random cases solved", randomSolved );
	std::printf( "ok   %d random scenarios solved and verified\n", randomSolved );

	std::printf( "-- Out of range must report no solution --\n" );
	{
		BallisticSolver::Result r;
		bool solved = BallisticSolver::Solve( { 0, 0, 0 }, { 0, 0, 200000 }, { 0, 0, 0 }, 1000, g, r, false );
		CHECK( !solved, "unreachable target reported a solution" );
		std::printf( "ok   unreachable target rejected\n" );
	}

	std::printf( "-- Skyrim angle convention round trip --\n" );
	TestSkyrimAngleConvention( "ahead, level", { 0, 0, 100 }, { 0, 2000, 100 }, { 0, 0, 0 }, 4500, g * 0.35 );
	TestSkyrimAngleConvention( "right and up", { 0, 0, 100 }, { 1500, 1500, 700 }, { 0, 0, 0 }, 4500, g * 0.35 );
	TestSkyrimAngleConvention( "behind-left, below", { 0, 0, 900 }, { -1200, -2500, 100 }, { 0, 0, 0 }, 4500, g * 0.35 );
	TestSkyrimAngleConvention( "moving target", { 0, 0, 100 }, { 800, 2500, 150 }, { -400, 100, 0 }, 4500, g * 0.35 );

	std::printf( "\n%d checks, %d failures\n", g_checks, g_failures );
	return g_failures == 0 ? 0 : 1;
}
