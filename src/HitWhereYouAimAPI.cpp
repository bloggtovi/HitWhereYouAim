#pragma once
#include "Settings.h"
#include "HitWhereYouAimAPI.h"

namespace HWYA_API
{
	class HitWhereYouAimAPI final : public IVHitWhereYouAim1
	{
	public:
		[[nodiscard]] Feature GetEnabledFeatures() const noexcept override
		{
			if( !Settings::bEnabled )
				return Feature::kNone;

			Feature features = Feature::kEnabled;

			if( Settings::bEnabledPitchCorrection )
			{
				features |= Feature::kPitchCorrection;

				if( Settings::iPitchCorrectionMode == 0 )
				{
					features |= Feature::kAutomaticPitchCorrection;

					if( Settings::bTargetDepthPrediction )
						features |= Feature::kTargetDepthPrediction;
				}
				else
				{
					features |= Feature::kFixedPitchCorrection;
				}
			}

			if( Settings::bEnabledTargetLeading )
				features |= Feature::kTargetLeading;

			return features;
		}
	};

	static HitWhereYouAimAPI api;
}

extern "C" _declspec(dllexport) void* RequestPluginAPI( HWYA_API::InterfaceVersion a_version ) noexcept
{
	switch( a_version )
	{
	case HWYA_API::InterfaceVersion::kV1:
		return static_cast<HWYA_API::IVHitWhereYouAim1*>( &HWYA_API::api );
	default:
		return nullptr;
	}
}
