// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Saxon Nicholls

//
//  encode_decode_bitset.hpp
//  BaseEncodeDecode
//
//  Optional drop-in adapter for std::bitset - the portable sibling of the
//  BSD <bitstring.h> adapter, available on every platform. Include it
//  explicitly:
//
//      #include "encode_decode_bitset.hpp"
//
//      std::bitset<12> bits;
//      bits.set(0); bits.set(11);
//      std::string b64 = EncodeBase64Bitset(bits);
//      auto round = DecodeBase64Bitset<12>(b64);   // std::bitset<12>
//
//  Encodes the logical bit sequence bits[0], bits[1], ..., bits[N-1] - the
//  same convention as the bitstring.h adapter (bit 0 first), which differs
//  from the byte-stream order of the string/Binary functions. Unused
//  trailing bits of the final character are zero, per RFC 4648.
//
//  Decoding takes the target width as a template argument, since std::bitset
//  is sized at compile time: extra decoded fill bits beyond N are discarded,
//  and missing bits are zero. Encoding is constexpr; decoding is additionally
//  constexpr from C++23 (where std::bitset::set is constexpr).
//

#ifndef encode_decode_bitset_hpp
#define encode_decode_bitset_hpp

#include <bitset>

#include "encode_decode_base_whatever.hpp"

namespace snicholls {

    template<size_t BitGroupSize, size_t AlphabetSize, const std::array<char, AlphabetSize>& Alphabet, bool PaddingRequired, size_t NBits>
    constexpr std::string BaseEncodeBitset(const std::bitset<NBits>& bits) {
        constexpr size_t mask = (size_t{1} << BitGroupSize) - 1;
        std::string output;
        output.reserve((NBits + BitGroupSize - 1) / BitGroupSize);

        size_t bitBuffer = 0;
        int bitBufferLength = 0;

        for (size_t i = 0; i < NBits; ++i) {
            bitBuffer = (bitBuffer << 1) | (bits[i] ? size_t{1} : size_t{0});
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

    template<size_t NBits, size_t BitGroupSize, size_t AlphabetSize, const std::array<char, AlphabetSize>& Alphabet, bool PaddingRequired>
    std::bitset<NBits> BaseDecodeBitset(std::string_view input) {
        std::bitset<NBits> output;
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
                if (position < NBits && ((index >> k) & 1)) {
                    output.set(position);
                }
                ++position;
            }
        }

        return output;
    }

#define SNICHOLLS_DEFINE_BITSET_SCHEME(Name, BitGroupSize, AlphabetSize, Alphabet, Padded) \
    template<size_t NBits> \
    constexpr std::string Encode##Name##Bitset(const std::bitset<NBits>& bits) { \
        return BaseEncodeBitset<BitGroupSize, AlphabetSize, Alphabet, Padded>(bits); \
    } \
    template<size_t NBits> \
    std::bitset<NBits> Decode##Name##Bitset(std::string_view input) { \
        return BaseDecodeBitset<NBits, BitGroupSize, AlphabetSize, Alphabet, Padded>(input); \
    }

    SNICHOLLS_FOR_EACH_SCHEME(SNICHOLLS_DEFINE_BITSET_SCHEME)

#undef SNICHOLLS_DEFINE_BITSET_SCHEME
}

#endif /* encode_decode_bitset_hpp */
