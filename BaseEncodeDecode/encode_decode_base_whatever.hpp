// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Saxon Nicholls

//
//  encode_decode_base_whatever.hpp
//  BaseEncodeDecode
//
//  Created by Saxon Nicholls on 1/8/2024.
//
//  me [at] saxonnicholls.com

#ifndef encode_decode_base_whatever_hpp
#define encode_decode_base_whatever_hpp

#include <string>
#include <string_view>
#include <stdexcept>
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <numeric>
#include <ranges>
#include <thread>
#include <type_traits>
#include <vector>

#include "alphabet.hpp"

// Define Binary as a type alias for std::vector<uint8_t>
using Binary = std::vector<uint8_t>;

// Complies with RFC 4648 and RFC 2045 for Base64, Base32, Base32Hex, and Base16 encodings.
namespace snicholls {

    // Any contiguous range of byte-like elements: std::string, std::string_view,
    // std::vector<char>, std::vector<uint8_t>, std::span, std::array, C arrays,
    // mmap'd buffers wrapped in a span, ... Encoding and decoding read the bytes
    // in place, so no copy of the input is ever made.
    //
    // Note: const char* / string literals are NOT ranges; the named wrappers
    // below provide const char* overloads with the usual NUL-terminated
    // semantics (a literal's trailing '\0' is not encoded).
    template<typename R>
    concept ByteSource =
        std::ranges::contiguous_range<R> &&
        sizeof(std::ranges::range_value_t<R>) == 1 &&
        (std::is_integral_v<std::ranges::range_value_t<R>> ||
         std::is_same_v<std::remove_cv_t<std::ranges::range_value_t<R>>, std::byte>);

    namespace detail {

        // O(1) reverse lookup: character -> alphabet index, or -1 if not in the
        // alphabet. consteval: the table is always built at compile time.
        //
        // Only the first 2^BitGroupSize symbols are mapped. These are bit-group
        // codecs, so an index must fit in BitGroupSize bits; an alphabet listing
        // more symbols than that (Base36 lists 36 but is driven at 5 bits) can
        // never emit the surplus, and accepting them on decode would shift a
        // too-large index into the neighbouring group and silently corrupt the
        // output. Leaving them at -1 makes the existing checks reject them.
        template<size_t BitGroupSize, size_t AlphabetSize, const std::array<char, AlphabetSize>& Alphabet>
        consteval std::array<int8_t, 256> MakeReverseTable() {
            static_assert(AlphabetSize >= (size_t{1} << BitGroupSize),
                          "Alphabet is too small for BitGroupSize");
            std::array<int8_t, 256> table{};
            for (auto& entry : table) {
                entry = -1;
            }
            for (size_t i = 0; i < (size_t{1} << BitGroupSize); ++i) {
                table[static_cast<unsigned char>(Alphabet[i])] = static_cast<int8_t>(i);
            }
            return table;
        }

        template<size_t BitGroupSize, size_t AlphabetSize, const std::array<char, AlphabetSize>& Alphabet>
        inline constexpr std::array<int8_t, 256> ReverseTable =
            MakeReverseTable<BitGroupSize, AlphabetSize, Alphabet>();

        // A "block" is the smallest run of whole bytes that encodes to whole
        // characters: lcm(8, BitGroupSize) bits. Base64: 3 bytes <-> 4 chars;
        // Base32: 5 bytes <-> 8 chars; Base16: 1 byte <-> 2 chars.
        template<size_t BitGroupSize>
        inline constexpr size_t BlockBytes = BitGroupSize / std::gcd(size_t{8}, BitGroupSize);

        template<size_t BitGroupSize>
        inline constexpr size_t BlockChars = 8 / std::gcd(size_t{8}, BitGroupSize);

        // Below this input size the auto-threaded parallel functions stay serial
        inline constexpr size_t ParallelMinBytes = size_t{1} << 20;

        // Optional SIMD acceleration. The per-architecture drop-in headers
        // (encode_decode_simd_intel.hpp / encode_decode_simd_arm.hpp, included
        // below unless SNICHOLLS_NO_SIMD is defined) specialize this for the
        // alphabets they accelerate. The primary template means "no
        // acceleration here - use the scalar path". Kernels advance first/out
        // past the data they handled and leave the remainder (tails, or
        // anything after a suspect character) to the scalar code.
        template<size_t AlphabetSize, const std::array<char, AlphabetSize>& Alphabet>
        struct SimdCodec {
            static constexpr bool Available = false;
        };

