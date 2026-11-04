cmake_minimum_required(VERSION 3.31)
project(spc-fetchcontent-consumer LANGUAGES CXX)

include(FetchContent)
FetchContent_Declare(spc_cpp SOURCE_DIR "${SPC_SOURCE_DIR}")
FetchContent_MakeAvailable(spc_cpp)

# As a subproject, spc-cpp builds only its libraries.
if(TARGET spc_tests OR TARGET example_arcgis)
    message(FATAL_ERROR "spc-cpp built its tests or examples as a subproject")
endif()

add_executable(spc_fetchcontent_consumer ../main.cpp)
target_link_libraries(spc_fetchcontent_consumer PRIVATE spc::spc)

# tools/test_consumers.sh passes the README's quick start, to compile only.
if(SPC_QUICK_START)
    add_executable(spc_quick_start "${SPC_QUICK_START}")
    target_link_libraries(spc_quick_start PRIVATE spc::spc)
endif()

add_library(spc_fetchcontent_shared SHARED ../shared.cpp)
set_target_properties(spc_fetchcontent_shared PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_link_libraries(spc_fetchcontent_shared PRIVATE spc::spc)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(spc_fetchcontent_shared PRIVATE "LINKER:-z,defs")
endif()
