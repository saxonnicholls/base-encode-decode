//
//  benchmark_main.cpp
//  BaseEncodeDecode throughput benchmark
//
//  Measures serial vs parallel encode/decode throughput on a large
//  pseudo-random input, with ReneNyffenegger/cpp-base64 as an external
//  reference point for Base64.
//
//  Usage: bench [size_in_MiB]   (default 512; use 1024+ for multi-GB runs)
//
//  Rates are GB/s of raw binary bytes (input side for encode, output side
//  for decode); timings include allocation of the result buffer, i.e. what
//  a caller actually experiences. Best of 3 runs after a warm-up.
//

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "encode_decode_base_whatever.hpp"
#include "base64.h" // ReneNyffenegger/cpp-base64

using namespace snicholls;
using Clock = std::chrono::steady_clock;

static volatile size_t g_sink = 0; // keeps the optimizer honest

template<typename F>
static double BestSeconds(F&& fn, int iterations = 3) {
    double best = 1e100;
    for (int i = 0; i < iterations; ++i) {
        const auto start = Clock::now();
        fn();
        const auto stop = Clock::now();
        best = std::min(best, std::chrono::duration<double>(stop - start).count());
    }
    return best;
}

static double ToGBps(size_t bytes, double seconds) {
    return static_cast<double>(bytes) / seconds / 1e9;
}

static void PrintRow(const char* label, size_t rawBytes, double encodeSeconds, double decodeSeconds,
                     double encodeBaseline = 0.0, double decodeBaseline = 0.0) {
    std::printf("  %-24s encode %7.2f GB/s   decode %7.2f GB/s",
                label, ToGBps(rawBytes, encodeSeconds), ToGBps(rawBytes, decodeSeconds));
    if (encodeBaseline > 0.0) {
        std::printf("   (%.1fx / %.1fx vs serial)", encodeBaseline / encodeSeconds, decodeBaseline / decodeSeconds);
    }
    std::printf("\n");
}

template<typename EncSerial, typename DecSerial, typename EncParallel, typename DecParallel>
static void BenchScheme(const char* name, const Binary& data,
                        EncSerial encodeSerial, DecSerial decodeSerial,
                        EncParallel encodeParallel, DecParallel decodeParallel) {
    std::printf("%s\n", name);

    // Correctness gate before timing anything
    const std::string encoded = encodeSerial(data);
    if (encodeParallel(data, 0) != encoded || decodeSerial(encoded) != data || decodeParallel(encoded, 0) != data) {
        std::printf("  VERIFICATION FAILED - parallel and serial outputs differ\n");
        std::exit(1);
    }

    const double encS = BestSeconds([&] { g_sink += encodeSerial(data).size(); });
    const double decS = BestSeconds([&] { g_sink += decodeSerial(encoded).size(); });
    PrintRow("serial", data.size(), encS, decS);

    const double encP = BestSeconds([&] { g_sink += encodeParallel(data, 0).size(); });
    const double decP = BestSeconds([&] { g_sink += decodeParallel(encoded, 0).size(); });
    PrintRow("parallel (auto)", data.size(), encP, decP, encS, decS);
}

int main(int argc, char** argv) {
    size_t mib = 512;
    if (argc > 1) {
        mib = static_cast<size_t>(std::strtoull(argv[1], nullptr, 10));
        if (mib == 0) {
            std::printf("usage: %s [size_in_MiB]\n", argv[0]);
            return 1;
        }
    }
    const size_t size = mib << 20;

    std::printf("BaseEncodeDecode throughput benchmark\n");
    std::printf("input: %zu MiB pseudo-random bytes, hardware threads: %u\n",
                mib, std::thread::hardware_concurrency());
    std::printf("rates in GB/s of raw binary bytes, best of 3 runs\n\n");

    // Deterministic pseudo-random fill (xorshift64), 8 bytes at a time
    Binary data(size);
    uint64_t state = 0x9E3779B97F4A7C15ull;
    for (size_t i = 0; i + 8 <= size; i += 8) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        std::memcpy(&data[i], &state, 8);
    }

    BenchScheme("Base64", data,
                [](const Binary& d) { return EncodeBase64Binary(d); },
                [](const std::string& s) { return DecodeBase64Binary(s); },
                [](const Binary& d, unsigned t) { return EncodeBase64BinaryParallel(d, t); },
                [](const std::string& s, unsigned t) { return DecodeBase64BinaryParallel(s, t); });

    // External reference: cpp-base64 (serial, Base64 only)
    {
        const std::string encoded = EncodeBase64Binary(data);
        const double enc = BestSeconds([&] { g_sink += base64_encode(data.data(), data.size()).size(); });
        const double dec = BestSeconds([&] { g_sink += base64_decode(encoded).size(); });
        PrintRow("cpp-base64 (reference)", data.size(), enc, dec);
    }
    std::printf("\n");

    BenchScheme("Base32", data,
                [](const Binary& d) { return EncodeBase32Binary(d); },
                [](const std::string& s) { return DecodeBase32Binary(s); },
                [](const Binary& d, unsigned t) { return EncodeBase32BinaryParallel(d, t); },
                [](const std::string& s, unsigned t) { return DecodeBase32BinaryParallel(s, t); });
    std::printf("\n");

    BenchScheme("Base16", data,
                [](const Binary& d) { return EncodeBase16Binary(d); },
                [](const std::string& s) { return DecodeBase16Binary(s); },
                [](const Binary& d, unsigned t) { return EncodeBase16BinaryParallel(d, t); },
                [](const std::string& s, unsigned t) { return DecodeBase16BinaryParallel(s, t); });

    return 0;
}
