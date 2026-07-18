// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Saxon Nicholls

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
#include "encode_decode_bitstring.hpp" // no-op on platforms without <bitstring.h>
#include "encode_decode_bitset.hpp"
#include "encode_decode_stream.hpp"
#include "encode_decode_mmap.hpp"
#include "encode_decode_dna.hpp"
#include "encode_decode_object.hpp"
#include "utils/stl_support.hpp"
#include "utils/secure.hpp"
#include "utils/fixed_string.hpp"
#include "utils/overloaded.hpp"
#include "utils/type_registry.hpp"
#include "encode_decode_web.hpp"
#include "base64.h" // ReneNyffenegger/cpp-base64 reference implementation

#include <filesystem>
#include <fstream>

#include <version>
#if defined(__cpp_lib_format)
#include "encode_decode_format.hpp"
#define TEST_FORMAT_ADAPTER 1
#endif

#if __has_include(<nlohmann/json.hpp>)
#include "encode_decode_json.hpp"
#define TEST_JSON_ADAPTER 1
#endif

#include "encode_decode_ml.hpp" // after json so the optional JSON view enables

#include <sstream>

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

// RFC 4648 section 5: URL-safe alphabet, plus the unpadded (JWT-style) variant
static_assert(EncodeBase64Url("f") == "Zg==");
static_assert(EncodeBase64UrlNoPad("f") == "Zg");
static_assert(EncodeBase64("\xff\xef\xbe") == "/+++");
static_assert(EncodeBase64Url("\xff\xef\xbe") == "_---");
static_assert(DecodeBase64Url("_---") == "\xff\xef\xbe");
static_assert(DecodeBase64UrlNoPad("_---") == "\xff\xef\xbe");

// Long-input vectors, verified against the system base64/xxd tools. The same
// strings are asserted at runtime below, pinning the runtime path (block
// scalar or SIMD) to the constexpr path, which is an independent
// implementation of the codec.
constexpr const char* kLongText = "The quick brown fox jumps over the lazy dog, 0123456789!";
constexpr const char* kLongText64 = "VGhlIHF1aWNrIGJyb3duIGZveCBqdW1wcyBvdmVyIHRoZSBsYXp5IGRvZywgMDEyMzQ1Njc4OSE=";
constexpr const char* kLongText16 = "54686520717569636B2062726F776E20666F78206A756D7073206F76657220746865206C617A7920646F672C203031323334353637383921";
static_assert(EncodeBase64(kLongText) == kLongText64);
static_assert(EncodeBase16(kLongText) == kLongText16);
static_assert(DecodeBase64(kLongText64) == kLongText);
static_assert(DecodeBase16(kLongText16) == kLongText);

#ifdef SNICHOLLS_HAS_BITSTRING
constexpr bool BitstringCompileTimeCheck() {
    const bitstr_t bits[1] = {0x05}; // logical bits: 1, 0, 1
    return EncodeBase2Bitstring(bits, 3) == "101";
}
static_assert(BitstringCompileTimeCheck());
#endif

// The std::bitset adapter encodes in the same logical order, at compile time
static_assert(EncodeBase2Bitset(std::bitset<3>{0b101}) == "101");
static_assert(EncodeBase64Bitset(std::bitset<1>{1}).length() == 4); // padded

// DNA/RNA packing is constexpr. "ACGT" packs to one byte 0b00_01_10_11 = 0x1B
constexpr bool DnaCompileTimeCheck() {
    const Binary packed = PackDna("ACGT");
    return packed.size() == 1 && packed[0] == 0x1B && UnpackDna(packed, 4) == "ACGT";
}
static_assert(DnaCompileTimeCheck());

// Object serialization round-trips a scalar at compile time
constexpr bool ObjectCompileTimeCheck() {
    return DecodeBase64Object<uint32_t>(EncodeBase64Object<uint32_t>(0x1234ABCDu)) == 0x1234ABCDu;
}
static_assert(ObjectCompileTimeCheck());

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
    SCHEME(Base64Url, 4),
    SCHEME(Base64UrlNoPad, 0),
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
// 4b. The runtime path (block scalar or SIMD) reproduces the known-answer
//     vectors that the constexpr path proved at compile time, on inputs long
//     enough to engage the vector kernels
// ---------------------------------------------------------------------------
static void TestRuntimeKnownAnswers() {
    const std::string text = kLongText;
    assert(EncodeBase64(text) == kLongText64);
    assert(EncodeBase16(text) == kLongText16);
    assert(DecodeBase64(std::string(kLongText64)) == text);
    assert(DecodeBase16(std::string(kLongText16)) == text);
    std::puts("Runtime path matches compile-time known answers: OK");
}

// ---------------------------------------------------------------------------
// 4c. Base64Url: equivalent to Base64 modulo the two URL-safe characters;
//     NoPad additionally drops the trailing '='
// ---------------------------------------------------------------------------
static void TestBase64Url() {
    Rng rng;
    for (size_t len = 0; len <= 128; ++len) {
        const Binary data = rng.bytes(len);
        std::string translated = EncodeBase64Binary(data);
        for (auto& c : translated) {
            if (c == '+') c = '-';
            else if (c == '/') c = '_';
        }
        assert(EncodeBase64UrlBinary(data) == translated);

        std::string noPad = translated;
        while (!noPad.empty() && noPad.back() == '=') noPad.pop_back();
        assert(EncodeBase64UrlNoPadBinary(data) == noPad);

        assert(DecodeBase64UrlBinary(translated) == data);
        assert(DecodeBase64UrlBinary(noPad) == data); // unpadded input accepted
        assert(DecodeBase64UrlNoPadBinary(noPad) == data);
    }

    // Each alphabet rejects the other's special characters
    AssertThrowsInvalidArgument([] { DecodeBase64Url("+A=="); });
    AssertThrowsInvalidArgument([] { DecodeBase64("-A=="); });

    std::puts("Base64Url / Base64UrlNoPad: OK");
}

