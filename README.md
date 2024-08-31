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
- **Base36**: A base36 encoding scheme that uses digits `0-9` and letters `A-Z`.
- **Base32Crockford**: A variant of Base32 encoding created by Douglas Crockford, which includes additional error-correction features and alternative symbol mappings.

## Features

- **Encoding and Decoding**: Functions are provided to encode data into the base format and decode it back into its original form.
- **RFC Compliance**: The Base64, Base32, Base32Hex, and Base16 implementations comply with [RFC 4648](https://tools.ietf.org/html/rfc4648).

## Usage

### Encoding and Decoding

To encode a string using a specific encoding scheme:

```cpp
#include <string>
#include "encode_decode_base_whatever.hpp" 

using namespace snicholls;

std::string data = "Hello, World!";
std::string encoded = EncodeBase64(data);
std::string decoded = DecodeBase64(encoded);

std::cout << "Encoded: " << encoded << std::endl;
std::cout << "Decoded: " << decoded << std::endl;
```

### Supported Encoding Schemes

The following encoding and decoding functions are available:

- `EncodeBase64` / `DecodeBase64`
- `EncodeBase32` / `DecodeBase32`
- `EncodeBase32Hex` / `DecodeBase32Hex`
- `EncodeBase36` / `DecodeBase36`
- `EncodeBase32Crockford` / `DecodeBase32Crockford`
- `EncodeBase16` / `DecodeBase16`
- `EncodeBase8` / `DecodeBase8`
- `EncodeBase4` / `DecodeBase4`
- `EncodeBase2` / `DecodeBase2`

### What you should expect

Running main() should give you this:

```cpp
String Encoding/Decoding Demo:
Base2 Encoded (Serial): 01001000011001010110110001101100011011110010110000100000010101110110111101110010011011000110010000100001001000000100100101110100001000000110100101110011001000000110101001110101011100110111010000100000011101110110111101101110011001000110010101110010011001100111010101101100001000000111010001101111001000000111001101100101011001010010000001111001011011110111010100100001
Base2 Decoded (Serial): Hello, World! It is just wonderful to see you!
Base4 Encoded (Serial): 1020121112301230123302300200111312331302123012100201020010211310020012211303020012221311130313100200131312331232121012111302121213111230020013101233020013031211121102001321123313110201
Base4 Decoded (Serial): Hello, World! It is just wonderful to see you!
Base8 Encoded (Serial): 220625543306745410053557344661441022011135020151346201523527156410073557334621453446316533020164336201633126244036267565102
Base8 Decoded (Serial): Hello, World! It is just wonderful to see you!
Base16 Encoded (Serial): 48656C6C6F2C20576F726C6421204974206973206A75737420776F6E64657266756C20746F2073656520796F7521
Base16 Decoded (Serial): Hello, World! It is just wonderful to see you!
Base32 Encoded (Serial): JBSWY3DPFQQFO33SNRSCCICJOQQGS4ZANJ2XG5BAO5XW4ZDFOJTHK3BAORXSA43FMUQHS33VEE
Base32 Decoded (Serial): Hello, World! It is just wonderful to see you!
Base32Hex Encoded (Serial): 91IMOR3F5GG5ERRIDHI22829EGG6ISP0D9QN6T10ETNMSP35E9J7AR10EHNI0SR5CKG7IRRL44
Base32Hex Decoded (Serial): Hello, World! It is just wonderful to see you!
Base32Crockford Encoded (Serial): 91JPRV3F5GG5EVVJDHJ22829EGG6JWS0D9TQ6X10EXQPWS35E9K7AV10EHQJ0WV5CMG7JVVN44
Base32Crockford Decoded (Serial): Hello, World! It is just wonderful to see you!
Base36 Encoded (Serial): 91IMOR3F5GG5ERRIDHI22829EGG6ISP0D9QN6T10ETNMSP35E9J7AR10EHNI0SR5CKG7IRRL44
Base36 Decoded (Serial): Hello, World! It is just wonderful to see you!
Base64 Encoded (Serial): SGVsbG8sIFdvcmxkISBJdCBpcyBqdXN0IHdvbmRlcmZ1bCB0byBzZWUgeW91IQ
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
Base32 Encoded (Binary): JBSWY3DPFQQFO33SNRSCC
Base32 Decoded (Binary - ASCII): 48 65 6c 6c 6f 2c 20 57 6f 72 6c 64 21  | Hello, World!
Base32Hex Encoded (Binary): 91IMOR3F5GG5ERRIDHI22
Base32Hex Decoded (Binary - ASCII): 48 65 6c 6c 6f 2c 20 57 6f 72 6c 64 21  | Hello, World!
Base32Crockford Encoded (Binary): 91JPRV3F5GG5EVVJDHJ22
Base32Crockford Decoded (Binary - ASCII): 48 65 6c 6c 6f 2c 20 57 6f 72 6c 64 21  | Hello, World!
Base36 Encoded (Binary): 91IMOR3F5GG5ERRIDHI22
Base36 Decoded (Binary - ASCII): 48 65 6c 6c 6f 2c 20 57 6f 72 6c 64 21  | Hello, World!
Base64 Encoded (Binary): SGVsbG8sIFdvcmxkIQ
Base64 Decoded (Binary - ASCII): 48 65 6c 6c 6f 2c 20 57 6f 72 6c 64 21  | Hello, World!
Program ended with exit code: 0
```
