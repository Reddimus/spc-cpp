# Built only with SPC_BUILD_BENCHMARKS and never installed. Like the tests,
# the benchmarks reach the private JSON helpers in src/models/json.hpp.
add_executable(spc_benchmarks
    benchmarks.cpp
    ${PROJECT_SOURCE_DIR}/tests/support/allocation_counter.cpp
)
spc_set_warnings(spc_benchmarks)
target_link_libraries(spc_benchmarks PRIVATE spc::spc benchmark::benchmark)
target_include_directories(spc_benchmarks PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/tests
)
target_include_directories(spc_benchmarks SYSTEM PRIVATE ${glaze_SOURCE_DIR}/include)
target_compile_definitions(spc_benchmarks PRIVATE
    SPC_FIXTURES_DIR="${PROJECT_SOURCE_DIR}/tests/fixtures"
)