        // Encode bytes [first, last) into out; returns one past the last char written.
        // first must sit on a block boundary of the overall input.
        template<size_t BitGroupSize, size_t AlphabetSize, const std::array<char, AlphabetSize>& Alphabet>
        inline char* EncodeChunk(const uint8_t* first, const uint8_t* last, char* out) {
            constexpr size_t mask = (size_t{1} << BitGroupSize) - 1;
            constexpr size_t blockBytes = BlockBytes<BitGroupSize>;
            constexpr size_t blockChars = BlockChars<BitGroupSize>;

            if constexpr (SimdCodec<AlphabetSize, Alphabet>::Available) {
                SimdCodec<AlphabetSize, Alphabet>::Encode(first, last, out);
            }

            // Whole blocks: no bit buffer carried between iterations; both
            // fixed-count inner loops unroll completely
            while (static_cast<size_t>(last - first) >= blockBytes) {
                uint64_t word = 0;
                for (size_t i = 0; i < blockBytes; ++i) {
                    word = (word << 8) | first[i];
                }
                for (size_t i = 0; i < blockChars; ++i) {
                    out[i] = Alphabet[(word >> ((blockChars - 1 - i) * BitGroupSize)) & mask];
                }
                first += blockBytes;
                out += blockChars;
            }

            // Tail: fewer than blockBytes bytes remain
            size_t bitBuffer = 0;
            int bitBufferLength = 0;
            for (; first != last; ++first) {
                bitBuffer = (bitBuffer << 8) | *first;
                bitBufferLength += 8;

                while (bitBufferLength >= static_cast<int>(BitGroupSize)) {
                    *out++ = Alphabet[(bitBuffer >> (bitBufferLength - BitGroupSize)) & mask];
                    bitBufferLength -= BitGroupSize;
                }
            }
            if (bitBufferLength > 0) {
                *out++ = Alphabet[(bitBuffer << (BitGroupSize - bitBufferLength)) & mask];
            }
            return out;
        }

        // Decode characters [first, last) into out; returns one past the last byte
        // written. first must sit on a block boundary of the overall input and the
        // range must not contain padding.
        template<size_t BitGroupSize, size_t AlphabetSize, const std::array<char, AlphabetSize>& Alphabet>
        inline uint8_t* DecodeChunk(const char* first, const char* last, uint8_t* out) {
            constexpr size_t blockBytes = BlockBytes<BitGroupSize>;
            constexpr size_t blockChars = BlockChars<BitGroupSize>;

            if constexpr (SimdCodec<AlphabetSize, Alphabet>::Available) {
                // Stops early at the vector containing any suspect character;
                // the scalar code below re-examines it and throws precisely
                SimdCodec<AlphabetSize, Alphabet>::Decode(first, last, out);
            }

            // Whole blocks, validation deferred to the end of each block
            while (static_cast<size_t>(last - first) >= blockChars) {
                uint64_t word = 0;
                bool invalid = false;
                for (size_t i = 0; i < blockChars; ++i) {
                    const int index = ReverseTable<BitGroupSize, AlphabetSize, Alphabet>[static_cast<unsigned char>(first[i])];
                    invalid = invalid || (index < 0);
                    word = (word << BitGroupSize) | static_cast<uint8_t>(index);
                }
                if (invalid) {
                    break; // the tail loop below throws at the exact character
                }
                for (size_t j = 0; j < blockBytes; ++j) {
                    out[j] = static_cast<uint8_t>((word >> ((blockBytes - 1 - j) * 8)) & 0xFF);
                }
                first += blockChars;
                out += blockBytes;
            }

            // Tail (or rescan of a block containing an invalid character)
            size_t bitBuffer = 0;
            int bitBufferLength = 0;
            for (; first != last; ++first) {
                int index = ReverseTable<BitGroupSize, AlphabetSize, Alphabet>[static_cast<unsigned char>(*first)];
                if (index < 0) {
                    throw std::invalid_argument("Invalid character in encoded string");
                }

                bitBuffer = (bitBuffer << BitGroupSize) | static_cast<size_t>(index);
                bitBufferLength += BitGroupSize;

                if (bitBufferLength >= 8) {
                    *out++ = static_cast<uint8_t>((bitBuffer >> (bitBufferLength - 8)) & 0xFF);
                    bitBufferLength -= 8;
                }
            }
            return out;
        }

