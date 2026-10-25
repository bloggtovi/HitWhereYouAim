#pragma once
#pragma warning(push)
#pragma warning(disable: 4505)

#include "BallisticSolver.h"

struct ProjectedBounds
{
	RE::NiPoint2 min{ FLT_MAX, FLT_MAX };
	RE::NiPoint2 max{ FLT_MAX, FLT_MAX };
	bool valid = false;
};

struct TargetCandidateInfo
{
	RE::ObjectRefHandle handle;
	float verticalRadius = 0.0f;
	float horizontalRadius = 0.0f;
	float score = FLT_MAX;
	bool isHostile = false;
	bool validProjection = false;
	bool insideSelectionWindow = false;
	bool passedFriendlySelection = false;
	bool hasLineOfSight = false;
	bool selected = false;
};

struct CandidateSelectionInfo
{
	std::vector<TargetCandidateInfo> candidates;
	float xLimit = 0.25f;
	float yLimitUp = 0.25f;
	float yLimitDown = 0.25f;

	void Reset( float a_xLimit, float a_yLimitUp, float a_yLimitDown )
	{
		candidates.clear();
		xLimit = a_xLimit;
		yLimitUp = a_yLimitUp;
		yLimitDown = a_yLimitDown;
	}
};

static float havokScaling = 0.0142875f;

static bool SolveBallisticIntercept( const RE::NiPoint3& a_origin, const RE::NiPoint3& a_target, const RE::NiPoint3* a_targetVelocity,
	float a_projectileSpeed, float a_projectileGravity, RE::NiPoint3* out_intercept, float* out_angleX, float* out_angleZ )
{
	RE::NiPoint3 targetVelocity{ a_targetVelocity ? *a_targetVelocity : RE::NiPoint3::Zero() };

	// Convert to Y-up solver coord (Skyrim uses Z-up)
	BallisticSolver::Vector3 solverOrigin			{ a_origin.x,			a_origin.z,			a_origin.y };
	BallisticSolver::Vector3 solverTarget			{ a_target.x,			a_target.z,			a_target.y };
	BallisticSolver::Vector3 solverTargetVelocity	{ targetVelocity.x,		targetVelocity.z,	targetVelocity.y };

	BallisticSolver::Result result;
	if( BallisticSolver::Solve(
		solverOrigin,
		solverTarget, 
		solverTargetVelocity,
		a_projectileSpeed,
		a_projectileGravity,
		result, false) )
	{
		// Convert back to Skyrim coord
		if( out_intercept )
		{
			out_intercept->x = (float)result.interceptPosition.x;
			out_intercept->y = (float)result.interceptPosition.z;
			out_intercept->z = (float)result.interceptPosition.y;
		}

		if( out_angleX ) *out_angleX = (float)-result.pitch; // Skyrim negative pitch is upward
		if( out_angleZ ) *out_angleZ = (float)result.yaw;

		return true;
	}

	return false;
}

static std::vector<std::string> split( const char* a_input, const char* a_regex ) 
{
	// passing -1 as the submatch index parameter performs splitting
	std::string in(a_input);
	std::regex re(a_regex);
	std::sregex_token_iterator
		_first{in.begin(), in.end(), re, -1},
		_last;
	return {_first, _last};
}

static inline uint32_t GetCurrentGameRuntimeMS()
{
	return  RE::BSTimer::GetSingleton()->smoothedRunTimeMS;
}

static RE::NiPoint3 ExtendVectorFromOrigin( const RE::NiPoint3& a_origin, const RE::NiPoint3& a_target, float a_extension )
{
	RE::NiPoint3 delta = a_target - a_origin;
	float length = delta.Length();

	if( length == 0 )
		return a_target;

	RE::NiPoint3 direction = delta / length;

	return a_target + direction * a_extension;
}

static RE::NiPoint3 ClampVectorFromOrigin( const RE::NiPoint3& a_origin, const RE::NiPoint3& a_target, float a_maxLength )
{
	RE::NiPoint3 delta = a_target - a_origin;
	float length = delta.Length();

	if( length == 0 || length < a_maxLength )
		return a_target;

	RE::NiPoint3 direction = delta / length;
	return a_origin + direction * a_maxLength;
}

