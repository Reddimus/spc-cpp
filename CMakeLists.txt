cmake_minimum_required(VERSION 3.20)
project(spc-cpp VERSION 0.1.0 LANGUAGES CXX)

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
    else()
        include(CheckCXXCompilerFlag)
        check_cxx_compiler_flag("-march=x86-64-v3" COMPILER_SUPPORTS_X86_64_V3)
        if(COMPILER_SUPPORTS_X86_64_V3)
            set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -march=x86-64-v3")
        else()
            set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -mtune=generic")
        endif()
    endif()
endif()

# Sanitizer support
option(SPC_ENABLE_SANITIZERS "Enable AddressSanitizer and UBSanitizer" OFF)
if(SPC_ENABLE_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
    message(STATUS "Sanitizers enabled (ASan + UBSan)")
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
# License: MIT. Pinned to the same v7.6.0 the rest of the estate uses so a
# combined FetchContent build dedupes to a single glaze::glaze target.
FetchContent_Declare(
    glaze
    GIT_REPOSITORY https://github.com/stephenberry/glaze.git
    GIT_TAG v7.6.0
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
        GIT_TAG v1.15.2
        GIT_SHALLOW TRUE
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
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
