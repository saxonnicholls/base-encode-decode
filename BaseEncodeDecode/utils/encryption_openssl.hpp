// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Saxon Nicholls

//
//  utils/encryption_openssl.hpp
//  BaseEncodeDecode
//
//  OpenSSL drop-in implementation of the Encryptor interface, auto-enabled only
//  when <openssl/evp.h> is present. Provides authenticated encryption with
//  AES-256-GCM (a fresh random 96-bit IV per call).
//
//      #include "utils/encryption_openssl.hpp"
//
//      SecureBytes key = OpenSslAesGcmEncryptor::generateKey();  // 32 random bytes
//      OpenSslAesGcmEncryptor cipher(key);
//      SecureBytes blob = cipher.encrypt(plaintext);             // iv || ciphertext || tag
//      SecureBytes back = cipher.decrypt(blob);
//
//  The crypto is entirely OpenSSL's EVP layer; this header only marshals bytes.
//  Link with -lcrypto.
//

#ifndef encode_decode_encryption_openssl_hpp
#define encode_decode_encryption_openssl_hpp

#if __has_include(<openssl/evp.h>) && !defined(SNICHOLLS_NO_OPENSSL)
#define SNICHOLLS_HAVE_OPENSSL 1

#include <openssl/evp.h>
#include <openssl/rand.h>

#include "encryption.hpp"

namespace snicholls {

    class OpenSslAesGcmEncryptor : public Encryptor {
        SecureBytes key_;

        struct CtxGuard {
            EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
            CtxGuard() { if (!ctx) throw EncryptionError("EVP_CIPHER_CTX_new failed"); }
            ~CtxGuard() { if (ctx) EVP_CIPHER_CTX_free(ctx); }
            CtxGuard(const CtxGuard&) = delete;
            CtxGuard& operator=(const CtxGuard&) = delete;
        };

    public:
        static constexpr size_t KeyBytes = 32; // AES-256
        static constexpr size_t IvBytes = 12;  // GCM standard
        static constexpr size_t TagBytes = 16;

        explicit OpenSslAesGcmEncryptor(SecureBytes key) : key_(std::move(key)) {
            if (key_.size() != KeyBytes) {
                throw EncryptionError("OpenSslAesGcmEncryptor: key must be 32 bytes");
            }
        }

        static SecureBytes generateKey() {
            SecureBytes key(KeyBytes);
            if (RAND_bytes(key.data(), static_cast<int>(KeyBytes)) != 1) {
                throw EncryptionError("OpenSSL RAND_bytes failed");
            }
            return key;
        }

        SecureBytes encrypt(std::span<const uint8_t> plaintext) override {
            SecureBytes out(IvBytes + plaintext.size() + TagBytes);
            if (RAND_bytes(out.data(), static_cast<int>(IvBytes)) != 1) {
                throw EncryptionError("OpenSSL RAND_bytes failed");
            }

            CtxGuard g;
            if (EVP_EncryptInit_ex(g.ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
                EVP_CIPHER_CTX_ctrl(g.ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(IvBytes), nullptr) != 1 ||
                EVP_EncryptInit_ex(g.ctx, nullptr, nullptr, key_.data(), out.data()) != 1) {
                throw EncryptionError("OpenSSL AES-GCM encrypt init failed");
            }

            int len = 0;
            int total = 0;
            if (!plaintext.empty()) {
                if (EVP_EncryptUpdate(g.ctx, out.data() + IvBytes, &len,
                                      plaintext.data(), static_cast<int>(plaintext.size())) != 1) {
                    throw EncryptionError("OpenSSL AES-GCM encrypt update failed");
                }
                total = len;
            }
            if (EVP_EncryptFinal_ex(g.ctx, out.data() + IvBytes + total, &len) != 1) {
                throw EncryptionError("OpenSSL AES-GCM encrypt final failed");
            }
            total += len;
            if (EVP_CIPHER_CTX_ctrl(g.ctx, EVP_CTRL_GCM_GET_TAG, static_cast<int>(TagBytes),
                                    out.data() + IvBytes + total) != 1) {
                throw EncryptionError("OpenSSL AES-GCM get tag failed");
            }
            out.resize(IvBytes + static_cast<size_t>(total) + TagBytes);
            return out;
        }

        SecureBytes decrypt(std::span<const uint8_t> blob) override {
            if (blob.size() < IvBytes + TagBytes) {
                throw EncryptionError("OpenSslAesGcmEncryptor: ciphertext too short");
            }
            const size_t clen = blob.size() - IvBytes - TagBytes;
            SecureBytes out(clen);

            CtxGuard g;
            if (EVP_DecryptInit_ex(g.ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1 ||
                EVP_CIPHER_CTX_ctrl(g.ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(IvBytes), nullptr) != 1 ||
                EVP_DecryptInit_ex(g.ctx, nullptr, nullptr, key_.data(), blob.data()) != 1) {
                throw EncryptionError("OpenSSL AES-GCM decrypt init failed");
            }

            int len = 0;
            int total = 0;
            if (clen > 0) {
                if (EVP_DecryptUpdate(g.ctx, out.data(), &len,
                                      blob.data() + IvBytes, static_cast<int>(clen)) != 1) {
                    throw EncryptionError("OpenSSL AES-GCM decrypt update failed");
                }
                total = len;
            }
            // Expected tag sits at the end of the blob
            if (EVP_CIPHER_CTX_ctrl(g.ctx, EVP_CTRL_GCM_SET_TAG, static_cast<int>(TagBytes),
                                    const_cast<uint8_t*>(blob.data() + IvBytes + clen)) != 1) {
                throw EncryptionError("OpenSSL AES-GCM set tag failed");
            }
            if (EVP_DecryptFinal_ex(g.ctx, out.data() + total, &len) != 1) {
                throw EncryptionError("OpenSslAesGcmEncryptor: decryption failed (bad key or tampered data)");
            }
            total += len;
            out.resize(static_cast<size_t>(total));
            return out;
        }

        std::string algorithm() const override { return "AES-256-GCM"; }
    };
}

#endif /* __has_include(<openssl/evp.h>) */
#endif /* encode_decode_encryption_openssl_hpp */
