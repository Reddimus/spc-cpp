cmake_minimum_required(VERSION 3.20)
project(spc-installed-consumer LANGUAGES CXX)

find_package(spc 0.2 REQUIRED)

add_executable(spc_installed_consumer ../main.cpp)
target_link_libraries(spc_installed_consumer PRIVATE spc::spc)
target_compile_features(spc_installed_consumer PRIVATE cxx_std_23)
