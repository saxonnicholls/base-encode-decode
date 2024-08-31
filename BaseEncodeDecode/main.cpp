//
//  main.cpp
//  BaseEncodeDecode
//
//  Created by Saxon Nicholls on 1/8/2024.
//

#include <iostream>

#include "encode_decode_base_whatever.hpp"

// Test the implementation with output comparison
int main() {
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

    // Base32Crockford
    std::string encodedBase32Crockford = EncodeBase32Crockford(data);
    std::string decodedBase32Crockford = DecodeBase32Crockford(encodedBase32Crockford);
   
    std::cout << "Base32Crockford Encoded (Serial): " << encodedBase32Crockford << std::endl;
    std::cout << "Base32Crockford Decoded (Serial): " << decodedBase32Crockford << std::endl;
    
    // Base64 Encoding/Decoding
    std::string encodedBase64 = EncodeBase64(data);
    std::string decodedBase64 = DecodeBase64(encodedBase64);
 
    std::cout << "Base64 Encoded (Serial): " << encodedBase64 << std::endl;
    std::cout << "Base64 Decoded (Serial): " << decodedBase64 << std::endl;
    return 0;
}

