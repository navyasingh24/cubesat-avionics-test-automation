cmake_minimum_required(VERSION 3.16)

project(CubeSatAvionicsTestAutomation
    VERSION 1.0.0
    DESCRIPTION "Software-in-the-loop CubeSat flight-computer simulator (educational)"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(CUBESAT_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(CUBESAT_ENABLE_SANITIZERS "Build with AddressSanitizer and UBSan (GCC/Clang)" OFF)
option(CUBESAT_SEEDED_DEFECT
    "Compile a deliberate boundary defect to prove the tests detect it. NEVER enable for normal builds."
    OFF)

# ---------------------------------------------------------------------------
# Core library: everything except main(). Shared by the CLI and unit tests.
# ---------------------------------------------------------------------------
add_library(cubesat_core STATIC
    src/cli.cpp
    src/command_processor.cpp
    src/config.cpp
    src/flight_computer.cpp
    src/json.cpp
    src/logger.cpp
    src/safety_monitor.cpp
    src/telemetry.cpp
)
target_include_directories(cubesat_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_definitions(cubesat_core PRIVATE CUBESAT_VERSION="${PROJECT_VERSION}")

if(CUBESAT_SEEDED_DEFECT)
    message(WARNING "CUBESAT_SEEDED_DEFECT=ON: building a DELIBERATELY BROKEN simulator for defect-detection demos")
    target_compile_definitions(cubesat_core PRIVATE CUBESAT_SEEDED_DEFECT=1)
endif()

function(cubesat_apply_warnings target)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
            -Wnon-virtual-dtor -Wold-style-cast -Wnull-dereference -Wformat=2)
        if(CUBESAT_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
        if(CUBESAT_ENABLE_SANITIZERS)
            target_compile_options(${target} PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
            target_link_options(${target} PRIVATE -fsanitize=address,undefined)
        endif()
    elseif(MSVC)
        target_compile_options(${target} PRIVATE /W4)
        if(CUBESAT_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    endif()
endfunction()

cubesat_apply_warnings(cubesat_core)

# ---------------------------------------------------------------------------
# Command-line simulator
# ---------------------------------------------------------------------------
add_executable(cubesat_sim src/main.cpp)
target_link_libraries(cubesat_sim PRIVATE cubesat_core)
cubesat_apply_warnings(cubesat_sim)

# ---------------------------------------------------------------------------
# C++ unit tests (no third-party framework; registered with CTest)
# ---------------------------------------------------------------------------
include(CTest)
if(BUILD_TESTING)
    add_executable(cubesat_unit_tests unit_tests/unit_tests.cpp)
    target_link_libraries(cubesat_unit_tests PRIVATE cubesat_core)
    cubesat_apply_warnings(cubesat_unit_tests)
    add_test(NAME cpp_unit_tests COMMAND cubesat_unit_tests)
endif()
