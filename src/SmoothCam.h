#pragma once

#include <Windows.h>
#include <DbgHelp.h>
#include <d3d11.h>
#include <MinHook.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

class SmoothCamArrowTrail
{
public:
	struct alignas( 16 ) SmoothCamVec3
	{
		float x;
		float y;
		float z;
		float padding;
	};

	struct alignas( 16 ) SmoothCamVec4
	{
		float x;
		float y;
		float z;
		float w;
	};

	struct alignas( 16 ) PolyPoint
	{
		SmoothCamVec3 pos;
		SmoothCamVec4 col;

		PolyPoint( const SmoothCamVec3& a_position, const SmoothCamVec4& a_color ) :
			pos( a_position ),
			col( a_color )
		{}
	};

	struct PolylineView
	{
		float widthPx;
		uint32_t padding;
		const PolyPoint* begin;
		const PolyPoint* end;
		const PolyPoint* capacity;
	};

	struct alignas( 16 ) SmoothCamMat4
	{
		float data[4][4];
	};

	struct ProjectedPoint
	{
		RE::NiPoint3 worldPosition;
		SmoothCamVec3 clipPosition;
		bool visible = false;
	};

	struct ProjectedSegment
	{
		PolyPoint points[2];
		RE::NiPoint3 worldStart;
		RE::NiPoint3 worldEnd;
	};

	static_assert( sizeof( SmoothCamVec3 ) == 0x10 );
	static_assert( alignof( SmoothCamVec3 ) == 0x10 );
	static_assert( sizeof( SmoothCamVec4 ) == 0x10 );
	static_assert( alignof( SmoothCamVec4 ) == 0x10 );
	static_assert( sizeof( PolyPoint ) == 0x20 );
	static_assert( alignof( PolyPoint ) == 0x10 );
	static_assert( offsetof( PolylineView, widthPx ) == 0x00 );
	static_assert( offsetof( PolylineView, begin ) == 0x08 );
	static_assert( offsetof( PolylineView, end ) == 0x10 );
	static_assert( offsetof( PolylineView, capacity ) == 0x18 );
	static_assert( sizeof( PolylineView ) == 0x20 );
	static_assert( sizeof( SmoothCamMat4 ) == 0x40 );

	static bool Install()
	{
		smoothCamModule = GetModuleHandleW( L"SmoothCam.dll" );

		if( !smoothCamModule )
		{
			REX::DEBUG( "SmoothCam.dll is not loaded" );
			return false;
		}

		auto cameraRenderAddress = GetPDBSymbolAddress(
			smoothCamModule,
			"?Render@Camera@1@QEAAXAEAUD3DContext@0@@Z" );

		auto submitAddress = GetPDBSymbolAddress(
			smoothCamModule,
			"?Submit@LineDrawer@Render@@QEAAXAEBUPolyline@2@AEBU?$mat@$03$03M$02@glm@@@Z" );

		auto depthStateAddress = GetPDBSymbolAddress(
			smoothCamModule,
			"?SetDepthState@Render@@YAXAEAUD3DContext@1@_N1W4D3D11_COMPARISON_FUNC@@@Z" );

		if( !cameraRenderAddress || !submitAddress || !depthStateAddress )
		{
			REX::DEBUG( "Failed to resolve required SmoothCam trail rendering symbols" );
			return false;
		}

		lineDrawerSubmit = reinterpret_cast<LineDrawerSubmit_t>( *submitAddress );
		setDepthState = reinterpret_cast<SetDepthState_t>( *depthStateAddress );

		void* target = reinterpret_cast<void*>( *cameraRenderAddress );

		MH_STATUS status = MH_CreateHook(
			target,
			reinterpret_cast<void*>( CameraRenderThunk ),
			reinterpret_cast<void**>( &cameraRender ) );

		if( status != MH_OK && status != MH_ERROR_ALREADY_CREATED )
		{
			REX::DEBUG( "Failed to create SmoothCam render hook: {}", static_cast<uint32_t>( status ) );
			return false;
		}

		status = MH_EnableHook( target );

		if( status != MH_OK && status != MH_ERROR_ENABLED )
		{
			REX::DEBUG( "Failed to enable SmoothCam render hook: {}", static_cast<uint32_t>( status ) );
			return false;
		}

		REX::DEBUG(
			"SmoothCam arrow trail installed: Render={:016X}, Submit={:016X}",
			*cameraRenderAddress,
			*submitAddress );

		return true;
	}

