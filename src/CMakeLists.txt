# Static libraries: spc_core -> spc_http -> spc_models -> spc_api, and the
# spc umbrella that links them all.
#
# Glaze is added as a SYSTEM PRIVATE include directory rather than linked:
# no installed header uses it, and a linked glaze::glaze would have to join
# the export set.

set(SPC_GLAZE_INCLUDE_DIR ${glaze_SOURCE_DIR}/include)

add_library(spc_core STATIC
    core/error.cpp
    core/geometry.cpp
    core/rate_limit.cpp
    core/version.cpp
)
spc_configure_target(spc_core)
target_compile_features(spc_core PUBLIC cxx_std_23)
target_include_directories(spc_core PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_include_directories(spc_core SYSTEM PRIVATE ${SPC_GLAZE_INCLUDE_DIR})

add_library(spc_http STATIC
    http/client.cpp
)
spc_configure_target(spc_http)
target_link_libraries(spc_http PUBLIC spc_core CURL::libcurl)

add_library(spc_models STATIC
    models/convective.cpp
    models/fire_weather.cpp
    models/json.cpp
    models/mesoscale.cpp
    models/outlook.cpp
    models/storm_report.cpp
    models/watch.cpp
)
spc_configure_target(spc_models)
target_link_libraries(spc_models PUBLIC spc_core)
target_include_directories(spc_models PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_include_directories(spc_models SYSTEM PRIVATE ${SPC_GLAZE_INCLUDE_DIR})

add_library(spc_api STATIC
    api/client.cpp
)
spc_configure_target(spc_api)
target_link_libraries(spc_api PUBLIC spc_core spc_http spc_models)
target_include_directories(spc_api PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_include_directories(spc_api SYSTEM PRIVATE ${SPC_GLAZE_INCLUDE_DIR})

add_library(spc INTERFACE)
target_link_libraries(spc INTERFACE spc_core spc_http spc_models spc_api)

# In-tree (FetchContent) consumers get the same spc:: names that
# find_package(spc) provides.
add_library(spc::spc_core ALIAS spc_core)
add_library(spc::spc_http ALIAS spc_http)
add_library(spc::spc_models ALIAS spc_models)
add_library(spc::spc_api ALIAS spc_api)
add_library(spc::spc ALIAS spc)
