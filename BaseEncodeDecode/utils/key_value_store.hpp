// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Saxon Nicholls

//
//  utils/key_value_store.hpp
//  BaseEncodeDecode
//
//  A deliberately small key/value-store interface, an in-memory implementation,
//  and helpers to save/load any serialisable object under a key as Base64 -
//  plaintext, or encrypted. A RocksDB implementation lives in the autodetected
//  utils/kv_rocksdb.hpp. Include only what your use case needs.
//
//      #include "encode_decode_object.hpp"   // to serialise your types
//      #include "utils/key_value_store.hpp"
//
//      InMemoryKeyValueStore kv;
//      PutObject(kv, "user:1", user);                 // object -> Base64 -> store
//      auto u = GetObject<User>(kv, "user:1");        // std::optional<User>
//
//      // Encrypted at rest (needs an Encryptor, e.g. from utils/encryption_sodium.hpp):
//      PutObjectEncrypted(kv, "seed", wallet, cipher);
//      auto w = GetObjectEncrypted<Wallet>(kv, "seed", cipher);
//
//  The interface stores std::string values; that is exactly right for encrypted
//  data (the stored blob is ciphertext). For plaintext-Base64 storage the value
//  is readable, so use the encrypted helpers for anything sensitive.
//

#ifndef encode_decode_key_value_store_hpp
#define encode_decode_key_value_store_hpp

#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "../encode_decode_object.hpp"
#include "encryption.hpp" // the Encryptor interface (no crypto library needed for this)
#include "secure.hpp"

namespace snicholls {

    // Minimal CRUD key/value store. put/get/remove are pure virtual; the rest
    // have portable defaults that concrete stores may override for performance.
    class KeyValueStoreInterface {
    public:
        virtual ~KeyValueStoreInterface() = default;

        virtual bool put(const std::string& key, const std::string& value) = 0;
        virtual std::optional<std::string> get(const std::string& key) = 0;
        virtual bool remove(const std::string& key) = 0;
        virtual std::map<std::string, std::string> getAll() = 0;

        virtual bool contains(const std::string& key) { return get(key).has_value(); }

        // Keys that start with `prefix`.
        virtual std::map<std::string, std::string> getByPrefix(const std::string& prefix) {
            std::map<std::string, std::string> out;
            for (auto& [key, value] : getAll()) {
                if (key.compare(0, prefix.size(), prefix) == 0) {
                    out.emplace(key, value);
                }
            }
            return out;
        }
    };

    // Thread-safe in-memory store.
    class InMemoryKeyValueStore : public KeyValueStoreInterface {
        std::map<std::string, std::string> data_;
        std::mutex mutex_;
    public:
        bool put(const std::string& key, const std::string& value) override {
            std::lock_guard<std::mutex> lock(mutex_);
            data_[key] = value;
            return true;
        }
        std::optional<std::string> get(const std::string& key) override {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto it = data_.find(key);
            if (it == data_.end()) return std::nullopt;
            return it->second;
        }
        bool remove(const std::string& key) override {
            std::lock_guard<std::mutex> lock(mutex_);
            return data_.erase(key) > 0;
        }
        bool contains(const std::string& key) override {
            std::lock_guard<std::mutex> lock(mutex_);
            return data_.count(key) > 0;
        }
        std::map<std::string, std::string> getAll() override {
            std::lock_guard<std::mutex> lock(mutex_);
            return data_;
        }
    };

    // --- Object helpers ------------------------------------------------------

    // Plaintext: serialise object -> Base64 -> store. The stored value is
    // readable; use the encrypted helpers for anything sensitive.
    template<ObjectSerializable T>
    bool PutObject(KeyValueStoreInterface& kv, const std::string& key, const T& obj) {
        return kv.put(key, EncodeBase64Object(obj));
    }
    template<ObjectSerializable T>
    std::optional<T> GetObject(KeyValueStoreInterface& kv, const std::string& key) {
        const auto value = kv.get(key);
        if (!value) return std::nullopt;
        return DecodeBase64Object<T>(*value);
    }

    // Encrypted at rest: serialise -> encrypt -> Base64 -> store. The plaintext
    // exists only in wiped SecureBytes; the stored value is authenticated
    // ciphertext. Reconstruct into a Secure type (see IsSecure) to keep the
    // decrypted result wiped as well.
    template<ObjectSerializable T>
    bool PutObjectEncrypted(KeyValueStoreInterface& kv, const std::string& key,
                            const T& obj, Encryptor& cipher) {
        Binary plain = ObjectSerializer<T>::to_bytes(obj);
        SecureBytes secure(plain.begin(), plain.end());
        SecureWipe(plain.data(), plain.size()); // scrub the plaintext temporary
        const SecureBytes encrypted = cipher.encrypt(secure);
        return kv.put(key, EncodeBase64Binary(encrypted)); // ciphertext Base64
    }
    template<ObjectSerializable T>
    std::optional<T> GetObjectEncrypted(KeyValueStoreInterface& kv, const std::string& key,
                                        Encryptor& cipher) {
        const auto value = kv.get(key);
        if (!value) return std::nullopt;
        const SecureBytes encrypted = DecodeBase64Secure(*value);
        const SecureBytes plain = cipher.decrypt(encrypted);
        return ObjectSerializer<T>::from_bytes(plain);
    }
}

#endif /* encode_decode_key_value_store_hpp */
