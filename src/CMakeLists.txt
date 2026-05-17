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
)
target_link_libraries(spc_models PUBLIC spc_core)
target_include_directories(spc_models PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${glaze_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# API client library. PR-1 ships a placeholder TU so the layered target /
# install-export shape is locked from the start; the StaticFeed / ArcGIS /
# Archive clients land in PR-2.
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
