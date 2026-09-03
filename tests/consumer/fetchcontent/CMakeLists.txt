cmake_minimum_required(VERSION 3.20)
project(spc-fetchcontent-consumer LANGUAGES CXX)

include(FetchContent)
set(SPC_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(SPC_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(spc_cpp SOURCE_DIR "${SPC_SOURCE_DIR}")
FetchContent_MakeAvailable(spc_cpp)

add_executable(spc_fetchcontent_consumer ../main.cpp)
target_link_libraries(spc_fetchcontent_consumer PRIVATE spc::spc)
target_compile_features(spc_fetchcontent_consumer PRIVATE cxx_std_23)
