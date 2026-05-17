# Layered static libraries: spc_core -> spc_http -> spc_models -> spc_api -> spc
#
# Glaze is header-only and pulled in via include-path only (matching how
# nws-cpp / ncei-cpp integrate it). Linking glaze::glaze directly onto these
# installable static libs trips CMake's "INTERFACE target not in export set".

add_library(spc_core STATIC
    core/error.cpp
    core/geometry.cpp
    core/rate_limit.cpp
)
target_compile_features(spc_core PUBLIC cxx_std_23)
target_include_directories(spc_core PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${glaze_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

add_library(spc_http STATIC
    http/client.cpp
)
target_link_libraries(spc_http PUBLIC spc_core CURL::libcurl)
target_include_directories(spc_http PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

add_library(spc_models STATIC
    models/common.cpp
    models/outlook.cpp
    models/convective.cpp
    models/fire_weather.cpp
    models/watch.cpp
    models/mesoscale.cpp
    models/storm_report.cpp
)
target_link_libraries(spc_models PUBLIC spc_core)
target_include_directories(spc_models PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${glaze_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# API client library: StaticFeed / ArcGIS (+ ArcGISPager) / Archive clients.
add_library(spc_api STATIC
    api/client.cpp
)
target_link_libraries(spc_api PUBLIC spc_core spc_http spc_models)
target_include_directories(spc_api PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${glaze_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# Umbrella INTERFACE library: link spc::spc to get everything.
add_library(spc INTERFACE)
target_link_libraries(spc INTERFACE spc_core spc_http spc_models spc_api)

# Namespaced ALIAS targets so `spc::spc` (and the per-layer `spc::spc_*`)
# resolve IDENTICALLY whether this SDK is consumed via an installed
# `find_package(spc)` (where install(EXPORT ... NAMESPACE spc::) provides
# them) or in-tree via FetchContent (where, without these aliases, only the
# bare `spc` target exists). Downstream code can then always link `spc::spc`.
add_library(spc::spc_core ALIAS spc_core)
add_library(spc::spc_http ALIAS spc_http)
add_library(spc::spc_models ALIAS spc_models)
add_library(spc::spc_api ALIAS spc_api)
add_library(spc::spc ALIAS spc)