	static void BeginArrow( RE::Projectile* a_projectile )
	{
		std::scoped_lock lock( trailMutex );

		projectileHandle.reset();
		worldTrailPoints.clear();
		projectedSegments.clear();
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

	static void Stop()
	{
		std::scoped_lock lock( trailMutex );

		projectileHandle.reset();
		worldTrailPoints.clear();
		projectedSegments.clear();
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

		color.x = std::clamp( a_red, 0.0f, 1.0f );
		color.y = std::clamp( a_green, 0.0f, 1.0f );
		color.z = std::clamp( a_blue, 0.0f, 1.0f );
		color.w = std::clamp( a_alpha, 0.0f, 1.0f );
	}

	static void SetMinimumPointDistance( float a_distance )
	{
		std::scoped_lock lock( trailMutex );
		minimumPointDistance = std::max( a_distance, 0.0f );
	}

	static void SetMaximumPointCount( size_t a_count )
	{
		std::scoped_lock lock( trailMutex );
		maximumPointCount = std::max<size_t>( a_count, 2 );
	}

	static void SetDepthTest( bool a_enabled )
	{
		depthTest = a_enabled;
	}

	static void SetDepthBias( float a_bias )
	{
		depthBias = std::max( a_bias, 0.0f );
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
	using CameraRender_t = void( * )( void* a_camera, void* a_context );

	using LineDrawerSubmit_t = void( * )(
		void* a_lineDrawer,
		const PolylineView& a_line,
		const SmoothCamMat4& a_matProjView );

	using SetDepthState_t = void( * )(
		void* a_context,
		bool a_depthEnable,
		bool a_depthWrite,
		D3D11_COMPARISON_FUNC a_comparison );

	using GetInstance_t = void*( * )();

	static void CameraRenderThunk( void* a_camera, void* a_context )
	{
		cameraRender( a_camera, a_context );

		if( !enabled )
			return;

		auto ui = RE::UI::GetSingleton();
		if( !ui || ui->GameIsPaused() )
			return;

		std::scoped_lock lock( trailMutex );

		UpdateTrail();

		if( worldTrailPoints.size() < 2 || !BuildProjectedSegments() )
			return;

		void* lineDrawer = GetLineDrawer();

		if( !IsReadableAddress( lineDrawer, sizeof( void* ) ) || !lineDrawerSubmit || !setDepthState )
			return;

		auto camera = RE::Main::WorldRootCamera();

		if( !camera )
			return;

		const RE::NiPoint3& cameraPosition = camera->world.translate;

		setDepthState(
			a_context,
			depthTest,
			false,
			depthTest ? D3D11_COMPARISON_LESS_EQUAL : D3D11_COMPARISON_ALWAYS );

		SmoothCamMat4 identityMatrix = GetIdentityMatrix();

		for( const auto& segment : projectedSegments )
		{
			float width = GetSegmentWidth( segment.worldStart, segment.worldEnd, cameraPosition );
			const PolyPoint* segmentBegin = segment.points;
			const PolyPoint* segmentEnd = segmentBegin + 2;

			PolylineView line{
				width,
				0,
				segmentBegin,
				segmentEnd,
				segmentEnd
			};

			lineDrawerSubmit( lineDrawer, line, identityMatrix );
		}
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

		auto projectile3D = projectile->Get3D();

		if( projectile3D )
			AppendPosition( projectile3D->world.translate );
		else
			AppendPosition( projectile->GetPosition() );
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

	static bool BuildProjectedSegments()
	{
		auto camera = RE::Main::WorldRootCamera();

		if( !camera || worldTrailPoints.size() < 2 )
			return false;

		projectedSegments.clear();
		projectedSegments.reserve( worldTrailPoints.size() - 1 );

		for( size_t i = 1; i < worldTrailPoints.size(); i++ )
		{
			ProjectedPoint start = ProjectPoint( camera, worldTrailPoints[i - 1] );
			ProjectedPoint end = ProjectPoint( camera, worldTrailPoints[i] );

			if( !start.visible && !end.visible )
				continue;

			if( start.visible != end.visible && !ClipSegmentToNearPlane( camera, start, end ) )
				continue;

			if( !start.visible || !end.visible )
				continue;

			projectedSegments.push_back(
				{
					{
						PolyPoint{ start.clipPosition, color },
						PolyPoint{ end.clipPosition, color }
					},
				start.worldPosition,
				end.worldPosition
				} );
		}

		return !projectedSegments.empty();
	}

	static ProjectedPoint ProjectPoint( const RE::NiCamera* a_camera, const RE::NiPoint3& a_worldPosition )
	{
		ProjectedPoint result{};
		result.worldPosition = a_worldPosition;

		if( !a_camera )
			return result;

		RE::NiRect<float> viewport{
			0.0f,
			1.0f,
			0.0f,
			1.0f
		};

		float screenX = 0.0f;
		float screenY = 0.0f;
		float screenZ = 0.0f;

		bool projected = RE::NiCamera::WorldPtToScreenPt3(
			a_camera->GetRuntimeData().worldToCam,
			viewport,
			a_worldPosition,
			screenX,
			screenY,
			screenZ,
			projectionEpsilon );

		if( !projected || screenZ <= nearDepthEpsilon || screenZ >= 1.0f )
			return result;

		result.clipPosition = {
			screenX * 2.0f - 1.0f,
			1.0f - screenY * 2.0f,
			std::clamp( screenZ - depthBias, 0.0f, 1.0f ),
			0.0f
		};

		result.visible = true;

		return result;
	}

	static bool ClipSegmentToNearPlane(
		const RE::NiCamera* a_camera,
		ProjectedPoint& a_start,
		ProjectedPoint& a_end )
	{
		if( !a_camera || a_start.visible == a_end.visible )
			return false;

		bool startWasVisible = a_start.visible;
		ProjectedPoint visiblePoint = startWasVisible ? a_start : a_end;
		ProjectedPoint hiddenPoint = startWasVisible ? a_end : a_start;
		RE::NiPoint3 visibleWorld = visiblePoint.worldPosition;
		RE::NiPoint3 hiddenWorld = hiddenPoint.worldPosition;
		ProjectedPoint clippedPoint = visiblePoint;

		for( uint32_t i = 0; i < nearClipIterations; i++ )
		{
			RE::NiPoint3 midpoint{
				(visibleWorld.x + hiddenWorld.x) * 0.5f,
				(visibleWorld.y + hiddenWorld.y) * 0.5f,
				(visibleWorld.z + hiddenWorld.z) * 0.5f
			};

			ProjectedPoint projectedMidpoint = ProjectPoint( a_camera, midpoint );

			if( projectedMidpoint.visible )
			{
				visibleWorld = midpoint;
				clippedPoint = projectedMidpoint;
			}
			else
			{
				hiddenWorld = midpoint;
			}
		}

		if( !clippedPoint.visible )
			return false;

		clippedPoint.worldPosition = LerpWorldPoint(
			clippedPoint.worldPosition,
			visiblePoint.worldPosition,
			nearClipInset );

		clippedPoint = ProjectPoint( a_camera, clippedPoint.worldPosition );

		if( !clippedPoint.visible )
			return false;

		if( startWasVisible )
			a_end = clippedPoint;
		else
			a_start = clippedPoint;

		return true;
	}

	static RE::NiPoint3 LerpWorldPoint( const RE::NiPoint3& a_start, const RE::NiPoint3& a_end, float a_amount )
	{
		return {
			a_start.x + (a_end.x - a_start.x) * a_amount,
			a_start.y + (a_end.y - a_start.y) * a_amount,
			a_start.z + (a_end.z - a_start.z) * a_amount
		};
	}

	static float GetSegmentWidth( const RE::NiPoint3& a_start, const RE::NiPoint3& a_end, const RE::NiPoint3& a_cameraPosition )
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

	static void* GetSmoothCamManager()
	{
		static uintptr_t getInstanceAddress = 0;
		static uint32_t crosshairManagerOffset = 0;
		static bool symbolsResolved = false;
		static void* manager = nullptr;

		if( manager )
			return manager;

		if( !smoothCamModule )
			return nullptr;

		if( !symbolsResolved )
		{
			auto address = GetPDBSymbolAddress(
				smoothCamModule,
				"?GetInstance@SmoothCamInterface@Messaging@@SAPEAV12@XZ" );

			if( !address )
				return nullptr;

			getInstanceAddress = *address;

			HANDLE process = GetCurrentProcess();
			DWORD64 moduleBase = reinterpret_cast<DWORD64>( smoothCamModule );

			SYMBOL_INFO_PACKAGE interfaceType{};

			interfaceType.si.SizeOfStruct = sizeof( SYMBOL_INFO );
			interfaceType.si.MaxNameLen = MAX_SYM_NAME;

			if( !SymGetTypeFromName(
				process,
				moduleBase,
				"Messaging::SmoothCamInterface",
				&interfaceType.si ) )
			{
				return nullptr;
			}

			auto member = FindPDBMember(
				smoothCamModule,
				interfaceType.si.TypeIndex,
				"crosshairMgr" );

			if( !member )
				return nullptr;

			crosshairManagerOffset = member->offset;
			symbolsResolved = true;
		}

		if( !getInstanceAddress || !crosshairManagerOffset )
			return nullptr;

		auto getInstance = reinterpret_cast<GetInstance_t>( getInstanceAddress );
		void* smoothCamInterface = getInstance();

		if( !smoothCamInterface )
			return nullptr;

		auto managerStorage =
			reinterpret_cast<uintptr_t>( smoothCamInterface ) +
			crosshairManagerOffset;

		if( !IsReadableAddress( reinterpret_cast<void*>( managerStorage ), sizeof( void* ) ) )
			return nullptr;

		manager = *reinterpret_cast<void**>( managerStorage );

		return manager;
	}

	static void* GetLineDrawer()
	{
		static uint32_t renderablesOffset = 0;
		static uint32_t tailDrawerOffset = 0;
		static bool offsetsResolved = false;

		void* manager = GetSmoothCamManager();

		if( !manager || !smoothCamModule )
			return nullptr;

		if( !offsetsResolved )
		{
			HANDLE process = GetCurrentProcess();
			DWORD64 moduleBase = reinterpret_cast<DWORD64>( smoothCamModule );

			SYMBOL_INFO_PACKAGE managerType{};

			managerType.si.SizeOfStruct = sizeof( SYMBOL_INFO );
			managerType.si.MaxNameLen = MAX_SYM_NAME;

			if( !SymGetTypeFromName( process, moduleBase, "Crosshair::Manager", &managerType.si ) )
				return nullptr;

			auto renderablesMember = FindPDBMember(
				smoothCamModule,
				managerType.si.TypeIndex,
				"renderables" );

			if( !renderablesMember )
				return nullptr;

			auto tailDrawerMember = FindPDBMember(
				smoothCamModule,
				renderablesMember->typeIndex,
				"tailDrawer" );

			if( !tailDrawerMember )
				return nullptr;

			renderablesOffset = renderablesMember->offset;
			tailDrawerOffset = tailDrawerMember->offset;
			offsetsResolved = true;
		}

		auto lineDrawerStorage =
			reinterpret_cast<uintptr_t>( manager ) +
			renderablesOffset +
			tailDrawerOffset;

		if( !IsReadableAddress( reinterpret_cast<void*>( lineDrawerStorage ), sizeof( void* ) ) )
			return nullptr;

		return *reinterpret_cast<void**>( lineDrawerStorage );
	}

	static SmoothCamMat4 GetIdentityMatrix()
	{
		SmoothCamMat4 matrix{};

		matrix.data[0][0] = 1.0f;
		matrix.data[1][1] = 1.0f;
		matrix.data[2][2] = 1.0f;
		matrix.data[3][3] = 1.0f;

		return matrix;
	}

	static bool IsReadableAddress( const void* a_address, size_t a_size )
	{
		if( !a_address || !a_size )
			return false;

		MEMORY_BASIC_INFORMATION memoryInfo{};

		if( !VirtualQuery( a_address, &memoryInfo, sizeof( memoryInfo ) ) )
			return false;

		if( memoryInfo.State != MEM_COMMIT )
			return false;

		if( memoryInfo.Protect & ( PAGE_NOACCESS | PAGE_GUARD ) )
			return false;

		auto regionBegin = reinterpret_cast<uintptr_t>( memoryInfo.BaseAddress );
		auto regionEnd = regionBegin + memoryInfo.RegionSize;
		auto addressBegin = reinterpret_cast<uintptr_t>( a_address );
		auto addressEnd = addressBegin + a_size;

		return addressBegin >= regionBegin && addressEnd >= addressBegin && addressEnd <= regionEnd;
	}

	static constexpr float projectionEpsilon = 9.99999975e-06f;
	static constexpr float nearDepthEpsilon = 0.000001f;
	static constexpr float nearClipInset = 0.001f;
	static constexpr uint32_t nearClipIterations = 16;

	static inline HMODULE smoothCamModule = nullptr;

	static inline CameraRender_t cameraRender = nullptr;
	static inline LineDrawerSubmit_t lineDrawerSubmit = nullptr;
	static inline SetDepthState_t setDepthState = nullptr;

	static inline RE::ObjectRefHandle projectileHandle;

	static inline std::vector<RE::NiPoint3> worldTrailPoints;
	static inline std::vector<ProjectedSegment> projectedSegments;

	static inline std::mutex trailMutex;

	static inline SmoothCamVec4 color{
		1.0f,
		0.25f,
		0.05f,
		1.0f
	};

	static inline float trailWidth = 3.0f;
	static inline float minimumPointDistance = 5.0f;
	static inline float depthBias = 0.0f;
	static inline float widthReferenceDistance = 1000.0f;
	static inline float minimumTrailWidth = 1.0f;
	static inline float maximumTrailWidth = 6.0f;

	static inline size_t maximumPointCount = 256;

	static inline bool scaleWidthByDistance = true;
	static inline bool depthTest = true;
	static inline bool active = false;
	static inline bool enabled = true;
};
