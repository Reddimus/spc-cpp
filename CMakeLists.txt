cmake_minimum_required(VERSION 3.20)
project(spc-cpp VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Options
option(SPC_BUILD_TESTS "Build tests" ON)
option(SPC_BUILD_EXAMPLES "Build examples" ON)
option(SPC_ENABLE_LTO "Enable Link Time Optimization" ON)
option(SPC_NATIVE_ARCH "Use -march=native for CPU-specific tuning" OFF)
option(SPC_TUNE_X86_64_V3
    "Build Release with -march=x86-64-v3 (AVX2/BMI2/FMA). Makes the artifact non-portable." OFF)
option(SPC_ENABLE_CLANG_TIDY "Run clang-tidy while compiling project targets" OFF)
option(SPC_WARNINGS_AS_ERRORS "Treat project warnings as errors" OFF)

if(SPC_ENABLE_CLANG_TIDY)
    find_program(SPC_CLANG_TIDY_EXECUTABLE NAMES clang-tidy REQUIRED)
endif()

function(spc_configure_target target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
        if(SPC_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(SPC_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
    if(SPC_ENABLE_CLANG_TIDY)
        set_property(TARGET ${target} PROPERTY CXX_CLANG_TIDY
            "${SPC_CLANG_TIDY_EXECUTABLE};--warnings-as-errors=*")
    endif()
endfunction()

# Aggressive optimization flags for Release builds. MSVC has its own
# Release defaults; only override for GCC/Clang.
if(NOT MSVC)
    set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")
endif()

# Enable LTO if requested and supported
if(SPC_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT LTO_SUPPORTED OUTPUT LTO_ERROR)
    if(LTO_SUPPORTED)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)
        message(STATUS "LTO enabled for Release builds")
    else()
        message(WARNING "LTO not supported: ${LTO_ERROR}")
    endif()
endif()

# CPU-specific tuning. GCC/Clang only.
if(NOT MSVC)
    if(SPC_NATIVE_ARCH)
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -march=native")
        message(STATUS "Using -march=native for CPU-specific optimizations")
    elseif(SPC_TUNE_X86_64_V3)
        # Opt-in only. check_cxx_compiler_flag proves the *compiler* accepts
        # the flag, never that the *run* host has AVX2/BMI2/FMA -- and this is
        # an installable SDK (install(EXPORT) + find_package(spc)), so the
        # build host and the run host are routinely different. Defaulting this
        # on made every stock Release build SIGILL on pre-Haswell / pre-Zen
        # hardware and inside VMs that do not expose AVX2.
        include(CheckCXXCompilerFlag)
        check_cxx_compiler_flag("-march=x86-64-v3" COMPILER_SUPPORTS_X86_64_V3)
        if(COMPILER_SUPPORTS_X86_64_V3)
            set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -march=x86-64-v3")
            message(STATUS "Release builds target x86-64-v3; the artifact is not portable")
        else()
            message(WARNING "SPC_TUNE_X86_64_V3 requested but the compiler rejects the flag")
            set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -mtune=generic")
        endif()
    else()
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -mtune=generic")
    endif()
endif()

# Sanitizer support
option(SPC_ENABLE_SANITIZERS "Enable AddressSanitizer and UBSanitizer" OFF)
option(SPC_ENABLE_THREAD_SANITIZER "Enable ThreadSanitizer" OFF)
if(SPC_ENABLE_SANITIZERS AND SPC_ENABLE_THREAD_SANITIZER)
    message(FATAL_ERROR "AddressSanitizer and ThreadSanitizer require separate builds")
endif()
if(SPC_ENABLE_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
    message(STATUS "Sanitizers enabled (ASan + UBSan)")
endif()
if(SPC_ENABLE_THREAD_SANITIZER)
    if(MSVC)
        message(FATAL_ERROR "ThreadSanitizer is not supported by this MSVC configuration")
    endif()
    add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
    add_link_options(-fsanitize=thread)
    message(STATUS "ThreadSanitizer enabled")
endif()

# Code coverage support
option(SPC_ENABLE_COVERAGE "Enable code coverage instrumentation (gcov)" OFF)
if(SPC_ENABLE_COVERAGE)
    add_compile_options(--coverage -fprofile-arcs -ftest-coverage)
    add_link_options(--coverage)
    message(STATUS "Code coverage enabled")
endif()

# Find dependencies
find_package(CURL REQUIRED)

include(FetchContent)

# Glaze - JSON library (compile-time reflection + a generic AST). spc-cpp
# parses through `glz::generic` because SPC's properties block is shape-loose
# (case-variant `LABEL`/`label`/`dn`, numeric-as-string) and geometry is
# polymorphic (Polygon/MultiPolygon). This DIVERGES from the nlohmann-based
# nws-cpp / ncei-cpp SDKs on purpose (see README + CHANGELOG). The convective
# parse path is lifted verbatim from the internal spc-data service.
# License: MIT. Pinned to the same v8.3.0 the rest of the estate uses so a
# combined FetchContent build dedupes to a single glaze::glaze target.
FetchContent_Declare(
    glaze
    GIT_REPOSITORY https://github.com/stephenberry/glaze.git
    GIT_TAG v8.3.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glaze)

# Include directories
include_directories(${PROJECT_SOURCE_DIR}/include)

# Add subdirectories
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

if(SPC_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

# Install targets
set(SPC_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/spc")

install(TARGETS spc_core spc_http spc_models spc_api spc
    EXPORT spcTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)
install(DIRECTORY include/spc DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
# Public model headers use Glaze's generic JSON types. Install the pinned
# headers so find_package consumers do not need a second package manager.
install(DIRECTORY ${glaze_SOURCE_DIR}/include/glaze DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES ${glaze_SOURCE_DIR}/LICENSE
    DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/spc-cpp
    RENAME glaze-LICENSE
)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/spcConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/spcConfig.cmake
    INSTALL_DESTINATION ${SPC_INSTALL_CMAKEDIR}
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/spcConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)
export(EXPORT spcTargets
    FILE ${CMAKE_CURRENT_BINARY_DIR}/spcTargets.cmake
    NAMESPACE spc::
)
install(EXPORT spcTargets
    FILE spcTargets.cmake
    NAMESPACE spc::
    DESTINATION ${SPC_INSTALL_CMAKEDIR}
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/spcConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/spcConfigVersion.cmake
    DESTINATION ${SPC_INSTALL_CMAKEDIR}
)
