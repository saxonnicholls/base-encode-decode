
# Base Encoding and Decoding in C++

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