        // Build a string of exactly `size` characters, skipping the serial
        // zero-fill that resize() performs when the library supports it
        // (C++23 resize_and_overwrite; plain resize as the C++20 fallback).
        // Out is any std::basic_string<char, ...> (e.g. std::string or the
        // wiped SecureString from utils/secure.hpp).
        template<typename Out = std::string, typename Fill>
        inline Out MakeFilledString(size_t size, Fill&& fill) {
#if defined(__cpp_lib_string_resize_and_overwrite)
            Out output;
            output.resize_and_overwrite(size, [&](char* data, size_t n) {
                fill(data);
                return n;
            });
            return output;
#else
            Out output;
            output.resize(size);
            fill(output.data());
            return output;
#endif
        }

    } // namespace detail
} // namespace snicholls (reopened below)

// Architecture-specific SIMD drop-ins (each is a no-op on other architectures).
// Define SNICHOLLS_NO_SIMD before including this header to stay purely scalar.
#if !defined(SNICHOLLS_NO_SIMD)
#include "encode_decode_simd_intel.hpp"
#include "encode_decode_simd_arm.hpp"
#endif

namespace snicholls {

    // Forward declarations: the constexpr front-ends below delegate to these
    // chunked implementations at runtime (threadCount 1 = single-threaded).
    // Out is the output string type (defaults to std::string).
    template<typename Out, size_t BitGroupSize, size_t AlphabetSize, const std::array<char, AlphabetSize>& Alphabet, bool PaddingRequired>
    Out BaseEncodeParallelBytes(const uint8_t* data, size_t size, unsigned threadCount = 0);

    template<typename Container, size_t BitGroupSize, size_t AlphabetSize, const std::array<char, AlphabetSize>& Alphabet, bool PaddingRequired>
    Container BaseDecodeParallelImpl(const char* data, size_t size, unsigned threadCount = 0);

    // Templated function for Base Encoding. Accepts any ByteSource and reads it
    // in place. constexpr: usable at compile time, e.g.
    //   static_assert(EncodeBase64("foo") == "Zm9v");
    template<typename Out, size_t BitGroupSize, size_t AlphabetSize, const std::array<char, AlphabetSize>& Alphabet, bool PaddingRequired, ByteSource R>
    constexpr Out BaseEncode(const R& input) {
        if (!std::is_constant_evaluated()) {
            // Runtime: block-unrolled chunk path with optional SIMD;
            // threadCount 1 keeps this strictly single-threaded
            return BaseEncodeParallelBytes<Out, BitGroupSize, AlphabetSize, Alphabet, PaddingRequired>(
                reinterpret_cast<const uint8_t*>(std::ranges::data(input)), std::ranges::size(input), 1);
        }

        constexpr size_t mask = (size_t{1} << BitGroupSize) - 1;
        Out output;
        size_t bitBuffer = 0;
        int bitBufferLength = 0;

        for (auto element : input) {
            bitBuffer = (bitBuffer << 8) | static_cast<uint8_t>(element);
            bitBufferLength += 8;

            while (bitBufferLength >= static_cast<int>(BitGroupSize)) {
                output += Alphabet[(bitBuffer >> (bitBufferLength - BitGroupSize)) & mask];
                bitBufferLength -= BitGroupSize;
            }
        }

        if (bitBufferLength > 0) {
            output += Alphabet[(bitBuffer << (BitGroupSize - bitBufferLength)) & mask];
        }

        if (PaddingRequired) {
            // RFC 4648: pad to a full block. A block is lcm(8, BitGroupSize) bits,
            // i.e. 8 / gcd(8, BitGroupSize) characters (4 for Base64, 8 for Base32).
            while (output.length() % detail::BlockChars<BitGroupSize>) {
                output += '=';
            }
        }

        return output;
    }

