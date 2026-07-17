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

#if __has_include(<bitstring.h>)
#include <bitstring.h>
#define SNICHOLLS_HAS_BITSTRING 1
#endif

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
        template<size_t AlphabetSize, const std::array<char, AlphabetSize>& Alphabet>
        consteval std::array<int8_t, 256> MakeReverseTable() {
            std::array<int8_t, 256> table{};
            for (auto& entry : table) {
                entry = -1;
            }
            for (size_t i = 0; i < AlphabetSize; ++i) {
                table[static_cast<unsigned char>(Alphabet[i])] = static_cast<int8_t>(i);
            }
            return table;
        }

        template<size_t AlphabetSize, const std::array<char, AlphabetSize>& Alphabet>
        inline constexpr std::array<int8_t, 256> ReverseTable = MakeReverseTable<AlphabetSize, Alphabet>();

        // A "block" is the smallest run of whole bytes that encodes to whole
        // characters: lcm(8, BitGroupSize) bits. Base64: 3 bytes <-> 4 chars;
        // Base32: 5 bytes <-> 8 chars; Base16: 1 byte <-> 2 chars.
        template<size_t BitGroupSize>
        inline constexpr size_t BlockBytes = BitGroupSize / std::gcd(size_t{8}, BitGroupSize);

        template<size_t BitGroupSize>
        inline constexpr size_t BlockChars = 8 / std::gcd(size_t{8}, BitGroupSize);

        // Below this input size the auto-threaded parallel functions stay serial
        inline constexpr size_t ParallelMinBytes = size_t{1} << 20;

        // Encode bytes [first, last) into out; returns one past the last char written.
        // first must sit on a block boundary of the overall input.
        template<size_t BitGroupSize, size_t AlphabetSize, const std::array<char, AlphabetSize>& Alphabet>
        inline char* EncodeChunk(const uint8_t* first, const uint8_t* last, char* out) {
            constexpr size_t mask = (size_t{1} << BitGroupSize) - 1;
            size_t bitBuffer = 0;
            int bitBufferLength = 0;

            for (const uint8_t* p = first; p != last; ++p) {
                bitBuffer = (bitBuffer << 8) | *p;
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
            size_t bitBuffer = 0;
            int bitBufferLength = 0;

            for (const char* p = first; p != last; ++p) {
                int index = ReverseTable<AlphabetSize, Alphabet>[static_cast<unsigned char>(*p)];
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

    } // namespace detail

    // Templated function for Base Encoding. Accepts any ByteSource and reads it
    // in place. constexpr: usable at compile time, e.g.
    //   static_assert(EncodeBase64("foo") == "Zm9v");
    template<size_t BitGroupSize, size_t AlphabetSize, const std::array<char, AlphabetSize>& Alphabet, bool PaddingRequired, ByteSource R>
    constexpr std::string BaseEncode(const R& input) {
        constexpr size_t mask = (size_t{1} << BitGroupSize) - 1;
        std::string output;
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
    template<size_t BitGroupSize, size_t AlphabetSize, const std::array<char, AlphabetSize>& Alphabet, bool PaddingRequired, ByteSource R>
    constexpr std::string BaseEncodeBinary(const R& input) {
        return BaseEncode<BitGroupSize, AlphabetSize, Alphabet, PaddingRequired>(input);
    }

    // Templated function for Base Decoding (string version)
    template<size_t BitGroupSize, size_t AlphabetSize, const std::array<char, AlphabetSize>& Alphabet, bool PaddingRequired, ByteSource R>
    constexpr std::string BaseDecode(const R& input) {
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

            int index = detail::ReverseTable<AlphabetSize, Alphabet>[c];
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

            int index = detail::ReverseTable<AlphabetSize, Alphabet>[c];
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

    template<size_t BitGroupSize, size_t AlphabetSize, const std::array<char, AlphabetSize>& Alphabet, bool PaddingRequired>
    std::string BaseEncodeParallelBytes(const uint8_t* data, size_t size, unsigned threadCount = 0) {
        constexpr size_t blockBytes = detail::BlockBytes<BitGroupSize>;
        constexpr size_t blockChars = detail::BlockChars<BitGroupSize>;

        const bool autoThreads = (threadCount == 0);
        if (autoThreads) {
            threadCount = std::thread::hardware_concurrency();
            if (size < detail::ParallelMinBytes) {
                threadCount = 1;
            }
        }
        const size_t fullBlocks = size / blockBytes;
        threadCount = static_cast<unsigned>(std::min<size_t>(std::max(threadCount, 1u), std::max<size_t>(fullBlocks, 1)));

        const size_t tailBytes = size - fullBlocks * blockBytes;
        const size_t tailChars = (tailBytes * 8 + BitGroupSize - 1) / BitGroupSize;
        size_t encodedLength = fullBlocks * blockChars + tailChars;
        if (PaddingRequired && encodedLength % blockChars) {
            encodedLength += blockChars - encodedLength % blockChars;
        }

        std::string output;
        output.resize(encodedLength);

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
                char* dest = output.data() + blockStart * blockChars;
                workers.emplace_back([first, last, dest] {
                    detail::EncodeChunk<BitGroupSize, AlphabetSize, Alphabet>(first, last, dest);
                });
                blockStart += blockCount;
            }
            detail::EncodeChunk<BitGroupSize, AlphabetSize, Alphabet>(
                data, data + (blocksPerThread + (extraBlocks > 0 ? 1 : 0)) * blockBytes, output.data());
            for (auto& worker : workers) {
                worker.join();
            }
            // Tail (partial block) plus any padding
            char* end = detail::EncodeChunk<BitGroupSize, AlphabetSize, Alphabet>(
                data + fullBlocks * blockBytes, data + size, output.data() + fullBlocks * blockChars);
            while (end != output.data() + encodedLength) {
                *end++ = '=';
            }
        } else {
            char* end = detail::EncodeChunk<BitGroupSize, AlphabetSize, Alphabet>(data, data + size, output.data());
            while (end != output.data() + encodedLength) {
                *end++ = '=';
            }
        }

        return output;
    }

    template<typename Container, size_t BitGroupSize, size_t AlphabetSize, const std::array<char, AlphabetSize>& Alphabet, bool PaddingRequired>
    Container BaseDecodeParallelImpl(const char* data, size_t size, unsigned threadCount = 0) {
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

        const bool autoThreads = (threadCount == 0);
        if (autoThreads) {
            threadCount = std::thread::hardware_concurrency();
            if (contentLength < detail::ParallelMinBytes) {
                threadCount = 1;
            }
        }
        const size_t fullBlocks = contentLength / blockChars;
        threadCount = static_cast<unsigned>(std::min<size_t>(std::max(threadCount, 1u), std::max<size_t>(fullBlocks, 1)));

        const size_t tailChars = contentLength - fullBlocks * blockChars;
        const size_t tailBytes = tailChars * BitGroupSize / 8;

        Container output;
        output.resize(fullBlocks * blockBytes + tailBytes);
        uint8_t* dest = reinterpret_cast<uint8_t*>(output.data());

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

        return output;
    }

    // Zero-copy adapters: any ByteSource goes straight to the workers
    template<size_t BitGroupSize, size_t AlphabetSize, const std::array<char, AlphabetSize>& Alphabet, bool PaddingRequired, ByteSource R>
    std::string BaseEncodeParallel(const R& input, unsigned threadCount = 0) {
        return BaseEncodeParallelBytes<BitGroupSize, AlphabetSize, Alphabet, PaddingRequired>(
            reinterpret_cast<const uint8_t*>(std::ranges::data(input)), std::ranges::size(input), threadCount);
    }

    template<size_t BitGroupSize, size_t AlphabetSize, const std::array<char, AlphabetSize>& Alphabet, bool PaddingRequired, ByteSource R>
    std::string BaseEncodeBinaryParallel(const R& input, unsigned threadCount = 0) {
        return BaseEncodeParallel<BitGroupSize, AlphabetSize, Alphabet, PaddingRequired>(input, threadCount);
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

#ifdef SNICHOLLS_HAS_BITSTRING
    // ------------------------------------------------------------------
    // BSD <bitstring.h> interoperability
    //
    // Encodes the logical bit sequence bit_test(bits, 0), bit_test(bits, 1),
    // ..., bit_test(bits, nbits - 1). Note this is bitstring.h's logical
    // order (bit 0 is the LSB of byte 0), which differs from the byte-stream
    // order used by the string/Binary functions above. Any bit count is
    // supported, not just multiples of 8; unused trailing bits of the final
    // character are zero, per RFC 4648.
    //
    // Decoding returns a BitString whose storage works directly with the
    // bit_test/bit_set/bit_clear macros. Because an encoded character always
    // carries BitGroupSize bits, the decoder cannot know the original bit
    // count on its own; pass expectedBits to trim the result (the round trip
    // otherwise returns nbits rounded up to a whole number of characters).
    // ------------------------------------------------------------------

    struct BitString {
        std::vector<bitstr_t> storage; // use with bit_test/bit_set/bit_clear
        size_t nbits = 0;

        bitstr_t* data() { return storage.data(); }
        const bitstr_t* data() const { return storage.data(); }
    };

    template<size_t BitGroupSize, size_t AlphabetSize, const std::array<char, AlphabetSize>& Alphabet, bool PaddingRequired>
    constexpr std::string BaseEncodeBitstring(const bitstr_t* bits, size_t nbits) {
        constexpr size_t mask = (size_t{1} << BitGroupSize) - 1;
        std::string output;
        output.reserve((nbits + BitGroupSize - 1) / BitGroupSize);

        size_t bitBuffer = 0;
        int bitBufferLength = 0;

        for (size_t i = 0; i < nbits; ++i) {
            bitBuffer = (bitBuffer << 1) | (bit_test(bits, i) ? size_t{1} : size_t{0});
            if (++bitBufferLength == static_cast<int>(BitGroupSize)) {
                output += Alphabet[bitBuffer & mask];
                bitBufferLength = 0;
            }
        }

        if (bitBufferLength > 0) {
            output += Alphabet[(bitBuffer << (BitGroupSize - bitBufferLength)) & mask];
        }

        if (PaddingRequired) {
            while (output.length() % detail::BlockChars<BitGroupSize>) {
                output += '=';
            }
        }

        return output;
    }

    template<size_t BitGroupSize, size_t AlphabetSize, const std::array<char, AlphabetSize>& Alphabet, bool PaddingRequired>
    constexpr BitString BaseDecodeBitstring(std::string_view input, size_t expectedBits = SIZE_MAX) {
        BitString output;
        output.storage.assign(bitstr_size(input.size() * BitGroupSize), 0);

        size_t position = 0;
        bool paddingSeen = false;

        for (char c : input) {
            if (PaddingRequired && c == '=') {
                paddingSeen = true;
                continue;
            }

            // RFC 4648: '=' is only valid as trailing padding
            if (paddingSeen) {
                throw std::invalid_argument("Invalid character after padding");
            }

            int index = detail::ReverseTable<AlphabetSize, Alphabet>[static_cast<unsigned char>(c)];
            if (index < 0) {
                throw std::invalid_argument("Invalid character in encoded string");
            }

            for (int k = static_cast<int>(BitGroupSize) - 1; k >= 0; --k) {
                if ((index >> k) & 1) {
                    bit_set(output.storage.data(), position);
                }
                ++position;
            }
        }

        output.nbits = std::min(position, expectedBits);
        output.storage.resize(bitstr_size(output.nbits));
        // Zero the unused bits of the final byte so equal bitstrings compare equal
        for (size_t i = output.nbits; i < output.storage.size() * 8; ++i) {
            bit_clear(output.storage.data(), i);
        }

        return output;
    }

#define SNICHOLLS_BITSTRING_FUNCTIONS(Name, BitGroupSize, AlphabetSize, Alphabet, Padded) \
    constexpr std::string Encode##Name##Bitstring(const bitstr_t* bits, size_t nbits) { \
        return BaseEncodeBitstring<BitGroupSize, AlphabetSize, Alphabet, Padded>(bits, nbits); \
    } \
    constexpr BitString Decode##Name##Bitstring(std::string_view input, size_t expectedBits = SIZE_MAX) { \
        return BaseDecodeBitstring<BitGroupSize, AlphabetSize, Alphabet, Padded>(input, expectedBits); \
    }
#else
#define SNICHOLLS_BITSTRING_FUNCTIONS(Name, BitGroupSize, AlphabetSize, Alphabet, Padded)
#endif /* SNICHOLLS_HAS_BITSTRING */

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
    template<ByteSource R> \
    constexpr std::string Encode##Name(const R& input) { \
        return BaseEncode<BitGroupSize, AlphabetSize, Alphabet, Padded>(input); \
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
    template<ByteSource R> \
    constexpr std::string Encode##Name##Binary(const R& input) { \
        return BaseEncode<BitGroupSize, AlphabetSize, Alphabet, Padded>(input); \
    } \
    template<ByteSource R> \
    constexpr Binary Decode##Name##Binary(const R& input) { \
        return BaseDecodeBinary<BitGroupSize, AlphabetSize, Alphabet, Padded>(input); \
    } \
    constexpr Binary Decode##Name##Binary(const char* input) { \
        return Decode##Name##Binary(std::string_view{input}); \
    } \
    template<ByteSource R> \
    std::string Encode##Name##Parallel(const R& input, unsigned threadCount = 0) { \
        return BaseEncodeParallel<BitGroupSize, AlphabetSize, Alphabet, Padded>(input, threadCount); \
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
    template<ByteSource R> \
    std::string Encode##Name##BinaryParallel(const R& input, unsigned threadCount = 0) { \
        return BaseEncodeParallel<BitGroupSize, AlphabetSize, Alphabet, Padded>(input, threadCount); \
    } \
    template<ByteSource R> \
    Binary Decode##Name##BinaryParallel(const R& input, unsigned threadCount = 0) { \
        return BaseDecodeBinaryParallel<BitGroupSize, AlphabetSize, Alphabet, Padded>(input, threadCount); \
    } \
    inline Binary Decode##Name##BinaryParallel(const char* input, unsigned threadCount = 0) { \
        return Decode##Name##BinaryParallel(std::string_view{input}, threadCount); \
    } \
    SNICHOLLS_BITSTRING_FUNCTIONS(Name, BitGroupSize, AlphabetSize, Alphabet, Padded)

    SNICHOLLS_DEFINE_SCHEME(Base64,          6, 64, Base64Alphabet,          true)
    SNICHOLLS_DEFINE_SCHEME(Base32,          5, 32, Base32Alphabet,          true)
    SNICHOLLS_DEFINE_SCHEME(Base32Hex,       5, 32, Base32HexAlphabet,       true)
    SNICHOLLS_DEFINE_SCHEME(Base36,          5, 36, Base36Alphabet,          false)
    SNICHOLLS_DEFINE_SCHEME(Base32Crockford, 5, 32, Base32CrockfordAlphabet, false)
    SNICHOLLS_DEFINE_SCHEME(Base16,          4, 16, Base16Alphabet,          false)
    SNICHOLLS_DEFINE_SCHEME(Base8,           3, 8,  Base8Alphabet,           false)
    SNICHOLLS_DEFINE_SCHEME(Base4,           2, 4,  Base4Alphabet,           false)
    SNICHOLLS_DEFINE_SCHEME(Base2,           1, 2,  Base2Alphabet,           false)

#undef SNICHOLLS_DEFINE_SCHEME
#undef SNICHOLLS_BITSTRING_FUNCTIONS
}

#endif /* encode_decode_base_whatever_hpp */
