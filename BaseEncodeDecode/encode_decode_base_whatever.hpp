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
#include <stdexcept>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <vector>

#include "alphabet.hpp"

// Define Binary as a type alias for std::vector<uint8_t>
using Binary = std::vector<uint8_t>;

// Complies with RFC 4648 and RFC 2045 for Base64, Base32, Base32Hex, and Base16 encodings.
namespace snicholls {

    // Templated function for Base Encoding (string version)
    template<size_t BitGroupSize, size_t AlphabetSize, const std::array<char, AlphabetSize>& Alphabet, bool PaddingRequired>
    std::string BaseEncode(const std::string& input) {
        std::string output;
        size_t bitBuffer = 0;
        int bitBufferLength = 0;

        for (char c : input) {
            bitBuffer = (bitBuffer << 8) | static_cast<unsigned char>(c);
            bitBufferLength += 8;

            while (bitBufferLength >= BitGroupSize) {
                int index = (bitBuffer >> (bitBufferLength - BitGroupSize)) & ((1 << BitGroupSize) - 1);
                output += Alphabet[index];
                bitBufferLength -= BitGroupSize;
            }
        }

        if (bitBufferLength > 0) {
            int index = (bitBuffer << (BitGroupSize - bitBufferLength)) & ((1 << BitGroupSize) - 1);
            output += Alphabet[index];
        }

        if (PaddingRequired) {
            while (output.length() % (8 / BitGroupSize)) {
                output += '=';
            }
        }

        return output;
    }

    // Templated function for Base Decoding (string version)
    template<size_t BitGroupSize, size_t AlphabetSize, const std::array<char, AlphabetSize>& Alphabet, bool PaddingRequired>
    std::string BaseDecode(const std::string& input) {
        std::string output;
        size_t bitBuffer = 0;
        int bitBufferLength = 0;

        for (char c : input) {
            if (PaddingRequired && c == '=') {
                continue;
            }

            auto it = std::find(Alphabet.begin(), Alphabet.end(), c);
            if (it == Alphabet.end()) {
                throw std::invalid_argument("Invalid character in encoded string");
            }

            auto index = std::distance(Alphabet.begin(), it);
            bitBuffer = (bitBuffer << BitGroupSize) | index;
            bitBufferLength += BitGroupSize;

            if (bitBufferLength >= 8) {
                output += static_cast<char>((bitBuffer >> (bitBufferLength - 8)) & 0xFF);
                bitBufferLength -= 8;
            }
        }

        return output;
    }

    // Templated function for Base Encoding (binary version)
    template<size_t BitGroupSize, size_t AlphabetSize, const std::array<char, AlphabetSize>& Alphabet, bool PaddingRequired>
    std::string BaseEncodeBinary(const Binary& input) {
        std::string output;
        size_t bitBuffer = 0;
        int bitBufferLength = 0;

        for (uint8_t byte : input) {
            bitBuffer = (bitBuffer << 8) | byte;
            bitBufferLength += 8;

            while (bitBufferLength >= BitGroupSize) {
                int index = (bitBuffer >> (bitBufferLength - BitGroupSize)) & ((1 << BitGroupSize) - 1);
                output += Alphabet[index];
                bitBufferLength -= BitGroupSize;
            }
        }

        if (bitBufferLength > 0) {
            int index = (bitBuffer << (BitGroupSize - bitBufferLength)) & ((1 << BitGroupSize) - 1);
            output += Alphabet[index];
        }

        if (PaddingRequired) {
            while (output.length() % (8 / BitGroupSize)) {
                output += '=';
            }
        }

        return output;
    }

    // Templated function for Base Decoding (binary version)
    template<size_t BitGroupSize, size_t AlphabetSize, const std::array<char, AlphabetSize>& Alphabet, bool PaddingRequired>
    Binary BaseDecodeBinary(const std::string& input) {
        Binary output;
        size_t bitBuffer = 0;
        int bitBufferLength = 0;

        for (char c : input) {
            if (PaddingRequired && c == '=') {
                continue;
            }

            auto it = std::find(Alphabet.begin(), Alphabet.end(), c);
            if (it == Alphabet.end()) {
                throw std::invalid_argument("Invalid character in encoded string");
            }

            auto index = std::distance(Alphabet.begin(), it);
            bitBuffer = (bitBuffer << BitGroupSize) | index;
            bitBufferLength += BitGroupSize;

            if (bitBufferLength >= 8) {
                output.push_back(static_cast<uint8_t>((bitBuffer >> (bitBufferLength - 8)) & 0xFF));
                bitBufferLength -= 8;
            }
        }

        return output;
    }

    // Specializations for encoding (string version)
    inline std::string EncodeBase64(const std::string& input) {
        return BaseEncode<6, 64, Base64Alphabet, true>(input);
    }

    inline std::string EncodeBase32(const std::string& input) {
        return BaseEncode<5, 32, Base32Alphabet, true>(input);
    }

    inline std::string EncodeBase32Hex(const std::string& input) {
        return BaseEncode<5, 32, Base32HexAlphabet, true>(input);
    }

