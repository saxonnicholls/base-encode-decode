# Base 64/32/16/8/4/2 Encoding and Decoding in C++

[![CI](https://github.com/saxonnicholls/base-encode-decode/actions/workflows/ci.yml/badge.svg)](https://github.com/saxonnicholls/base-encode-decode/actions/workflows/ci.yml)

This header only project provides a set of C++ functions for encoding and decoding data using various base encoding schemes, including Base64, Base64Url, Base32, Base16, Base8, Base4, Base2, and custom variants like Base36 and Base32Crockford. These encoding schemes are commonly used for representing binary data in a textual format, making it easier to transmit and store.

At a glance:

- **11 schemes** — Base64, Base64Url (+ no-pad), Base32, Base32Hex, Base16, Base8, Base4, Base2, Base36, Base32Crockford — one uniform API.
- **Correct** — RFC 4648 padding and validation, verified against the RFC vectors (at compile time) and against [cpp-base64](https://github.com/ReneNyffenegger/cpp-base64).
- **Fast** — `constexpr` scalar core, auto-detected SSE/NEON SIMD, and a multithreaded path for multi-GB inputs; several GB/s on a desktop.
- **Flexible input** — any contiguous byte range (`string`, `string_view`, `span`, `vector`, `array`, C arrays, `std::byte`), read zero-copy.
- **Opt-in adapters** — streaming, memory-mapped files, `std::format`, data URIs / HTTP Basic auth, `std::bitset`, BSD `<bitstring.h>`, DNA/RNA packing, extensible object/STL serialization, nlohmann::json.
- **No dependencies** — copy the headers in; C++20, works with Clang, GCC, and MSVC.

## Design philosophy: simplicity first

The emphasis here is on **simplicity**. The library is a handful of headers you copy into your project — no build step, no dependencies, no configuration. Everything works out of the box with sensible defaults, and every extra capability is an *option*, never a requirement:

- The scalar, `constexpr` implementation is the canonical one and is always available.
- SIMD acceleration (x86 SSSE3, AArch64 NEON) is a drop-in: it activates automatically on supported hardware and can be switched off with one macro (`SNICHOLLS_NO_SIMD`). Same results, bit for bit — only faster.
- Everything else is an explicit-include adapter on top of the same core.

To drop the library into a project, copy the `BaseEncodeDecode` headers you need:

| File                                | Role                                                        |
| ----------------------------------- | ----------------------------------------------------------- |
| `encode_decode_base_whatever.hpp` | the library (include this)                                  |
| `alphabet.hpp`                    | the encoding alphabets                                      |
| `encode_decode_simd_intel.hpp`    | x86 SSSE3 kernels (auto-included; optional)                 |
| `encode_decode_simd_arm.hpp`      | AArch64 NEON kernels (auto-included; optional)              |
| `encode_decode_stream.hpp`        | adapter: constant-memory streaming (files, sockets)         |
| `encode_decode_mmap.hpp`          | adapter: memory-mapped whole-file encode/decode (zero-copy) |
| `encode_decode_format.hpp`        | adapter:`std::format` support                             |
| `encode_decode_web.hpp`           | adapter: data URIs, HTTP Basic auth                         |
| `encode_decode_bitset.hpp`        | adapter:`std::bitset` (portable bit arrays)               |
| `encode_decode_bitstring.hpp`     | adapter: BSD`<bitstring.h>` bit arrays                    |
| `encode_decode_dna.hpp`           | adapter: DNA/RNA 2-bit and 4-bit (IUPAC) packing            |
| `encode_decode_object.hpp`        | adapter: serialise an object (trait-based, extensible)      |
| `utils/stl_support.hpp`           | adapter add-on: ObjectSerializer for the STL container zoo  |
| `encode_decode_json.hpp`          | adapter: nlohmann::json (`Binary` as Base64 strings)      |

Adapters are included explicitly and only when you want them; the SIMD headers may simply be omitted (the library falls back to scalar). Requires C++20 (C++23 unlocks a faster output-allocation path automatically). All schemes are defined in a single table (`SNICHOLLS_FOR_EACH_SCHEME`), which every adapter reuses — adding a scheme there adds it everywhere.

## Overview

The project includes implementations of the following encoding schemes:

- **Base64**: Standard Base64 encoding as defined in [RFC 4648](https://tools.ietf.org/html/rfc4648).
- **Base64Url**: The URL- and filename-safe alphabet of [RFC 4648 section 5](https://tools.ietf.org/html/rfc4648#section-5) (`-` and `_` instead of `+` and `/`). Also available as **Base64UrlNoPad**, the unpadded form used by JWTs and web tokens.
- **Base32**: Standard Base32 encoding as defined in [RFC 4648](https://tools.ietf.org/html/rfc4648).
- **Base32Hex**: A variant of Base32 that uses a hexadecimal alphabet, also defined in [RFC 4648](https://tools.ietf.org/html/rfc4648).
- **Base16**: Also known as hexadecimal encoding, defined in [RFC 4648](https://tools.ietf.org/html/rfc4648).
- **Base8**: Custom implementation of octal encoding.
- **Base4**: Custom implementation using a 4-character alphabet.
- **Base2**: Simple binary encoding.
- **Base36**: A base36 encoding scheme that uses digits `0-9` and letters `A-Z` (a 5-bit-per-character variant, not arithmetic base conversion).
- **Base32Crockford**: A variant of Base32 encoding created by Douglas Crockford, which includes additional error-correction features and alternative symbol mappings.

## Features

- **RFC 4648 compliance**: padded output to full blocks (4 chars for Base64, 8 for Base32), `=` accepted only as trailing padding, unpadded input tolerated on decode. All RFC 4648 section 10 test vectors pass — at compile time.
- **Generic inputs**: every function accepts any contiguous range of byte-like elements (`std::string`, `std::string_view`, `std::vector<char>`, `std::vector<uint8_t>`, `std::span`, `std::array`, C arrays, `std::byte` buffers) plus `const char*` literals, read zero-copy.
- **Compile time (`constexpr`/`consteval`)**: the serial path is fully `constexpr`; you can `static_assert` your encodings.
- **SIMD drop-ins**: SSSE3 and NEON kernels for Base64, Base64Url and Base16 activate automatically on x86 and AArch64. Other schemes use a block-unrolled scalar core (itself 3-7x faster than a naive loop).
- **Parallel API for very large data**: `Encode*Parallel`/`Decode*Parallel` split the input at block boundaries across hardware threads — multi-GB/s throughput, output bit-identical to the serial functions.
- **Streaming**: incremental encoders/decoders and `istream`/`ostream` one-liners process any amount of data in constant memory.
- **Adapters** for `std::format`, data URIs and HTTP Basic auth, `std::bitset`, BSD `<bitstring.h>`, DNA/RNA packing, extensible object + full-STL serialization, and nlohmann::json.
- **Tested against established libraries**: cross-validated against [ReneNyffenegger/cpp-base64](https://github.com/ReneNyffenegger/cpp-base64) on thousands of inputs, plus known-answer vectors generated with the system `base64`/`xxd` tools. The suite runs on x86 (SSSE3) and ARM (NEON), with SIMD on and off.

## Usage

### Encoding and Decoding

```cpp
#include <string>
#include "encode_decode_base_whatever.hpp"

using namespace snicholls;

std::string data = "Hello, World!";
std::string encoded = EncodeBase64(data);   // "SGVsbG8sIFdvcmxkIQ=="
std::string decoded = DecodeBase64(encoded);
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

### URL-safe Base64 (tokens, JWTs)

```cpp
EncodeBase64Url(data);        // RFC 4648 section 5, padded:   "SGVsbG8sIFdvcmxkIQ=="
EncodeBase64UrlNoPad(data);   // JWT-style, unpadded:          "SGVsbG8sIFdvcmxkIQ"
DecodeBase64Url(token);       // accepts both padded and unpadded input
```

### Compile time

```cpp
static_assert(EncodeBase64("foo") == "Zm9v");
static_assert(EncodeBase64Url("\xff\xef\xbe") == "_---");
static_assert(DecodeBase64("SGVsbG8sIFdvcmxkIQ==") == "Hello, World!");
```

### Parallel encoding/decoding for very large data

```cpp
Binary huge = LoadGigabytesOfData();
std::string encoded = EncodeBase64BinaryParallel(huge);        // all hardware threads
Binary decoded = DecodeBase64BinaryParallel(encoded);
std::string encoded4 = EncodeBase64BinaryParallel(huge, 4);    // exactly 4 threads
```

### Streaming: any size, constant memory (`encode_decode_stream.hpp`)

```cpp
#include "encode_decode_stream.hpp"

std::ifstream in("movie.mkv", std::ios::binary);
std::ofstream out("movie.b64");
EncodeBase64Stream(in, out);                  // never holds the file in memory

// Or drive it chunk by chunk (sockets, pipes, custom I/O):
Base64StreamEncoder encoder;
std::string piece1 = encoder.Update(chunk1);  // any ByteSource
std::string piece2 = encoder.Update(chunk2);
std::string tail = encoder.Finish();          // final group + padding
```

Concatenating the pieces yields exactly the one-shot result regardless of how the input was split; bulk data flows through the same SIMD kernels.

### Memory-mapped files: zero-copy on both ends (`encode_decode_mmap.hpp`)

For multi-GB files where you want maximum throughput rather than minimum memory, map the file and let the parallel codec work over it directly. The input is never copied into a buffer, and the output file is sized, mapped, and written into by the worker threads — no intermediate string/vector the size of the data:

```cpp
#include "encode_decode_mmap.hpp"

EncodeBase64File("firmware.bin", "firmware.b64");   // input mapped, output mapped, parallel
DecodeBase64File("firmware.b64", "firmware.out");   // decoded straight into the output map

// Or map a file yourself; MappedFile is a ByteSource, so it feeds any API:
MappedFile in("firmware.bin");
std::string b64 = EncodeBase64Parallel(in);         // zero-copy view, no read() into RAM
```

Portable over POSIX (`mmap`) and Windows (`CreateFileMapping`/`MapViewOfFile`); mapping an empty file is well-defined and yields an empty view. This complements `encode_decode_stream.hpp`: use streaming for constant-memory pipes/sockets, and mmap when the file is on disk and you want the parallel path with no copies. (Reminder: base-encoding *to disk* inflates data by 33% and only makes sense when the encoded form must reach a text-only channel; to just store bytes, store them raw.)

### `std::format` (`encode_decode_format.hpp`)

```cpp
#include "encode_decode_format.hpp"

std::format("payload={}", Encoded(blob));       // Base64 (default)
std::format("token={:b64un}", Encoded(blob));   // Base64Url, no padding
std::format("digest={:hex}", Encoded(blob));    // Base16
// specs: b64 (default), b64u, b64un, b32, b16/hex, b2
```

### Web: data URIs and Basic auth (`encode_decode_web.hpp`)

```cpp
#include "encode_decode_web.hpp"

std::string uri = MakeDataUri("image/png", pngBytes);   // data:image/png;base64,...
DataUri parsed = ParseDataUri(uri);                     // .mediaType, .data

std::string header = BasicAuthHeader("Aladdin", "open sesame");
// "Basic QWxhZGRpbjpvcGVuIHNlc2FtZQ=="  (encoding, not encryption - use TLS)
BasicCredentials creds = ParseBasicAuthHeader(header);
```

### Bit arrays: `std::bitset` and BSD `<bitstring.h>`

```cpp
#include "encode_decode_bitset.hpp"       // portable, all platforms

std::bitset<12> bits;
bits.set(0); bits.set(11);
std::string b64 = EncodeBase64Bitset(bits);
auto round = DecodeBase64Bitset<12>(b64); // std::bitset<12>
```

`encode_decode_bitstring.hpp` provides the same operations for BSD `<bitstring.h>` arrays (`bitstr_t*` + bit count) on macOS/BSD; on other platforms it compiles to nothing, so it is safe to include unconditionally. Both adapters use bitstring's *logical* bit order — bit 0 is the first bit of the encoded stream — which differs from the byte-stream order of the string/Binary functions. Any bit count works, not just multiples of 8.

### DNA/RNA packing (`encode_decode_dna.hpp`)

The mirror image of base encoding: instead of expanding binary into a text alphabet, this *contracts* a nucleotide sequence into packed bits — 4× smaller than one ASCII byte per base (2× for the ambiguity-aware variant). It's the same bit-group machinery with a biology alphabet.

```cpp
#include "encode_decode_dna.hpp"

// 2-bit: canonical A/C/G/T only, 4 bases per byte (4x). For clean data:
// k-mers, QC'd reference sequence, oligos.
Binary packed = PackDna("ACGTACGT");        // 8 bases -> 2 bytes
std::string seq = UnpackDna(packed, 8);      // "ACGTACGT"

// 4-bit: full IUPAC set incl. N and ambiguity codes, 2 bases per byte (2x).
// Handles real FASTA; nibble codes follow the SAM/BAM seq_nt16 convention.
Binary iupac = PackDnaIupac("ACGTNRYSWKM");
std::string full = UnpackDnaIupac(iupac, 11);

// RNA variants use U in place of T:
PackRna("ACGU");  PackRnaIupac("ACGUN");
```

Input is case-insensitive (lowercase soft-masking is accepted but not preserved). Invalid characters throw `std::invalid_argument` — so 2-bit `PackDna` rejects `N`, gaps, and ambiguity codes (use the 4-bit variant for those), and DNA/RNA reject each other's odd base out (`T` vs `U`). Unpacking takes the base count, since the final byte may be partly used — store the length alongside the packed bytes, as `.2bit`/FASTA-index formats do (omit it to unpack every whole base present). Packed output is plain `Binary`, so it composes with everything else — Base64 it for a text channel, mmap it to a file, put it in JSON. Packing and unpacking are `constexpr`.

This is a convenience for sequence data you already have in a program using this library, not a genomics toolkit — it does packing and unpacking only (no reverse-complement, GC content, alignment, etc.).

### Object serialization (`encode_decode_object.hpp`)

Serialise a whole object to a base-N string and back:

```cpp
#include "encode_decode_object.hpp"

struct Vec3 { float x, y, z; };
Vec3 v{1.0f, 2.0f, 3.0f};
std::string s = EncodeBase64Object(v);       // object -> string
Vec3 back = DecodeBase64Object<Vec3>(s);     // string -> object (throws on size mismatch)
```

Available for every scheme (`EncodeBase32Object`, `EncodeBase64UrlObject`, …), and `constexpr` for padding-free types.

**Extensible by specialization.** Serialisation goes through one customization point — the trait `ObjectSerializer<T>` with `to_bytes`/`from_bytes` hooks. Three kinds of type are handled:

- **Trivially-copyable** types (scalars, enums, PODs, `std::array` of them) work *automatically*: their object representation is their bytes, copied via `std::bit_cast` (no `reinterpret_cast`, no UB).
- **`std::string` and `std::vector<trivially-copyable>`** are covered by built-in specialisations (raw content bytes).
- **Anything else** — types owning heap memory, polymorphic types, nested containers — works *only* if you provide a specialisation.

A type that is neither trivially copyable nor specialised does **not** satisfy the `ObjectSerializable` concept, so `Encode*Object`/`Decode*Object` aren't callable for it — a clean "constraints not satisfied" error, never a silent byte-copy of pointers. You can't misuse it by accident:

```cpp
struct Widget { std::string label; int weight; };
EncodeBase64Object(Widget{...});   // compile error: no ObjectSerializer<Widget>
```

To add your own type (a polymorphic hierarchy works the same way — serialise its logical state, not its vtable):

```cpp
struct Person { std::string name; uint32_t age; };

namespace snicholls {
    template<> struct ObjectSerializer<Person> {
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
EncodeBase64Object(Person{"Ada", 36});   // now works
```

**Same-ABI, not a portable wire format.** The trivially-copyable and vector serialisers copy element bytes verbatim, so round trips are perfect within one program (or builds sharing a compiler/architecture) — ideal for config blobs, fixed-layout records, numeric arrays, or a struct in a token — but *not* safe across architectures (endianness, struct padding, type sizes differ). `std::string`'s bytes are its characters, so that one is portable. If you transmit object snapshots between machines and want a compile-time guarantee they agree, define `SNICHOLLS_REQUIRE_LITTLE_ENDIAN` before including the header — a big-endian build then fails to compile rather than silently producing incompatible bytes (mixed-endian platforms are always rejected). For a fully portable format, fix the byte order in a custom serialiser, or route through CBOR via the json adapter:

```cpp
#include "encode_decode_json.hpp"
std::string s = EncodeBase64(nlohmann::json(obj).to_cbor());          // any JSON-able type
auto back = nlohmann::json::from_cbor(DecodeBase64Binary(s)).get<T>();
```

### The STL container zoo (`utils/stl_support.hpp`)

The object header covers scalars/PODs, `std::string`, and `std::vector`. Include `utils/stl_support.hpp` for the rest of the standard library, and it all composes recursively — a container serialises its size plus each element through that element's own `ObjectSerializer` (variable-length elements are varint length-prefixed):

```cpp
#include "utils/stl_support.hpp"

std::map<std::string, std::vector<int>> scores{{"alice", {90, 85, 92}}, {"bob", {70, 88}}};
std::string s = EncodeBase64Object(scores);
auto back = DecodeBase64Object<std::map<std::string, std::vector<int>>>(s);   // == scores
```

Covered: **utility** (`pair`, `tuple`, `optional`, `variant`), **sequence** (`array`, `deque`, `list`, `forward_list`), **associative** (`set`, `multiset`, `map`, `multimap`), **unordered** (`unordered_{set,multiset,map,multimap}`), and **adaptors** (`stack`, `queue`, `priority_queue`). Arbitrary nesting works — `std::map<std::string, std::vector<std::pair<int, std::string>>>` round-trips — and your own types (once they have an `ObjectSerializer`) compose into every container automatically, e.g. `std::vector<Person>` or `std::map<std::string, Person>`. A container of a non-serialisable type is itself not serialisable — a clean compile error, never a silent byte-copy. The container framing (counts, variant tags) is portable; only the trivially-copyable *leaves* carry native byte order.

### nlohmann::json (`encode_decode_json.hpp`)

```cpp
#include "encode_decode_json.hpp"   // requires nlohmann/json.hpp on the include path

Binary blob = {0x48, 0x65, 0x6C, 0x6C, 0x6F};
nlohmann::json j;
j["payload"] = blob;                        // -> "SGVsbG8="
Binary back = j["payload"].get<Binary>();   // decoded
```

Reads are tolerant: Base64 strings (padded or not), nlohmann's default array-of-numbers form, and binary subtype values (CBOR/MessagePack) all deserialize into `Binary`. Include the header consistently in every translation unit that converts `Binary` to or from json.

### Supported Encoding Schemes

For each of `Base64`, `Base64Url`, `Base64UrlNoPad`, `Base32`, `Base32Hex`, `Base36`, `Base32Crockford`, `Base16`, `Base8`, `Base4`, `Base2`:

- `EncodeXxx` / `DecodeXxx` — encode any byte source / decode to `std::string`
- `EncodeXxxBinary` / `DecodeXxxBinary` — same encode; decode to `Binary`
- `EncodeXxxParallel` / `DecodeXxxParallel` / `EncodeXxxBinaryParallel` / `DecodeXxxBinaryParallel` — multithreaded versions
- `EncodeXxxStream` / `DecodeXxxStream` and `XxxStreamEncoder` / `XxxStreamDecoder` — streaming (adapter)
- `EncodeXxxFile` / `DecodeXxxFile` and `MappedFile` — memory-mapped whole-file I/O (adapter)
- `EncodeXxxBitset` / `DecodeXxxBitset<N>` — `std::bitset` (adapter)
- `EncodeXxxObject` / `DecodeXxxObject<T>` — trivially-copyable object serialization (adapter)
- `EncodeXxxBitstring` / `DecodeXxxBitstring` — BSD `<bitstring.h>` (adapter, where available)

Decoders throw `std::invalid_argument` on characters outside the alphabet or on `=` anywhere but trailing padding.

### Options

| Switch                | Effect                                                                                                                        |
| --------------------- | ----------------------------------------------------------------------------------------------------------------------------- |
| `SNICHOLLS_NO_SIMD` | define before including to disable the SIMD drop-ins entirely                                                                 |
| C++23                 | automatically uses`resize_and_overwrite` to skip zeroing output buffers                                                     |
| (default)             | SIMD auto-detected:`__SSSE3__` on x86 (on by default on macOS; `-mssse3` or `-march=native` elsewhere), NEON on AArch64 |

## Building

Consume with CMake:

```cmake
add_subdirectory(base-encode-decode)
target_link_libraries(your_app PRIVATE snicholls::base_encode_decode)
```

### Tests and benchmark

```sh
make test                 # runs the suite twice: SIMD build and scalar build
make bench                # throughput benchmark, 512 MiB input
make bench-scalar         # same benchmark with SIMD disabled, for comparison
make bench BENCH_MB=2048  # multi-GB benchmark
make demo
```

or with CMake: `cmake -S . -B build/cmake -DCMAKE_BUILD_TYPE=Release && cmake --build build/cmake -j && ctest --test-dir build/cmake`.

The test suite is deliberately lightweight — plain `assert()`, no framework. It covers: RFC 4648 section 10 vectors (also as `static_assert`s), padding shape, round trips for every scheme over string/Binary/generic inputs, Base64Url equivalence properties, streaming with every chunking of the input, the bitset/bitstring/format/web/json adapters, parallel-equals-serial across sizes and thread counts, malformed input rejection (including errors surfacing from worker threads and SIMD validation), and cross-validation of Base64 against cpp-base64. The whole suite runs twice — SIMD active and forced scalar — and is exercised on both x86 and ARM.

Third-party code (`cpp-base64`, `nlohmann/json`) is vendored under `tests/third_party/` and used **only** by the tests; the library itself depends on nothing.

## Benchmarks

`bench` reports GB/s of raw binary bytes (best of 3 runs, timings include output allocation). What the SIMD drop-ins add on top of the block-unrolled scalar core, Intel Xeon W-3245, 512 MiB input:

```text
Base64                        encode        decode
  scalar serial               0.79 GB/s     1.24 GB/s
  SIMD serial (SSSE3)         1.06 GB/s     2.20 GB/s
  SIMD parallel (auto)        4.71 GB/s     4.57 GB/s
  cpp-base64 (reference)      0.21 GB/s     0.06 GB/s

Base16
  scalar serial               0.60 GB/s     1.01 GB/s
  SIMD serial (SSSE3)         0.85 GB/s     1.89 GB/s
  SIMD parallel (auto)        3.48 GB/s     4.44 GB/s

Base32 (no SIMD kernel - block scalar core)
  serial                      0.67 GB/s     1.00 GB/s
  parallel (auto)             4.12 GB/s     4.10 GB/s
```

Raspberry Pi (aarch64, 4 cores, 128 MiB input):

```text
Base64                        encode        decode
  scalar serial               0.26 GB/s     0.27 GB/s
  SIMD serial (NEON)          0.42 GB/s     0.37 GB/s
  SIMD parallel (auto)        0.70 GB/s     0.51 GB/s
  cpp-base64 (reference)      0.11 GB/s     0.03 GB/s

Base16
  scalar serial               0.20 GB/s     0.20 GB/s
  SIMD serial (NEON)          0.34 GB/s     0.37 GB/s
```

At these sizes parallel throughput is bounded by memory bandwidth and page faults on the freshly allocated output, not by the codec. Numbers vary with hardware.

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
  serial encode:   1.1955 GB/s
  parallel encode: 2.17205 GB/s
  outputs identical: yes
  parallel decode: 1.96782 GB/s
  round trip ok:   yes

DNA/RNA Packing Demo:
Sequence (26 bases): ACGTACGTTTAGGCCANNNNRYSWKM
  4-bit IUPAC packed: 13 bytes (26 -> 13, 2x)
  unpacked: ACGTACGTTTAGGCCANNNNRYSWKM
Canonical (16 bases): ACGTACGTTTAGGCCA
  2-bit packed: 4 bytes (4x)
  as Base64 for transport: GxvylA==
  round trip: ACGTACGTTTAGGCCA

Object Serialization Demo:
SensorReading{7, 21.5, 'K'} as Base64Url: BwAAAAAAAAAAAAAAAIA1QEsAAAAAAAAA
  decoded: id=7 celsius=21.5 status=K
  24 bytes -> 32 chars (same-ABI snapshot)
map<string,vector<int>> as Base64: AgVhbGljZQ0DWgAAAFUAAABcAAAAA2JvYgkCRgAAAFgAAAA=
  round trip ok: yes (2 entries)

BSD <bitstring.h> Interop Demo:
12-bit bitstring as Base2:  100110000001
12-bit bitstring as Base64: mB==
decoded bits set: 0 3 4 11
```

## Notes

- **Padding**: earlier versions of this library emitted unpadded Base64/Base32; the encoders now pad per RFC 4648. Decoders accept both padded and unpadded input.
- **Bitstring functions moved**: `Encode*Bitstring`/`Decode*Bitstring` now live in the `encode_decode_bitstring.hpp` adapter — add that include if you used them from the main header.
- **Endianness**: base encodings operate on byte streams, so output is identical on all platforms. If you encode multi-byte types, convert them to a defined byte order first — that is a serialization concern, not a codec one.
- **SIMD verification**: both kernel sets are runtime-verified by this repository's test suite — SSSE3 on an Intel Xeon (Apple clang and Homebrew clang) and NEON on a Raspberry Pi running 64-bit Raspberry Pi OS (GCC 14). Run `make test` on your own target to re-verify; the suite always runs both the SIMD and forced-scalar builds.
- **Secrets**: the decoders use data-dependent table lookups and are not constant-time. For encoding cryptographic key material where side channels matter, use a hardened implementation (e.g. libsodium's `sodium_bin2base64`).
- **Base36 / Base32Crockford**: bit-group encodings (5 bits per character) sharing the machinery of the RFC schemes — not arithmetic base-N conversions. (Crockford Base32 is also the ULID alphabet, if you are implementing ULIDs.)
- **Thread safety**: all functions are stateless and safe to call concurrently; each stream encoder/decoder instance is single-threaded.

## Contributing

Improvements and pull requests are very welcome — new adapters, more `ObjectSerializer` specialisations, additional SIMD kernels (AVX2/AVX-512, or SIMD for more schemes), bug fixes, docs, and platform reports all help. A few pointers to keep things consistent:

- **Stay simple and dependency-free.** The library core takes no dependencies; anything heavier belongs behind an opt-in adapter header, and third-party code used only for testing goes under `tests/third_party/`.
- **Add tests.** The suite is plain `assert()` (no framework) in `tests/test_main.cpp`. `make test` builds and runs it twice — once with SIMD, once with `-DSNICHOLLS_NO_SIMD` — so both paths stay honest. New behaviour should come with a round-trip and, where it applies, a `static_assert` for the compile-time path.
- **Keep every scheme uniform.** Schemes live in the single `SNICHOLLS_FOR_EACH_SCHEME` table; adding one there propagates it across every API and adapter.
- **Verify where it matters.** SIMD and platform-specific code should be run on the relevant target — `make test` on an ARM box (e.g. a Raspberry Pi) exercises the NEON path; the Intel path is covered on x86.

Feel free to open an issue to discuss larger changes first. Be kind and constructive in reviews.

## License

MIT — see [LICENSE](LICENSE). Copyright (c) 2024 Saxon Nicholls.

The vendored test-only dependencies keep their own licenses: [cpp-base64](https://github.com/ReneNyffenegger/cpp-base64) and [nlohmann/json](https://github.com/nlohmann/json) live under `tests/third_party/` and are never part of the library you ship.
