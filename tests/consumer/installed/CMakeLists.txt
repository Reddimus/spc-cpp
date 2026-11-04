cmake_minimum_required(VERSION 3.21)
project(spc-installed-consumer LANGUAGES CXX)

set(SPC_EXPECTED_VERSION "" CACHE STRING "Version to request from find_package(spc)")
find_package(spc ${SPC_EXPECTED_VERSION} REQUIRED)

add_executable(spc_installed_consumer ../main.cpp)
target_link_libraries(spc_installed_consumer PRIVATE spc::spc)

add_library(spc_installed_shared SHARED ../shared.cpp)
set_target_properties(spc_installed_shared PROPERTIES WINDOWS_EXPORT_ALL_SYMBOLS ON)
target_link_libraries(spc_installed_shared PRIVATE spc::spc)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(spc_installed_shared PRIVATE "LINKER:-z,defs")
endif()
