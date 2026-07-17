# Base 64/32/16/8/4/2 Encoding and Decoding in C++

This header only project provides a set of C++ functions for encoding and decoding data using various base encoding schemes, including Base64, Base32, Base16, Base8, Base4, Base2, and custom variants like Base36 and Base32Crockford. These encoding schemes are commonly used for representing binary data in a textual format, making it easier to transmit and store.

## Overview

The project includes implementations of the following encoding schemes:

- **Base64**: Standard Base64 encoding as defined in [RFC 4648](https://tools.ietf.org/html/rfc4648).
- **Base32**: Standard Base32 encoding as defined in [RFC 4648](https://tools.ietf.org/html/rfc4648).
- **Base32Hex**: A variant of Base32 that uses a hexadecimal alphabet, also defined in [RFC 4648](https://tools.ietf.org/html/rfc4648).
- **Base16**: Also known as hexadecimal encoding, defined in [RFC 4648](https://tools.ietf.org/html/rfc4648).
- **Base8**: Custom implementation of octal encoding.
- **Base4**: Custom implementation using a 4-character alphabet.
- **Base2**: Simple binary encoding.
- **Base36**: A base36 encoding scheme that uses digits `0-9` and letters `A-Z` (a 5-bit-per-character variant, not arithmetic base conversion).
- **Base32Crockford**: A variant of Base32 encoding created by Douglas Crockford, which includes additional error-correction features and alternative symbol mappings.

## Features

- **RFC 4648 compliance**: Base64, Base32 and Base32Hex output is padded with `=` to a full block (4 characters for Base64, 8 for Base32), and decoders reject `=` anywhere but trailing padding. Unpadded input is still accepted on decode. All RFC 4648 section 10 test vectors pass — at compile time.
- **Generic inputs**: every function accepts any contiguous range of byte-like elements (`std::string`, `std::string_view`, `std::vector<char>`, `std::vector<uint8_t>`, `std::span`, `std::array`, C arrays, `std::byte` buffers) plus `const char*` literals. The input is read in place — no copy is made, which matters for very large buffers.
- **Compile time (`constexpr`/`consteval`)**: the serial encode/decode path is fully `constexpr`, and the decode lookup tables are built with `consteval`. You can `static_assert` your encodings.
- **Parallel API for very large data**: `Encode*Parallel`/`Decode*Parallel` split the input at block boundaries and process chunks on multiple threads, producing bit-identical output to the serial functions. Multi-GB/s throughput on desktop hardware.
- **O(1) decoding**: character lookup uses a compile-time-built 256-entry reverse table rather than a linear alphabet scan.
- **BSD `<bitstring.h>` interop**: encode and decode bit arrays of *any* bit count (not just whole bytes) directly from/to `bitstr_t` storage, honoring bitstring's logical bit order.
- **Tested against an established library**: the test suite cross-validates Base64 against [ReneNyffenegger/cpp-base64](https://github.com/ReneNyffenegger/cpp-base64) on thousands of inputs, in both directions.

## Usage

### Encoding and Decoding

```cpp
#include <string>
#include "encode_decode_base_whatever.hpp"

using namespace snicholls;

std::string data = "Hello, World!";
std::string encoded = EncodeBase64(data);   // "SGVsbG8sIFdvcmxkIQ=="
std::string decoded = DecodeBase64(encoded);

std::cout << "Encoded: " << encoded << std::endl;
std::cout << "Decoded: " << decoded << std::endl;
```

Inputs are generic — all of these encode identically, with no input copy:

```cpp
std::string_view view = data;
std::vector<char> chars(data.begin(), data.end());
Binary bytes(data.begin(), data.end());          // Binary = std::vector<uint8_t>
std::span<const uint8_t> span(bytes);            // e.g. a view of an mmap'd file

EncodeBase64(view);
EncodeBase64(chars);
EncodeBase64(bytes);
EncodeBase64(span);
EncodeBase64("a string literal");                // strlen semantics, no trailing NUL
```

### Compile time

The serial path is `constexpr`, so the compiler can do the work — and prove it correct:

```cpp
static_assert(EncodeBase64("foo") == "Zm9v");
static_assert(EncodeBase32("foobar") == "MZXW6YTBOI======");
static_assert(DecodeBase64("SGVsbG8sIFdvcmxkIQ==") == "Hello, World!");
```

### Parallel encoding/decoding for very large data

For multi-GB inputs, the `*Parallel` functions fan the work out across hardware threads. Output is bit-for-bit identical to the serial functions; an optional second argument forces a thread count (`0` = auto, which stays serial below 1 MiB):

```cpp
Binary huge = LoadGigabytesOfData();
std::string encoded = EncodeBase64BinaryParallel(huge);        // all hardware threads
Binary decoded = DecodeBase64BinaryParallel(encoded);
std::string encoded4 = EncodeBase64BinaryParallel(huge, 4);    // exactly 4 threads
```

### BSD `<bitstring.h>` interop

On platforms that ship `<bitstring.h>` (macOS/BSD), bit arrays encode and decode directly, with any bit count:

```cpp
bitstr_t* bits = bit_alloc(12);
bit_set(bits, 0); bit_set(bits, 3); bit_set(bits, 4); bit_set(bits, 11);

std::string b2 = EncodeBase2Bitstring(bits, 12);    // "100110000001" - bit 0 first
std::string b64 = EncodeBase64Bitstring(bits, 12);  // "mB=="

BitString round = DecodeBase64Bitstring(b64, 12);   // 12 = expected bit count
bit_test(round.data(), 3);                          // set
```

Note the bitstring functions use bitstring's *logical* bit order (bit 0 = LSB of byte 0, emitted first), which differs from the byte-stream order of the string/Binary functions. Because each encoded character carries a fixed number of bits, pass the expected bit count to `Decode*Bitstring` to trim the zero fill bits of the final character.

### Supported Encoding Schemes

For each of `Base64`, `Base32`, `Base32Hex`, `Base36`, `Base32Crockford`, `Base16`, `Base8`, `Base4`, `Base2`:

- `EncodeXxx` / `DecodeXxx` — encode any byte source / decode to `std::string`
- `EncodeXxxBinary` / `DecodeXxxBinary` — same encode; decode to `Binary` (`std::vector<uint8_t>`)
- `EncodeXxxParallel` / `DecodeXxxParallel` / `EncodeXxxBinaryParallel` / `DecodeXxxBinaryParallel` — multithreaded versions for very large inputs
- `EncodeXxxBitstring` / `DecodeXxxBitstring` — BSD `<bitstring.h>` bit arrays (where available)

Decoders throw `std::invalid_argument` on characters outside the alphabet or on `=` anywhere but trailing padding.

## Building

The library itself is a single header — copy `BaseEncodeDecode/encode_decode_base_whatever.hpp` (plus `alphabet.hpp`) into your project, or consume it with CMake:

```cmake
add_subdirectory(base-encode-decode)
target_link_libraries(your_app PRIVATE snicholls::base_encode_decode)
```

Requires C++20.

### Tests and benchmark

With make:

```sh
make test                 # build + run the test suite
make bench                # throughput benchmark, 512 MiB input
make bench BENCH_MB=2048  # multi-GB benchmark
make demo                 # the demo below
```

With CMake:

```sh
cmake -S . -B build/cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build/cmake -j
ctest --test-dir build/cmake
./build/cmake/bench 1024
```

The Xcode project continues to build the demo as before.

The test suite is deliberately lightweight — plain `assert()`, no framework. It covers: RFC 4648 section 10 vectors (also as `static_assert`s), padding shape for every length, round trips for every scheme over string/Binary/generic inputs, parallel-equals-serial across sizes and thread counts, malformed input rejection (including errors surfacing from worker threads), `<bitstring.h>` round trips at every bit count 0–100, and cross-validation of Base64 against [ReneNyffenegger/cpp-base64](https://github.com/ReneNyffenegger/cpp-base64) (vendored under `tests/third_party/`, used only by the tests).

## Benchmarks

`bench` reports GB/s of raw binary bytes (best of 3 runs, timings include output allocation). Sample results, 512 MiB pseudo-random input, 32 hardware threads:

```text
Base64
  serial                   encode    0.22 GB/s   decode    0.28 GB/s
  parallel (auto)          encode    1.12 GB/s   decode    3.32 GB/s   (5.1x / 11.7x vs serial)
  cpp-base64 (reference)   encode    0.20 GB/s   decode    0.07 GB/s

Base32
  serial                   encode    0.17 GB/s   decode    0.38 GB/s
  parallel (auto)          encode    0.91 GB/s   decode    3.20 GB/s   (5.5x / 8.4x vs serial)

Base16
  serial                   encode    0.15 GB/s   decode    0.35 GB/s
  parallel (auto)          encode    0.77 GB/s   decode    3.04 GB/s   (5.2x / 8.8x vs serial)
```

Numbers vary with hardware; at these sizes throughput is largely memory-bound, so speedup saturates below the thread count.

## What you should expect

Running the demo (`make demo`) should give you this (parallel timings will vary):

```text
String Encoding/Decoding Demo:
Base2 Encoded (Serial): 01001000011001010110110001101100011011110010110000100000010101110110111101110010011011000110010000100001001000000100100101110100001000000110100101110011001000000110101001110101011100110111010000100000011101110110111101101110011001000110010101110010011001100111010101101100001000000111010001101111001000000111001101100101011001010010000001111001011011110111010100100001
Base2 Decoded (Serial): Hello, World! It is just wonderful to see you!
Base4 Encoded (Serial): 1020121112301230123302300200111312331302123012100201020010211310020012211303020012221311130313100200131312331232121012111302121213111230020013101233020013031211121102001321123313110201
Base4 Decoded (Serial): Hello, World! It is just wonderful to see you!
Base8 Encoded (Serial): 220625543306745410053557344661441022011135020151346201523527156410073557334621453446316533020164336201633126244036267565102
Base8 Decoded (Serial): Hello, World! It is just wonderful to see you!
Base16 Encoded (Serial): 48656C6C6F2C20576F726C6421204974206973206A75737420776F6E64657266756C20746F2073656520796F7521
Base16 Decoded (Serial): Hello, World! It is just wonderful to see you!
Base32 Encoded (Serial): JBSWY3DPFQQFO33SNRSCCICJOQQGS4ZANJ2XG5BAO5XW4ZDFOJTHK3BAORXSA43FMUQHS33VEE======
Base32 Decoded (Serial): Hello, World! It is just wonderful to see you!
Base32Hex Encoded (Serial): 91IMOR3F5GG5ERRIDHI22829EGG6ISP0D9QN6T10ETNMSP35E9J7AR10EHNI0SR5CKG7IRRL44======
Base32Hex Decoded (Serial): Hello, World! It is just wonderful to see you!
Base32Crockford Encoded (Serial): 91JPRV3F5GG5EVVJDHJ22829EGG6JWS0D9TQ6X10EXQPWS35E9K7AV10EHQJ0WV5CMG7JVVN44
Base32Crockford Decoded (Serial): Hello, World! It is just wonderful to see you!
Base36 Encoded (Serial): 91IMOR3F5GG5ERRIDHI22829EGG6ISP0D9QN6T10ETNMSP35E9J7AR10EHNI0SR5CKG7IRRL44
Base36 Decoded (Serial): Hello, World! It is just wonderful to see you!
Base64 Encoded (Serial): SGVsbG8sIFdvcmxkISBJdCBpcyBqdXN0IHdvbmRlcmZ1bCB0byBzZWUgeW91IQ==
Base64 Decoded (Serial): Hello, World! It is just wonderful to see you!

Binary Encoding/Decoding Demo:
Base2 Encoded (Binary): 01001000011001010110110001101100011011110010110000100000010101110110111101110010011011000110010000100001
Base2 Decoded (Binary - ASCII): 48 65 6c 6c 6f 2c 20 57 6f 72 6c 64 21  | Hello, World!
Base4 Encoded (Binary): 1020121112301230123302300200111312331302123012100201
Base4 Decoded (Binary - ASCII): 48 65 6c 6c 6f 2c 20 57 6f 72 6c 64 21  | Hello, World!
Base8 Encoded (Binary): 22062554330674541005355734466144102
Base8 Decoded (Binary - ASCII): 48 65 6c 6c 6f 2c 20 57 6f 72 6c 64 21  | Hello, World!
Base16 Encoded (Binary): 48656C6C6F2C20576F726C6421
Base16 Decoded (Binary - ASCII): 48 65 6c 6c 6f 2c 20 57 6f 72 6c 64 21  | Hello, World!
Base32 Encoded (Binary): JBSWY3DPFQQFO33SNRSCC===
Base32 Decoded (Binary - ASCII): 48 65 6c 6c 6f 2c 20 57 6f 72 6c 64 21  | Hello, World!
Base32Hex Encoded (Binary): 91IMOR3F5GG5ERRIDHI22===
Base32Hex Decoded (Binary - ASCII): 48 65 6c 6c 6f 2c 20 57 6f 72 6c 64 21  | Hello, World!
Base32Crockford Encoded (Binary): 91JPRV3F5GG5EVVJDHJ22
Base32Crockford Decoded (Binary - ASCII): 48 65 6c 6c 6f 2c 20 57 6f 72 6c 64 21  | Hello, World!
Base36 Encoded (Binary): 91IMOR3F5GG5ERRIDHI22
Base36 Decoded (Binary - ASCII): 48 65 6c 6c 6f 2c 20 57 6f 72 6c 64 21  | Hello, World!
Base64 Encoded (Binary): SGVsbG8sIFdvcmxkIQ==
Base64 Decoded (Binary - ASCII): 48 65 6c 6c 6f 2c 20 57 6f 72 6c 64 21  | Hello, World!

Parallel Encoding/Decoding Demo (large input):
Base64, 64 MiB input:
  serial encode:   0.210012 GB/s
  parallel encode: 1.08058 GB/s
  outputs identical: yes
  parallel decode: 1.4492 GB/s
  round trip ok:   yes

BSD <bitstring.h> Interop Demo:
12-bit bitstring as Base2:  100110000001
12-bit bitstring as Base64: mB==
decoded bits set: 0 3 4 11
```

## Notes

- **Padding**: earlier versions of this library emitted unpadded Base64/Base32; the encoders now pad per RFC 4648. Decoders accept both padded and unpadded input.
- **Base36 / Base32Crockford**: these are bit-group encodings (5 bits per character) sharing the machinery of the RFC schemes — Base36's `W`–`Z` are never produced by the encoder. They are not arithmetic base-N conversions.
- **Thread safety**: all functions are stateless and safe to call concurrently.
