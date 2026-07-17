# Builds the test suite and benchmark from the command line.
# The Xcode project continues to build the demo (main.cpp) as before.
#
#   make test                 build and run the test suite
#   make bench                build and run the benchmark (default 512 MiB)
#   make bench BENCH_MB=2048  benchmark with a 2 GiB input
#   make demo                 build and run the demo
#   make clean

CXX ?= clang++
CXXFLAGS ?= -std=gnu++20 -O3 -Wall -Wextra
INCLUDES = -IBaseEncodeDecode -Itests/third_party/cpp-base64

HEADERS = BaseEncodeDecode/encode_decode_base_whatever.hpp BaseEncodeDecode/alphabet.hpp
CPP_BASE64 = tests/third_party/cpp-base64/base64.cpp

BENCH_MB ?= 512

all: build/tests build/bench build/demo

build:
	mkdir -p build

build/tests: tests/test_main.cpp $(CPP_BASE64) $(HEADERS) | build
	$(CXX) $(CXXFLAGS) $(INCLUDES) tests/test_main.cpp $(CPP_BASE64) -o $@

build/bench: benchmarks/benchmark_main.cpp $(CPP_BASE64) $(HEADERS) | build
	$(CXX) $(CXXFLAGS) $(INCLUDES) benchmarks/benchmark_main.cpp $(CPP_BASE64) -o $@

build/demo: BaseEncodeDecode/main.cpp $(HEADERS) | build
	$(CXX) $(CXXFLAGS) $(INCLUDES) BaseEncodeDecode/main.cpp -o $@

test: build/tests
	./build/tests

bench: build/bench
	./build/bench $(BENCH_MB)

demo: build/demo
	./build/demo

clean:
	rm -rf build

.PHONY: all test bench demo clean
