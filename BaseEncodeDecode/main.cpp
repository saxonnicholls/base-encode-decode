//
//  main.cpp
//  BaseEncodeDecode
//
//  Created by Saxon Nicholls on 1/8/2024.
//

#include <iostream>

#include "encode_decode_base_whatever.hpp"

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
        std::cout << std::endl;
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

    // Base64 Encoding/Decoding
    std::string encodedBase64 = EncodeBase64Binary(data);
    Binary decodedBase64 = DecodeBase64Binary(encodedBase64);
    std::cout << "Base64 Encoded (Binary): " << encodedBase64 << std::endl;
    std::cout << "Base64 Decoded (Binary - ASCII): ";
    printBinaryData(decodedBase64);
}

// Main function
int main() {
    std::cout << "String Encoding/Decoding Demo:" << std::endl;
    StringDemo();
    
    std::cout << "\nBinary Encoding/Decoding Demo:" << std::endl;
    BinaryDemo();

    return 0;
}