#ifdef TEST_JSON_ADAPTER
// ---------------------------------------------------------------------------
// 4d. nlohmann::json adapter: Binary <-> Base64 string via adl_serializer
// ---------------------------------------------------------------------------
static void TestJsonAdapter() {
    const Binary blob = {'H', 'e', 'l', 'l', 'o'};
    nlohmann::json j;
    j["payload"] = blob;
    assert(j["payload"].is_string());
    assert(j["payload"].get<std::string>() == "SGVsbG8=");
    assert(j["payload"].get<Binary>() == blob);

    // Survives a full dump/parse round trip
    const auto parsed = nlohmann::json::parse(j.dump());
    assert(parsed["payload"].get<Binary>() == blob);

    // Tolerant reads: nlohmann's default array-of-numbers form and unpadded Base64
    assert(nlohmann::json::parse("[72,101,108,108,111]").get<Binary>() == blob);
    assert(nlohmann::json("SGVsbG8").get<Binary>() == blob);

    // Wrong JSON types are rejected with nlohmann's own exception
    bool threw = false;
    try {
        (void)nlohmann::json(42).get<Binary>();
    } catch (const nlohmann::json::type_error&) {
        threw = true;
    }
    assert(threw);
    (void)threw;

    // A whole JSON document rides the object pipeline via CBOR (portable).
    const nlohmann::json doc = {
        {"model", "llama"},
        {"layers", 32},
        {"temps", {0.1, 0.7, 1.0}},
        {"nested", {{"a", 1}, {"b", {true, false, nullptr}}}},
    };
    assert(DecodeBase64Object<nlohmann::json>(EncodeBase64Object(doc)) == doc);
    assert(DecodeBase32Object<nlohmann::json>(EncodeBase32Object(doc)) == doc);
    // The object bytes are exactly nlohmann's own CBOR framing
    assert(ObjectSerializer<nlohmann::json>::to_bytes(doc) == nlohmann::json::to_cbor(doc));

    std::puts("nlohmann::json adapter (Binary <-> Base64 + JSON-as-object via CBOR): OK");
}
#endif

