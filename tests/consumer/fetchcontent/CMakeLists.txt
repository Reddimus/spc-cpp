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
