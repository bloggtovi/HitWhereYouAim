#pragma once
#define NOMINMAX
#define NOGDI
#define WIN32_LEAN_AND_MEAN

#include <RE/Skyrim.h>
#include <REX/REX.h>
#include <REX/LOG.h>
#include <SKSE/SKSE.h>

#ifdef NDEBUG
#	include <spdlog/sinks/basic_file_sink.h>
#else
#	include <spdlog/sinks/msvc_sink.h>
#endif

#include <xbyak\xbyak.h>
#include <SimpleIni.h>
#include <MinHook.h>

using namespace std::literals;

namespace Runtime
{
	inline constexpr REL::Version SSE_1_7_99(1, 7, 99, 0);
	inline constexpr REL::Version MIN_ADDRESS_LIBRARY_V5 = SSE_1_7_99;

	inline REL::Version version{};

	[[nodiscard]] inline bool IsAtLeast1_7_99() noexcept
	{
		return version >= Runtime::SSE_1_7_99;
	}
}
