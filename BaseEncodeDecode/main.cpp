// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Saxon Nicholls

//
//  main.cpp
//  BaseEncodeDecode
//
//  Created by Saxon Nicholls on 1/8/2024.
//

#include <chrono>
#include <iostream>

#include "encode_decode_base_whatever.hpp"
#include "encode_decode_bitstring.hpp"
#include "encode_decode_dna.hpp"
#include "encode_decode_object.hpp"
#include "utils/stl_support.hpp"

// The object-encryption tour is opt-in so the default demo stays dependency-free
// (the Xcode project and `make demo` link no crypto library). Build it with the
// crypto libraries via `make demo-crypto`.
#ifdef SNICHOLLS_DEMO_CRYPTO
#include "utils/encryption_sodium.hpp"
#include "utils/secure.hpp"
#endif

// The serial path is constexpr: the compiler verifies these while building
static_assert(snicholls::EncodeBase64("Hello, World!") == "SGVsbG8sIFdvcmxkIQ==");
static_assert(snicholls::DecodeBase64("SGVsbG8sIFdvcmxkIQ==") == "Hello, World!");

// Function to demonstrate encoding/decoding with strings
void StringDemo() {
    using namespace snicholls;

    std::string data = "Hello, World! It is just wonderful to see you!";

    // Test and output comparison for Base2
    std::string encodedBase2 = EncodeBase2(data);
    std::string decodedBase2 = DecodeBase2(encodedBase2);
    std::cout << "Base2 Encoded (Serial): " << encodedBase2 << std::endl;
    std::cout << "Base2 Decoded (Serial): " << decodedBase2 << std::endl;

    std::string encodedBase4 = EncodeBase4(data);
    std::string decodedBase4 = DecodeBase4(encodedBase4);
    std::cout << "Base4 Encoded (Serial): " << encodedBase4 << std::endl;
    std::cout << "Base4 Decoded (Serial): " << decodedBase4 << std::endl;

    // Base8 Encoding/Decoding
    std::string encodedBase8 = EncodeBase8(data);
    std::string decodedBase8 = DecodeBase8(encodedBase8);
    std::cout << "Base8 Encoded (Serial): " << encodedBase8 << std::endl;
    std::cout << "Base8 Decoded (Serial): " << decodedBase8 << std::endl;

    // Base16 Encoding/Decoding
    std::string encodedBase16 = EncodeBase16(data);
    std::string decodedBase16 = DecodeBase16(encodedBase16);
    std::cout << "Base16 Encoded (Serial): " << encodedBase16 << std::endl;
    std::cout << "Base16 Decoded (Serial): " << decodedBase16 << std::endl;

    // Base32 Encoding/Decoding
    std::string encodedBase32 = EncodeBase32(data);
    std::string decodedBase32 = DecodeBase32(encodedBase32);
    std::cout << "Base32 Encoded (Serial): " << encodedBase32 << std::endl;
    std::cout << "Base32 Decoded (Serial): " << decodedBase32 << std::endl;

    // Base32Hex Encoding/Decoding
    std::string encodedBase32Hex = EncodeBase32Hex(data);
    std::string decodedBase32Hex = DecodeBase32Hex(encodedBase32Hex);
    std::cout << "Base32Hex Encoded (Serial): " << encodedBase32Hex << std::endl;
    std::cout << "Base32Hex Decoded (Serial): " << decodedBase32Hex << std::endl;

    // Base32Crockford Encoding/Decoding
    std::string encodedBase32Crockford = EncodeBase32Crockford(data);
    std::string decodedBase32Crockford = DecodeBase32Crockford(encodedBase32Crockford);
    std::cout << "Base32Crockford Encoded (Serial): " << encodedBase32Crockford << std::endl;
    std::cout << "Base32Crockford Decoded (Serial): " << decodedBase32Crockford << std::endl;

    // Base36 Encoding/Decoding
    std::string encodedBase36 = EncodeBase36(data);
    std::string decodedBase36 = DecodeBase36(encodedBase36);
    std::cout << "Base36 Encoded (Serial): " << encodedBase36 << std::endl;
    std::cout << "Base36 Decoded (Serial): " << decodedBase36 << std::endl;
    
    // Base64 Encoding/Decoding
    std::string encodedBase64 = EncodeBase64(data);
    std::string decodedBase64 = DecodeBase64(encodedBase64);
    std::cout << "Base64 Encoded (Serial): " << encodedBase64 << std::endl;
    std::cout << "Base64 Decoded (Serial): " << decodedBase64 << std::endl;
}

