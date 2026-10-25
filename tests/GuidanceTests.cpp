// Simulation tests for GuidanceMath: the projectile is integrated at 60 fps with gravity while the
// target zigzags, and guidance runs once per frame with one frame of latency, as in the plugin
// (it updates after the player, using last frame's positions). The launch is deliberately aimed
// from a wrong origin to reproduce the launch point offset measured in game.

#include "GuidanceMath.h"

#include <cmath>
#include <cstdio>
#include <numbers>
#include <random>

using BallisticSolver::Vector3;

static int g_failures = 0;
static int g_checks = 0;

#define CHECK( cond, ... )                                     \
	do {                                                       \
		++g_checks;                                            \
		if( !( cond ) ) {                                      \
			++g_failures;                                      \
			std::printf( "FAIL %s:%d: ", __FILE__, __LINE__ ); \
			std::printf( __VA_ARGS__ );                        \
			std::printf( "\n" );                               \
		}                                                      \
	} while( 0 )

static constexpr double kUnitsPerMeter = 1.0 / 0.0142875;
static constexpr double kFrame = 1.0 / 60.0;
static constexpr double kDegToRad = std::numbers::pi / 180.0;

struct Target
{
	Vector3 position;
	Vector3 velocity;
	double zigzagPeriod = 0.0;  // flip horizontal direction every this many seconds (0 = never)
	double sinceFlip = 0.0;

	void Advance( double a_dt )
	{
		position = position + velocity * a_dt;
		if( zigzagPeriod > 0.0 )
		{
			sinceFlip += a_dt;
			if( sinceFlip >= zigzagPeriod )
			{
				sinceFlip = 0.0;
				// Sharp turn: swap the horizontal axes and flip one, like a fleeing animal cutting sideways
				velocity = { -velocity.z, velocity.y, velocity.x };
			}
		}
	}
};

static double SegmentPointDistance( const Vector3& a, const Vector3& b, const Vector3& p )
{
	Vector3 ab = b - a;
	double len2 = BallisticSolver::Dot( ab, ab );
	double t = len2 > 1e-12 ? std::clamp( BallisticSolver::Dot( p - a, ab ) / len2, 0.0, 1.0 ) : 0.0;
	return BallisticSolver::Length( a + ab * t - p );
}

struct FlightResult
{
	double closest = 1e300;
	double maxTurnPerFrame = 0.0;
	bool solvedAtLaunch = false;
};

// a_launchOffset: the solver thinks the arrow starts at origin + offset (the measured mismatch)
static FlightResult Fly( Vector3 a_origin, Target a_target, double a_speed, double a_gravity, Vector3 a_launchOffset,
	bool a_guided, double a_maxTurnRate )
{
	FlightResult out;

	// Release: solved from the wrong origin, fired from the real one
	BallisticSolver::Result launch;
	out.solvedAtLaunch = BallisticSolver::Solve( a_origin + a_launchOffset, a_target.position, a_target.velocity, a_speed, a_gravity, launch, false );
	Vector3 velocity = out.solvedAtLaunch ? launch.launchVelocity : GuidanceMath::Normalize( a_target.position - a_origin ) * a_speed;
	Vector3 position = a_origin;

	// One frame of latency: guidance sees the state from the end of the previous frame
	Vector3 seenPosition = position;
	Vector3 seenVelocity = velocity;
	Vector3 seenTarget = a_target.position;
	Vector3 seenTargetVelocity = a_target.velocity;
	bool guiding = a_guided;

	for( int frame = 0; frame < 60 * 6; ++frame )
	{
		if( guiding )
		{
			auto step = GuidanceMath::Step( seenPosition, seenVelocity, seenTarget, seenTargetVelocity, a_gravity, a_maxTurnRate * kFrame );
			if( step.passedTarget )
			{
				guiding = false;
			}
			else
			{
				// Apply the turn to the live velocity, keeping its current speed
				Vector3 before = GuidanceMath::Normalize( velocity );
				Vector3 after = GuidanceMath::Normalize( step.velocity );
				double turned = std::acos( std::clamp( BallisticSolver::Dot( before, after ), -1.0, 1.0 ) );
				out.maxTurnPerFrame = std::max( out.maxTurnPerFrame, turned );
				velocity = after * BallisticSolver::Length( velocity );
			}
		}

		Vector3 previous = position;
		velocity.y -= a_gravity * kFrame;
		position = position + velocity * kFrame;
		a_target.Advance( kFrame );

		out.closest = std::min( out.closest, SegmentPointDistance( previous, position, a_target.position ) );

		seenPosition = position;
		seenVelocity = velocity;
		seenTarget = a_target.position;
		seenTargetVelocity = a_target.velocity;

		if( position.y < -2000.0 )
			break;
	}

	return out;
}

