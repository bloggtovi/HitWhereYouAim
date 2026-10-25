cmake_minimum_required(VERSION 3.21)

# Standalone unit tests for game-independent code. Built separately from the plugin so they need
# neither vcpkg nor CommonLib:
#   cmake -S tests -B build-tests && cmake --build build-tests --config Release && ctest --test-dir build-tests -C Release
project(HitWhereYouAimTests LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

enable_testing()

add_executable(BallisticSolverTests BallisticSolverTests.cpp)
target_include_directories(BallisticSolverTests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../src")
if(MSVC)
	target_compile_options(BallisticSolverTests PRIVATE /W4 /WX /permissive- /utf-8)
endif()

add_test(NAME BallisticSolverTests COMMAND BallisticSolverTests)