    // Kept for compatibility: Binary is just one of the ByteSources BaseEncode accepts
    template<typename Out, size_t BitGroupSize, size_t AlphabetSize, const std::array<char, AlphabetSize>& Alphabet, bool PaddingRequired, ByteSource R>
    constexpr Out BaseEncodeBinary(const R& input) {
        return BaseEncode<Out, BitGroupSize, AlphabetSize, Alphabet, PaddingRequired>(input);
    }

    // Templated function for Base Decoding (string version)
    template<size_t BitGroupSize, size_t AlphabetSize, const std::array<char, AlphabetSize>& Alphabet, bool PaddingRequired, ByteSource R>
    constexpr std::string BaseDecode(const R& input) {
        if (!std::is_constant_evaluated()) {
            return BaseDecodeParallelImpl<std::string, BitGroupSize, AlphabetSize, Alphabet, PaddingRequired>(
                reinterpret_cast<const char*>(std::ranges::data(input)), std::ranges::size(input), 1);
        }

        std::string output;
        size_t bitBuffer = 0;
        int bitBufferLength = 0;
        bool paddingSeen = false;

        for (auto element : input) {
            const auto c = static_cast<unsigned char>(element);
            if (PaddingRequired && c == '=') {
                paddingSeen = true;
                continue;
            }

            // RFC 4648: '=' is only valid as trailing padding
            if (paddingSeen) {
                throw std::invalid_argument("Invalid character after padding");
            }

            int index = detail::ReverseTable<BitGroupSize, AlphabetSize, Alphabet>[c];
            if (index < 0) {
                throw std::invalid_argument("Invalid character in encoded string");
            }

            bitBuffer = (bitBuffer << BitGroupSize) | static_cast<size_t>(index);
            bitBufferLength += BitGroupSize;

            if (bitBufferLength >= 8) {
                output += static_cast<char>((bitBuffer >> (bitBufferLength - 8)) & 0xFF);
                bitBufferLength -= 8;
            }
        }

        return output;
    }

    // Templated function for Base Decoding (binary version)
    template<size_t BitGroupSize, size_t AlphabetSize, const std::array<char, AlphabetSize>& Alphabet, bool PaddingRequired, ByteSource R>
    constexpr Binary BaseDecodeBinary(const R& input) {
        if (!std::is_constant_evaluated()) {
            return BaseDecodeParallelImpl<Binary, BitGroupSize, AlphabetSize, Alphabet, PaddingRequired>(
                reinterpret_cast<const char*>(std::ranges::data(input)), std::ranges::size(input), 1);
        }

        Binary output;
        size_t bitBuffer = 0;
        int bitBufferLength = 0;
        bool paddingSeen = false;

        for (auto element : input) {
            const auto c = static_cast<unsigned char>(element);
            if (PaddingRequired && c == '=') {
                paddingSeen = true;
                continue;
            }

            // RFC 4648: '=' is only valid as trailing padding
            if (paddingSeen) {
                throw std::invalid_argument("Invalid character after padding");
            }

            int index = detail::ReverseTable<BitGroupSize, AlphabetSize, Alphabet>[c];
            if (index < 0) {
                throw std::invalid_argument("Invalid character in encoded string");
            }

            bitBuffer = (bitBuffer << BitGroupSize) | static_cast<size_t>(index);
            bitBufferLength += BitGroupSize;

            if (bitBufferLength >= 8) {
                output.push_back(static_cast<uint8_t>((bitBuffer >> (bitBufferLength - 8)) & 0xFF));
                bitBufferLength -= 8;
            }
        }

        return output;
    }

    // ------------------------------------------------------------------
    // Parallel versions for very large inputs (multi-GB)
    //
    // Every scheme here encodes a fixed number of bits per character, so the
    // input can be split at block boundaries (lcm(8, BitGroupSize) bits) and
    // the pieces processed independently, each thread writing to its own
    // slice of a preallocated result. Output is bit-for-bit identical to the
    // serial versions.
    //
    // threadCount == 0 means "auto": use all hardware threads, falling back
    // to a single thread when the input is too small to benefit. An explicit
    // threadCount forces that many threads (capped at one per block).
    // ------------------------------------------------------------------