// Function to demonstrate encoding/decoding with binary data
void BinaryDemo() {
    using namespace snicholls;

    Binary data = {0x48, 0x65, 0x6C, 0x6C, 0x6F, 0x2C, 0x20, 0x57, 0x6F, 0x72, 0x6C, 0x64, 0x21}; // "Hello, World!" in binary

    // Helper function to print binary data as hex and ASCII
    auto printBinaryData = [](const Binary& binaryData) {
        for (auto byte : binaryData) {
            std::cout << std::hex << static_cast<int>(byte) << " ";
        }
        std::cout << " | ";
        for (auto byte : binaryData) {
            if (std::isprint(byte)) {
                std::cout << static_cast<char>(byte);
            } else {
                std::cout << '.';
            }
        }
        std::cout << std::dec << std::endl; // restore stream state after std::hex
    };

    // Base2 Encoding/Decoding
    std::string encodedBase2 = EncodeBase2Binary(data);
    Binary decodedBase2 = DecodeBase2Binary(encodedBase2);
    std::cout << "Base2 Encoded (Binary): " << encodedBase2 << std::endl;
    std::cout << "Base2 Decoded (Binary - ASCII): ";
    printBinaryData(decodedBase2);

    // Base4 Encoding/Decoding
    std::string encodedBase4 = EncodeBase4Binary(data);
    Binary decodedBase4 = DecodeBase4Binary(encodedBase4);
    std::cout << "Base4 Encoded (Binary): " << encodedBase4 << std::endl;
    std::cout << "Base4 Decoded (Binary - ASCII): ";
    printBinaryData(decodedBase4);

    // Base8 Encoding/Decoding
    std::string encodedBase8 = EncodeBase8Binary(data);
    Binary decodedBase8 = DecodeBase8Binary(encodedBase8);
    std::cout << "Base8 Encoded (Binary): " << encodedBase8 << std::endl;
    std::cout << "Base8 Decoded (Binary - ASCII): ";
    printBinaryData(decodedBase8);

    // Base16 Encoding/Decoding
    std::string encodedBase16 = EncodeBase16Binary(data);
    Binary decodedBase16 = DecodeBase16Binary(encodedBase16);
    std::cout << "Base16 Encoded (Binary): " << encodedBase16 << std::endl;
    std::cout << "Base16 Decoded (Binary - ASCII): ";
    printBinaryData(decodedBase16);

    // Base32 Encoding/Decoding
    std::string encodedBase32 = EncodeBase32Binary(data);
    Binary decodedBase32 = DecodeBase32Binary(encodedBase32);
    std::cout << "Base32 Encoded (Binary): " << encodedBase32 << std::endl;
    std::cout << "Base32 Decoded (Binary - ASCII): ";
    printBinaryData(decodedBase32);

    // Base32Hex Encoding/Decoding
    std::string encodedBase32Hex = EncodeBase32HexBinary(data);
    Binary decodedBase32Hex = DecodeBase32HexBinary(encodedBase32Hex);
    std::cout << "Base32Hex Encoded (Binary): " << encodedBase32Hex << std::endl;
    std::cout << "Base32Hex Decoded (Binary - ASCII): ";
    printBinaryData(decodedBase32Hex);

    // Base32Crockford Encoding/Decoding
    std::string encodedBase32Crockford = EncodeBase32CrockfordBinary(data);
    Binary decodedBase32Crockford = DecodeBase32CrockfordBinary(encodedBase32Crockford);
    std::cout << "Base32Crockford Encoded (Binary): " << encodedBase32Crockford << std::endl;
    std::cout << "Base32Crockford Decoded (Binary - ASCII): ";
    printBinaryData(decodedBase32Crockford);

    // Base36 Encoding/Decoding
    std::string encodedBase36 = EncodeBase36Binary(data);
    Binary decodedBase36 = DecodeBase36Binary(encodedBase36);
    std::cout << "Base36 Encoded (Binary): " << encodedBase36 << std::endl;
    std::cout << "Base36 Decoded (Binary - ASCII): ";
    printBinaryData(decodedBase36);
    
    // Base64 Encoding/Decoding
    std::string encodedBase64 = EncodeBase64Binary(data);
    Binary decodedBase64 = DecodeBase64Binary(encodedBase64);
    std::cout << "Base64 Encoded (Binary): " << encodedBase64 << std::endl;
    std::cout << "Base64 Decoded (Binary - ASCII): ";
    printBinaryData(decodedBase64);
}