static float CalculateProjectileFlightTimeToTarget( const RE::NiPoint3& a_origin, const RE::NiPoint3& a_target, float a_speed )
{
	if( a_speed > 0 )
	{
		auto targetDelta = a_target - a_origin;
		auto targetDist = targetDelta.Length();
		return targetDist / a_speed;
	}

	return INFINITY;
}

static bool CalculateProjectileAngles( const RE::NiPoint3& a_origin, const RE::NiPoint3& a_target, float& a_angleX, float& a_angleZ )
{
	RE::NiPoint3 delta = a_target - a_origin;
	float horizontalSquared = delta.x * delta.x + delta.y * delta.y;
	float lengthSquared = horizontalSquared + delta.z * delta.z;

	if( lengthSquared <= 0.000001f )
		return false;

	float horizontal = sqrt( horizontalSquared );
	a_angleX =	-atan2( delta.z, horizontal );

	if( horizontalSquared > 0.000001f )
		a_angleZ = atan2( delta.x, delta.y );

	return true;
}

static bool SetProjectileAngle( RE::Projectile* a_projectile, float a_angleX, float a_angleZ )
{
	if( !a_projectile )
		return false;

	auto& runtimeData = a_projectile->GetProjectileRuntimeData();
	float speed = runtimeData.linearVelocity.Length();

	if( speed <= 0.000001f )
		return false;

	float sinX = sin(a_angleX);
	float cosX = cos(a_angleX);
	float sinZ = sin(a_angleZ);
	float cosZ = cos(a_angleZ);

	const RE::NiPoint3 direction{
		sinZ * cosX,
		cosZ * cosX,
		-sinX
	};

	a_projectile->data.angle.x = a_angleX;
	a_projectile->data.angle.z = a_angleZ;

	runtimeData.linearVelocity = direction * speed;

	if( auto* projectile3D = a_projectile->Get3D() )
	{
		auto& rotation = projectile3D->local.rotate;

		rotation.entry[0][0] = cosZ;
		rotation.entry[0][1] = direction.x;
		rotation.entry[0][2] = sinZ * sinX;

		rotation.entry[1][0] = -sinZ;
		rotation.entry[1][1] = direction.y;
		rotation.entry[1][2] = cosZ * sinX;

		rotation.entry[2][0] = 0.0F;
		rotation.entry[2][1] = direction.z;
		rotation.entry[2][2] = cosX;
	}

	return true;
}

static RE::NiPoint3* CalculatePredictedProjectileAimPoint( RE::NiPoint3* a_outAimPoint, RE::NiPoint3* a_origin, RE::NiPoint3* a_targetPosition, RE::NiPoint3* a_targetVelocity,
	float a_projectileSpeed, float a_gravity, bool a_highArc )
{
	// Check reachablility because the hooked function doesn't do it
	if( a_projectileSpeed <= 0.0F || a_gravity <= 0.0F )
	{
		return nullptr;
	}

	float x_delta = a_targetPosition->x - a_origin->x;
	float y_delta = a_targetPosition->y - a_origin->y;
	float z_delta = a_targetPosition->z - a_origin->z;

	float horizontalDistanceSquared = x_delta * x_delta + y_delta * y_delta;

	// Hooked function doesn't like the distance larger than 8192 game unit
	if( horizontalDistanceSquared + z_delta * z_delta >= 8192.0f * 8192.0f )
		return nullptr;

	float speedSquared = a_projectileSpeed * a_projectileSpeed;
	float discriminant =
		speedSquared * speedSquared -
		a_gravity *
		(
			a_gravity * horizontalDistanceSquared +
			2.0F * z_delta * speedSquared
			);

	// Return if cannot reach the target
	if( discriminant <= 0 )
		return nullptr;

	using func_t = decltype(&CalculatePredictedProjectileAimPoint);
	static REL::Relocation<func_t> func{ RELOCATION_ID(46025, 47289) };
	return func( a_outAimPoint, a_origin, a_targetPosition, a_targetVelocity, a_projectileSpeed, a_gravity, a_highArc );
}

// World gravity acceleration used by Skyrim projectile ballistic prediction.
// Native CombatProjectileAimController multiplies Projectile::GetGravity()
// by this value before solving the trajectory.

static float GetWorldGravity()
{
	static REL::Relocation<const float*> worldGravity{ RELOCATION_ID(518746, 405286) };
	return *worldGravity;
}

