// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Saxon Nicholls

//
//  utils/encryption.hpp
//  BaseEncodeDecode
//
//  A general, algorithm-agnostic interface for authenticated symmetric
//  encryption. This library NEVER implements a cipher itself - the concrete
//  implementations (utils/encryption_openssl.hpp, utils/encryption_sodium.hpp)
//  delegate entirely to vetted libraries, and are auto-enabled only when those
//  libraries' headers are present. Include only what your use case needs.
//
//      #include "utils/encryption_sodium.hpp"   // or _openssl
//
//      SecureBytes key = SodiumEncryptor::generateKey();
//      SodiumEncryptor cipher(key);
//      SecureBytes blob = cipher.encrypt(plaintext);   // nonce + ciphertext + tag
//      SecureBytes back = cipher.decrypt(blob);        // throws if tampered / wrong key
//
//  Contract for every Encryptor:
//   * encrypt() returns a SELF-CONTAINED blob = random nonce || ciphertext ||
//     auth tag. You do not manage nonces; a fresh random one is used each call.
//   * decrypt() verifies the authentication tag and throws EncryptionError on
//     any tampering or a wrong key - it never returns unauthenticated data.
//   * Plaintext and keys live in SecureBytes (wiped storage). Ciphertext is
//     also returned as SecureBytes; it is safe to then Base64/DNA-encode and
//     store it (it is encrypted and authenticated).
//
//  Keep it general: the interface speaks only "bytes in, bytes out", so an
//  object can be serialised (ObjectSerializer) -> encrypted -> Base64 or DNA
//  -> stored, and reversed, without any layer knowing the algorithm.
//

#ifndef encode_decode_encryption_hpp
#define encode_decode_encryption_hpp

#include <span>
#include <stdexcept>
#include <string>

#include "secure.hpp" // SecureBytes, SecureWipe

namespace snicholls {

    class EncryptionError : public std::runtime_error {
    public:
        using std::runtime_error::runtime_error;
    };

    // Authenticated symmetric encryptor. Concrete subclasses hold a key and are
    // backed by OpenSSL, libsodium, etc. All inputs accept any contiguous byte
    // range (Binary, SecureBytes, std::span, ...) via std::span conversion.
    class Encryptor {
    public:
        virtual ~Encryptor() = default;

        // plaintext -> (nonce || ciphertext || tag), in wiped storage
        virtual SecureBytes encrypt(std::span<const uint8_t> plaintext) = 0;

        // (nonce || ciphertext || tag) -> plaintext; throws EncryptionError on
        // authentication failure (tampering or wrong key)
        virtual SecureBytes decrypt(std::span<const uint8_t> ciphertext) = 0;

        // e.g. "AES-256-GCM", "XChaCha20-Poly1305"
        virtual std::string algorithm() const = 0;
    };
}

#endif /* encode_decode_encryption_hpp */
