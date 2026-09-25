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