// Helper to get object reference from an AV object returned from raycast
static RE::TESObjectREFR* GetObjectReferenceFromAVObject( RE::NiAVObject* a_object )
{
	if( a_object->userData )
		return a_object->userData;

	if( a_object->parent )
		return GetObjectReferenceFromAVObject( a_object->parent );

	return nullptr;
}

static RE::TESObjectREFR* GetRaycastTarget( RE::NiPoint3& a_origin, RE::NiPoint3& a_target, uint32_t a_collisionGroup,
	RE::COL_LAYER a_collisionLayer, RE::NiPoint3* out_hitLocation = nullptr, RE::bhkPickData* out_pickData = nullptr )
{
	RE::bhkPickData pickData;
	RE::CFilter filter{0};
	memset( &pickData, 0, sizeof(pickData) );
	filter.SetCollisionLayer( a_collisionLayer );
	filter.SetSystemGroup( a_collisionGroup );

	pickData.rayInput.from = a_origin * havokScaling;
	pickData.rayInput.to = a_target * havokScaling;
	pickData.rayInput.filterInfo = filter;

	auto target = RE::TES::GetSingleton()->Pick( pickData );
	if( out_pickData )
		*out_pickData = pickData;

	if( out_hitLocation )
	{
		RE::hkVector4 hitLocation = pickData.rayInput.from + (pickData.rayInput.to - pickData.rayInput.from) * pickData.rayOutput.hitFraction;
		hitLocation = hitLocation * ( 1.0f / havokScaling );

		*out_hitLocation = (RE::NiPoint3&)hitLocation.quad.m128_f32;
	}

	if( target )
		return GetObjectReferenceFromAVObject( target );

	return nullptr;
}

static RE::TESObjectREFR* GetScreenRaycastTarget( uint32_t a_x, uint32_t a_y, uint32_t a_collisionGroup,
	RE::COL_LAYER a_collisionLayer, RE::NiPoint3* out_hitLocation = nullptr, RE::bhkPickData* out_pickData = nullptr, float a_maxDistance = 10000.0f )
{
	auto camera = RE::Main::WorldRootCamera();
	auto renderer = RE::BSGraphics::Renderer::GetSingleton();
	if( !camera || !renderer )
		return nullptr;

	auto screenSize = renderer->GetScreenSize();

	RE::NiPoint3 origin, direction;
	camera->WindowPointToRay( a_x, a_y, origin, direction, (float)screenSize.width, (float)screenSize.height );

	RE::NiPoint3 target = camera->world.translate + direction * a_maxDistance;
	return GetRaycastTarget( camera->world.translate, target, a_collisionGroup, a_collisionLayer, out_hitLocation, out_pickData );
}

static void UpdateAutoAimTarget( RE::PlayerCharacter* a_player, float a_maxDistance, bool a_alternateMode )
{
	using func_t = decltype(&UpdateAutoAimTarget);
	static REL::Relocation<func_t> func{ RELOCATION_ID(40243, 41246) };
	func( a_player, a_maxDistance, a_alternateMode );
}

static int8_t RequestDetectionResult( RE::Actor* a_observer, RE::Actor* a_target, RE::DETECTION_PRIORITY a_priority )
{
	using func_t = decltype(&RequestDetectionResult);
	static REL::Relocation<func_t> func{ RELOCATION_ID(36745, 37761) };
	return func( a_observer, a_target, a_priority );
}

static bool HasLineOfSight( RE::TESObjectREFR* a_observer, RE::TESObjectREFR* a_target )
{
	static auto hasLOSCondition = RE::TESCondition();
	static RE::TESConditionItem hasLOS = RE::TESConditionItem();
	hasLOS.data.comparisonValue.f = 1.0f;
	hasLOS.data.functionData.function = RE::FUNCTION_DATA::FunctionID::kGetLineOfSight;
	hasLOSCondition.head = &hasLOS;

	return hasLOSCondition.IsTrue( a_observer, a_target );
}

static void ProjectBoundToScreen( const RE::NiCamera* a_camera, const RE::NiBound& a_bound, RE::NiPoint3& out_projectedMin, RE::NiPoint3& out_projectedMax, float a_nearThreshold )
{
	using func_t = decltype(&ProjectBoundToScreen);
	static REL::Relocation<func_t> func{ RELOCATION_ID(69265, 70632) };
	func( a_camera, a_bound, out_projectedMin, out_projectedMax, a_nearThreshold );
}

