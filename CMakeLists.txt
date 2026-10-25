cmake_minimum_required(VERSION 3.21)

set(VCPKG_OVERLAY_PORTS "")

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ports")
	list(APPEND VCPKG_OVERLAY_PORTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/ports")
endif()

message(STATUS "vcpkg overlay ports: ${VCPKG_OVERLAY_PORTS}")

set(PLUGIN_NAME "HitWhereYouAim")
set(PLUGIN_VERSION "1.5.0")
set(PLUGIN_AUTHOR "Tossapon")
set(PLUGIN_DESCRIPTION "A crosshair aim correction plugin")

option(COPY_BUILD "Copy build output to configured plugin directories" OFF)
option(BUILD_TESTS "Build tests" OFF)

project(${PLUGIN_NAME} VERSION ${PLUGIN_VERSION} LANGUAGES CXX)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
	message(FATAL_ERROR "In-source builds are not supported. Use a build directory or CMake preset.")
endif()

if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
	set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE FILEPATH "")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)

# CommonLibVR supports one binary with SE/AE/VR runtime detection.
set(COMMONLIB_PATH "${CMAKE_CURRENT_SOURCE_DIR}/extern/CommonLibSSE" CACHE PATH "Path to CommonLib")
if(NOT EXISTS "${COMMONLIB_PATH}/CMakeLists.txt")
	message(FATAL_ERROR "CommonLib was not found at ${COMMONLIB_PATH}. Run: git submodule update --init --recursive")
endif()

set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
# PCH.h uses Trampoline::allocate(Xbyak::CodeGenerator&), which CommonLib only declares with this on
set(SKSE_SUPPORT_XBYAK ON CACHE BOOL "" FORCE)
add_subdirectory("${COMMONLIB_PATH}" "${CMAKE_BINARY_DIR}/CommonLib")

find_package(commonlib-shared CONFIG REQUIRED)
find_package(minhook CONFIG REQUIRED)

configure_file(
	${CMAKE_CURRENT_SOURCE_DIR}/cmake/Version.h.in
	${CMAKE_CURRENT_BINARY_DIR}/include/Version.h
	@ONLY
)

configure_file(
	${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.rc.in
	${CMAKE_CURRENT_BINARY_DIR}/version.rc
	@ONLY
)

set(sources ${headers}
	src/PCH.h
	src/Hooks.h
	src/Settings.h
	src/BallisticSolver.h
	src/Utils.h
	src/HitWhereYouAimAPI.h
	src/TrueHUDAPI.h
	src/TrueHUDArrowTrail.h
	src/TargetLock.h
	src/ProjectileDiagnostics.h
	src/ProjectileGuidance.h
	src/GuidanceMath.h
)

set(sources ${sources}
	src/PCH.cpp
	src/main.cpp
	src/Hooks.cpp
	src/Settings.cpp
	src/HitWhereYouAimAPI.cpp
)

source_group(
	TREE
	${CMAKE_CURRENT_SOURCE_DIR}
	FILES
	${headers}
	${sources}
)

source_group(
	TREE
	${CMAKE_CURRENT_BINARY_DIR}
	FILES
	${CMAKE_CURRENT_BINARY_DIR}/include/Version.h
)

# ---- Create DLL ----
add_library(
	${PROJECT_NAME}
	SHARED
	${headers}
	${sources}
	${CMAKE_CURRENT_BINARY_DIR}/include/Version.h
	${CMAKE_CURRENT_BINARY_DIR}/version.rc
	.clang-format
	.editorconfig
)

set(
	DEBUG_VISUALIZERS
	${CMAKE_CURRENT_SOURCE_DIR}/debug/CommonLibSSE-NG.natvis
)

target_sources(
	${PROJECT_NAME}
	PRIVATE
		${DEBUG_VISUALIZERS}
)

source_group(
	"Debugger Visualizers"
	FILES
		${DEBUG_VISUALIZERS}
)

target_compile_features(
	${PROJECT_NAME}
	PRIVATE
	cxx_std_23
)

target_compile_definitions(
	${PROJECT_NAME}
	PRIVATE
	_UNICODE
)

target_include_directories(
	${PROJECT_NAME}
	PRIVATE
	${CMAKE_CURRENT_BINARY_DIR}/include
	${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(
	${PROJECT_NAME}
	PRIVATE
	CommonLibSSE::CommonLibSSE
	commonlib-shared::commonlib-shared
	minhook::minhook
)

target_precompile_headers(
	${PROJECT_NAME}
	PRIVATE
	src/PCH.h
)

if(MSVC)
	target_compile_options(${PROJECT_NAME} 
		PRIVATE 
		/MP 
		/W4 
		$<$<CONFIG:Release>:/WX>
		/utf-8 
		/permissive- 
		/Zi
		/Zc:preprocessor 
		/external:W0
	)
	
	target_link_options(
		${PROJECT_NAME}
		PRIVATE
		"$<$<CONFIG:DEBUG>:/INCREMENTAL;/OPT:NOREF;/OPT:NOICF>"
		"$<$<CONFIG:RELEASE>:/INCREMENTAL:NO;/OPT:REF;/OPT:ICF;/DEBUG:FULL>"
	)
endif()

# Semicolon-separated destination directories, each normally ending in SKSE/Plugins.
set(SKYRIM_PLUGIN_TARGETS "$ENV{SkyrimPluginTargets}" CACHE STRING "Output plugin directories")
if(COPY_BUILD)
	if(SKYRIM_PLUGIN_TARGETS STREQUAL "")
		message(WARNING "COPY_BUILD is enabled, but SKYRIM_PLUGIN_TARGETS is empty")
	else()
		foreach(TARGET_DIR IN LISTS SKYRIM_PLUGIN_TARGETS)
			add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
				COMMAND ${CMAKE_COMMAND} -E make_directory "${TARGET_DIR}/"
				COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${TARGET_DIR}/"
				COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_PDB_FILE:${PROJECT_NAME}>" "${TARGET_DIR}/"
			)
		endforeach()
	endif()
endif()

# Ready-to-install mod layout in <build>/package/<config>/Data, including the default MCM settings
set(PACKAGE_DATA_DIR "${CMAKE_BINARY_DIR}/package/$<CONFIG>/Data")
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E make_directory "${PACKAGE_DATA_DIR}/SKSE/Plugins"
	COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:${PROJECT_NAME}>" "${PACKAGE_DATA_DIR}/SKSE/Plugins/"
	COMMAND ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/dist/Data" "${PACKAGE_DATA_DIR}"
)
