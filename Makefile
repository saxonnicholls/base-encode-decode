# Builds the test suite and benchmark from the command line.
# The Xcode project continues to build the demo (main.cpp) as before.
#
#   make test                 build and run the test suite (SIMD and scalar builds)
#   make bench                build and run the benchmark (default 512 MiB)
#   make bench BENCH_MB=2048  benchmark with a 2 GiB input
#   make demo                 build and run the demo
#   make clean
#
# The library itself only requires C++20; the tests and benchmark build as
# C++23 so the resize_and_overwrite fast path is exercised where available.

CXX ?= clang++
CXXFLAGS ?= -std=gnu++23 -O3 -Wall -Wextra
INCLUDES = -IBaseEncodeDecode -Itests/third_party/cpp-base64 -Itests/third_party

HEADERS = $(wildcard BaseEncodeDecode/*.hpp BaseEncodeDecode/utils/*.hpp)
CPP_BASE64 = tests/third_party/cpp-base64/base64.cpp

BENCH_MB ?= 512

all: build/tests build/tests_scalar build/bench build/demo

build:
	mkdir -p build

build/tests: tests/test_main.cpp $(CPP_BASE64) $(HEADERS) | build
	$(CXX) $(CXXFLAGS) $(INCLUDES) tests/test_main.cpp $(CPP_BASE64) -o $@

# Same suite with the SIMD drop-ins disabled: proves the scalar path alone
build/tests_scalar: tests/test_main.cpp $(CPP_BASE64) $(HEADERS) | build
	$(CXX) $(CXXFLAGS) -DSNICHOLLS_NO_SIMD $(INCLUDES) tests/test_main.cpp $(CPP_BASE64) -o $@

build/bench: benchmarks/benchmark_main.cpp $(CPP_BASE64) $(HEADERS) | build
	$(CXX) $(CXXFLAGS) $(INCLUDES) benchmarks/benchmark_main.cpp $(CPP_BASE64) -o $@

# Same benchmark with the SIMD drop-ins disabled, to measure what they add
build/bench_scalar: benchmarks/benchmark_main.cpp $(CPP_BASE64) $(HEADERS) | build
	$(CXX) $(CXXFLAGS) -DSNICHOLLS_NO_SIMD $(INCLUDES) benchmarks/benchmark_main.cpp $(CPP_BASE64) -o $@

build/demo: BaseEncodeDecode/main.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) $(INCLUDES) BaseEncodeDecode/main.cpp -o $@

test: build/tests build/tests_scalar
	./build/tests
	./build/tests_scalar

bench: build/bench
	./build/bench $(BENCH_MB)

bench-scalar: build/bench_scalar
	./build/bench_scalar $(BENCH_MB)

demo: build/demo
	./build/demo

clean:
	rm -rf build

.PHONY: all test bench bench-scalar demo clean