// ---------------------------------------------------------------------------
// 4e. std::bitset adapter: round trips for every scheme at several widths,
//     logical bit order, trimming, and (on BSD) agreement with bitstring.h
// ---------------------------------------------------------------------------
#define BITSET_ROUND_TRIP(Name, BitGroupSize, AlphabetSize, Alphabet, Padded) \
    assert((Decode##Name##Bitset<NBits>(Encode##Name##Bitset(bits)) == bits));

template<size_t NBits>
static void BitsetRoundTripAllSchemes(Rng& rng) {
    std::bitset<NBits> bits;
    for (size_t i = 0; i < NBits; ++i) {
        if (rng.next() & 1) {
            bits.set(i);
        }
    }
    SNICHOLLS_FOR_EACH_SCHEME(BITSET_ROUND_TRIP)

#ifdef SNICHOLLS_HAS_BITSTRING
    // Same logical order as the bitstring.h adapter: identical encodings
    std::vector<bitstr_t> raw(bitstr_size(NBits) + 1, 0);
    for (size_t i = 0; i < NBits; ++i) {
        if (bits[i]) {
            bit_set(raw.data(), i);
        }
    }
    assert(EncodeBase64Bitset(bits) == EncodeBase64Bitstring(raw.data(), NBits));
    assert(EncodeBase2Bitset(bits) == EncodeBase2Bitstring(raw.data(), NBits));
#endif
}
#undef BITSET_ROUND_TRIP

static void TestBitset() {
    Rng rng;
    BitsetRoundTripAllSchemes<1>(rng);
    BitsetRoundTripAllSchemes<5>(rng);
    BitsetRoundTripAllSchemes<8>(rng);
    BitsetRoundTripAllSchemes<12>(rng);
    BitsetRoundTripAllSchemes<64>(rng);
    BitsetRoundTripAllSchemes<100>(rng);

    // Logical order: bit 0 first; decoding into a wider bitset zero-fills
    std::bitset<3> small{0b101};
    assert(EncodeBase2Bitset(small) == "101");
    const auto wide = DecodeBase2Bitset<8>("101");
    assert(wide[0] && !wide[1] && wide[2]);
    for (size_t i = 3; i < 8; ++i) assert(!wide[i]);

    AssertThrowsInvalidArgument([] { DecodeBase64Bitset<8>("Zg==Zg=="); });
    AssertThrowsInvalidArgument([] { DecodeBase2Bitset<8>("012"); });

    std::puts("std::bitset adapter: OK");
}

// ---------------------------------------------------------------------------
// 4f. Streaming adapter: any chunking of the input produces exactly the
//     one-shot result, in both directions, including padding split across
//     chunk boundaries
// ---------------------------------------------------------------------------
template<size_t BitGroupSize, size_t AlphabetSize, const std::array<char, AlphabetSize>& Alphabet, bool Padded>
static void StreamRoundTrip(std::string (*oneShotEncode)(const Binary&)) {
    Rng rng;
    const size_t totals[] = {0, 1, 2, 3, 5, 16, 47, 48, 49, 1000, 4099};
    const size_t chunks[] = {1, 2, 3, 7, 64, 1024};

    for (size_t total : totals) {
        const Binary data = rng.bytes(total);
        const std::string expected = oneShotEncode(data);

        for (size_t chunk : chunks) {
            BaseStreamEncoder<BitGroupSize, AlphabetSize, Alphabet, Padded> encoder;
            std::string encoded;
            for (size_t pos = 0; pos < data.size(); pos += chunk) {
                const size_t n = std::min(chunk, data.size() - pos);
                encoded += encoder.Update(std::span<const uint8_t>(data.data() + pos, n));
            }
            encoded += encoder.Finish();
            assert(encoded == expected);

            BaseStreamDecoder<BitGroupSize, AlphabetSize, Alphabet, Padded> decoder;
            Binary decoded;
            for (size_t pos = 0; pos < expected.size(); pos += chunk) {
                const size_t n = std::min(chunk, expected.size() - pos);
                const Binary piece = decoder.Update(std::string_view(expected.data() + pos, n));
                decoded.insert(decoded.end(), piece.begin(), piece.end());
            }
            const Binary tail = decoder.Finish();
            decoded.insert(decoded.end(), tail.begin(), tail.end());
            assert(decoded == data);
        }
    }
}

#define STREAM_ROUND_TRIP(Name, BitGroupSize, AlphabetSize, Alphabet, Padded) \
    StreamRoundTrip<BitGroupSize, AlphabetSize, Alphabet, Padded>( \
        +[](const Binary& b) { return Encode##Name##Binary(b); });

static void TestStream() {
    SNICHOLLS_FOR_EACH_SCHEME(STREAM_ROUND_TRIP)

    // iostream convenience round trip
    Rng rng;
    const Binary data = rng.bytes(100003);
    const std::string dataStr = ToString(data);

    std::istringstream plainIn(dataStr);
    std::ostringstream encodedOut;
    EncodeBase64Stream(plainIn, encodedOut, 4096);
    assert(encodedOut.str() == EncodeBase64Binary(data));

    std::istringstream encodedIn(encodedOut.str());
    std::ostringstream plainOut;
    DecodeBase64Stream(encodedIn, plainOut, 4096);
    assert(plainOut.str() == dataStr);

    // Malformed input surfaces from Update, including '=' handled across chunks
    {
        Base64StreamDecoder decoder;
        AssertThrowsInvalidArgument([&] { decoder.Update(std::string_view{"Zm9v!!!!"}); });
    }
    {
        Base64StreamDecoder decoder;
        (void)decoder.Update(std::string_view{"Zg=="});
        AssertThrowsInvalidArgument([&] { decoder.Update(std::string_view{"Zg"}); });
    }

    std::puts("Streaming adapter: OK");
}
#undef STREAM_ROUND_TRIP

#ifdef TEST_FORMAT_ADAPTER
// ---------------------------------------------------------------------------
// 4g. std::format adapter
// ---------------------------------------------------------------------------
static void TestFormatAdapter() {
    const Binary blob = {'H', 'e', 'l', 'l', 'o'};
    assert(std::format("{}", Encoded(blob)) == EncodeBase64Binary(blob));
    assert(std::format("{:b64}", Encoded(blob)) == EncodeBase64Binary(blob));
    assert(std::format("{:b64u}", Encoded(blob)) == EncodeBase64UrlBinary(blob));
    assert(std::format("{:b64un}", Encoded(blob)) == EncodeBase64UrlNoPadBinary(blob));
    assert(std::format("{:b32}", Encoded(blob)) == EncodeBase32Binary(blob));
    assert(std::format("{:b16}", Encoded(blob)) == EncodeBase16Binary(blob));
    assert(std::format("{:hex}", Encoded(blob)) == EncodeBase16Binary(blob));
    assert(std::format("{:b2}", Encoded(blob)) == EncodeBase2Binary(blob));
    assert(std::format("{}", Encoded("Hi")) == "SGk=");

    // Unknown specs are rejected (at runtime through vformat; at compile
    // time when the format string is a literal)
    bool threw = false;
    try {
        const Encoded value(blob);
        (void)std::vformat("{:nope}", std::make_format_args(value));
    } catch (const std::format_error&) {
        threw = true;
    }
    assert(threw);
    (void)threw;

    std::puts("std::format adapter: OK");
}
#endif

// ---------------------------------------------------------------------------
// 4i. mmap adapter: whole-file encode/decode with zero-copy on both ends,
//     plus MappedFile as a ByteSource, over several sizes incl. empty
// ---------------------------------------------------------------------------
namespace {
    // RAII scratch directory under the system temp location
    struct ScratchDir {
        std::filesystem::path dir;
        ScratchDir() {
            const auto base = std::filesystem::temp_directory_path();
            for (unsigned n = 0; ; ++n) {
                auto candidate = base / ("bed_mmap_test_" + std::to_string(n));
                std::error_code ec;
                if (std::filesystem::create_directory(candidate, ec)) {
                    dir = candidate;
                    return;
                }
            }
        }
        ~ScratchDir() {
            std::error_code ec;
            std::filesystem::remove_all(dir, ec);
        }
        std::string path(const char* name) const { return (dir / name).string(); }
    };

    void WriteFile(const std::string& path, const Binary& data) {
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
    }
    Binary ReadFile(const std::string& path) {
        std::ifstream in(path, std::ios::binary);
        return Binary(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }
}

static void TestMmap() {
    ScratchDir scratch;
    Rng rng;
    const size_t sizes[] = {0, 1, 2, 3, 47, 48, 49, 4096, (size_t{1} << 20) + 3};

    for (size_t size : sizes) {
        const Binary data = rng.bytes(size);
        const std::string rawPath = scratch.path("raw.bin");
        const std::string encPath = scratch.path("enc.b64");
        const std::string outPath = scratch.path("out.bin");
        WriteFile(rawPath, data);

        // Whole-file encode: output must match the in-memory encoder exactly
        EncodeBase64File(rawPath, encPath);
        const Binary encoded = ReadFile(encPath);
        assert(ToString(encoded) == EncodeBase64Binary(data));

        // Whole-file decode straight back into a mapped output file
        DecodeBase64File(encPath, outPath);
        assert(ReadFile(outPath) == data);

        // MappedFile is a ByteSource: feed it to the in-memory API zero-copy
        MappedFile mapped(rawPath);
        assert(mapped.size() == size);
        assert(EncodeBase64Parallel(mapped) == EncodeBase64Binary(data));
        assert(EncodeBase16(mapped) == EncodeBase16Binary(data));
    }

    // A different scheme end to end, and the forced-thread path
    {
        const Binary data = rng.bytes(500000);
        const std::string rawPath = scratch.path("raw32.bin");
        const std::string encPath = scratch.path("enc32.b32");
        const std::string outPath = scratch.path("out32.bin");
        WriteFile(rawPath, data);
        EncodeBase32File(rawPath, encPath, 3);
        assert(ToString(ReadFile(encPath)) == EncodeBase32Binary(data));
        DecodeBase32File(encPath, outPath, 3);
        assert(ReadFile(outPath) == data);
    }

    // Malformed encoded input is rejected by the file decoder
    {
        const std::string badPath = scratch.path("bad.b64");
        const std::string outPath = scratch.path("bad.out");
        WriteFile(badPath, Binary{'Z', 'm', '9', 'v', '!', '!', '!', '!'});
        AssertThrowsInvalidArgument([&] { DecodeBase64File(badPath, outPath); });
    }

    std::puts("mmap adapter (zero-copy file encode/decode): OK");
}

// ---------------------------------------------------------------------------
// 4j. DNA/RNA packing: 2-bit (canonical) and 4-bit (IUPAC) codecs
// ---------------------------------------------------------------------------
static void TestDna() {
    // Known-answer packings (bit layout is stable and documented)
    assert(PackDna("ACGT") == (Binary{0x1B}));      // 00 01 10 11
    assert(PackDna("AAAA") == (Binary{0x00}));
    assert(PackDna("TTTT") == (Binary{0xFF}));
    assert(PackDnaIupac("AC") == (Binary{0x12}));   // BAM nibbles A=1, C=2
    assert(PackDnaIupac("N") == (Binary{0xF0}));    // N=15 in the high nibble

    // Compression ratios
    assert(PackDna(std::string(64, 'A')).size() == 16);       // 4x
    assert(PackDnaIupac(std::string(64, 'A')).size() == 32);  // 2x
    assert(PackedDnaSize(7) == 2 && PackedDnaIupacSize(7) == 4);

    // Case folding: lowercase accepted, not preserved
    assert(PackDna("acgt") == PackDna("ACGT"));
    assert(UnpackDna(PackDna("acgt"), 4) == "ACGT");

    // Partial final byte: length is required to trim the padding bits
    assert(PackDna("ACG").size() == 1);
    assert(UnpackDna(PackDna("ACG"), 3) == "ACG");
    assert(UnpackDna(PackDna("ACG")) == "ACGA"); // no length -> trailing pad base shows

    // RNA uses U; the two alphabets reject each other's odd base out
    assert(UnpackRna(PackRna("ACGU"), 4) == "ACGU");
    AssertThrowsInvalidArgument([] { (void)PackRna("ACGT"); }); // T not in RNA
    AssertThrowsInvalidArgument([] { (void)PackDna("ACGU"); }); // U not in DNA

    // 2-bit rejects anything non-canonical; 4-bit accepts the IUPAC set
    AssertThrowsInvalidArgument([] { (void)PackDna("ACGTN"); });   // N needs 4-bit
    AssertThrowsInvalidArgument([] { (void)PackDna("ACGT-"); });   // gap
    AssertThrowsInvalidArgument([] { (void)PackDnaIupac("ACGT$"); }); // '$' still invalid
    assert(UnpackDnaIupac(PackDnaIupac("ACGTNRYSWKMBDHV"), 15) == "ACGTNRYSWKMBDHV");

    // Empty input
    assert(PackDna("").empty());
    assert(UnpackDna(Binary{}) == "");

    // Randomised round trips for all four codecs at many lengths
    Rng rng;
    const char* dnaBases = "ACGT";
    const char* rnaBases = "ACGU";
    const char* iupacBases = "ACGTNRYSWKMBDHV";
    const char* iupacRnaBases = "ACGUNRYSWKMBDHV";
    for (size_t len = 0; len <= 130; ++len) {
        std::string dna, rna, dnaI, rnaI;
        for (size_t i = 0; i < len; ++i) {
            dna += dnaBases[rng.next() % 4];
            rna += rnaBases[rng.next() % 4];
            dnaI += iupacBases[rng.next() % 15];
            rnaI += iupacRnaBases[rng.next() % 15];
        }
        assert(UnpackDna(PackDna(dna), len) == dna);
        assert(UnpackRna(PackRna(rna), len) == rna);
        assert(UnpackDnaIupac(PackDnaIupac(dnaI), len) == dnaI);
        assert(UnpackRnaIupac(PackRnaIupac(rnaI), len) == rnaI);
    }

    // Composes with the rest of the library: pack, then Base64 for transport
    {
        const std::string seq = "ACGTACGTACGTACGT";
        const Binary packed = PackDna(seq);
        const std::string transported = EncodeBase64Binary(packed);
        const Binary recovered = DecodeBase64Binary(transported);
        assert(UnpackDna(recovered, seq.size()) == seq);
    }

    std::puts("DNA/RNA packing (2-bit + 4-bit IUPAC): OK");
}

// ---------------------------------------------------------------------------
// 4k. Object serialization: trivially-copyable types <-> base-N and back
// ---------------------------------------------------------------------------
namespace {
    enum class Colour : uint16_t { Red = 1, Green = 2, Blue = 0xBEEF };
    struct Record {
        uint32_t id;
        double score;
        char tag;
        Colour colour;
        std::array<uint8_t, 3> rgb;
        bool operator==(const Record&) const = default;
    };

    // A non-trivially-copyable type (owns a std::string). With no serializer
    // specialization it must NOT be ObjectSerializable.
    struct Widget {
        std::string label;
        int weight;
    };

    // A user type made serializable via a custom specialization below.
    struct Person {
        std::string name;
        uint32_t age;
        bool operator==(const Person&) const = default;
    };
}

namespace snicholls {
    // User-provided serializer for a non-trivially-copyable type: length-prefixed
    // name, then the age. Demonstrates the extension mechanism.
    template<>
    struct ObjectSerializer<Person> {
        static Binary to_bytes(const Person& p) {
            Binary out = ToBytes(static_cast<uint32_t>(p.name.size()));
            out.insert(out.end(), p.name.begin(), p.name.end());
            const Binary age = ToBytes(p.age);
            out.insert(out.end(), age.begin(), age.end());
            return out;
        }
        static Person from_bytes(std::span<const uint8_t> b) {
            const uint32_t n = FromBytes<uint32_t>(b.subspan(0, 4));
            Person p;
            p.name.assign(b.begin() + 4, b.begin() + 4 + n);
            p.age = FromBytes<uint32_t>(b.subspan(4 + n, 4));
            return p;
        }
    };
}

static void TestObject() {
    // Scalars round-trip through several schemes
    assert(DecodeBase64Object<uint32_t>(EncodeBase64Object<uint32_t>(0xDEADBEEFu)) == 0xDEADBEEFu);
    assert(DecodeBase32Object<int64_t>(EncodeBase32Object<int64_t>(-123456789)) == -123456789);
    assert(DecodeBase16Object<double>(EncodeBase16Object<double>(3.14159)) == 3.14159);
    assert(DecodeBase64Object<Colour>(EncodeBase64Object<Colour>(Colour::Blue)) == Colour::Blue);

    // A POD struct round-trips, all bytes preserved
    const Record rec{42, 9.81, 'Z', Colour::Green, {{10, 20, 30}}};
    const std::string encoded = EncodeBase64Object(rec);
    assert(DecodeBase64Object<Record>(encoded) == rec);
    assert(DecodeBase32Object<Record>(EncodeBase32Object(rec)) == rec);
    assert(DecodeBase64UrlObject<Record>(EncodeBase64UrlObject(rec)) == rec);

    // Fixed-size arrays (extra parens so the ',' in the template arg is not
    // mistaken for a second assert() macro argument)
    const std::array<int, 4> arr{{-1, 0, 7, 1000000}};
    assert((DecodeBase64Object<std::array<int, 4>>(EncodeBase64Object(arr)) == arr));

    // The encoding is exactly the encoding of the object's bytes
    assert(EncodeBase64Object(rec) == EncodeBase64Binary(ToBytes(rec)));
    assert(FromBytes<Record>(ToBytes(rec)) == rec);

    // Decoding into the wrong type is caught by the size check
    const std::string eightBytes = EncodeBase64Object<uint64_t>(1);
    AssertThrowsInvalidArgument([&] { (void)DecodeBase64Object<uint32_t>(eightBytes); });

    // Built-in specialization: std::string <-> its content bytes
    assert(DecodeBase64Object<std::string>(EncodeBase64Object(std::string("hello, world"))) == "hello, world");
    assert(DecodeBase64Object<std::string>(EncodeBase64Object(std::string{})).empty());

    // Built-in specialization: std::vector<trivially-copyable>
    const std::vector<int> vi{1, 2, 3, -4, 5};
    assert(DecodeBase64Object<std::vector<int>>(EncodeBase64Object(vi)) == vi);
    const std::vector<double> vd{1.5, -2.5, 3.25};
    assert(DecodeBase32Object<std::vector<double>>(EncodeBase32Object(vd)) == vd);
    const Binary bin{9, 8, 7, 6};
    assert(DecodeBase64Object<Binary>(EncodeBase64Object(bin)) == bin);
    // Wrong-sized byte stream for the element type is rejected
    AssertThrowsInvalidArgument([] { (void)DecodeBase64Object<std::vector<int>>(EncodeBase64Binary(Binary(5))); });

    // User-provided specialization makes a non-trivial type serializable
    const Person person{"Ada Lovelace", 36};
    assert(DecodeBase64Object<Person>(EncodeBase64Object(person)) == person);
    assert(DecodeBase32Object<Person>(EncodeBase32Object(person)) == person);

    // The concept gates correctly: trivially-copyable, string, vector and the
    // specialized Person qualify (and, with stl_support included, so do nested
    // containers); a type with no specialization does not, nor a container of it.
    static_assert(ObjectSerializable<uint32_t>);
    static_assert(ObjectSerializable<Record>);
    static_assert(ObjectSerializable<std::string>);
    static_assert(ObjectSerializable<std::vector<double>>);
    static_assert(ObjectSerializable<std::vector<std::string>>);   // composes recursively
    static_assert(ObjectSerializable<Person>);
    static_assert(!ObjectSerializable<Widget>);                    // no specialization
    static_assert(!ObjectSerializable<std::vector<Widget>>);       // container of a non-serializable type

    std::puts("Object serialization (trait-based, extensible): OK");
}

// ---------------------------------------------------------------------------
// 4k-stl. STL container serialization (utils/stl_support.hpp): every family,
//         plus deep nesting and user types composing into containers
// ---------------------------------------------------------------------------
static void TestStlSupport() {
    // Sequence containers
    assert((DecodeBase64Object<std::deque<int>>(EncodeBase64Object(std::deque<int>{1, 2, 3})) == std::deque<int>{1, 2, 3}));
    assert((DecodeBase64Object<std::list<std::string>>(EncodeBase64Object(std::list<std::string>{"a", "bb", "ccc"}))
            == std::list<std::string>{"a", "bb", "ccc"}));
    assert((DecodeBase64Object<std::forward_list<int>>(EncodeBase64Object(std::forward_list<int>{5, 6, 7}))
            == std::forward_list<int>{5, 6, 7}));
    assert((DecodeBase64Object<std::array<std::string, 2>>(EncodeBase64Object(std::array<std::string, 2>{{"x", "yy"}}))
            == std::array<std::string, 2>{{"x", "yy"}}));

    // Associative + unordered
    assert((DecodeBase64Object<std::set<int>>(EncodeBase64Object(std::set<int>{3, 1, 2})) == std::set<int>{1, 2, 3}));
    assert((DecodeBase64Object<std::multiset<int>>(EncodeBase64Object(std::multiset<int>{1, 1, 2}))
            == std::multiset<int>{1, 1, 2}));
    const std::map<std::string, int> m{{"one", 1}, {"two", 2}};
    assert((DecodeBase64Object<std::map<std::string, int>>(EncodeBase64Object(m)) == m));
    const std::unordered_map<int, std::string> um{{1, "a"}, {2, "b"}};
    assert((DecodeBase64Object<std::unordered_map<int, std::string>>(EncodeBase64Object(um)) == um));
    const std::unordered_set<int> us{1, 2, 3};
    assert(DecodeBase64Object<std::unordered_set<int>>(EncodeBase64Object(us)) == us);

    // Utility types
    const std::pair<std::string, int> pr{"key", 42};
    assert((DecodeBase64Object<std::pair<std::string, int>>(EncodeBase64Object(pr)) == pr));
    const std::tuple<int, std::string, double> tp{7, "mid", 1.5};
    assert((DecodeBase64Object<std::tuple<int, std::string, double>>(EncodeBase64Object(tp)) == tp));
    const std::optional<std::string> opt{"here"};
    assert((DecodeBase64Object<std::optional<std::string>>(EncodeBase64Object(opt)) == opt));
    assert((DecodeBase64Object<std::optional<std::string>>(EncodeBase64Object(std::optional<std::string>{})).has_value() == false));
    std::variant<int, std::string, double> var{std::string("v")};
    assert((DecodeBase64Object<std::variant<int, std::string, double>>(EncodeBase64Object(var)) == var));
    var = 99;
    assert((DecodeBase64Object<std::variant<int, std::string, double>>(EncodeBase64Object(var)) == var));

    // Adaptors
    std::stack<int> stk;
    stk.push(1); stk.push(2); stk.push(3);
    assert(DecodeBase64Object<std::stack<int>>(EncodeBase64Object(stk)) == stk);
    std::queue<std::string> q;
    q.push("first"); q.push("second");
    assert(DecodeBase64Object<std::queue<std::string>>(EncodeBase64Object(q)) == q);
    std::priority_queue<int> pq;
    pq.push(3); pq.push(1); pq.push(4); pq.push(1);
    auto pq2 = DecodeBase64Object<std::priority_queue<int>>(EncodeBase64Object(pq));
    assert(pq2.size() == pq.size() && pq2.top() == 4);

    // Deep nesting: map<string, vector<pair<int,string>>>
    using Deep = std::map<std::string, std::vector<std::pair<int, std::string>>>;
    const Deep deep{
        {"alpha", {{1, "one"}, {2, "two"}}},
        {"beta", {{3, "three"}}},
    };
    assert(DecodeBase32Object<Deep>(EncodeBase32Object(deep)) == deep);

    // A user type composes into containers automatically
    const std::vector<Person> people{{"Ada", 36}, {"Alan", 41}};
    assert(DecodeBase64Object<std::vector<Person>>(EncodeBase64Object(people)) == people);
    const std::map<std::string, Person> byName{{"a", {"Ada", 36}}};
    assert((DecodeBase64Object<std::map<std::string, Person>>(EncodeBase64Object(byName)) == byName));

    std::puts("STL container serialization (nested, all families): OK");
}

// ---------------------------------------------------------------------------
// 4l. Secure-memory utilities (utils/secure.hpp)
// ---------------------------------------------------------------------------
static void TestSecure() {
    // SecureWipe zeroes a buffer
    Binary scratch{1, 2, 3, 4, 5};
    SecureWipe(scratch.data(), scratch.size());
    for (uint8_t b : scratch) assert(b == 0);
    SecureWipe(nullptr, 0); // no-op, must not crash

    Rng rng;
    const Binary secret = rng.bytes(48);

    // SecureBytes is a drop-in for Binary: same content encodes identically,
    // across schemes, as a ByteSource with no copy
    SecureBytes sb(secret.begin(), secret.end());
    assert(sb.size() == secret.size());
    assert(EncodeBase64(sb) == EncodeBase64Binary(secret));
    assert(EncodeBase32(sb) == EncodeBase32Binary(secret));
    assert(EncodeBase16(sb) == EncodeBase16Binary(secret));
    // Full vector API works (push_back, resize, iterators)
    sb.push_back(0xAB);
    assert(sb.size() == secret.size() + 1 && sb.back() == 0xAB);

    // Decode*Secure decodes into wiped storage, matching the plain decoder
    const std::string b64 = EncodeBase64Binary(secret);
    const SecureBytes decoded = DecodeBase64Secure(b64);
    assert(decoded.size() == secret.size());
    assert(std::equal(decoded.begin(), decoded.end(), secret.begin()));
    // A different scheme, and the const char* overload
    assert(DecodeBase32Secure(EncodeBase32Binary(secret).c_str()).size() == secret.size());

    // SecureString drop-in for std::string, usable as a ByteSource
    SecureString ss = "passphrase";
    assert(EncodeBase64(ss) == EncodeBase64(std::string("passphrase")));

    // Encoders can target the output type: encode a secret straight into wiped
    // storage (no std::string intermediate), across schemes and parallel forms
    const SecureString enc64 = EncodeBase64Binary<SecureString>(secret);
    assert(enc64.size() == EncodeBase64Binary(secret).size());
    assert(std::equal(enc64.begin(), enc64.end(), EncodeBase64Binary(secret).begin()));
    const SecureBytes sbFresh(secret.begin(), secret.end());
    const SecureString enc32 = EncodeBase32<SecureString>(sbFresh);
    assert(std::equal(enc32.begin(), enc32.end(), EncodeBase32Binary(secret).begin()));
    const SecureString encPar = EncodeBase64BinaryParallel<SecureString>(secret, 2);
    assert(std::equal(encPar.begin(), encPar.end(), EncodeBase64Binary(secret).begin()));
    // Full loop with no plaintext std::string anywhere:
    const SecureBytes roundTrip = DecodeBase64Secure(enc64);
    assert(roundTrip.size() == secret.size());
    assert(std::equal(roundTrip.begin(), roundTrip.end(), secret.begin()));

    // ConstantTimeEqual
    const Binary a{1, 2, 3, 4};
    const Binary bEq{1, 2, 3, 4};
    const Binary bNe{1, 2, 3, 5};
    assert(ConstantTimeEqual(a.data(), bEq.data(), a.size()));
    assert(!ConstantTimeEqual(a.data(), bNe.data(), a.size()));
    assert(ConstantTimeEqual(std::span<const uint8_t>(a), std::span<const uint8_t>(bEq)));
    assert(!ConstantTimeEqual(std::span<const uint8_t>(a), std::span<const uint8_t>(bNe)));
    const Binary shorter{1, 2, 3};
    assert(!ConstantTimeEqual(std::span<const uint8_t>(a), std::span<const uint8_t>(shorter)));

    // SecureSTL: standard containers on the wiping allocator have the identical
    // API and compose with the object serializer
    SecureVector<int> sv{1, 2, 3};
    sv.push_back(4); // exercises reallocation (old buffer wiped by the allocator)
    assert((sv == SecureVector<int>{1, 2, 3, 4}));
    assert((DecodeBase64Object<SecureVector<int>>(EncodeBase64Object(sv)) == sv));

    SecureMap<std::string, int> sm{{"a", 1}, {"b", 2}};
    assert(sm.at("a") == 1 && sm.size() == 2);
    assert((DecodeBase64Object<SecureMap<std::string, int>>(EncodeBase64Object(sm)) == sm));

    SecureList<uint8_t> sl{9, 8, 7};
    assert((DecodeBase64Object<SecureList<uint8_t>>(EncodeBase64Object(sl)) == sl));

    // Fully-wiped nesting: element type is itself Secure
    SecureVector<SecureString> nested;
    nested.emplace_back("one");
    nested.emplace_back("two");
    assert(nested.size() == 2 && nested[1] == "two");

    // IsSecure<T> is composable, verified at compile time
    static_assert(IsSecure<int>::value);                              // no heap to leak
    static_assert(IsSecureV<SecureBytes>);
    static_assert(IsSecureV<SecureString>);
    static_assert(!IsSecureV<std::string>);
    static_assert(!IsSecureV<Binary>);                               // std::vector<uint8_t>
    static_assert(IsSecureV<SecureVector<int>>);
    static_assert(IsSecureV<SecureVector<SecureString>>);            // fully secure nesting
    static_assert(!IsSecureV<SecureVector<std::string>>);           // elements not secure
    static_assert(IsSecureV<SecureMap<SecureString, int>>);
    static_assert(!IsSecureV<SecureMap<std::string, int>>);         // key not secure
    static_assert(IsSecureV<std::pair<SecureString, int>>);
    static_assert(!IsSecureV<std::pair<std::string, int>>);
    static_assert(IsSecureV<std::optional<SecureBytes>>);
    static_assert(!IsSecureV<std::variant<int, std::string>>);
    static_assert(SecureStorage<SecureVector<SecureString>>);        // concept form

    std::puts("Secure-memory utilities + SecureSTL + IsSecure trait: OK");
}

// ---------------------------------------------------------------------------
// 4h. Web adapter: data URIs and HTTP Basic auth
// ---------------------------------------------------------------------------
static void TestWebAdapter() {
    const Binary blob = {'H', 'e', 'l', 'l', 'o'};

    // Data URIs
    const std::string uri = MakeDataUri("image/png", blob);
    assert(uri == "data:image/png;base64,SGVsbG8=");
    const DataUri parsed = ParseDataUri(uri);
    assert(parsed.mediaType == "image/png");
    assert(parsed.base64);
    assert(parsed.data == blob);

    assert(MakeDataUri("", blob).starts_with("data:application/octet-stream;base64,"));

    const DataUri plain = ParseDataUri("data:,Hello");
    assert(plain.mediaType == "text/plain;charset=US-ASCII");
    assert(!plain.base64);
    assert(plain.data == blob);

    bool threw = false;
    try { (void)ParseDataUri("https://example.com"); } catch (const std::invalid_argument&) { threw = true; }
    assert(threw);
    threw = false;
    try { (void)ParseDataUri("data:no-comma-here"); } catch (const std::invalid_argument&) { threw = true; }
    assert(threw);
    (void)threw;

    // HTTP Basic auth - the canonical RFC 7617 example
    assert(BasicAuthHeader("Aladdin", "open sesame") == "Basic QWxhZGRpbjpvcGVuIHNlc2FtZQ==");
    const BasicCredentials creds = ParseBasicAuthHeader("Basic QWxhZGRpbjpvcGVuIHNlc2FtZQ==");
    assert(creds.user == "Aladdin");
    assert(creds.password == "open sesame");

    // Scheme name is case-insensitive; password may contain ':'
    assert(ParseBasicAuthHeader(BasicAuthHeader("u", "a:b:c")).password == "a:b:c");
    assert(ParseBasicAuthHeader("bASIC QWxhZGRpbjpvcGVuIHNlc2FtZQ==").user == "Aladdin");

    AssertThrowsInvalidArgument([] { (void)BasicAuthHeader("user:name", "pw"); });

    std::puts("Web adapter (data URIs, Basic auth): OK");
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
// 4m. Compile-time fixed_string + type registry: runtime name -> construct and
//     dispatch the right type, with no variant, base class, or switch.
// ---------------------------------------------------------------------------
static_assert(fixed_string("abc").size() == 3);
static_assert(fixed_string("abc") == "abc");
static_assert((fixed_string("ab") + fixed_string("cd")) == "abcd");
static_assert(fixed_string("hello").substr<1, 3>() == "ell");

namespace {
    struct Ping { uint32_t seq; bool operator==(const Ping&) const = default; };
    struct Pong { double when; bool operator==(const Pong&) const = default; };
    struct Note { std::string text; bool operator==(const Note&) const = default; }; // non-trivial
}

namespace snicholls {
    // A user type that owns heap data needs a serializer; then it works in the
    // registry exactly like the trivially-copyable types.
    template<> struct ObjectSerializer<Note> {
        static Binary to_bytes(const Note& n) {
            Binary out; ByteWriter w{out}; w.element<std::string>(n.text); return out;
        }
        static Note from_bytes(std::span<const uint8_t> b) {
            ByteReader r{b}; return Note{ r.element<std::string>() };
        }
    };
}

using DemoRegistry = TypeRegistry<
    Named<"ping", Ping>,
    Named<"pong", Pong>,
    Named<"note", Note>>;

static_assert(DemoRegistry::count == 3);
static_assert(DemoRegistry::contains("pong") && !DemoRegistry::contains("nope"));
static_assert(DemoRegistry::nameOf<Pong>() == "pong");

static void TestTypeRegistry() {
    // Reverse: object -> { registered name, Base64 }
    const Note note{"remember the milk"};
    const auto [name, serial] = DemoRegistry::serialize(note);
    assert(name == "note");

    // Forward: runtime name + serialised string -> reconstruct -> dispatch to a
    // per-type handler via `overloaded` (no if constexpr, no switch, no variant).
    Note gotNote; Ping gotPing{}; int routed = 0;
    const bool handled = DemoRegistry::dispatch(name, serial, overloaded{
        [&](Ping&& p) { gotPing = p; ++routed; },
        [&](Pong&&)   { ++routed; },
        [&](Note&& n) { gotNote = std::move(n); ++routed; },
    });
    assert(handled && routed == 1 && gotNote == note);

    // A trivially-copyable type through the same registry
    const auto [pname, pserial] = DemoRegistry::serialize(Ping{42});
    Ping p2{};
    DemoRegistry::dispatch(pname, pserial, overloaded{
        [&](Ping&& p) { p2 = p; }, [&](Pong&&) {}, [&](Note&&) {},
    });
    assert(p2.seq == 42);

    // Unknown name -> not handled, no throw
    assert(!DemoRegistry::dispatch("mystery", serial, [](auto&&) {}));

    // A custom deserialiser (any format) drops in without a base class
    bool viaCustom = false;
    DemoRegistry::dispatch("note", serial,
        [&](auto&& obj) { if constexpr (std::is_same_v<std::decay_t<decltype(obj)>, Note>) viaCustom = (obj == note); },
        []<typename T>(std::type_identity<T>, std::string_view s) { return DecodeBase64Object<T>(s); });
    assert(viaCustom);

    std::puts("Type registry (compile-time name -> type dispatch, no variant/switch): OK");
}

// ---------------------------------------------------------------------------
// ML tensor representation: dtype metadata, sub-byte lane packing, a Tensor /
// Model riding the object pipeline, opaque GGML-style passthrough, JSON view.
// ---------------------------------------------------------------------------
static void TestMachineLearning() {
    using namespace snicholls::ml;

    // dtype metadata / name round trips (safetensors-aligned)
    static_assert(BitsPerElement(Dtype::F32) == 32);
    static_assert(BitsPerElement(Dtype::I4) == 4);
    static_assert(BitsPerElement(Dtype::U1) == 1);
    static_assert(IsSubByte(Dtype::I4) && !IsSubByte(Dtype::F16));
    for (int i = 0; i <= static_cast<int>(Dtype::Opaque); ++i) {
        const auto d = static_cast<Dtype>(i);
        assert(DtypeFromName(DtypeName(d)) == d);
    }
    assert(DtypeName(Dtype::F8_E4M3) == "F8_E4M3");
    assert(NumpyTypestr(Dtype::F32) == "<f4" && NumpyTypestr(Dtype::BF16).empty());

    // Sub-byte lane packing: density + MSB-first order + round trip
    {
        const std::vector<uint8_t> nibbles = {0x0, 0xF, 0x3, 0xA, 0x5}; // 5 int4 values
        const Binary packed = PackBits(nibbles, 4);
        assert(packed.size() == 3);            // ceil(5/2)
        assert(packed[0] == 0x0F);             // first value high nibble
        assert(UnpackBits(packed, 5, 4) == nibbles);

        const std::vector<uint8_t> bits = {1, 0, 1, 1, 0, 0, 1}; // 7 x 1-bit
        const Binary packed1 = PackBits(bits, 1);
        assert(packed1.size() == 1 && packed1[0] == 0b10110010);
        assert(UnpackBits(packed1, 7, 1) == bits);
    }

    // A float layer -> Tensor -> Base64 object -> back
    {
        const std::vector<float> w = {1.0f, -2.5f, 3.14159f, 0.0f, 42.0f, -0.5f};
        const Tensor t = Tensor::From<float>("mlp.weight", {2, 3}, w);
        assert(t.dtype == Dtype::F32 && t.shape == std::vector<int64_t>({2, 3}));
        assert(t.ElementCount() == 6 && t.Valid());
        assert(t.ToVector<float>() == w);

        const std::string b64 = EncodeBase64Object(t);
        const Tensor back = DecodeBase64Object<Tensor>(b64);
        assert(back == t && back.ToVector<float>() == w);
        // dtype mismatch is caught
        bool threw = false;
        try { (void)t.ToVector<double>(); } catch (const std::invalid_argument&) { threw = true; }
        assert(threw);
    }

    // A sub-byte (int4) quantised layer round-trips through the pipeline
    {
        const std::vector<uint8_t> q = {0, 1, 2, 15, 8, 7, 3};
        const Tensor t = Tensor::FromLanes("attn.q", Dtype::I4, {7}, q);
        assert(t.data.size() == 4 && t.Valid());          // ceil(7/2)
        const Tensor back = DecodeBase32Object<Tensor>(EncodeBase32Object(t));
        assert(back == t && back.Lanes() == q);
    }

    // F16 element bytes carried raw (no native type needed)
    {
        const Binary halfBytes = {0x00, 0x3C, 0x00, 0xC0}; // 1.0, -2.0 in IEEE half
        const Tensor t = Tensor::FromRaw("norm", Dtype::F16, {2}, halfBytes);
        assert(t.Valid() && t.data == halfBytes);
        assert(DecodeBase64Object<Tensor>(EncodeBase64Object(t)) == t);
    }

    // Opaque GGML-style block-quant passthrough: byte-for-byte faithful
    {
        Binary block(210); // e.g. a Q6_K super-block's worth of bytes
        for (size_t i = 0; i < block.size(); ++i) block[i] = static_cast<uint8_t>(i * 7 + 1);
        const Tensor t = Tensor::Opaque("blk.0.ffn", "Q6_K", {256}, block);
        assert(t.dtype == Dtype::Opaque && t.quant == "Q6_K" && t.Valid());
        const Tensor back = DecodeBase64Object<Tensor>(EncodeBase64Object(t));
        assert(back == t && back.data == block);
    }

    // A whole Model (state_dict) as one Base64 string, via the vector serialiser
    {
        const Model model = {
            Tensor::From<float>("w1", {2}, std::vector<float>{1.f, 2.f}),
            Tensor::From<int32_t>("b1", {2}, std::vector<int32_t>{7, -7}),
            Tensor::Opaque("w2", "Q4_K", {32}, Binary{1, 2, 3, 4, 5}),
        };
        const Model back = DecodeBase64UrlObject<Model>(EncodeBase64UrlObject(model));
        assert(back == model);
        assert(Find(back, "b1") != nullptr && Find(back, "b1")->ToVector<int32_t>()[0] == 7);
        assert(Find(back, "missing") == nullptr);
    }

#ifdef TEST_JSON_ADAPTER
    // JSON view: the numpy/torch-friendly { name, dtype, shape, data(base64) }
    {
        const Tensor t = Tensor::From<float>("layer.w", {2, 2},
                                             std::vector<float>{1.f, 2.f, 3.f, 4.f});
        const nlohmann::json j = t;
        assert(j["dtype"] == "F32");
        assert(j["shape"] == nlohmann::json::array({2, 2}));
        assert(j["data"].is_string()); // Base64, not an int array
        assert(j.get<Tensor>() == t);

        // Opaque carries its quant tag through JSON too
        const nlohmann::json jq = Tensor::Opaque("q", "Q4_K", {8}, Binary{9, 9, 9});
        assert(jq["dtype"] == "OPAQUE" && jq["quant"] == "Q4_K");
        assert(jq.get<Tensor>().quant == "Q4_K");
    }
#endif

    std::puts("ML tensors (dtype, sub-byte packing, Model pipeline, GGML passthrough): OK");
}

// ---------------------------------------------------------------------------
int main() {
#if defined(SNICHOLLS_SIMD_INTEL)
    std::puts("SIMD: Intel SSSE3 kernels active");
#elif defined(SNICHOLLS_SIMD_ARM)
    std::puts("SIMD: ARM NEON kernels active");
#else
    std::puts("SIMD: disabled (scalar path)");
#endif

    TestRfc4648Vectors();
    TestPaddingShape();
    TestRoundTrips();
    TestGenericInputs();
    TestRuntimeKnownAnswers();
    TestBase64Url();
    TestBitset();
    TestStream();
    TestMmap();
    TestDna();
    TestObject();
    TestStlSupport();
    TestSecure();
    TestTypeRegistry();
    TestMachineLearning();
#ifdef TEST_FORMAT_ADAPTER
    TestFormatAdapter();
#endif
    TestWebAdapter();
#ifdef TEST_JSON_ADAPTER
    TestJsonAdapter();
#endif
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