static ProjectedBounds ProjectReferenceBoundsToScreen( const RE::NiCamera* a_camera, const RE::TESObjectREFR* a_ref, float a_predictionTime, float a_nearThreshold )
{
	ProjectedBounds result;

	if( a_camera && a_ref )
	{
		auto ref3D = a_ref->Get3D2();
		if( ref3D )
		{
			RE::NiBound bound = ref3D->worldBound;
			RE::NiPoint3 moveDelta{ 0, 0, 0 };

			// Predict future position before projecting onto the screen
			RE::NiPoint3 velocity;
			a_ref->GetLinearVelocity( velocity );
			moveDelta = velocity * a_predictionTime;

			bound.center.x = a_ref->GetPositionX();
			bound.center.y = a_ref->GetPositionY();
			bound.center += moveDelta;

			float normalizedX;
			float normalizedY;
			float depth;

			if( !RE::NiCamera::WorldPtToScreenPt3(
				a_camera->GetRuntimeData().worldToCam,
				a_camera->GetRuntimeData2().port,
				bound.center,
				normalizedX,
				normalizedY,
				depth,
				1.0e-5f ) )
			{
				return result;
			}

			if( depth <= 0.0f || depth >= 1.0f )
				return result;

			RE::NiPoint3 projectedYMin, projectedYMax;
			ProjectBoundToScreen( a_camera, bound, projectedYMin, projectedYMax, a_nearThreshold );

			bound.radius = a_ref->GetBoundMax().x;
			RE::NiPoint3 projectedXMin, projectedXMax;
			ProjectBoundToScreen( a_camera, bound, projectedXMin, projectedXMax, a_nearThreshold );

			if( projectedXMin.x == -1.0f && projectedXMax.x == 1.0f )
				return result;

			if( projectedYMin.y == -1.0f && projectedYMax.y == 1.0f )
				return result;

			result.min.x = projectedXMin.x;
			result.min.y = projectedYMin.y;

			result.max.x = projectedXMax.x;
			result.max.y = projectedYMax.y;

			result.valid = true;
		}
	}

	return result;
}

struct ProjectedSweep
{
	std::vector<RE::NiPoint2> points;
	ProjectedBounds bounds;
	bool valid = false;
};

static float Cross2D( const RE::NiPoint2& a_origin, const RE::NiPoint2& a_point1, const RE::NiPoint2& a_point2 )
{
	return
		(a_point1.x - a_origin.x) * (a_point2.y - a_origin.y) -
		(a_point1.y - a_origin.y) * (a_point2.x - a_origin.x);
}

static ProjectedSweep GetProjectedSweep( const ProjectedBounds& a_currentBounds, const ProjectedBounds& a_predictedBounds )
{
	ProjectedSweep result;

	if( !a_currentBounds.valid || !a_predictedBounds.valid )
		return result;

	std::vector<RE::NiPoint2> points{
		{ a_currentBounds.min.x, a_currentBounds.min.y },
		{ a_currentBounds.min.x, a_currentBounds.max.y },
		{ a_currentBounds.max.x, a_currentBounds.min.y },
		{ a_currentBounds.max.x, a_currentBounds.max.y },
		{ a_predictedBounds.min.x, a_predictedBounds.min.y },
		{ a_predictedBounds.min.x, a_predictedBounds.max.y },
		{ a_predictedBounds.max.x, a_predictedBounds.min.y },
		{ a_predictedBounds.max.x, a_predictedBounds.max.y }
	};

	std::sort(
		points.begin(),
		points.end(),
		[]( const RE::NiPoint2& a_left, const RE::NiPoint2& a_right )
		{
			return a_left.x < a_right.x || a_left.x == a_right.x && a_left.y < a_right.y;
		} );

	points.erase(
		std::unique(
			points.begin(),
			points.end(),
			[]( const RE::NiPoint2& a_left, const RE::NiPoint2& a_right )
			{
				return a_left.x == a_right.x && a_left.y == a_right.y;
			} ),
		points.end() );

	if( points.size() < 3 )
		return result;

	result.points.resize( points.size() * 2 );
	size_t hullSize = 0;

	for( const auto& point : points )
	{
		while( hullSize >= 2 && Cross2D( result.points[hullSize - 2], result.points[hullSize - 1], point ) <= 0 )
			--hullSize;

		result.points[hullSize++] = point;
	}

	size_t lowerHullSize = hullSize;
	for( auto iter = points.rbegin() + 1; iter != points.rend(); ++iter )
	{
		while( hullSize > lowerHullSize && Cross2D( result.points[hullSize - 2], result.points[hullSize - 1], *iter ) <= 0 )
			--hullSize;

		result.points[hullSize++] = *iter;
	}

	if( hullSize > 1 )
		--hullSize;

	result.points.resize( hullSize );
	if( result.points.size() < 3 )
	{
		result.points.clear();
		return result;
	}

	result.bounds.min = result.points.front();
	result.bounds.max = result.points.front();

	for( const auto& point : result.points )
	{
		result.bounds.min.x = std::min( result.bounds.min.x, point.x );
		result.bounds.min.y = std::min( result.bounds.min.y, point.y );
		result.bounds.max.x = std::max( result.bounds.max.x, point.x );
		result.bounds.max.y = std::max( result.bounds.max.y, point.y );
	}

	result.bounds.valid = true;
	result.valid = true;
	return result;
}

