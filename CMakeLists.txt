# Glaze 8.3 needs 3.31.
cmake_minimum_required(VERSION 3.31)

# include/spc/version.hpp holds the version, so builds without CMake get it too.
file(STRINGS ${CMAKE_CURRENT_SOURCE_DIR}/include/spc/version.hpp spc_version_lines
    REGEX "^#define SPC_VERSION_(MAJOR|MINOR|PATCH) [0-9]+$")
foreach(line IN LISTS spc_version_lines)
    string(REGEX MATCH "SPC_VERSION_([A-Z]+) ([0-9]+)" _ "${line}")
    set(spc_version_${CMAKE_MATCH_1} ${CMAKE_MATCH_2})
endforeach()
project(spc-cpp
    VERSION ${spc_version_MAJOR}.${spc_version_MINOR}.${spc_version_PATCH}
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(PROJECT_IS_TOP_LEVEL)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Tests, examples, and install rules default on only for a top-level build,
# so a FetchContent consumer gets just the libraries.
option(SPC_BUILD_TESTS "Build the unit tests" ${PROJECT_IS_TOP_LEVEL})
option(SPC_BUILD_EXAMPLES "Build the examples" ${PROJECT_IS_TOP_LEVEL})
option(SPC_INSTALL "Generate install and package-config rules" ${PROJECT_IS_TOP_LEVEL})
# Off even at top level, since it fetches Google Benchmark. Never installed.
option(SPC_BUILD_BENCHMARKS "Build the benchmarks" OFF)
# Off by default: LTO puts compiler-specific bitcode in the static libraries,
# which a consumer on another compiler or version cannot link.
option(SPC_ENABLE_LTO "Enable link-time optimization for Release builds" OFF)
option(SPC_ENABLE_CLANG_TIDY "Run clang-tidy while compiling the libraries" OFF)
option(SPC_WARNINGS_AS_ERRORS "Treat project warnings as errors" OFF)
option(SPC_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(SPC_ENABLE_THREAD_SANITIZER "Enable ThreadSanitizer" OFF)
option(SPC_ENABLE_COVERAGE "Enable gcov coverage instrumentation" OFF)

if(SPC_ENABLE_CLANG_TIDY)
    find_program(SPC_CLANG_TIDY_EXECUTABLE NAMES clang-tidy REQUIRED)
endif()

# Warnings for any project target.
function(spc_set_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 $<$<BOOL:${SPC_WARNINGS_AS_ERRORS}>:/WX>)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic
            $<$<BOOL:${SPC_WARNINGS_AS_ERRORS}>:-Werror>)
    endif()
endfunction()

# Warnings plus clang-tidy, for the libraries.
function(spc_configure_target target)
    spc_set_warnings(${target})
    if(SPC_ENABLE_CLANG_TIDY)
        set_property(TARGET ${target} PROPERTY CXX_CLANG_TIDY
            "${SPC_CLANG_TIDY_EXECUTABLE};--warnings-as-errors=*")
    endif()
endfunction()

if(SPC_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT SPC_LTO_SUPPORTED OUTPUT SPC_LTO_ERROR)
    if(SPC_LTO_SUPPORTED)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
    else()
        message(WARNING "LTO not supported: ${SPC_LTO_ERROR}")
    endif()
endif()

if(SPC_ENABLE_SANITIZERS AND SPC_ENABLE_THREAD_SANITIZER)
    message(FATAL_ERROR "AddressSanitizer and ThreadSanitizer need separate builds")
endif()
if((SPC_ENABLE_SANITIZERS OR SPC_ENABLE_THREAD_SANITIZER OR SPC_ENABLE_COVERAGE) AND MSVC)
    message(FATAL_ERROR "Sanitizer and coverage options support GCC and Clang only")
endif()
if(SPC_ENABLE_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
endif()
if(SPC_ENABLE_THREAD_SANITIZER)
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
endif()
if(SPC_ENABLE_COVERAGE)
    add_compile_options(--coverage)
    add_link_options(--coverage)
endif()

# CURLOPT_PROTOCOLS_STR, which keeps the transport on http and https, needs 7.85.
find_package(CURL 7.85 REQUIRED)

include(FetchContent)

# Glaze reads SPC's loosely shaped JSON. It is a private build dependency: no
# installed header includes it. kalshi-cpp pins the same version, so a build
# that fetches both SDKs gets a single glaze target.
FetchContent_Declare(
    glaze
    GIT_REPOSITORY https://github.com/stephenberry/glaze.git
    GIT_TAG v8.3.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glaze)

add_subdirectory(src)

if(SPC_BUILD_TESTS)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.18.0
        GIT_SHALLOW TRUE
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
    enable_testing()
    add_subdirectory(tests)
endif()

if(SPC_BUILD_BENCHMARKS)
    FetchContent_Declare(
        benchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG v1.9.5
        GIT_SHALLOW TRUE
        SYSTEM
        EXCLUDE_FROM_ALL
    )
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(benchmark)
    add_subdirectory(benchmarks)
endif()

if(SPC_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(SPC_INSTALL)
    set(SPC_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/spc")

    install(TARGETS spc_core spc_http spc_models spc_api spc
        EXPORT spcTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
    install(DIRECTORY include/spc DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    # The static libraries contain compiled Glaze code, so ship its license.
    install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/spc-cpp)
    install(FILES ${glaze_SOURCE_DIR}/LICENSE
        DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/spc-cpp
        RENAME glaze-LICENSE
    )

    configure_package_config_file(
        ${PROJECT_SOURCE_DIR}/cmake/spcConfig.cmake.in
        ${PROJECT_BINARY_DIR}/spcConfig.cmake
        INSTALL_DESTINATION ${SPC_INSTALL_CMAKEDIR}
    )
    # Minor releases may break the API while the major version is 0.
    write_basic_package_version_file(
        ${PROJECT_BINARY_DIR}/spcConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMinorVersion
    )
    export(EXPORT spcTargets
        FILE ${PROJECT_BINARY_DIR}/spcTargets.cmake
        NAMESPACE spc::
    )
    install(EXPORT spcTargets
        FILE spcTargets.cmake
        NAMESPACE spc::
        DESTINATION ${SPC_INSTALL_CMAKEDIR}
    )
    install(FILES
        ${PROJECT_BINARY_DIR}/spcConfig.cmake
        ${PROJECT_BINARY_DIR}/spcConfigVersion.cmake
        DESTINATION ${SPC_INSTALL_CMAKEDIR}
    )
endif()
