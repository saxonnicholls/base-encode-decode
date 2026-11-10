cmake_minimum_required(VERSION 3.21)
project(BaseEncodeDecode VERSION 1.0.0 LANGUAGES CXX)

# ---------------------------------------------------------------------------
# The library itself: header-only, so an INTERFACE target. Consumers just
#   add_subdirectory(base-encode-decode)   (or FetchContent)
#   target_link_libraries(app PRIVATE snicholls::base_encode_decode)
# ---------------------------------------------------------------------------
# snicholls-core (third_party/core): the vocabulary types, snicholls::overloaded among them. One copy of core per
# build: a consumer that added core first keeps its own (its pin is the version), and this one is skipped.
if(NOT TARGET snicholls::core)
    add_subdirectory(third_party/core EXCLUDE_FROM_ALL)
endif()
snicholls_core_require(0.5.0)

add_library(base_encode_decode INTERFACE)
add_library(snicholls::base_encode_decode ALIAS base_encode_decode)

target_include_directories(base_encode_decode INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/BaseEncodeDecode>
    $<INSTALL_INTERFACE:include>)
target_compile_features(base_encode_decode INTERFACE cxx_std_20)

find_package(Threads REQUIRED) # the parallel API uses std::thread
target_link_libraries(base_encode_decode INTERFACE Threads::Threads snicholls::core)

install(DIRECTORY BaseEncodeDecode/ DESTINATION include FILES_MATCHING PATTERN "*.hpp")

# ---------------------------------------------------------------------------
# Tests, benchmark and demo: only when this is the top-level project
# ---------------------------------------------------------------------------
option(BASE_ENCODE_DECODE_BUILD_TESTS "Build the test suite and benchmark" ${PROJECT_IS_TOP_LEVEL})

if(BASE_ENCODE_DECODE_BUILD_TESTS)
    enable_testing()

    # Tests and benchmark build as C++23 so the resize_and_overwrite fast
    # path is exercised; the library itself only requires C++20
    add_executable(tests tests/test_main.cpp tests/third_party/cpp-base64/base64.cpp)
    target_include_directories(tests PRIVATE tests/third_party/cpp-base64 tests/third_party)
    target_link_libraries(tests PRIVATE snicholls::base_encode_decode)
    target_compile_features(tests PRIVATE cxx_std_23)
    add_test(NAME base_encode_decode_tests COMMAND tests)

    # Same suite with the SIMD drop-ins disabled: proves the scalar path alone
    add_executable(tests_scalar tests/test_main.cpp tests/third_party/cpp-base64/base64.cpp)
    target_include_directories(tests_scalar PRIVATE tests/third_party/cpp-base64 tests/third_party)
    target_link_libraries(tests_scalar PRIVATE snicholls::base_encode_decode)
    target_compile_features(tests_scalar PRIVATE cxx_std_23)
    target_compile_definitions(tests_scalar PRIVATE SNICHOLLS_NO_SIMD)
    add_test(NAME base_encode_decode_tests_scalar COMMAND tests_scalar)

    # The codec-free registry on its own (fails to compile if it pulls in the encoder)
    add_executable(tests_basic_type_registry tests/test_basic_type_registry.cpp)
    target_link_libraries(tests_basic_type_registry PRIVATE snicholls::base_encode_decode)
    add_test(NAME base_encode_decode_tests_basic_type_registry COMMAND tests_basic_type_registry)

    add_executable(bench benchmarks/benchmark_main.cpp tests/third_party/cpp-base64/base64.cpp)
    target_include_directories(bench PRIVATE tests/third_party/cpp-base64)
    target_link_libraries(bench PRIVATE snicholls::base_encode_decode)
    target_compile_features(bench PRIVATE cxx_std_23)

    add_executable(demo BaseEncodeDecode/main.cpp)
    target_link_libraries(demo PRIVATE snicholls::base_encode_decode)
endif()

# One core version per build: a second copy reached through another library is a configure error.
if(PROJECT_IS_TOP_LEVEL)
    snicholls_core_assert_single_version()
endif()
