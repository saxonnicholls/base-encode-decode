// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Saxon Nicholls

//
//  encode_decode_bitstring.hpp
//  BaseEncodeDecode
//
//  Optional drop-in adapter for BSD <bitstring.h> (macOS, the BSDs).
//  Include it explicitly:
//
//      #include "encode_decode_bitstring.hpp"
//
//      bitstr_t* bits = bit_alloc(12);
//      bit_set(bits, 0); bit_set(bits, 11);
//      std::string b64 = EncodeBase64Bitstring(bits, 12);   // "gB=="-style
//      BitString round = DecodeBase64Bitstring(b64, 12);    // 12 = expected bits
//
//  Encodes the logical bit sequence bit_test(bits, 0), bit_test(bits, 1),
//  ..., bit_test(bits, nbits - 1). Note this is bitstring.h's logical order
//  (bit 0 is the LSB of byte 0), which differs from the byte-stream order of
//  the string/Binary functions. Any bit count is supported, not just
//  multiples of 8; unused trailing bits of the final character are zero, per
//  RFC 4648.
//
//  Decoding returns a BitString whose storage works directly with the
//  bit_test/bit_set/bit_clear macros. Because an encoded character always
//  carries BitGroupSize bits, the decoder cannot know the original bit count
//  on its own; pass expectedBits to trim the result (the round trip
//  otherwise returns nbits rounded up to a whole number of characters).
//
//  On platforms without <bitstring.h> this header is an empty no-op (test
//  SNICHOLLS_HAS_BITSTRING), so it is safe to include unconditionally. For a
//  portable equivalent see encode_decode_bitset.hpp.
//

#ifndef encode_decode_bitstring_hpp
#define encode_decode_bitstring_hpp

#if __has_include(<bitstring.h>)
#include <bitstring.h>
#define SNICHOLLS_HAS_BITSTRING 1

#include "encode_decode_base_whatever.hpp"

namespace snicholls {

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

            int index = detail::ReverseTable<BitGroupSize, AlphabetSize, Alphabet>[static_cast<unsigned char>(c)];
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

#define SNICHOLLS_DEFINE_BITSTRING_SCHEME(Name, BitGroupSize, AlphabetSize, Alphabet, Padded) \
    constexpr std::string Encode##Name##Bitstring(const bitstr_t* bits, size_t nbits) { \
        return BaseEncodeBitstring<BitGroupSize, AlphabetSize, Alphabet, Padded>(bits, nbits); \
    } \
    constexpr BitString Decode##Name##Bitstring(std::string_view input, size_t expectedBits = SIZE_MAX) { \
        return BaseDecodeBitstring<BitGroupSize, AlphabetSize, Alphabet, Padded>(input, expectedBits); \
    }

    SNICHOLLS_FOR_EACH_SCHEME(SNICHOLLS_DEFINE_BITSTRING_SCHEME)

#undef SNICHOLLS_DEFINE_BITSTRING_SCHEME
}

#endif /* __has_include(<bitstring.h>) */
#endif /* encode_decode_bitstring_hpp */