static constexpr float friendlyGravityAimErrorMultiplier = 1.25f;

static ProjectedBounds ExtendProjectedBoundsUpwardForGravity(
	const RE::NiCamera* a_camera,
	const ProjectedBounds& a_bounds,
	const RE::NiPoint3& a_fireOrigin,
	const RE::NiPoint3& a_targetPosition,
	float a_projectileSpeed,
	float a_projectileGravity )
{
	ProjectedBounds result = a_bounds;

	if( !result.valid || !a_camera || a_projectileSpeed <= 0.0f || a_projectileGravity <= 0.0f )
		return result;

	RE::NiPoint3 delta = a_targetPosition - a_fireOrigin;
	float horizontalDistanceSquared = delta.x * delta.x + delta.y * delta.y;

	if( horizontalDistanceSquared <= 0.000001f )
		return result;

	float speedSquared = a_projectileSpeed * a_projectileSpeed;
	float discriminant =
		speedSquared * speedSquared -
		a_projectileGravity *
		(a_projectileGravity * horizontalDistanceSquared + 2.0f * delta.z * speedSquared);

	if( discriminant <= 0.0f )
		return result;

	float horizontalDistance = sqrt( horizontalDistanceSquared );
	float tangent =
		(speedSquared - sqrt( discriminant )) /
		(a_projectileGravity * horizontalDistance);
	RE::NiPoint3 compensatedPosition = a_targetPosition;
	compensatedPosition.z = a_fireOrigin.z + horizontalDistance * tangent;

	RE::NiBound targetPoint;
	targetPoint.center = a_targetPosition;
	targetPoint.radius = 0.0f;
	RE::NiPoint3 targetMin, targetMax;
	ProjectBoundToScreen( a_camera, targetPoint, targetMin, targetMax, 0.0f );

	RE::NiBound compensatedPoint;
	compensatedPoint.center = compensatedPosition;
	compensatedPoint.radius = 0.0f;
	RE::NiPoint3 compensatedMin, compensatedMax;
	ProjectBoundToScreen( a_camera, compensatedPoint, compensatedMin, compensatedMax, 0.0f );

	float upwardExtension = compensatedMin.y - targetMin.y;
	if( upwardExtension > 0.0f )
		result.max.y += upwardExtension * friendlyGravityAimErrorMultiplier;

	return result;
}

static ProjectedBounds GetHorizontallyInsetBounds( const ProjectedBounds& a_bounds, float a_inset )
{
	ProjectedBounds result = a_bounds;

	if( !result.valid )
		return result;

	float xInset = (result.max.x - result.min.x) * a_inset;
	result.min.x += xInset;
	result.max.x -= xInset;

	if( result.min.x > result.max.x )
		result.valid = false;

	return result;
}

static bool IsScreenCenterInsideProjectedSweep( const ProjectedSweep& a_sweep )
{
	if( !a_sweep.valid || a_sweep.points.size() < 3 )
		return false;

	RE::NiPoint2 center{ 0, 0 };
	bool hasNegative = false;
	bool hasPositive = false;

	for( size_t i = 0; i < a_sweep.points.size(); ++i )
	{
		float cross = Cross2D( a_sweep.points[i], a_sweep.points[(i + 1) % a_sweep.points.size()], center );
		hasNegative |= cross < 0;
		hasPositive |= cross > 0;

		if( hasNegative && hasPositive )
			return false;
	}

	return true;
}