    // Number of encoded characters produced from `inputSize` raw bytes.
    // Lets callers (e.g. the mmap adapter) size a destination file up front.
    template<size_t BitGroupSize, bool PaddingRequired>
    constexpr size_t EncodedLength(size_t inputSize) noexcept {
        constexpr size_t blockBytes = detail::BlockBytes<BitGroupSize>;
        constexpr size_t blockChars = detail::BlockChars<BitGroupSize>;
        const size_t fullBlocks = inputSize / blockBytes;
        const size_t tailBytes = inputSize - fullBlocks * blockBytes;
        const size_t tailChars = (tailBytes * 8 + BitGroupSize - 1) / BitGroupSize;
        size_t encodedLength = fullBlocks * blockChars + tailChars;
        if (PaddingRequired && encodedLength % blockChars) {
            encodedLength += blockChars - encodedLength % blockChars;
        }
        return encodedLength;
    }

    // Encode [data, data+size) into the caller-provided buffer outData, which
    // must hold at least EncodedLength<BitGroupSize, PaddingRequired>(size)
    // characters. Worker threads write disjoint slices, so outData may be a
    // freshly mapped output file (the mmap adapter relies on this). No
    // allocation and no zero-fill. threadCount 0 = auto.
    template<size_t BitGroupSize, size_t AlphabetSize, const std::array<char, AlphabetSize>& Alphabet, bool PaddingRequired>
    void BaseEncodeParallelInto(const uint8_t* data, size_t size, char* outData, unsigned threadCount = 0) {
        constexpr size_t blockBytes = detail::BlockBytes<BitGroupSize>;
        constexpr size_t blockChars = detail::BlockChars<BitGroupSize>;

        if (threadCount == 0) {
            threadCount = std::thread::hardware_concurrency();
            if (size < detail::ParallelMinBytes) {
                threadCount = 1;
            }
        }
        const size_t fullBlocks = size / blockBytes;
        threadCount = static_cast<unsigned>(std::min<size_t>(std::max(threadCount, 1u), std::max<size_t>(fullBlocks, 1)));
        const size_t encodedLength = EncodedLength<BitGroupSize, PaddingRequired>(size);

        if (threadCount > 1) {
            std::vector<std::thread> workers;
            workers.reserve(threadCount - 1);
            const size_t blocksPerThread = fullBlocks / threadCount;
            const size_t extraBlocks = fullBlocks % threadCount;

            size_t blockStart = blocksPerThread + (extraBlocks > 0 ? 1 : 0); // thread 0's share runs on this thread
            for (unsigned t = 1; t < threadCount; ++t) {
                const size_t blockCount = blocksPerThread + (t < extraBlocks ? 1 : 0);
                const uint8_t* first = data + blockStart * blockBytes;
                const uint8_t* last = first + blockCount * blockBytes;
                char* dest = outData + blockStart * blockChars;
                workers.emplace_back([first, last, dest] {
                    detail::EncodeChunk<BitGroupSize, AlphabetSize, Alphabet>(first, last, dest);
                });
                blockStart += blockCount;
            }
            detail::EncodeChunk<BitGroupSize, AlphabetSize, Alphabet>(
                data, data + (blocksPerThread + (extraBlocks > 0 ? 1 : 0)) * blockBytes, outData);
            for (auto& worker : workers) {
                worker.join();
            }
            // Tail (partial block) plus any padding
            char* end = detail::EncodeChunk<BitGroupSize, AlphabetSize, Alphabet>(
                data + fullBlocks * blockBytes, data + size, outData + fullBlocks * blockChars);
            while (end != outData + encodedLength) {
                *end++ = '=';
            }
        } else {
            char* end = detail::EncodeChunk<BitGroupSize, AlphabetSize, Alphabet>(data, data + size, outData);
            while (end != outData + encodedLength) {
                *end++ = '=';
            }
        }
    }

    template<typename Out, size_t BitGroupSize, size_t AlphabetSize, const std::array<char, AlphabetSize>& Alphabet, bool PaddingRequired>
    Out BaseEncodeParallelBytes(const uint8_t* data, size_t size, unsigned threadCount) {
        // MakeFilledString avoids zeroing the buffer before it is written, and
        // worker threads first-touch the pages of their own output slices
        return detail::MakeFilledString<Out>(EncodedLength<BitGroupSize, PaddingRequired>(size), [&](char* outData) {
            BaseEncodeParallelInto<BitGroupSize, AlphabetSize, Alphabet, PaddingRequired>(data, size, outData, threadCount);
        });
    }

