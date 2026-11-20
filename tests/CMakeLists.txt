find_package(Threads REQUIRED)

add_executable(spc_tests
    test_arcgis.cpp
    test_client.cpp
    test_client_parity.cpp
    test_corpus.cpp
    test_geometry.cpp
    test_golden.cpp
    test_locale.cpp
    test_parser.cpp
    test_rate_limit.cpp
    test_retry.cpp
)
target_sources(spc_tests PRIVATE test_http_client.cpp)
if(WIN32)
    target_link_libraries(spc_tests PRIVATE ws2_32)
    target_compile_definitions(spc_tests PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX
        _CRT_SECURE_NO_WARNINGS)
endif()

# The https redirect tests need a TLS server, which tests/support builds on
# OpenSSL 3. It is optional and those tests skip without it. The link check
# turns it off where the copy found cannot serve the build, such as a
# universal macOS binary against Homebrew's single-architecture OpenSSL; it
# is not cached, because the architectures can change between configures.
# Windows is left out: its tests would need OpenSSL's DLLs on PATH to start.
unset(SPC_TEST_OPENSSL_LINKS CACHE)
if(NOT WIN32)
    find_package(OpenSSL 3.0 QUIET)
    if(OpenSSL_FOUND)
        include(CheckCXXSourceCompiles)
        set(CMAKE_REQUIRED_LIBRARIES OpenSSL::SSL)
        set(CMAKE_REQUIRED_QUIET ON)
        check_cxx_source_compiles("
            #include <openssl/ssl.h>
            int main() { return SSL_CTX_new(TLS_server_method()) == nullptr ? 1 : 0; }"
            SPC_TEST_OPENSSL_LINKS)
        unset(CMAKE_REQUIRED_LIBRARIES)
        unset(CMAKE_REQUIRED_QUIET)
        if(SPC_TEST_OPENSSL_LINKS)
            target_link_libraries(spc_tests PRIVATE OpenSSL::SSL)
            target_compile_definitions(spc_tests PRIVATE SPC_TEST_HAS_TLS_SERVER)
        endif()
    endif()
endif()
if(NOT SPC_TEST_OPENSSL_LINKS)
    message(STATUS "No usable OpenSSL 3: the https redirect tests will skip")
endif()

spc_set_warnings(spc_tests)
target_link_libraries(spc_tests PRIVATE spc::spc GTest::gtest_main Threads::Threads)
# Tests reach the private JSON helpers in src/models/json.hpp.
target_include_directories(spc_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(spc_tests PRIVATE glaze::glaze)
target_compile_definitions(spc_tests PRIVATE
    SPC_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    SPC_GOLDEN_DIR="${CMAKE_CURRENT_SOURCE_DIR}/golden"
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