static std::pair<RE::TESObjectREFR*,RE::NiPoint3> GetPotentialPlayerTarget(
	float a_distance,
	float a_projectileSpeed = 0.0f,
	float a_projectileGravity = 0.0f,
	const RE::NiPoint3* a_fireOrigin = nullptr,
	float a_xLimit = 0.25f,
	float a_yLimit = 0.25f,
	CandidateSelectionInfo* out_debugInfo = nullptr,
	bool a_fixedPitchMode = false )
{
	if( a_xLimit <= 0 ) a_xLimit = 0.0001f;
	if( a_yLimit <= 0 ) a_yLimit = 0.0001f;

	float yLimitUp = a_yLimit;
	float yLimitDown = a_fixedPitchMode ? a_yLimit * 2.0f : a_yLimit;

	if( out_debugInfo )
		out_debugInfo->Reset( a_xLimit, yLimitUp, yLimitDown );

	auto processLists = RE::ProcessLists::GetSingleton();
	auto player = RE::PlayerCharacter::GetSingleton();
	RE::NiCamera* camera = RE::Main::WorldRootCamera();

	if( !processLists || !player || !camera )
		return {};

	float distanceSquared = a_distance * a_distance;
	float bestScore = FLT_MAX;
	float bestDistanceSquared = FLT_MAX;

	RE::NiPoint3 origin = a_fireOrigin ? *a_fireOrigin : player->GetPosition();

	RE::Actor* targetCandidate = nullptr;
	RE::NiPoint3 targetMoveDelta{ 0, 0, 0 };
	bool bestContainsCrosshair = false;
	bool isPlayerInCombat = player->IsInCombat();
	for( auto highHandle : processLists->highActorHandles )
	{
		if( auto highActor = highHandle.get() )
		{
			if( highActor->IsDead() )
				continue;

			auto actor3D = highActor->Get3D2();
			if( !actor3D )
				continue;

			if( actor3D->flags.any( RE::NiAVObject::Flag::kNotVisible ) )
				continue;

			bool isHostile = highActor->IsHostileToActor( player );
			if( isPlayerInCombat && !isHostile )
				continue;

			auto commander = highActor->GetCommandingActor();
			if( commander && commander.get() == player )
				continue;

			RE::NiPoint3 delta = origin - highActor->GetPosition();
			float actorDistanceSquared = delta.SqrLength();
			if( actorDistanceSquared > distanceSquared )
				continue;

			if( highActor->IsPlayerTeammate() )
				continue;

			RE::NiBound bound = actor3D->worldBound;
			RE::NiPoint3 moveDelta{ 0, 0, 0 };
			float ttr = 0;

			// Predict future position before projecting onto the screen
			if( a_projectileSpeed > 0 )
			{
				RE::NiPoint3 velocity;
				highActor->GetLinearVelocity( velocity );

				// Simple two pass predictor
				ttr = CalculateProjectileFlightTimeToTarget( origin, bound.center,					a_projectileSpeed );
				ttr = CalculateProjectileFlightTimeToTarget( origin, bound.center + velocity * ttr, a_projectileSpeed );

				moveDelta = velocity * ttr;
			}

			auto currentBounds = ProjectReferenceBoundsToScreen( camera, highActor.get(), 0, 0 );
			auto projectedBounds = ttr > 0 ?
				ProjectReferenceBoundsToScreen( camera, highActor.get(), ttr, 0 ) :
				currentBounds;

			auto projectedSweep = GetProjectedSweep( currentBounds, projectedBounds );

			RE::NiPoint3 currentTargetPosition = bound.center;
			currentTargetPosition.x = highActor->GetPositionX();
			currentTargetPosition.y = highActor->GetPositionY();

			RE::NiPoint3 predictedTargetPosition = currentTargetPosition + moveDelta;
			auto friendlyCurrentBounds = GetHorizontallyInsetBounds( currentBounds, 0.1f );
			auto friendlyProjectedBounds = GetHorizontallyInsetBounds( projectedBounds, 0.1f );

			if( a_fixedPitchMode )
			{
				friendlyCurrentBounds = ExtendProjectedBoundsUpwardForGravity(
					camera, friendlyCurrentBounds, origin, currentTargetPosition, a_projectileSpeed, a_projectileGravity );

				friendlyProjectedBounds = ExtendProjectedBoundsUpwardForGravity(
					camera, friendlyProjectedBounds, origin, predictedTargetPosition, a_projectileSpeed, a_projectileGravity );
			}

			auto friendlyProjectedSweep = GetProjectedSweep( friendlyCurrentBounds, friendlyProjectedBounds );
			auto& projectedMin = projectedBounds.min;
			auto& projectedMax = projectedBounds.max;

			float hScore = abs( projectedMin.x + projectedMax.x ) / a_xLimit;
			float vScore = abs( projectedMin.y + projectedMax.y ) / a_yLimit;
			float score = hScore * hScore + vScore * vScore;

			TargetCandidateInfo* debugCandidate = nullptr;
			if( out_debugInfo )
			{
				out_debugInfo->candidates.push_back(
					{
						highActor->GetHandle(),
						bound.radius,
						highActor->GetBoundMax().x,
						score,
						isHostile
					} );

				debugCandidate = &out_debugInfo->candidates.back();
			}

			bool validProjection = projectedBounds.valid;

			if( debugCandidate )
				debugCandidate->validProjection = validProjection;
			else if( !validProjection )
				continue;

			bool insideSelectionWindow =
				projectedSweep.valid &&
				projectedSweep.bounds.max.x >= -a_xLimit && projectedSweep.bounds.min.x <= a_xLimit &&
				projectedSweep.bounds.max.y >= -yLimitDown && projectedSweep.bounds.min.y <= yLimitUp;

			if( debugCandidate )
				debugCandidate->insideSelectionWindow = insideSelectionWindow;
			else if( !insideSelectionWindow )
				continue;

			bool passedFriendlySelection = true;
			if( !isHostile )
				passedFriendlySelection = IsScreenCenterInsideProjectedSweep( friendlyProjectedSweep );

			if( debugCandidate )
				debugCandidate->passedFriendlySelection = passedFriendlySelection;
			else if( !passedFriendlySelection )
				continue;

			bool containsCrosshair = IsScreenCenterInsideProjectedSweep( projectedSweep );

			if( !isHostile )
				containsCrosshair = IsScreenCenterInsideProjectedSweep( friendlyProjectedSweep );

			if( !debugCandidate )
			{
				if( bestContainsCrosshair && !containsCrosshair )
					continue;

				if( containsCrosshair == bestContainsCrosshair )
				{
					if( containsCrosshair && actorDistanceSquared >= bestDistanceSquared )
						continue;

					if( !containsCrosshair && score >= bestScore )
						continue;
				}
			}

			bool hasLineOfSight = false;

			if( debugCandidate || validProjection && insideSelectionWindow && passedFriendlySelection )
				hasLineOfSight = HasLineOfSight( player, highActor.get() );

			if( debugCandidate )
				debugCandidate->hasLineOfSight = hasLineOfSight;

			bool eligible =
				validProjection &&
				insideSelectionWindow &&
				passedFriendlySelection &&
				hasLineOfSight;

			if( !eligible )
				continue;

			bool isBetterCandidate = false;

			if( containsCrosshair != bestContainsCrosshair )
				isBetterCandidate = containsCrosshair;
			else if( containsCrosshair )
				isBetterCandidate = actorDistanceSquared < bestDistanceSquared;
			else
				isBetterCandidate = score < bestScore;

			if( !isBetterCandidate )
				continue;

			bestContainsCrosshair = containsCrosshair;
			bestDistanceSquared = actorDistanceSquared;
			bestScore = score;
			targetCandidate = highActor.get();
			targetMoveDelta = moveDelta;
		}
	}

	if( out_debugInfo && targetCandidate )
	{
		auto targetHandle = targetCandidate->GetHandle();

		for( auto& candidate : out_debugInfo->candidates )
		{
			if( candidate.handle == targetHandle )
			{
				candidate.selected = true;
				break;
			}
		}
	}

	if( targetCandidate )
		return std::make_pair( targetCandidate, targetCandidate->GetPosition() + targetMoveDelta );

	return std::make_pair( nullptr, targetMoveDelta );
}