    // Number of raw bytes decoding [data, data+size) yields. Strips trailing
    // padding for padded schemes, so it inspects the buffer tail.
    template<size_t BitGroupSize, bool PaddingRequired>
    size_t DecodedLength(const char* data, size_t size) noexcept {
        constexpr size_t blockBytes = detail::BlockBytes<BitGroupSize>;
        constexpr size_t blockChars = detail::BlockChars<BitGroupSize>;
        size_t contentLength = size;
        if (PaddingRequired) {
            while (contentLength > 0 && data[contentLength - 1] == '=') {
                --contentLength;
            }
        }
        const size_t fullBlocks = contentLength / blockChars;
        const size_t tailChars = contentLength - fullBlocks * blockChars;
        const size_t tailBytes = tailChars * BitGroupSize / 8;
        return fullBlocks * blockBytes + tailBytes;
    }

    // Decode [data, data+size) into the caller-provided buffer dest, which must
    // hold at least DecodedLength<BitGroupSize, PaddingRequired>(data, size)
    // bytes. dest may be a freshly mapped output file. Throws
    // std::invalid_argument on malformed input (including from worker threads).
    // threadCount 0 = auto.
    template<size_t BitGroupSize, size_t AlphabetSize, const std::array<char, AlphabetSize>& Alphabet, bool PaddingRequired>
    void BaseDecodeParallelInto(const char* data, size_t size, uint8_t* dest, unsigned threadCount = 0) {
        constexpr size_t blockBytes = detail::BlockBytes<BitGroupSize>;
        constexpr size_t blockChars = detail::BlockChars<BitGroupSize>;

        // RFC 4648: '=' may only appear as trailing padding. Interior '=' is
        // rejected by DecodeChunk since it belongs to no alphabet.
        size_t contentLength = size;
        if (PaddingRequired) {
            while (contentLength > 0 && data[contentLength - 1] == '=') {
                --contentLength;
            }
        }

        if (threadCount == 0) {
            threadCount = std::thread::hardware_concurrency();
            if (contentLength < detail::ParallelMinBytes) {
                threadCount = 1;
            }
        }
        const size_t fullBlocks = contentLength / blockChars;
        threadCount = static_cast<unsigned>(std::min<size_t>(std::max(threadCount, 1u), std::max<size_t>(fullBlocks, 1)));

        if (threadCount > 1) {
            std::vector<std::thread> workers;
            workers.reserve(threadCount - 1);
            std::vector<std::exception_ptr> errors(threadCount);
            const size_t blocksPerThread = fullBlocks / threadCount;
            const size_t extraBlocks = fullBlocks % threadCount;

            size_t blockStart = blocksPerThread + (extraBlocks > 0 ? 1 : 0); // thread 0's share runs on this thread
            for (unsigned t = 1; t < threadCount; ++t) {
                const size_t blockCount = blocksPerThread + (t < extraBlocks ? 1 : 0);
                const char* first = data + blockStart * blockChars;
                const char* last = first + blockCount * blockChars;
                uint8_t* chunkDest = dest + blockStart * blockBytes;
                std::exception_ptr& error = errors[t];
                workers.emplace_back([first, last, chunkDest, &error] {
                    try {
                        detail::DecodeChunk<BitGroupSize, AlphabetSize, Alphabet>(first, last, chunkDest);
                    } catch (...) {
                        error = std::current_exception();
                    }
                });
                blockStart += blockCount;
            }
            try {
                detail::DecodeChunk<BitGroupSize, AlphabetSize, Alphabet>(
                    data, data + (blocksPerThread + (extraBlocks > 0 ? 1 : 0)) * blockChars, dest);
            } catch (...) {
                errors[0] = std::current_exception();
            }
            for (auto& worker : workers) {
                worker.join();
            }
            for (auto& error : errors) {
                if (error) {
                    std::rethrow_exception(error);
                }
            }
            // Tail (partial block, if the input was not padded to a full block)
            detail::DecodeChunk<BitGroupSize, AlphabetSize, Alphabet>(
                data + fullBlocks * blockChars, data + contentLength, dest + fullBlocks * blockBytes);
        } else {
            detail::DecodeChunk<BitGroupSize, AlphabetSize, Alphabet>(data, data + contentLength, dest);
        }
    }