// Function to demonstrate the parallel API on a large input, with timings
void ParallelDemo() {
    using namespace snicholls;
    using Clock = std::chrono::steady_clock;

    // 64 MiB of deterministic pseudo-random data
    Binary data(size_t{64} << 20);
    uint64_t state = 0x9E3779B97F4A7C15ull;
    for (auto& byte : data) {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        byte = static_cast<uint8_t>(state);
    }

    const auto gbPerSecond = [](size_t bytes, double seconds) {
        return static_cast<double>(bytes) / seconds / 1e9;
    };
    const auto seconds = [](Clock::time_point start, Clock::time_point stop) {
        return std::chrono::duration<double>(stop - start).count();
    };

    auto t0 = Clock::now();
    const std::string serial = EncodeBase64Binary(data);
    auto t1 = Clock::now();
    const std::string parallel = EncodeBase64BinaryParallel(data);
    auto t2 = Clock::now();

    std::cout << "Base64, " << (data.size() >> 20) << " MiB input:" << std::endl;
    std::cout << "  serial encode:   " << gbPerSecond(data.size(), seconds(t0, t1)) << " GB/s" << std::endl;
    std::cout << "  parallel encode: " << gbPerSecond(data.size(), seconds(t1, t2)) << " GB/s" << std::endl;
    std::cout << "  outputs identical: " << (serial == parallel ? "yes" : "NO - BUG") << std::endl;

    t0 = Clock::now();
    const Binary decoded = DecodeBase64BinaryParallel(parallel);
    t1 = Clock::now();
    std::cout << "  parallel decode: " << gbPerSecond(decoded.size(), seconds(t0, t1)) << " GB/s" << std::endl;
    std::cout << "  round trip ok:   " << (decoded == data ? "yes" : "NO - BUG") << std::endl;
}

#ifdef SNICHOLLS_HAS_BITSTRING
// Function to demonstrate BSD <bitstring.h> interop
void BitstringDemo() {
    using namespace snicholls;

    // A 12-bit bitstring: set bits 0, 3, 4, 11
    std::vector<bitstr_t> bits(bitstr_size(12), 0);
    bit_set(bits.data(), 0);
    bit_set(bits.data(), 3);
    bit_set(bits.data(), 4);
    bit_set(bits.data(), 11);

    const std::string base2 = EncodeBase2Bitstring(bits.data(), 12);
    const std::string base64 = EncodeBase64Bitstring(bits.data(), 12);
    std::cout << "12-bit bitstring as Base2:  " << base2 << std::endl;
    std::cout << "12-bit bitstring as Base64: " << base64 << std::endl;

    const BitString decoded = DecodeBase64Bitstring(base64, 12);
    std::cout << "decoded bits set: ";
    for (size_t i = 0; i < decoded.nbits; ++i) {
        if (bit_test(decoded.data(), i)) {
            std::cout << i << " ";
        }
    }
    std::cout << std::endl;
}
#endif

