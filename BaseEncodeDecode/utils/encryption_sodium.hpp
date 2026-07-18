// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Saxon Nicholls

//
//  utils/encryption_sodium.hpp
//  BaseEncodeDecode
//
//  libsodium drop-in implementation of the Encryptor interface, auto-enabled
//  only when <sodium.h> is present. Provides authenticated encryption with
//  XChaCha20-Poly1305 (IETF) - a large 192-bit random nonce makes it safe to
//  generate nonces randomly, which is what we do.
//
//      #include "utils/encryption_sodium.hpp"
//
//      SecureBytes key = SodiumEncryptor::generateKey();   // 32 random bytes
//      SodiumEncryptor cipher(key);
//      SecureBytes blob = cipher.encrypt(plaintext);
//      SecureBytes back = cipher.decrypt(blob);
//
//  You may also derive a key from a passphrase with SodiumEncryptor::keyFrom-
//  Passphrase (Argon2id). Call order: sodium_init() is invoked for you.
//
//  The crypto is entirely libsodium's; this header only marshals bytes.
//

#ifndef encode_decode_encryption_sodium_hpp
#define encode_decode_encryption_sodium_hpp

#if __has_include(<sodium.h>) && !defined(SNICHOLLS_NO_SODIUM)
#define SNICHOLLS_HAVE_SODIUM 1

#include <sodium.h>

#include "encryption.hpp"

namespace snicholls {

    class SodiumEncryptor : public Encryptor {
        SecureBytes key_;

        static void ensureInit() {
            if (sodium_init() < 0) {
                throw EncryptionError("libsodium initialisation failed");
            }
        }

    public:
        static constexpr size_t KeyBytes = crypto_aead_xchacha20poly1305_ietf_KEYBYTES;   // 32
        static constexpr size_t NonceBytes = crypto_aead_xchacha20poly1305_ietf_NPUBBYTES; // 24
        static constexpr size_t TagBytes = crypto_aead_xchacha20poly1305_ietf_ABYTES;      // 16

        explicit SodiumEncryptor(SecureBytes key) : key_(std::move(key)) {
            ensureInit();
            if (key_.size() != KeyBytes) {
                throw EncryptionError("SodiumEncryptor: key must be 32 bytes");
            }
        }

        static SecureBytes generateKey() {
            ensureInit();
            SecureBytes key(KeyBytes);
            crypto_aead_xchacha20poly1305_ietf_keygen(key.data());
            return key;
        }

        // Argon2id key derivation from a passphrase. `salt` must be
        // crypto_pwhash_SALTBYTES (16) bytes and stored with the ciphertext so
        // the same key can be re-derived; pass the same salt to decrypt.
        static SecureBytes keyFromPassphrase(std::span<const uint8_t> passphrase,
                                             std::span<const uint8_t> salt) {
            ensureInit();
            if (salt.size() != crypto_pwhash_SALTBYTES) {
                throw EncryptionError("SodiumEncryptor: salt must be crypto_pwhash_SALTBYTES bytes");
            }
            SecureBytes key(KeyBytes);
            if (crypto_pwhash(key.data(), key.size(),
                              reinterpret_cast<const char*>(passphrase.data()), passphrase.size(),
                              salt.data(),
                              crypto_pwhash_OPSLIMIT_MODERATE, crypto_pwhash_MEMLIMIT_MODERATE,
                              crypto_pwhash_ALG_ARGON2ID13) != 0) {
                throw EncryptionError("SodiumEncryptor: key derivation failed (out of memory?)");
            }
            return key;
        }

        SecureBytes encrypt(std::span<const uint8_t> plaintext) override {
            SecureBytes out(NonceBytes + plaintext.size() + TagBytes);
            randombytes_buf(out.data(), NonceBytes);
            unsigned long long clen = 0;
            crypto_aead_xchacha20poly1305_ietf_encrypt(
                out.data() + NonceBytes, &clen,
                plaintext.data(), plaintext.size(),
                nullptr, 0, nullptr,
                out.data() /* nonce */, key_.data());
            out.resize(NonceBytes + static_cast<size_t>(clen));
            return out;
        }

        SecureBytes decrypt(std::span<const uint8_t> blob) override {
            if (blob.size() < NonceBytes + TagBytes) {
                throw EncryptionError("SodiumEncryptor: ciphertext too short");
            }
            SecureBytes out(blob.size() - NonceBytes - TagBytes);
            unsigned long long mlen = 0;
            if (crypto_aead_xchacha20poly1305_ietf_decrypt(
                    out.data(), &mlen, nullptr,
                    blob.data() + NonceBytes, blob.size() - NonceBytes,
                    nullptr, 0,
                    blob.data() /* nonce */, key_.data()) != 0) {
                throw EncryptionError("SodiumEncryptor: decryption failed (bad key or tampered data)");
            }
            out.resize(static_cast<size_t>(mlen));
            return out;
        }

        std::string algorithm() const override { return "XChaCha20-Poly1305"; }
    };
}

#endif /* __has_include(<sodium.h>) */
#endif /* encode_decode_encryption_sodium_hpp */