    template<typename Container, size_t BitGroupSize, size_t AlphabetSize, const std::array<char, AlphabetSize>& Alphabet, bool PaddingRequired>
    Container BaseDecodeParallelImpl(const char* data, size_t size, unsigned threadCount) {
        const size_t decodedSize = DecodedLength<BitGroupSize, PaddingRequired>(data, size);

        if constexpr (std::is_same_v<Container, std::string>) {
            return detail::MakeFilledString(decodedSize, [&](char* outData) {
                BaseDecodeParallelInto<BitGroupSize, AlphabetSize, Alphabet, PaddingRequired>(
                    data, size, reinterpret_cast<uint8_t*>(outData), threadCount);
            });
        } else {
            Container output;
            output.resize(decodedSize); // std::vector has no resize_and_overwrite equivalent
            BaseDecodeParallelInto<BitGroupSize, AlphabetSize, Alphabet, PaddingRequired>(
                data, size, reinterpret_cast<uint8_t*>(output.data()), threadCount);
            return output;
        }
    }

    // Zero-copy adapters: any ByteSource goes straight to the workers
    template<typename Out, size_t BitGroupSize, size_t AlphabetSize, const std::array<char, AlphabetSize>& Alphabet, bool PaddingRequired, ByteSource R>
    Out BaseEncodeParallel(const R& input, unsigned threadCount = 0) {
        return BaseEncodeParallelBytes<Out, BitGroupSize, AlphabetSize, Alphabet, PaddingRequired>(
            reinterpret_cast<const uint8_t*>(std::ranges::data(input)), std::ranges::size(input), threadCount);
    }

    template<typename Out, size_t BitGroupSize, size_t AlphabetSize, const std::array<char, AlphabetSize>& Alphabet, bool PaddingRequired, ByteSource R>
    Out BaseEncodeBinaryParallel(const R& input, unsigned threadCount = 0) {
        return BaseEncodeParallel<Out, BitGroupSize, AlphabetSize, Alphabet, PaddingRequired>(input, threadCount);
    }

    template<size_t BitGroupSize, size_t AlphabetSize, const std::array<char, AlphabetSize>& Alphabet, bool PaddingRequired, ByteSource R>
    std::string BaseDecodeParallel(const R& input, unsigned threadCount = 0) {
        return BaseDecodeParallelImpl<std::string, BitGroupSize, AlphabetSize, Alphabet, PaddingRequired>(
            reinterpret_cast<const char*>(std::ranges::data(input)), std::ranges::size(input), threadCount);
    }

    template<size_t BitGroupSize, size_t AlphabetSize, const std::array<char, AlphabetSize>& Alphabet, bool PaddingRequired, ByteSource R>
    Binary BaseDecodeBinaryParallel(const R& input, unsigned threadCount = 0) {
        return BaseDecodeParallelImpl<Binary, BitGroupSize, AlphabetSize, Alphabet, PaddingRequired>(
            reinterpret_cast<const char*>(std::ranges::data(input)), std::ranges::size(input), threadCount);
    }