// Function to demonstrate DNA/RNA packing
void DnaDemo() {
    using namespace snicholls;

    const std::string sequence = "ACGTACGTTTAGGCCANNNNRYSWKM"; // canonical + N + ambiguity
    std::cout << "Sequence (" << sequence.size() << " bases): " << sequence << std::endl;

    // 4-bit IUPAC handles N and ambiguity codes (2x smaller than ASCII)
    const Binary iupac = PackDnaIupac(sequence);
    std::cout << "  4-bit IUPAC packed: " << iupac.size() << " bytes ("
              << sequence.size() << " -> " << iupac.size() << ", "
              << static_cast<double>(sequence.size()) / iupac.size() << "x)" << std::endl;
    std::cout << "  unpacked: " << UnpackDnaIupac(iupac, sequence.size()) << std::endl;

    // 2-bit is 4x smaller but only for canonical A/C/G/T
    const std::string canonical = "ACGTACGTTTAGGCCA";
    const Binary twoBit = PackDna(canonical);
    std::cout << "Canonical (" << canonical.size() << " bases): " << canonical << std::endl;
    std::cout << "  2-bit packed: " << twoBit.size() << " bytes ("
              << static_cast<double>(canonical.size()) / twoBit.size() << "x)" << std::endl;
    std::cout << "  as Base64 for transport: " << EncodeBase64Binary(twoBit) << std::endl;
    std::cout << "  round trip: " << UnpackDna(twoBit, canonical.size()) << std::endl;
}

// Function to demonstrate whole-object and STL-container serialization
void ObjectDemo() {
    using namespace snicholls;

    struct SensorReading {
        uint32_t id;
        double celsius;
        char status;
    };

    const SensorReading reading{7, 21.5, 'K'};
    const std::string token = EncodeBase64UrlObject(reading); // URL-safe, could go in a link
    std::cout << "SensorReading{7, 21.5, 'K'} as Base64Url: " << token << std::endl;

    const SensorReading back = DecodeBase64UrlObject<SensorReading>(token);
    std::cout << "  decoded: id=" << back.id << " celsius=" << back.celsius
              << " status=" << back.status << std::endl;
    std::cout << "  " << sizeof(SensorReading) << " bytes -> " << token.size()
              << " chars (same-ABI snapshot)" << std::endl;

    // With utils/stl_support.hpp, whole STL containers serialise recursively -
    // here a nested map<string, vector<int>>.
    std::map<std::string, std::vector<int>> scores{
        {"alice", {90, 85, 92}},
        {"bob", {70, 88}},
    };
    const std::string encoded = EncodeBase64Object(scores);
    std::cout << "map<string,vector<int>> as Base64: " << encoded << std::endl;
    const auto restored = DecodeBase64Object<std::map<std::string, std::vector<int>>>(encoded);
    std::cout << "  round trip ok: " << (restored == scores ? "yes" : "NO - BUG")
              << " (" << restored.size() << " entries)" << std::endl;
}

#ifdef SNICHOLLS_DEMO_CRYPTO
// A user type that owns heap data (so it needs a custom ObjectSerializer, not
// the trivially-copyable default). Once it has one, it composes into any nesting
// of standard containers automatically.
struct Account {
    std::string name;
    uint64_t balance;
    std::vector<std::string> tags;
    bool operator==(const Account&) const = default;
};

namespace snicholls {
    template<> struct ObjectSerializer<Account> {
        static Binary to_bytes(const Account& a) {
            Binary out;
            ByteWriter w{out};
            w.element<std::string>(a.name);
            w.element<uint64_t>(a.balance);
            w.element<std::vector<std::string>>(a.tags);
            return out;
        }
        static Account from_bytes(std::span<const uint8_t> bytes) {
            ByteReader r{bytes};
            Account a;
            a.name = r.element<std::string>();
            a.balance = r.element<uint64_t>();
            a.tags = r.element<std::vector<std::string>>();
            return a;
        }
    };
}

