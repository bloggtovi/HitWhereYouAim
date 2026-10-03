#pragma once

namespace HWYA_API
{
	inline constexpr auto DLL_NAME = L"HitWhereYouAim.dll";
	inline constexpr auto EXPORT_NAME = "RequestPluginAPI";

	enum class InterfaceVersion : uint8_t
	{
		kV1 = 0
	};

	enum class Feature : uint32_t
	{
		kNone = 0,
		kEnabled = 1U << 0,
		kPitchCorrection = 1U << 1,
		kAutomaticPitchCorrection = 1U << 2,
		kFixedPitchCorrection = 1U << 3,
		kTargetDepthPrediction = 1U << 4,
		kTargetLeading = 1U << 5
	};

	[[nodiscard]] constexpr Feature operator|( Feature a_lhs, Feature a_rhs ) noexcept
	{
		return static_cast<Feature>(
			static_cast<uint32_t>( a_lhs ) |
			static_cast<uint32_t>( a_rhs ) );
	}

	constexpr Feature& operator|=( Feature& a_lhs, Feature a_rhs ) noexcept
	{
		return a_lhs = a_lhs | a_rhs;
	}

	[[nodiscard]] constexpr Feature operator&( Feature a_lhs, Feature a_rhs ) noexcept
	{
		return static_cast<Feature>(
			static_cast<uint32_t>( a_lhs ) &
			static_cast<uint32_t>( a_rhs ) );
	}

	[[nodiscard]] constexpr bool HasFeature( Feature a_flags, Feature a_feature ) noexcept
	{
		return ( a_flags & a_feature ) != Feature::kNone;
	}

	class IVHitWhereYouAim1
	{
	public:
		[[nodiscard]] virtual Feature GetEnabledFeatures() const noexcept = 0;

	protected:
		~IVHitWhereYouAim1() = default;
	};

	using RequestPluginAPI_t = void*( * )( InterfaceVersion );

	[[nodiscard]] inline void* RequestPluginAPI( InterfaceVersion a_version ) noexcept
	{
		const auto module = REX::W32::GetModuleHandleW( DLL_NAME );
		if( !module )
			return nullptr;

		const auto request = reinterpret_cast<RequestPluginAPI_t>( REX::W32::GetProcAddress( module, EXPORT_NAME ) );

		return request ? request( a_version ) : nullptr;
	}

	[[nodiscard]] inline IVHitWhereYouAim1* GetAPI1() noexcept
	{
		return static_cast<IVHitWhereYouAim1*>( RequestPluginAPI( InterfaceVersion::kV1 ) );
	}
}