static float GetImpactDistanceAheadOfProjectileOrigin(
	const RE::NiPoint3& a_cameraRayOrigin,
	const RE::NiPoint3& a_cameraRayEndpoint,
	const RE::NiPoint3& a_rayImpactPosition,
	const RE::NiPoint3& a_projectileOrigin )
{
	RE::NiPoint3 rayVector = a_cameraRayEndpoint - a_cameraRayOrigin;

	float rayLengthSquared = rayVector.SqrLength();

	if( rayLengthSquared <= 0.000001f )
		return 0.0f;

	RE::NiPoint3 rayDirection = rayVector / sqrt( rayLengthSquared );

	return ( a_rayImpactPosition - a_projectileOrigin ).Dot( rayDirection );
}

extern float crosshairOriginalX;
extern float crosshairOriginalY;
static std::mutex crosshairMutex;
static bool SetCrosshairPosition( float a_screenX, float a_screenY )
{
	// Touching UI Movie is dangerous without lock
	std::lock_guard lock{ crosshairMutex };

	auto* ui = RE::UI::GetSingleton();
	if( !ui )
		return false;

	auto movie = ui->GetMovieView( RE::HUDMenu::MENU_NAME );
	if( !movie )
		return false;

	RE::GViewport viewport;
	movie->GetViewport( &viewport );

	if( viewport.width <= 0 || viewport.height <= 0 )
		return false;

	auto frame = movie->GetVisibleFrameRect();
	float frameWidth = frame.right - frame.left;
	float frameHeight = frame.bottom - frame.top;

	float viewportCenterX = viewport.left + viewport.width * 0.5f;
	float viewportCenterY = viewport.top + viewport.height * 0.5f;

	float screenDeltaX = a_screenX - viewportCenterX;
	float screenDeltaY = a_screenY - viewportCenterY;

	constexpr float crosshairCenterX = 23.2f;
	constexpr float crosshairCenterY = -115.3f;

	float crosshairX = crosshairCenterX + screenDeltaX * frameWidth / viewport.width;
	float crosshairY = crosshairCenterY + screenDeltaY * frameHeight / viewport.height;

	RE::GFxValue crosshair;
	if( !movie->GetVariable(&crosshair, "_root.HUDMovieBaseInstance.CrosshairInstance" ) || !crosshair.IsDisplayObject() )
		return false;

	RE::GFxValue::DisplayInfo info;
	if( !crosshair.GetDisplayInfo( &info ) )
		return false;

	if( crosshairOriginalX == FLT_MAX )
	{
		crosshairOriginalX = viewportCenterX + (float)(info.GetX() - crosshairCenterX) * viewport.width / frameWidth;
		crosshairOriginalY = viewportCenterY + (float)(info.GetY() - crosshairCenterY) * viewport.height / frameHeight;
	}

	info.SetPosition( crosshairX, crosshairY );

	return crosshair.SetDisplayInfo( info );
}

