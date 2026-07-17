//
//  test_main.cpp
//  BaseEncodeDecode test suite
//
//  Deliberately lightweight: no test framework, just <cassert>.
//
//  Validates:
//   - RFC 4648 section 10 test vectors (Base64, Base32, Base32Hex, Base16)
//   - RFC 4648 padding rules ('=' to a full block, only as trailing padding)
//   - Round trips for every scheme, string and Binary APIs, lengths 0..64
//   - Generic ByteSource inputs (string_view, vector<char>, span, arrays, std::byte)
//   - Parallel functions produce bit-identical output to the serial ones
//   - Cross-validation against ReneNyffenegger/cpp-base64
//   - BSD <bitstring.h> interop round trips
//   - Compile-time (constexpr/consteval) evaluation via static_assert
//

// The tests are assert()-based; make sure they survive Release builds
#undef NDEBUG

#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "encode_decode_base_whatever.hpp"
#include "base64.h" // ReneNyffenegger/cpp-base64 reference implementation

using namespace snicholls;

// ---------------------------------------------------------------------------
// Compile-time checks: the serial path is constexpr, so RFC 4648 test vectors
// can be verified by the compiler itself. A failure here is a build error.
// ---------------------------------------------------------------------------
static_assert(EncodeBase64("") == "");
static_assert(EncodeBase64("f") == "Zg==");
static_assert(EncodeBase64("fo") == "Zm8=");
static_assert(EncodeBase64("foo") == "Zm9v");
static_assert(EncodeBase64("foob") == "Zm9vYg==");
static_assert(EncodeBase64("fooba") == "Zm9vYmE=");
static_assert(EncodeBase64("foobar") == "Zm9vYmFy");

static_assert(DecodeBase64("Zg==") == "f");
static_assert(DecodeBase64("Zm9vYmFy") == "foobar");
static_assert(DecodeBase64("Zg") == "f"); // unpadded input is accepted

static_assert(EncodeBase32("f") == "MY======");
static_assert(EncodeBase32("fooba") == "MZXW6YTB");
static_assert(EncodeBase32("foobar") == "MZXW6YTBOI======");
static_assert(DecodeBase32("MZXW6===") == "foo");

static_assert(EncodeBase32Hex("foobar") == "CPNMUOJ1E8======");
static_assert(DecodeBase32Hex("CPNMUOG=") == "foob");

static_assert(EncodeBase16("foobar") == "666F6F626172");
static_assert(DecodeBase16("666F") == "fo");

static_assert(EncodeBase2("A") == "01000001");
static_assert(DecodeBase2("01000001") == "A");

// Generic inputs work at compile time too
static_assert(EncodeBase64(std::string_view{"foo"}) == "Zm9v");
static_assert(EncodeBase64(std::array<uint8_t, 3>{'f', 'o', 'o'}) == "Zm9v");

#ifdef SNICHOLLS_HAS_BITSTRING
constexpr bool BitstringCompileTimeCheck() {
    const bitstr_t bits[1] = {0x05}; // logical bits: 1, 0, 1
    return EncodeBase2Bitstring(bits, 3) == "101";
}
static_assert(BitstringCompileTimeCheck());
#endif

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Deterministic pseudo-random bytes (xorshift64) so failures are reproducible
struct Rng {
    uint64_t state = 0x9E3779B97F4A7C15ull;
    uint64_t next() {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    }
    uint8_t byte() { return static_cast<uint8_t>(next() & 0xFF); }
    Binary bytes(size_t n) {
        Binary out(n);
        for (auto& b : out) b = byte();
        return out;
    }
};

static std::string ToString(const Binary& data) {
    return std::string(data.begin(), data.end());
}