int main()
{
	const double g = 686.0;
	const double fullSpeed = 3465.0;
	const double fullGravity = 240.0;   // logged solver gravity at full draw
	const double hitRadius = 12.0;      // well inside a rabbit's torso (hit radius logged ~18-22 units)
	const double turnRate = 3600.0 * kDegToRad;
	const Vector3 offset{ 30.0, 55.0, -20.0 };  // ~1 m launch point mismatch, as measured

	std::printf( "-- Unguided vs guided (same launch error) --\n" );
	{
		Target rabbit{ { 0, 0, 40 * kUnitsPerMeter }, { 4.5 * kUnitsPerMeter, 0, 0 }, 0.35 };
		auto unguided = Fly( { 0, 60, 0 }, rabbit, fullSpeed, fullGravity, offset, false, turnRate );
		auto guided = Fly( { 0, 60, 0 }, rabbit, fullSpeed, fullGravity, offset, true, turnRate );
		std::printf( "info zigzag rabbit 40 m: unguided closest %.1f u, guided closest %.1f u\n", unguided.closest, guided.closest );
		CHECK( unguided.closest > hitRadius, "unguided case should miss to prove the scenario is hard (%.1f)", unguided.closest );
		CHECK( guided.closest <= hitRadius, "guided shot missed by %.1f units", guided.closest );
	}

	std::printf( "-- Fixed scenarios, guided --\n" );
	struct Scenario
	{
		const char* name;
		double distance;
		double targetSpeed;  // m/s
		double zigzag;
		double speed;
		double gravity;
		double height;       // target height relative to shooter, meters
	};
	const Scenario scenarios[] = {
		{ "static 5 m", 5, 0, 0, fullSpeed, fullGravity, 0 },
		{ "static 40 m", 40, 0, 0, fullSpeed, fullGravity, 0 },
		{ "static 100 m", 100, 0, 0, fullSpeed, fullGravity, 0 },
		{ "uphill 50 m", 50, 0, 0, fullSpeed, fullGravity, 15 },
		{ "downhill 50 m", 50, 0, 0, fullSpeed, fullGravity, -15 },
		{ "zigzag rabbit 3 m", 3, 4.5, 0.3, fullSpeed, fullGravity, -1 },
		{ "zigzag rabbit 10 m", 10, 4.5, 0.3, fullSpeed, fullGravity, -1 },
		{ "zigzag rabbit 60 m", 60, 5.5, 0.35, fullSpeed, fullGravity, -1 },
		{ "zigzag fox 30 m", 30, 6.0, 0.4, fullSpeed, fullGravity, -1 },
		{ "running bandit 70 m", 70, 6.0, 0.0, fullSpeed, fullGravity, 0 },
		{ "slow arrow, rabbit 8 m", 8, 3.0, 0.4, 693.0, 865.0, 0 },
		{ "half draw, fox 25 m", 25, 4.0, 0.5, 1733.0, 640.0, 0 },
	};
	for( const auto& s : scenarios )
	{
		Target target{ { 0, s.height * kUnitsPerMeter, s.distance * kUnitsPerMeter }, { s.targetSpeed * kUnitsPerMeter, 0, 0 }, s.zigzag };
		auto r = Fly( { 0, 60, 0 }, target, s.speed, s.gravity, offset, true, turnRate );
		CHECK( r.closest <= hitRadius, "%s: missed by %.1f units", s.name, r.closest );
		std::printf( "ok   %-26s closest %.2f u\n", s.name, r.closest );
	}

	std::printf( "-- Turn rate limit is honoured --\n" );
	{
		double limit = 90.0 * kDegToRad;
		Target target{ { 0, 0, 20 * kUnitsPerMeter }, { 5.0 * kUnitsPerMeter, 0, 0 }, 0.3 };
		auto r = Fly( { 0, 60, 0 }, target, fullSpeed, fullGravity, offset, true, limit );
		CHECK( r.maxTurnPerFrame <= limit * kFrame + 1e-9, "turned %.4f rad in one frame, limit %.4f", r.maxTurnPerFrame, limit * kFrame );
		std::printf( "ok   max turn per frame %.4f rad (limit %.4f)\n", r.maxTurnPerFrame, limit * kFrame );
	}

	std::printf( "-- Never turns back once past the target --\n" );
	{
		auto step = GuidanceMath::Step( { 0, 0, 100 }, { 0, 0, 3000 }, { 0, 0, 50 }, { 0, 0, 0 }, g * 0.35, 1.0 );
		CHECK( step.passedTarget, "should report passed target" );
		CHECK( step.velocity.z == 3000.0, "velocity must be unchanged when past the target" );
		std::printf( "ok   passed target detected, velocity untouched\n" );
	}

	std::printf( "-- Randomized: 1000 shots, zigzagging targets, launch error up to 1.5 m --\n" );
	{
		std::mt19937 rng( 2026 );
		std::uniform_real_distribution<double> distance( 3.0, 80.0 );
		std::uniform_real_distribution<double> targetSpeed( 0.0, 6.0 );
		std::uniform_real_distribution<double> zigzag( 0.25, 0.8 );
		std::uniform_real_distribution<double> height( -10.0, 10.0 );
		std::uniform_real_distribution<double> err( -60.0, 60.0 );
		std::uniform_real_distribution<double> angle( 0.0, 2.0 * std::numbers::pi );
		int hits = 0;
		double worst = 0.0;
		const int shots = 1000;
		for( int i = 0; i < shots; ++i )
		{
			double d = distance( rng ) * kUnitsPerMeter;
			double a = angle( rng );
			double v = targetSpeed( rng ) * kUnitsPerMeter;
			double heading = angle( rng );
			Target target{ { d * std::sin( a ), height( rng ) * kUnitsPerMeter, d * std::cos( a ) },
				{ v * std::sin( heading ), 0, v * std::cos( heading ) }, zigzag( rng ) };
			Vector3 launchError{ err( rng ), err( rng ), err( rng ) };
			auto r = Fly( { 0, 60, 0 }, target, fullSpeed, fullGravity, launchError, true, turnRate );
			if( r.closest <= hitRadius )
				++hits;
			worst = std::max( worst, r.closest );
		}
		CHECK( hits == shots, "%d of %d guided shots hit (worst miss %.1f units)", hits, shots, worst );
		std::printf( "ok   %d / %d hits, worst closest approach %.2f u\n", hits, shots, worst );
	}

	std::printf( "\n%d checks, %d failures\n", g_checks, g_failures );
	return g_failures == 0 ? 0 : 1;
}