static void ResetCrosshairPosition()
{
	if( crosshairOriginalX != FLT_MAX )
	{
		SetCrosshairPosition( crosshairOriginalX, crosshairOriginalY );
		crosshairOriginalX = FLT_MAX;
		crosshairOriginalY = FLT_MAX;
	}
}

static bool SetCrosshairToWorldPosition( const RE::NiPoint3& a_world )
{
	auto* ui = RE::UI::GetSingleton();
	auto camera = RE::Main::WorldRootCamera();
	if( !ui || !camera )
		return false;

	float normalizedX;
	float normalizedY;
	float depth;

	if( !RE::NiCamera::WorldPtToScreenPt3(
		camera->GetRuntimeData().worldToCam,
		camera->GetRuntimeData2().port,
		a_world,
		normalizedX,
		normalizedY,
		depth,
		1.0e-5f ) )
	{
		return false;
	}

	auto movie = ui->GetMovieView( RE::HUDMenu::MENU_NAME );
	if( !movie )
		return false;

	RE::GViewport viewport;
	movie->GetViewport( &viewport );

	float screenX = viewport.left + normalizedX * viewport.width;
	float screenY = viewport.top + ( 1.0f - normalizedY ) * viewport.height;

	return SetCrosshairPosition( screenX, screenY );
}

using conditionFunc_t = bool(RE::TESObjectREFR* a_ref, void* a_param1, void* a_param2, double& out_result);
static conditionFunc_t* GetConditionFunctionEntry( RE::FUNCTION_DATA::FunctionID a_id )
{
	static auto functionTable = REL::Relocation<uintptr_t>( RELOCATION_ID( 361129, 361129 ) ).address();
	static size_t stride = 0x50;
	return *reinterpret_cast<conditionFunc_t**>( functionTable + (uint32_t)a_id * stride );
}

#pragma warning(pop)