template<typename Fn>
static void AssertThrowsInvalidArgument(Fn&& fn) {
    bool threw = false;
    try {
        fn();
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    assert(threw);
    (void)threw;
}

// Scheme table so every test sweeps all encodings. The named entry points are
// templates, so the table stores captureless lambdas instead of raw pointers.
struct Scheme {
    const char* name;
    std::string (*encode)(const std::string&);
    std::string (*decode)(const std::string&);
    std::string (*encodeBin)(const Binary&);
    Binary (*decodeBin)(const std::string&);
    std::string (*encodePar)(const std::string&, unsigned);
    std::string (*decodePar)(const std::string&, unsigned);
    std::string (*encodeBinPar)(const Binary&, unsigned);
    Binary (*decodeBinPar)(const std::string&, unsigned);
#ifdef SNICHOLLS_HAS_BITSTRING
    std::string (*encodeBits)(const bitstr_t*, size_t);
    BitString (*decodeBits)(std::string_view, size_t);
#endif
    size_t blockChars; // encoded block size; 0 = scheme is unpadded
};

#ifdef SNICHOLLS_HAS_BITSTRING
#define SCHEME_BITSTRING_ROWS(Name) \
    +[](const bitstr_t* b, size_t n) { return Encode##Name##Bitstring(b, n); }, \
    +[](std::string_view s, size_t n) { return Decode##Name##Bitstring(s, n); },
#else
#define SCHEME_BITSTRING_ROWS(Name)
#endif

#define SCHEME(Name, BlockChars) \
    { #Name, \
      +[](const std::string& s) { return Encode##Name(s); }, \
      +[](const std::string& s) { return Decode##Name(s); }, \
      +[](const Binary& b) { return Encode##Name##Binary(b); }, \
      +[](const std::string& s) { return Decode##Name##Binary(s); }, \
      +[](const std::string& s, unsigned t) { return Encode##Name##Parallel(s, t); }, \
      +[](const std::string& s, unsigned t) { return Decode##Name##Parallel(s, t); }, \
      +[](const Binary& b, unsigned t) { return Encode##Name##BinaryParallel(b, t); }, \
      +[](const std::string& s, unsigned t) { return Decode##Name##BinaryParallel(s, t); }, \
      SCHEME_BITSTRING_ROWS(Name) \
      BlockChars }

static const Scheme kSchemes[] = {
    SCHEME(Base64, 4),
    SCHEME(Base32, 8),
    SCHEME(Base32Hex, 8),
    SCHEME(Base36, 0),
    SCHEME(Base32Crockford, 0),
    SCHEME(Base16, 0),
    SCHEME(Base8, 0),
    SCHEME(Base4, 0),
    SCHEME(Base2, 0),
};

// ---------------------------------------------------------------------------
// 1. RFC 4648 section 10 test vectors
// ---------------------------------------------------------------------------
static void TestRfc4648Vectors() {
    struct Vector { const char* plain; const char* encoded; };

    static const Vector base64[] = {
        {"", ""}, {"f", "Zg=="}, {"fo", "Zm8="}, {"foo", "Zm9v"},
        {"foob", "Zm9vYg=="}, {"fooba", "Zm9vYmE="}, {"foobar", "Zm9vYmFy"},
    };
    static const Vector base32[] = {
        {"", ""}, {"f", "MY======"}, {"fo", "MZXQ===="}, {"foo", "MZXW6==="},
        {"foob", "MZXW6YQ="}, {"fooba", "MZXW6YTB"}, {"foobar", "MZXW6YTBOI======"},
    };
    static const Vector base32hex[] = {
        {"", ""}, {"f", "CO======"}, {"fo", "CPNG===="}, {"foo", "CPNMU==="},
        {"foob", "CPNMUOG="}, {"fooba", "CPNMUOJ1"}, {"foobar", "CPNMUOJ1E8======"},
    };
    static const Vector base16[] = {
        {"", ""}, {"f", "66"}, {"fo", "666F"}, {"foo", "666F6F"},
        {"foob", "666F6F62"}, {"fooba", "666F6F6261"}, {"foobar", "666F6F626172"},
    };

    struct Suite {
        const Vector* vectors;
        size_t count;
        std::string (*encode)(const std::string&);
        std::string (*decode)(const std::string&);
        std::string (*encodeBin)(const Binary&);
        Binary (*decodeBin)(const std::string&);
    };
    const Suite suites[] = {
        {base64, std::size(base64),
         +[](const std::string& s) { return EncodeBase64(s); },
         +[](const std::string& s) { return DecodeBase64(s); },
         +[](const Binary& b) { return EncodeBase64Binary(b); },
         +[](const std::string& s) { return DecodeBase64Binary(s); }},
        {base32, std::size(base32),
         +[](const std::string& s) { return EncodeBase32(s); },
         +[](const std::string& s) { return DecodeBase32(s); },
         +[](const Binary& b) { return EncodeBase32Binary(b); },
         +[](const std::string& s) { return DecodeBase32Binary(s); }},
        {base32hex, std::size(base32hex),
         +[](const std::string& s) { return EncodeBase32Hex(s); },
         +[](const std::string& s) { return DecodeBase32Hex(s); },
         +[](const Binary& b) { return EncodeBase32HexBinary(b); },
         +[](const std::string& s) { return DecodeBase32HexBinary(s); }},
        {base16, std::size(base16),
         +[](const std::string& s) { return EncodeBase16(s); },
         +[](const std::string& s) { return DecodeBase16(s); },
         +[](const Binary& b) { return EncodeBase16Binary(b); },
         +[](const std::string& s) { return DecodeBase16Binary(s); }},
    };

    for (const auto& suite : suites) {
        for (size_t i = 0; i < suite.count; ++i) {
            const std::string plain = suite.vectors[i].plain;
            const std::string encoded = suite.vectors[i].encoded;
            const Binary plainBin(plain.begin(), plain.end());

            assert(suite.encode(plain) == encoded);
            assert(suite.decode(encoded) == plain);
            assert(suite.encodeBin(plainBin) == encoded);
            assert(suite.decodeBin(encoded) == plainBin);
        }
    }
    std::puts("RFC 4648 test vectors: OK");
}

// ---------------------------------------------------------------------------
// 2. Padding shape: encoded length is always a whole number of blocks
// ---------------------------------------------------------------------------
static void TestPaddingShape() {
    Rng rng;
    for (const auto& scheme : kSchemes) {
        if (scheme.blockChars == 0) continue;
        for (size_t len = 0; len <= 40; ++len) {
            const std::string encoded = scheme.encodeBin(rng.bytes(len));
            assert(encoded.length() % scheme.blockChars == 0);
            // Padding, when present, is trailing only
            const size_t firstPad = encoded.find('=');
            if (firstPad != std::string::npos) {
                assert(encoded.find_first_not_of('=', firstPad) == std::string::npos);
            }
        }
    }
    std::puts("Padding shape: OK");
}

// ---------------------------------------------------------------------------
// 3. Round trips for every scheme, lengths 0..64, string and Binary APIs
// ---------------------------------------------------------------------------
static void TestRoundTrips() {
    Rng rng;
    for (const auto& scheme : kSchemes) {
        for (size_t len = 0; len <= 64; ++len) {
            const Binary data = rng.bytes(len);
            const std::string dataStr = ToString(data);

            assert(scheme.decode(scheme.encode(dataStr)) == dataStr);
            assert(scheme.decodeBin(scheme.encodeBin(data)) == data);
        }
        // Edge bytes: all zeros and all 0xFF
        for (uint8_t fill : {uint8_t{0x00}, uint8_t{0xFF}}) {
            const Binary data(17, fill);
            assert(scheme.decodeBin(scheme.encodeBin(data)) == data);
        }
    }
    std::puts("Round trips (all schemes, lengths 0..64): OK");
}

// ---------------------------------------------------------------------------
// 4. Generic ByteSource inputs: every container of bytes encodes identically
// ---------------------------------------------------------------------------
static void TestGenericInputs() {
    const std::string str = "Hello, World!";
    const std::string expected = EncodeBase64(str);

    assert(EncodeBase64(std::string_view{str}) == expected);
    assert(EncodeBase64("Hello, World!") == expected); // literal: no trailing NUL encoded

    const std::vector<char> asChars(str.begin(), str.end());
    assert(EncodeBase64(asChars) == expected);

    const Binary asBytes(str.begin(), str.end());
    assert(EncodeBase64(asBytes) == expected);

    std::vector<std::byte> asStdByte;
    for (char c : str) asStdByte.push_back(static_cast<std::byte>(c));
    assert(EncodeBase64(asStdByte) == expected);

    const std::span<const uint8_t> asSpan(asBytes);
    assert(EncodeBase64(asSpan) == expected);
    assert(EncodeBase64Parallel(asSpan, 2) == expected); // zero-copy parallel

    // Decoding accepts generic sources too
    const std::vector<char> encodedChars(expected.begin(), expected.end());
    assert(DecodeBase64(encodedChars) == str);
    assert(DecodeBase64(std::string_view{expected}) == str);
    assert(DecodeBase64Binary(expected) == asBytes);

    std::puts("Generic ByteSource inputs: OK");
}

// ---------------------------------------------------------------------------
// 5. Parallel output is bit-identical to serial, for all schemes and sizes
//    spanning the auto-threshold, with forced and automatic thread counts
// ---------------------------------------------------------------------------
static void TestParallelMatchesSerial() {
    Rng rng;
    const size_t sizes[] = {0, 1, 2, 3, 4, 5, 7, 15, 16, 63, 64, 65, 255, 4096,
                            (size_t{1} << 20) + 7};
    const unsigned threadCounts[] = {0, 1, 2, 5};

    for (const auto& scheme : kSchemes) {
        for (size_t size : sizes) {
            const Binary data = rng.bytes(size);
            const std::string dataStr = ToString(data);
            const std::string serial = scheme.encodeBin(data);

            for (unsigned threads : threadCounts) {
                assert(scheme.encodeBinPar(data, threads) == serial);
                assert(scheme.encodePar(dataStr, threads) == serial);
                assert(scheme.decodeBinPar(serial, threads) == data);
                assert(scheme.decodePar(serial, threads) == dataStr);
            }
        }
    }
    std::puts("Parallel == serial (all schemes, sizes, thread counts): OK");
}

// ---------------------------------------------------------------------------
// 6. Cross-validation against ReneNyffenegger/cpp-base64
// ---------------------------------------------------------------------------
static void TestAgainstCppBase64() {
    Rng rng;
    std::vector<size_t> lengths;
    for (size_t len = 0; len <= 300; ++len) lengths.push_back(len);
    lengths.push_back(1000);
    lengths.push_back(65537);
    lengths.push_back((size_t{1} << 20) + 5);

    for (size_t len : lengths) {
        const std::string data = ToString(rng.bytes(len));
        const std::string reference = base64_encode(data);

        assert(EncodeBase64(data) == reference);           // we encode like they do
        assert(EncodeBase64Parallel(data, 3) == reference);
        assert(DecodeBase64(reference) == data);            // we decode their output
        assert(DecodeBase64Parallel(reference, 3) == data);
        assert(base64_decode(EncodeBase64(data)) == data);  // they decode our output
    }
    std::puts("Cross-validation vs cpp-base64 (ReneNyffenegger): OK");
}

// ---------------------------------------------------------------------------
// 7. Malformed input is rejected
// ---------------------------------------------------------------------------
static void TestInvalidInput() {
    // Characters outside the alphabet
    AssertThrowsInvalidArgument([] { DecodeBase64("Zm9v!!!!"); });
    AssertThrowsInvalidArgument([] { DecodeBase32("MZXW6\x07=="); });
    AssertThrowsInvalidArgument([] { DecodeBase16("0G"); });
    AssertThrowsInvalidArgument([] { DecodeBase2("012"); });
    AssertThrowsInvalidArgument([] { DecodeBase64Binary("Zm9v****"); });

    // '=' is only valid as trailing padding
    AssertThrowsInvalidArgument([] { DecodeBase64("Zg==Zg=="); });
    AssertThrowsInvalidArgument([] { DecodeBase64("Z=g="); });
    AssertThrowsInvalidArgument([] { DecodeBase32("MY======MY======"); });
    AssertThrowsInvalidArgument([] { DecodeBase64Binary("Zg==Zg=="); });
    AssertThrowsInvalidArgument([] { DecodeBase64Parallel("Zg==Zg==", 2); });

    // '=' in unpadded schemes is simply not in the alphabet
    AssertThrowsInvalidArgument([] { DecodeBase16("66=="); });

    // Invalid byte deep inside a large input must surface from worker threads
    std::string big(2 * 1024 * 1024, 'A');
    big[big.size() / 2] = '!';
    AssertThrowsInvalidArgument([&] { DecodeBase64Parallel(big, 4); });
    AssertThrowsInvalidArgument([&] { DecodeBase64BinaryParallel(big, 4); });

    // Leniency preserved: unpadded input decodes
    assert(DecodeBase64("Zg") == "f");
    assert(DecodeBase32("MZXW6") == "foo");

    std::puts("Malformed input rejection: OK");
}

// ---------------------------------------------------------------------------
// 8. BSD <bitstring.h> interop
// ---------------------------------------------------------------------------
#ifdef SNICHOLLS_HAS_BITSTRING
static void TestBitstring() {
    // Logical order: bit 0 is emitted first
    {
        std::vector<bitstr_t> bits(bitstr_size(3), 0);
        bit_set(bits.data(), 0);
        bit_set(bits.data(), 2);
        assert(EncodeBase2Bitstring(bits.data(), 3) == "101");
    }

    // Logical order differs from the byte-stream API: 0x01 has bit 0 set,
    // which the bitstring API emits first while the Binary API emits last
    {
        std::vector<bitstr_t> bits = {0x01};
        assert(EncodeBase2Bitstring(bits.data(), 8) == "10000000");
        assert(EncodeBase2Binary(Binary{0x01}) == "00000001");
    }

    // RFC padding applies to bitstring input as well
    {
        std::vector<bitstr_t> bits = {0x01};
        const std::string encoded = EncodeBase64Bitstring(bits.data(), 1);
        assert(encoded.length() == 4);
        assert(encoded.substr(1) == "===");
    }

    // Round trips for every scheme at every bit count 0..100, including
    // counts that are not a multiple of the character group or of 8
    Rng rng;
    for (const auto& scheme : kSchemes) {
        for (size_t nbits = 0; nbits <= 100; ++nbits) {
            std::vector<bitstr_t> bits(bitstr_size(nbits) + 1, 0); // +1 so data() is never null
            for (size_t i = 0; i < nbits; ++i) {
                if (rng.next() & 1) {
                    bit_set(bits.data(), i);
                }
            }
            // Zero unused bits of the final byte so storage comparison is exact
            for (size_t i = nbits; i < bits.size() * 8; ++i) {
                bit_clear(bits.data(), i);
            }

            const std::string encoded = scheme.encodeBits(bits.data(), nbits);
            const BitString decoded = scheme.decodeBits(encoded, nbits);

            assert(decoded.nbits == nbits);
            assert(decoded.storage.size() == bitstr_size(nbits));
            assert(std::memcmp(decoded.data(), bits.data(), bitstr_size(nbits)) == 0);
        }
    }

    // Without expectedBits the decoder returns whole characters' worth of
    // bits; the original bits come first, the fill bits are zero
    {
        std::vector<bitstr_t> bits = {0x07}; // bits 1,1,1
        const std::string encoded = EncodeBase64Bitstring(bits.data(), 3); // 1 char = 6 bits
        const BitString decoded = DecodeBase64Bitstring(encoded);
        assert(decoded.nbits == 6);
        for (size_t i = 0; i < 3; ++i) assert(bit_test(decoded.data(), i) != 0);
        for (size_t i = 3; i < 6; ++i) assert(bit_test(decoded.data(), i) == 0);
    }

    // Malformed input rejected on the bitstring path too
    AssertThrowsInvalidArgument([] { DecodeBase64Bitstring("Zg==Zg=="); });
    AssertThrowsInvalidArgument([] { DecodeBase2Bitstring("012"); });

    std::puts("BSD <bitstring.h> interop: OK");
}
#endif

// ---------------------------------------------------------------------------
int main() {
    TestRfc4648Vectors();
    TestPaddingShape();
    TestRoundTrips();
    TestGenericInputs();
    TestParallelMatchesSerial();
    TestAgainstCppBase64();
    TestInvalidInput();
#ifdef SNICHOLLS_HAS_BITSTRING
    TestBitstring();
#else
    std::puts("note: <bitstring.h> not available on this platform, interop tests skipped");
#endif

    std::puts("All tests passed.");
    return 0;
}
