#pragma once

enum NotificationMode
{
	None,
	Floating,
	Screen,
	Both
};

struct Settings
{
	enum class BindingType
	{
		kBoolean,
		kInteger,
		kFloat,
		kDouble,
		kString,
	};



	struct Binding
	{
		void*				pData;
		BindingType			type;
		std::string_view	section;
		std::string_view	key;
		std::variant
			<bool,
			int32_t,
			float,
			double,
			std::string>	defaults;
	};

	std::map<std::pair<std::string_view,std::string_view>,Binding> _settings;

	static Settings* GetSingleton()
	{
		static Settings singleton;
		return &singleton;
	}

	void RegisterBinding( Binding a_binding )
	{
		auto pair = std::make_pair( a_binding.section, a_binding.key );
		auto [it, inserted] = _settings.try_emplace( pair );

		it->second = a_binding;
	}

	void Load( CSimpleIniA& a_ini )
	{
		for( auto pair : _settings )
		{
			auto& binding = pair.second;
			if( a_ini.KeyExists( binding.section.data(), binding.key.data() ) )
			{
				switch( binding.type )
				{
				case BindingType::kBoolean:
					*reinterpret_cast<bool*>( binding.pData ) = a_ini.GetBoolValue( binding.section.data(), binding.key.data(), std::get<bool>( binding.defaults ) );
					break;
				case BindingType::kInteger:
					*reinterpret_cast<int32_t*>( binding.pData ) = a_ini.GetLongValue( binding.section.data(), binding.key.data(), std::get<int32_t>( binding.defaults ) );
					break;
				case BindingType::kFloat:
					*reinterpret_cast<float*>( binding.pData ) = (float)a_ini.GetDoubleValue( binding.section.data(), binding.key.data(), std::get<float>( binding.defaults ) );
					break;
				case BindingType::kDouble:
					*reinterpret_cast<double*>( binding.pData ) = a_ini.GetDoubleValue( binding.section.data(), binding.key.data(), std::get<double>( binding.defaults ) );
					break;
				case BindingType::kString:
					*reinterpret_cast<std::string*>( binding.pData ) = a_ini.GetValue( binding.section.data(), binding.key.data(), std::get<std::string>( binding.defaults ).c_str() );
					break;
				}
			}
		}
	}

	static void Load();

	// Main functions
	static inline bool		bEnabled;
	static inline bool			bEnabledPitchCorrection;
	static inline uint32_t		iPitchCorrectionMode;
	static inline float				fPitchOffsetDegree;
	static inline bool			bEnabledTargetLeading;
	static inline float				fMaxLeadingAngle;
	static inline bool		bTargetDepthPrediction;
	static inline float		fPredictionWindowWidth;
	static inline float		fPredictionWindowHeight;

	// Calibration
	static inline float		fMaximumTargetDistance;
	static inline float		fArcheryPitchCalibration;
	static inline float		fCrosshairPosX;
	static inline float		fCrosshairPosY;
	static inline float		fRayPenetrationDepth;

	// 3rd-person angle correction
	static inline float		fPitchBias;
	static inline float		fHeightBias;
	static inline float		fBiasFadingStart;
	static inline float		fMaxPitchAdjustmentSpeed;
	static inline float		fMountedYawTolerance;

	// Debug
	static inline bool		bEnableLogging;
	static inline bool		bShowTargetPredictionInfo;
	static inline bool		bShowCrosshairRayImpact;
	static inline bool		bShowLastProjectilePath;
	static inline bool		bEnabledTargetPrediction;
};

