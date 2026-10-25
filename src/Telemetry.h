#pragma once

#include "MultiplayerGuard.h"
#include "Settings.h"
#include "Utils.h"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

// Read-only telemetry for external validation tools. Publishes launch and impact records of
// the player's projectiles to a named shared-memory ring buffer
// ("Local\HitWhereYouAim.Telemetry"). It never changes projectile behavior.
// Disabled unless [Debug] bEnableTelemetry=1 in the MCM settings ini.
class Telemetry
{
public:
	static constexpr uint32_t kMagic = 0x54595748;	// "HWYT"
	static constexpr uint32_t kVersion = 1;
	static constexpr uint32_t kCapacity = 256;
	static constexpr uint32_t kMaxTracked = 16;
	static constexpr uint32_t kPendingLaunchMaxAgeMS = 1000;
	static constexpr uint32_t kTrackTimeoutMS = 15000;

	enum class RecordKind : uint32_t
	{
		kLaunch = 1,	// position = projectile position after init, angle = applied launch angle
		kImpact = 2,	// position = ImpactData::desiredTargetLoc, aimPoint = projectile position
		kLost = 3,		// projectile vanished without impact data, position = last seen position
	};

	enum TargetFlags : uint32_t
	{
		kHasTarget = 1 << 0,
		kTargetIsActor = 1 << 1,
		kTargetIsOtherHuman = 1 << 2,
	};

#pragma pack( push, 4 )
	struct Record
	{
		uint32_t sequence;			// 0 = empty, written last
		uint32_t kind;
		uint32_t gameTimeMS;
		uint32_t projectileHandle;
		uint32_t projectileFormID;	// BGSProjectile base form
		uint16_t projectileTypes;	// BGSProjectileData::types
		uint16_t projectileFlags;	// BGSProjectileData::flags
		float speed;				// Projectile::GetSpeed()
		float gravity;				// Projectile::GetGravity()
		float worldGravity;
		float range;				// BGSProjectileData::range
		float position[ 3 ];
		float angleX;
		float angleZ;
		float aimPoint[ 3 ];
		uint32_t targetFormID;
		uint32_t targetFlags;
		float targetPosition[ 3 ];	// world bound center of the target
		float targetVelocity[ 3 ];
		uint32_t collideeFormID;
		uint32_t reserved;
	};
	static_assert( sizeof( Record ) == 112 );

	struct Header
	{
		uint32_t magic;
		uint32_t version;
		uint32_t recordSize;
		uint32_t capacity;
		uint32_t writeSequence;		// sequence of the newest complete record
		uint32_t reserved[ 3 ];
	};
	static_assert( sizeof( Header ) == 32 );
#pragma pack( pop )

	// Called from the launch angle hook with the aim data HWYA used.
	static void SetPendingLaunch( const RE::NiPoint3& a_aimPoint, RE::TESObjectREFR* a_target, const RE::NiPoint3& a_targetVelocity )
	{
		if( !Settings::bEnableTelemetry )
			return;

		pending.valid = true;
		pending.timeMS = GetCurrentGameRuntimeMS();
		pending.aimPoint = a_aimPoint;
		pending.target = a_target ? a_target->GetHandle() : RE::ObjectRefHandle{};
		pending.targetVelocity = a_targetVelocity;
	}

	// Called from the projectile init hook for projectiles shot by the player.
	static void OnPlayerProjectileInit( RE::Projectile* a_projectile )
	{
		if( !Settings::bEnableTelemetry || !a_projectile || !Open() )
			return;

		Record record{};
		record.kind = (uint32_t)RecordKind::kLaunch;
		record.gameTimeMS = GetCurrentGameRuntimeMS();
		record.projectileHandle = a_projectile->GetHandle().native_handle();
		FillProjectile( record, a_projectile );
		SetPoint( record.position, a_projectile->GetPosition() );
		record.angleX = a_projectile->data.angle.x;
		record.angleZ = a_projectile->data.angle.z;

		RE::ObjectRefHandle targetHandle;
		if( pending.valid && record.gameTimeMS - pending.timeMS <= kPendingLaunchMaxAgeMS )
		{
			SetPoint( record.aimPoint, pending.aimPoint );
			targetHandle = pending.target;
			auto target = targetHandle.get();
			FillTarget( record, target.get(), pending.targetVelocity );
		}
		pending.valid = false;

		Publish( record );

		if( tracked.size() >= kMaxTracked )
			tracked.erase( tracked.begin() );

		tracked.push_back( { a_projectile->GetHandle(), targetHandle, a_projectile->GetPosition(), record } );
	}

	// Called once per player update. Emits impact/lost records for tracked projectiles.
	static void Update()
	{
		if( !Settings::bEnableTelemetry || !header || tracked.empty() )
			return;

		uint32_t now = GetCurrentGameRuntimeMS();
		for( auto it = tracked.begin(); it != tracked.end(); )
		{
			bool finished = false;
			auto reference = it->projectile.get();
			auto projectile = reference ? reference->As<RE::Projectile>() : nullptr;

			if( !projectile || now - it->launch.gameTimeMS > kTrackTimeoutMS )
			{
				PublishEnd( *it, RecordKind::kLost, it->lastPosition, it->lastPosition, nullptr, now );
				finished = true;
			}
			else
			{
				auto& runtimeData = projectile->GetProjectileRuntimeData();
				RE::NiPoint3 position = projectile->GetPosition();

				if( !runtimeData.impacts.empty() )
				{
					auto impact = runtimeData.impacts.front();
					auto collidee = impact ? impact->collidee.get() : RE::NiPointer<RE::TESObjectREFR>{};
					PublishEnd( *it, RecordKind::kImpact, impact ? impact->desiredTargetLoc : position, position, collidee.get(), now );
					finished = true;
				}
				else if( runtimeData.flags.any( RE::Projectile::Flags::kProcessedImpacts ) ||
					runtimeData.flags.any( RE::Projectile::Flags::kDestroyed ) )
				{
					PublishEnd( *it, RecordKind::kImpact, position, position, nullptr, now );
					finished = true;
				}
				else
				{
					it->lastPosition = position;
				}
			}

			it = finished ? tracked.erase( it ) : it + 1;
		}
	}

private:
	struct PendingLaunch
	{
		bool valid = false;
		uint32_t timeMS = 0;
		RE::NiPoint3 aimPoint;
		RE::ObjectRefHandle target;
		RE::NiPoint3 targetVelocity;
	};

