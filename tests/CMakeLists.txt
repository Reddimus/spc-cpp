add_executable(spc_tests
    test_geometry.cpp
    test_parser.cpp
    test_corpus.cpp
    test_arcgis.cpp
)
target_link_libraries(spc_tests PRIVATE
    spc_core spc_models spc_api
    GTest::gtest_main
)
target_compile_definitions(spc_tests PRIVATE
    SPC_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
)
target_compile_options(spc_tests PRIVATE -Wall -Wextra -Wpedantic)
include(GoogleTest)
gtest_discover_tests(spc_tests)