    // ------------------------------------------------------------------
    // Named entry points, one family per scheme, generated from one table.
    //
    // Every Encode*/Decode* name accepts any ByteSource plus a const char*
    // overload for NUL-terminated strings and literals. The serial forms are
    // constexpr and evaluate at compile time; the *Parallel forms take an
    // optional thread count (0 = auto). The *Binary decode forms return
    // Binary instead of std::string; the *Binary encode forms are kept for
    // compatibility and behave exactly like their plain counterparts.
    // ------------------------------------------------------------------

#define SNICHOLLS_DEFINE_SCHEME(Name, BitGroupSize, AlphabetSize, Alphabet, Padded) \
    template<typename Out = std::string, ByteSource R> \
    constexpr Out Encode##Name(const R& input) { \
        return BaseEncode<Out, BitGroupSize, AlphabetSize, Alphabet, Padded>(input); \
    } \
    constexpr std::string Encode##Name(const char* input) { \
        return Encode##Name(std::string_view{input}); \
    } \
    template<ByteSource R> \
    constexpr std::string Decode##Name(const R& input) { \
        return BaseDecode<BitGroupSize, AlphabetSize, Alphabet, Padded>(input); \
    } \
    constexpr std::string Decode##Name(const char* input) { \
        return Decode##Name(std::string_view{input}); \
    } \
    template<typename Out = std::string, ByteSource R> \
    constexpr Out Encode##Name##Binary(const R& input) { \
        return BaseEncode<Out, BitGroupSize, AlphabetSize, Alphabet, Padded>(input); \
    } \
    template<ByteSource R> \
    constexpr Binary Decode##Name##Binary(const R& input) { \
        return BaseDecodeBinary<BitGroupSize, AlphabetSize, Alphabet, Padded>(input); \
    } \
    constexpr Binary Decode##Name##Binary(const char* input) { \
        return Decode##Name##Binary(std::string_view{input}); \
    } \
    template<typename Out = std::string, ByteSource R> \
    Out Encode##Name##Parallel(const R& input, unsigned threadCount = 0) { \
        return BaseEncodeParallel<Out, BitGroupSize, AlphabetSize, Alphabet, Padded>(input, threadCount); \
    } \
    inline std::string Encode##Name##Parallel(const char* input, unsigned threadCount = 0) { \
        return Encode##Name##Parallel(std::string_view{input}, threadCount); \
    } \
    template<ByteSource R> \
    std::string Decode##Name##Parallel(const R& input, unsigned threadCount = 0) { \
        return BaseDecodeParallel<BitGroupSize, AlphabetSize, Alphabet, Padded>(input, threadCount); \
    } \
    inline std::string Decode##Name##Parallel(const char* input, unsigned threadCount = 0) { \
        return Decode##Name##Parallel(std::string_view{input}, threadCount); \
    } \
    template<typename Out = std::string, ByteSource R> \
    Out Encode##Name##BinaryParallel(const R& input, unsigned threadCount = 0) { \
        return BaseEncodeParallel<Out, BitGroupSize, AlphabetSize, Alphabet, Padded>(input, threadCount); \
    } \
    template<ByteSource R> \
    Binary Decode##Name##BinaryParallel(const R& input, unsigned threadCount = 0) { \
        return BaseDecodeBinaryParallel<BitGroupSize, AlphabetSize, Alphabet, Padded>(input, threadCount); \
    } \
    inline Binary Decode##Name##BinaryParallel(const char* input, unsigned threadCount = 0) { \
        return Decode##Name##BinaryParallel(std::string_view{input}, threadCount); \
    }

// The scheme table: X(Name, BitGroupSize, AlphabetSize, Alphabet, PaddingRequired),
// one row per scheme. Base64Url is RFC 4648 section 5; Base64UrlNoPad is the
// unpadded JWT-style variant. The adapter headers (bitstring, bitset, stream,
// ...) apply their own X to this same table, so the scheme list lives in
// exactly one place. It intentionally stays defined after this header.
#define SNICHOLLS_FOR_EACH_SCHEME(X) \
    X(Base64,          6, 64, snicholls::Base64Alphabet,          true)  \
    X(Base64Url,       6, 64, snicholls::Base64UrlAlphabet,       true)  \
    X(Base64UrlNoPad,  6, 64, snicholls::Base64UrlAlphabet,       false) \
    X(Base32,          5, 32, snicholls::Base32Alphabet,          true)  \
    X(Base32Hex,       5, 32, snicholls::Base32HexAlphabet,       true)  \
    X(Base36,          5, 36, snicholls::Base36Alphabet,          false) \
    X(Base32Crockford, 5, 32, snicholls::Base32CrockfordAlphabet, false) \
    X(Base16,          4, 16, snicholls::Base16Alphabet,          false) \
    X(Base8,           3, 8,  snicholls::Base8Alphabet,           false) \
    X(Base4,           2, 4,  snicholls::Base4Alphabet,           false) \
    X(Base2,           1, 2,  snicholls::Base2Alphabet,           false)

    SNICHOLLS_FOR_EACH_SCHEME(SNICHOLLS_DEFINE_SCHEME)

#undef SNICHOLLS_DEFINE_SCHEME
}

#endif /* encode_decode_base_whatever_hpp */
