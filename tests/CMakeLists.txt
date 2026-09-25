find_package(Threads REQUIRED)

add_executable(spc_tests
    test_arcgis.cpp
    test_client.cpp
    test_corpus.cpp
    test_geometry.cpp
    test_locale.cpp
    test_parser.cpp
    test_rate_limit.cpp
    test_retry.cpp
)
# The loopback HTTP server uses POSIX sockets.
if(NOT WIN32)
    target_sources(spc_tests PRIVATE test_http_client.cpp)
endif()

spc_set_warnings(spc_tests)
target_link_libraries(spc_tests PRIVATE spc::spc GTest::gtest_main Threads::Threads)
# Tests reach the private JSON helpers in src/models/json.hpp.
target_include_directories(spc_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_include_directories(spc_tests SYSTEM PRIVATE ${glaze_SOURCE_DIR}/include)
target_compile_definitions(spc_tests PRIVATE
    SPC_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    SPC_PROJECT_VERSION="${PROJECT_VERSION}"
)

include(GoogleTest)
gtest_discover_tests(spc_tests DISCOVERY_TIMEOUT 30)

# The allocation counter replaces operator new and delete, as the sanitizers
# do, so it gets its own binary and sanitizer builds leave it out.
if(NOT SPC_ENABLE_SANITIZERS AND NOT SPC_ENABLE_THREAD_SANITIZER)
    add_executable(spc_allocation_tests
        test_allocation_counter.cpp
        support/allocation_counter.cpp
    )
    spc_set_warnings(spc_allocation_tests)
    target_link_libraries(spc_allocation_tests PRIVATE GTest::gtest_main)
    target_include_directories(spc_allocation_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    gtest_discover_tests(spc_allocation_tests DISCOVERY_TIMEOUT 30)
endif()

# Each public header must compile on its own, with no Glaze on the include path.
file(GLOB_RECURSE spc_public_headers CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/include/spc/*.hpp)
set(spc_header_check_sources)
foreach(header IN LISTS spc_public_headers)
    file(RELATIVE_PATH relative ${PROJECT_SOURCE_DIR}/include ${header})
    string(MAKE_C_IDENTIFIER ${relative} name)
    set(source ${CMAKE_CURRENT_BINARY_DIR}/header_check/${name}.cpp)
    file(CONFIGURE OUTPUT ${source} CONTENT "#include \"${relative}\"\n")
    list(APPEND spc_header_check_sources ${source})
endforeach()
add_library(spc_header_check OBJECT ${spc_header_check_sources})
target_link_libraries(spc_header_check PRIVATE spc::spc)
spc_set_warnings(spc_header_check)
