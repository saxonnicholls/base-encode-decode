// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Saxon Nicholls

//
//  encode_decode_stream.hpp
//  BaseEncodeDecode
//
//  Optional drop-in adapter for incremental (streaming) encoding and
//  decoding: process data of any size in constant memory - files, sockets,
//  pipes - without ever holding the whole input or output. Include it
//  explicitly:
//
//      #include "encode_decode_stream.hpp"
//
//      // One-liners for iostreams (files, stringstreams, ...):
//      std::ifstream in("movie.mkv", std::ios::binary);
//      std::ofstream out("movie.b64");
//      EncodeBase64Stream(in, out);                  // constant memory
//
//      // Or feed chunks yourself:
//      Base64StreamEncoder encoder;
//      std::string part1 = encoder.Update(chunk1);   // any ByteSource
//      std::string part2 = encoder.Update(chunk2);
//      std::string tail  = encoder.Finish();         // final group + padding
//
//  Concatenating the pieces yields exactly what the one-shot functions
//  produce, regardless of how the input was split. Bulk data flows through
//  the same block/SIMD kernels as the one-shot functions.
//
//  Decoders throw std::invalid_argument exactly like the one-shot decoders;
//  after a throw the codec object is in an unspecified state and should be
//  discarded. Update after Finish is a programming error (asserted).
//

#ifndef encode_decode_stream_hpp
#define encode_decode_stream_hpp

#include <cstring>
#include <istream>
#include <ostream>

#include "encode_decode_base_whatever.hpp"

namespace snicholls {

    template<size_t BitGroupSize, size_t AlphabetSize, const std::array<char, AlphabetSize>& Alphabet, bool PaddingRequired>
    class BaseStreamEncoder {
        static constexpr size_t blockBytes = detail::BlockBytes<BitGroupSize>;
        static constexpr size_t blockChars = detail::BlockChars<BitGroupSize>;

        std::array<uint8_t, blockBytes> pending{};
        size_t pendingCount = 0;
        bool finished = false;

    public:
        // Encode the next chunk; returns the characters that are now final
        template<ByteSource R>
        std::string Update(const R& chunk) {
            assert(!finished);
            const uint8_t* data = reinterpret_cast<const uint8_t*>(std::ranges::data(chunk));
            const size_t size = std::ranges::size(chunk);

            const size_t total = pendingCount + size;
            if (total < blockBytes) { // not enough for a whole block yet
                if (size > 0) {
                    std::memcpy(pending.data() + pendingCount, data, size);
                    pendingCount = total;
                }
                return {};
            }

            const size_t fullBlocks = total / blockBytes;
            std::string output = detail::MakeFilledString(fullBlocks * blockChars, [&](char* w) {
                size_t consumed = 0;
                if (pendingCount > 0) { // complete the carried block first
                    const size_t need = blockBytes - pendingCount;
                    std::memcpy(pending.data() + pendingCount, data, need);
                    w = detail::EncodeChunk<BitGroupSize, AlphabetSize, Alphabet>(
                        pending.data(), pending.data() + blockBytes, w);
                    consumed = need;
                    pendingCount = 0;
                }
                const size_t bulkBlocks = (size - consumed) / blockBytes;
                detail::EncodeChunk<BitGroupSize, AlphabetSize, Alphabet>(
                    data + consumed, data + consumed + bulkBlocks * blockBytes, w);
                consumed += bulkBlocks * blockBytes;

                pendingCount = size - consumed; // carry the remainder
                if (pendingCount > 0) {
                    std::memcpy(pending.data(), data + consumed, pendingCount);
                }
            });
            return output;
        }

        // Flush the final partial block plus any padding; the encoder is done
        std::string Finish() {
            assert(!finished);
            finished = true;

            std::array<char, blockChars> buffer;
            char* end = detail::EncodeChunk<BitGroupSize, AlphabetSize, Alphabet>(
                pending.data(), pending.data() + pendingCount, buffer.data());
            pendingCount = 0;

            std::string output(buffer.data(), end);
            if (PaddingRequired) {
                while (output.length() % blockChars) {
                    output += '=';
                }
            }
            return output;
        }
    };

    template<size_t BitGroupSize, size_t AlphabetSize, const std::array<char, AlphabetSize>& Alphabet, bool PaddingRequired>
    class BaseStreamDecoder {
        static constexpr size_t blockBytes = detail::BlockBytes<BitGroupSize>;
        static constexpr size_t blockChars = detail::BlockChars<BitGroupSize>;

        std::array<char, blockChars> pending{};
        size_t pendingCount = 0;
        bool paddingSeen = false;
        bool finished = false;