    inline std::string EncodeBase36(const std::string& input) {
        return BaseEncode<5, 36, Base36Alphabet, false>(input);
    }

    inline std::string EncodeBase32Crockford(const std::string& input) {
        return BaseEncode<5, 32, Base32CrockfordAlphabet, false>(input);
    }

    inline std::string EncodeBase16(const std::string& input) {
        return BaseEncode<4, 16, Base16Alphabet, false>(input);
    }

    inline std::string EncodeBase8(const std::string& input) {
        return BaseEncode<3, 8, Base8Alphabet, false>(input);
    }

    inline std::string EncodeBase4(const std::string& input) {
        return BaseEncode<2, 4, Base4Alphabet, false>(input);
    }

    inline std::string EncodeBase2(const std::string& input) {
        return BaseEncode<1, 2, Base2Alphabet, false>(input);
    }

    // Specializations for encoding (binary version)
    inline std::string EncodeBase64Binary(const Binary& input) {
        return BaseEncodeBinary<6, 64, Base64Alphabet, true>(input);
    }

    inline std::string EncodeBase32Binary(const Binary& input) {
        return BaseEncodeBinary<5, 32, Base32Alphabet, true>(input);
    }

    inline std::string EncodeBase32HexBinary(const Binary& input) {
        return BaseEncodeBinary<5, 32, Base32HexAlphabet, true>(input);
    }

    inline std::string EncodeBase36Binary(const Binary& input) {
        return BaseEncodeBinary<5, 36, Base36Alphabet, false>(input);
    }

    inline std::string EncodeBase32CrockfordBinary(const Binary& input) {
        return BaseEncodeBinary<5, 32, Base32CrockfordAlphabet, false>(input);
    }

    inline std::string EncodeBase16Binary(const Binary& input) {
        return BaseEncodeBinary<4, 16, Base16Alphabet, false>(input);
    }

    inline std::string EncodeBase8Binary(const Binary& input) {
        return BaseEncodeBinary<3, 8, Base8Alphabet, false>(input);
    }

    inline std::string EncodeBase4Binary(const Binary& input) {
        return BaseEncodeBinary<2, 4, Base4Alphabet, false>(input);
    }

    inline std::string EncodeBase2Binary(const Binary& input) {
        return BaseEncodeBinary<1, 2, Base2Alphabet, false>(input);
    }

    // Specializations for decoding (string version)
    inline std::string DecodeBase64(const std::string& input) {
        return BaseDecode<6, 64, Base64Alphabet, true>(input);
    }

    inline std::string DecodeBase32(const std::string& input) {
        return BaseDecode<5, 32, Base32Alphabet, true>(input);
    }

    inline std::string DecodeBase32Hex(const std::string& input) {
        return BaseDecode<5, 32, Base32HexAlphabet, true>(input);
    }

    inline std::string DecodeBase36(const std::string& input) {
        return BaseDecode<5, 36, Base36Alphabet, false>(input);
    }

    inline std::string DecodeBase32Crockford(const std::string& input) {
        return BaseDecode<5, 32, Base32CrockfordAlphabet, false>(input);
    }

    inline std::string DecodeBase16(const std::string& input) {
        return BaseDecode<4, 16, Base16Alphabet, false>(input);
    }

    inline std::string DecodeBase8(const std::string& input) {
        return BaseDecode<3, 8, Base8Alphabet, false>(input);
    }

    inline std::string DecodeBase4(const std::string& input) {
        return BaseDecode<2, 4, Base4Alphabet, false>(input);
    }

    inline std::string DecodeBase2(const std::string& input) {
        return BaseDecode<1, 2, Base2Alphabet, false>(input);
    }

    // Specializations for decoding (binary version)
    inline Binary DecodeBase64Binary(const std::string& input) {
        return BaseDecodeBinary<6, 64, Base64Alphabet, true>(input);
    }

    inline Binary DecodeBase32Binary(const std::string& input) {
        return BaseDecodeBinary<5, 32, Base32Alphabet, true>(input);
    }

    inline Binary DecodeBase32HexBinary(const std::string& input) {
        return BaseDecodeBinary<5, 32, Base32HexAlphabet, true>(input);
    }

    inline Binary DecodeBase36Binary(const std::string& input) {
        return BaseDecodeBinary<5, 36, Base36Alphabet, false>(input);
    }

    inline Binary DecodeBase32CrockfordBinary(const std::string& input) {
        return BaseDecodeBinary<5, 32, Base32CrockfordAlphabet, false>(input);
    }

    inline Binary DecodeBase16Binary(const std::string& input) {
        return BaseDecodeBinary<4, 16, Base16Alphabet, false>(input);
    }

    inline Binary DecodeBase8Binary(const std::string& input) {
        return BaseDecodeBinary<3, 8, Base8Alphabet, false>(input);
    }

    inline Binary DecodeBase4Binary(const std::string& input) {
        return BaseDecodeBinary<2, 4, Base4Alphabet, false>(input);
    }

    inline Binary DecodeBase2Binary(const std::string& input) {
        return BaseDecodeBinary<1, 2, Base2Alphabet, false>(input);
    }
}

#endif /* encode_decode_base_whatever_hpp */