	struct TrackedProjectile
	{
		RE::ObjectRefHandle projectile;
		RE::ObjectRefHandle target;
		RE::NiPoint3 lastPosition;
		Record launch;
	};

	static bool Open()
	{
		if( header )
			return true;

		if( openFailed )
			return false;

		constexpr uint32_t size = static_cast<uint32_t>( sizeof( Header ) + sizeof( Record ) * kCapacity );
		mapping = REX::W32::CreateFileMappingW( REX::W32::INVALID_HANDLE_VALUE, nullptr, REX::W32::PAGE_READWRITE, 0, size, L"Local\\HitWhereYouAim.Telemetry" );
		void* view = mapping ? REX::W32::MapViewOfFile( mapping, REX::W32::FILE_MAP_WRITE, 0, 0, size ) : nullptr;
		if( !view )
		{
			openFailed = true;
			REX::WARN( "Telemetry: shared memory could not be created" );
			return false;
		}

		memset( view, 0, size );
		header = static_cast<Header*>( view );
		records = reinterpret_cast<Record*>( header + 1 );
		header->recordSize = static_cast<uint32_t>( sizeof( Record ) );
		header->capacity = kCapacity;
		header->version = kVersion;
		std::atomic_ref<uint32_t>( header->magic ).store( kMagic, std::memory_order_release );
		REX::INFO( "Telemetry: shared memory ready" );
		return true;
	}

	static void Publish( Record& a_record )
	{
		if( !header )
			return;

		uint32_t sequence = nextSequence++;
		if( sequence == 0 )
			sequence = nextSequence++;

		Record& slot = records[ sequence % kCapacity ];
		std::atomic_ref<uint32_t>( slot.sequence ).store( 0, std::memory_order_release );
		a_record.sequence = 0;
		slot = a_record;
		std::atomic_ref<uint32_t>( slot.sequence ).store( sequence, std::memory_order_release );
		std::atomic_ref<uint32_t>( header->writeSequence ).store( sequence, std::memory_order_release );
	}

	static void PublishEnd( const TrackedProjectile& a_tracked, RecordKind a_kind, const RE::NiPoint3& a_position,
		const RE::NiPoint3& a_projectilePosition, RE::TESObjectREFR* a_collidee, uint32_t a_now )
	{
		Record record = a_tracked.launch;
		record.kind = (uint32_t)a_kind;
		record.gameTimeMS = a_now;
		SetPoint( record.position, a_position );
		SetPoint( record.aimPoint, a_projectilePosition );
		record.collideeFormID = a_collidee ? a_collidee->GetFormID() : 0;

		// Target position at the end of the flight, to measure the real miss distance.
		record.targetFlags = 0;
		record.targetFormID = 0;
		auto target = a_tracked.target.get();
		RE::NiPoint3 velocity{ 0, 0, 0 };
		if( target )
			target->GetLinearVelocity( velocity );
		FillTarget( record, target.get(), velocity );

		Publish( record );
	}

	static void FillProjectile( Record& a_record, RE::Projectile* a_projectile )
	{
		if( auto base = a_projectile->GetProjectileBase() )
		{
			a_record.projectileFormID = base->GetFormID();
			a_record.projectileTypes = base->data.types.underlying();
			a_record.projectileFlags = base->data.flags.underlying();
			a_record.range = base->data.range;
		}
		a_record.speed = a_projectile->GetSpeed();
		a_record.gravity = a_projectile->GetGravity();
		a_record.worldGravity = GetWorldGravity();
	}

	static void FillTarget( Record& a_record, RE::TESObjectREFR* a_target, const RE::NiPoint3& a_velocity )
	{
		if( !a_target )
			return;

		a_record.targetFlags = kHasTarget;
		a_record.targetFormID = a_target->GetFormID();
		if( a_target->As<RE::Actor>() )
			a_record.targetFlags |= kTargetIsActor;
		if( MultiplayerGuard::IsOtherHumanPlayer( a_target ) )
			a_record.targetFlags |= kTargetIsOtherHuman;

		auto target3D = a_target->Get3D2();
		SetPoint( a_record.targetPosition, target3D ? target3D->worldBound.center : a_target->GetPosition() );
		SetPoint( a_record.targetVelocity, a_velocity );
	}

	static void SetPoint( float ( &a_out )[ 3 ], const RE::NiPoint3& a_point )
	{
		a_out[ 0 ] = a_point.x;
		a_out[ 1 ] = a_point.y;
		a_out[ 2 ] = a_point.z;
	}

	static inline REX::W32::HANDLE mapping = nullptr;
	static inline Header* header = nullptr;
	static inline Record* records = nullptr;
	static inline bool openFailed = false;
	static inline uint32_t nextSequence = 1;
	static inline PendingLaunch pending;
	static inline std::vector<TrackedProjectile> tracked;
};