    public:
        // Decode the next chunk; returns the bytes that are now final
        template<ByteSource R>
        Binary Update(const R& chunk) {
            assert(!finished);
            const char* data = reinterpret_cast<const char*>(std::ranges::data(chunk));
            const size_t size = std::ranges::size(chunk);

            // RFC 4648: once '=' is seen, only more '=' may follow (in this
            // chunk or any later one). '=' carries no data.
            size_t contentLength = size;
            if constexpr (PaddingRequired) {
                if (paddingSeen) {
                    contentLength = 0;
                }
                const void* firstPad = contentLength ? std::memchr(data, '=', contentLength) : nullptr;
                if (firstPad != nullptr) {
                    contentLength = static_cast<size_t>(static_cast<const char*>(firstPad) - data);
                    paddingSeen = true;
                }
                for (size_t i = contentLength; i < size; ++i) {
                    if (data[i] != '=') {
                        throw std::invalid_argument("Invalid character after padding");
                    }
                }
            }

            const size_t total = pendingCount + contentLength;
            if (total < blockChars) { // not enough for a whole block yet
                if (contentLength > 0) {
                    std::memcpy(pending.data() + pendingCount, data, contentLength);
                    pendingCount = total;
                }
                return {};
            }

            const size_t fullBlocks = total / blockChars;
            Binary output(fullBlocks * blockBytes);
            uint8_t* w = output.data();
            size_t consumed = 0;
            if (pendingCount > 0) { // complete the carried block first
                const size_t need = blockChars - pendingCount;
                std::memcpy(pending.data() + pendingCount, data, need);
                w = detail::DecodeChunk<BitGroupSize, AlphabetSize, Alphabet>(
                    pending.data(), pending.data() + blockChars, w);
                consumed = need;
                pendingCount = 0;
            }
            const size_t bulkBlocks = (contentLength - consumed) / blockChars;
            detail::DecodeChunk<BitGroupSize, AlphabetSize, Alphabet>(
                data + consumed, data + consumed + bulkBlocks * blockChars, w);
            consumed += bulkBlocks * blockChars;

            pendingCount = contentLength - consumed; // carry the remainder
            if (pendingCount > 0) {
                std::memcpy(pending.data(), data + consumed, pendingCount);
            }
            return output;
        }

        // Decode the final partial group; the decoder is done
        Binary Finish() {
            assert(!finished);
            finished = true;

            Binary output(pendingCount * BitGroupSize / 8);
            detail::DecodeChunk<BitGroupSize, AlphabetSize, Alphabet>(
                pending.data(), pending.data() + pendingCount, output.data());
            pendingCount = 0;
            return output;
        }
    };

    // Convenience: pump a whole istream into an ostream in constant memory
    template<size_t BitGroupSize, size_t AlphabetSize, const std::array<char, AlphabetSize>& Alphabet, bool PaddingRequired>
    void BaseEncodeStream(std::istream& input, std::ostream& output, size_t chunkBytes = size_t{1} << 20) {
        BaseStreamEncoder<BitGroupSize, AlphabetSize, Alphabet, PaddingRequired> encoder;
        std::vector<char> buffer(chunkBytes);
        while (input) {
            input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const std::streamsize got = input.gcount();
            if (got <= 0) {
                break;
            }
            const std::string piece = encoder.Update(std::string_view(buffer.data(), static_cast<size_t>(got)));
            output.write(piece.data(), static_cast<std::streamsize>(piece.size()));
        }
        const std::string tail = encoder.Finish();
        output.write(tail.data(), static_cast<std::streamsize>(tail.size()));
    }

    template<size_t BitGroupSize, size_t AlphabetSize, const std::array<char, AlphabetSize>& Alphabet, bool PaddingRequired>
    void BaseDecodeStream(std::istream& input, std::ostream& output, size_t chunkBytes = size_t{1} << 20) {
        BaseStreamDecoder<BitGroupSize, AlphabetSize, Alphabet, PaddingRequired> decoder;
        std::vector<char> buffer(chunkBytes);
        while (input) {
            input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const std::streamsize got = input.gcount();
            if (got <= 0) {
                break;
            }
            const Binary piece = decoder.Update(std::string_view(buffer.data(), static_cast<size_t>(got)));
            output.write(reinterpret_cast<const char*>(piece.data()), static_cast<std::streamsize>(piece.size()));
        }
        const Binary tail = decoder.Finish();
        output.write(reinterpret_cast<const char*>(tail.data()), static_cast<std::streamsize>(tail.size()));
    }

#define SNICHOLLS_DEFINE_STREAM_SCHEME(Name, BitGroupSize, AlphabetSize, Alphabet, Padded) \
    using Name##StreamEncoder = BaseStreamEncoder<BitGroupSize, AlphabetSize, Alphabet, Padded>; \
    using Name##StreamDecoder = BaseStreamDecoder<BitGroupSize, AlphabetSize, Alphabet, Padded>; \
    inline void Encode##Name##Stream(std::istream& input, std::ostream& output, size_t chunkBytes = size_t{1} << 20) { \
        BaseEncodeStream<BitGroupSize, AlphabetSize, Alphabet, Padded>(input, output, chunkBytes); \
    } \
    inline void Decode##Name##Stream(std::istream& input, std::ostream& output, size_t chunkBytes = size_t{1} << 20) { \
        BaseDecodeStream<BitGroupSize, AlphabetSize, Alphabet, Padded>(input, output, chunkBytes); \
    }

    SNICHOLLS_FOR_EACH_SCHEME(SNICHOLLS_DEFINE_STREAM_SCHEME)

#undef SNICHOLLS_DEFINE_STREAM_SCHEME
}

#endif /* encode_decode_stream_hpp */
