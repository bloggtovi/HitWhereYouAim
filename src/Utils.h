#pragma once
#pragma warning(push)
#pragma warning(disable: 4505)

#include "BallisticSolver.h"

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
	float yLimit = 0.25f;

	void Reset( float a_xLimit, float a_yLimit )
	{
		candidates.clear();
		xLimit = a_xLimit;
		yLimit = a_yLimit;
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
	RE::NiPoint3 direction = delta / length;

	if( length == 0 || length < a_maxLength )
		return a_target;

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
	auto screenSize = RE::BSGraphics::Renderer::GetSingleton()->GetScreenSize();

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

static std::pair<RE::NiPoint2, RE::NiPoint2> ProjectReferenceBoundsToScreen( const RE::NiCamera* a_camera, const RE::TESObjectREFR* a_ref, float a_predictionTime, float a_nearThreshold )
{
	RE::NiPoint2 projectedMin( FLT_MAX, FLT_MAX );
	RE::NiPoint2 projectedMax( FLT_MAX, FLT_MAX );

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
			RE::NiPoint3 projectedYMin, projectedYMax;
			ProjectBoundToScreen( a_camera, bound, projectedYMin, projectedYMax, a_nearThreshold );

			bound.radius = a_ref->GetBoundMax().x;
			RE::NiPoint3 projectedXMin, projectedXMax;
			ProjectBoundToScreen( a_camera, bound, projectedXMin, projectedXMax, a_nearThreshold );

			projectedMin.x = projectedXMin.x;
			projectedMin.y = projectedYMin.y;

			projectedMax.x = projectedXMax.x;
			projectedMax.y = projectedYMax.y;
		}
	}

	return std::make_pair( projectedMin, projectedMax );
}

static std::pair<RE::TESObjectREFR*,RE::NiPoint3> GetPotentialPlayerTarget(
	float a_distance,
	RE::Projectile* a_projectile = nullptr,
	float a_xLimit = 0.25f,
	float a_yLimit = 0.25f,
	CandidateSelectionInfo* out_debugInfo = nullptr )
{
	if( a_xLimit <= 0 ) a_xLimit = 0.0001f;
	if( a_yLimit <= 0 ) a_yLimit = 0.0001f;

	if( out_debugInfo )
		out_debugInfo->Reset( a_xLimit, a_yLimit );

	auto processLists = RE::ProcessLists::GetSingleton();
	auto player = RE::PlayerCharacter::GetSingleton();
	RE::NiCamera* camera = RE::Main::WorldRootCamera();

	if( !processLists || !player || !camera )
		return {};

	float distanceSquared = a_distance * a_distance;
	float bestScore = FLT_MAX;
	float projectileSpeed = a_projectile ? a_projectile->GetSpeed() : 0;

	RE::NiPoint3 origin = a_projectile ? a_projectile->GetPosition() : player->GetPosition();

	RE::Actor* targetCandidate = nullptr;
	RE::NiPoint3 targetMoveDelta{ 0, 0, 0 };
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
			if( projectileSpeed > 0 )
			{
				RE::NiPoint3 velocity;
				highActor->GetLinearVelocity( velocity );

				// Simple two pass predictor
				ttr = CalculateProjectileFlightTimeToTarget( origin, bound.center,					projectileSpeed );
				ttr = CalculateProjectileFlightTimeToTarget( origin, bound.center + velocity * ttr, projectileSpeed );

				moveDelta = velocity * ttr;
			}

			auto [projectedMin, projectedMax] = ProjectReferenceBoundsToScreen( camera, highActor.get(), ttr, 0 );

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

			if( !debugCandidate && score >= bestScore )
				continue;

			bool validProjection =
				!(projectedMin.x == -1.0F && projectedMax.x == 1.0F) &&
				!(projectedMin.y == -1.0F && projectedMax.y == 1.0F);

			if( debugCandidate )
				debugCandidate->validProjection = validProjection;
			else if( !validProjection )
				continue;

			bool insideSelectionWindow =
				projectedMax.x >= -a_xLimit && projectedMin.x <= a_xLimit &&
				projectedMax.y >= -a_yLimit && projectedMin.y <= a_yLimit;

			if( debugCandidate )
				debugCandidate->insideSelectionWindow = insideSelectionWindow;
			else if( !insideSelectionWindow )
				continue;

			bool passedFriendlySelection = true;
			if( !isHostile )
			{
				float xDepth = (projectedMax.x - projectedMin.x) * 0.1f;
				float yDepth = (projectedMax.y - projectedMin.y) * 0.1f;

				passedFriendlySelection =
					projectedMin.x + xDepth <= 0 && projectedMax.x - xDepth >= 0 &&
					projectedMin.y + yDepth <= 0 && projectedMax.y - yDepth >= 0;
			}

			if( debugCandidate )
				debugCandidate->passedFriendlySelection = passedFriendlySelection;
			else if( !passedFriendlySelection )
				continue;

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

			if( !eligible || score >= bestScore )
				continue;

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