// A tour de force: a deeply nested object, serialised -> encrypted -> Base64
// -> (transport) -> Base64-decoded -> decrypted -> deserialised -> compared.
void EncryptionDemo() {
    using namespace snicholls;

    // Five container levels wrapping the custom Account type:
    //   L1 map< string,
    //   L2   vector< pair< uint32_t,
    //   L3     map< string,
    //   L4       vector<
    //   L5         Account > > > > >
    using L1 = std::map<std::string,
                 std::vector<std::pair<uint32_t,
                   std::map<std::string,
                     std::vector<Account>>>>>;

    const L1 original = {
        {"region:emea", {
            {1001, {{"gbp", {{"Alice", 4200, {"vip", "kyc"}}, {"Bob", 75, {}}}},
                    {"eur", {{"Chloe", 999999, {"whale", "kyc", "staff"}}}}}},
            {1002, {{"gbp", {{"Dan", 0, {"dormant"}}}}}},
        }},
        {"region:apac", {
            {2001, {{"aud", {{"Evie", 33330, {"vip"}}}},
                    {"jpy", {{"Fumi", 128, {"kyc"}}, {"Gus", 512, {"kyc", "vip"}}}}}},
        }},
    };

    // Count the leaves so the reader sees this is a real, chunky object.
    size_t accounts = 0, tags = 0;
    for (auto& [region, buckets] : original)
        for (auto& [id, ccymap] : buckets)
            for (auto& [ccy, accts] : ccymap)
                for (auto& acct : accts) { ++accounts; tags += acct.tags.size(); }

    std::cout << "Object: 5-level map<string, vector<pair<u32, map<string, vector<Account>>>>>\n";
    std::cout << "  " << original.size() << " regions, " << accounts
              << " Account leaves, " << tags << " tags total" << std::endl;

    // 1. Serialise the whole graph into wiped storage.
    Binary serialised = ObjectSerializer<L1>::to_bytes(original);
    SecureBytes plaintext(serialised.begin(), serialised.end());
    SecureWipe(serialised.data(), serialised.size());
    std::cout << "  1. serialise ............. " << plaintext.size() << " bytes" << std::endl;

    // 2. Encrypt (authenticated).
    SecureBytes key = SodiumEncryptor::generateKey();
    SodiumEncryptor cipher(key);
    SecureBytes encrypted = cipher.encrypt(plaintext);
    std::cout << "  2. encrypt (" << cipher.algorithm() << ") " << encrypted.size()
              << " bytes (+nonce +tag)" << std::endl;

    // 3. Base64 for transport / storage (ciphertext, so a std::string is fine).
    const std::string b64 = EncodeBase64Binary(encrypted);
    std::cout << "  3. Base64 ................ " << b64.size() << " chars: "
              << b64.substr(0, 44) << "..." << std::endl;

    // 4. The full reverse: decode -> decrypt -> deserialise -> reconstruct.
    SecureBytes encrypted2 = DecodeBase64Secure(b64);
    SecureBytes plaintext2 = cipher.decrypt(encrypted2); // throws if tampered / wrong key
    const L1 reconstructed = ObjectSerializer<L1>::from_bytes(plaintext2);
    std::cout << "  4. decode -> decrypt -> deserialise" << std::endl;

    // 5. Compare the reconstructed graph with the original.
    std::cout << "  5. reconstructed == original: "
              << (reconstructed == original ? "YES - identical" : "NO - BUG!") << std::endl;

    // A wrong key is rejected, proving the ciphertext is authenticated.
    SodiumEncryptor wrong(SodiumEncryptor::generateKey());
    bool rejected = false;
    try { (void)wrong.decrypt(encrypted); } catch (const EncryptionError&) { rejected = true; }
    std::cout << "  +  wrong key rejected: " << (rejected ? "yes" : "NO - BUG!") << std::endl;
}
#endif // SNICHOLLS_DEMO_CRYPTO

// Main function
int main() {
    std::cout << "String Encoding/Decoding Demo:" << std::endl;
    StringDemo();

    std::cout << "\nBinary Encoding/Decoding Demo:" << std::endl;
    BinaryDemo();

    std::cout << "\nParallel Encoding/Decoding Demo (large input):" << std::endl;
    ParallelDemo();

    std::cout << "\nDNA/RNA Packing Demo:" << std::endl;
    DnaDemo();

    std::cout << "\nObject Serialization Demo:" << std::endl;
    ObjectDemo();

#ifdef SNICHOLLS_HAS_BITSTRING
    std::cout << "\nBSD <bitstring.h> Interop Demo:" << std::endl;
    BitstringDemo();
#endif

#ifdef SNICHOLLS_DEMO_CRYPTO
    std::cout << "\nObject Encryption Demo (serialise -> encrypt -> Base64 -> ... -> compare):" << std::endl;
    EncryptionDemo();
#endif

    return 0;
}
