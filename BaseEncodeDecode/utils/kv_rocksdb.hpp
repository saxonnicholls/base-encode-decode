// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Saxon Nicholls

//
//  utils/kv_rocksdb.hpp
//  BaseEncodeDecode
//
//  RocksDB implementation of KeyValueStoreInterface, auto-enabled only when
//  <rocksdb/db.h> is present. Link with -lrocksdb.
//
//      #include "encode_decode_object.hpp"
//      #include "utils/key_value_store.hpp"
//      #include "utils/kv_rocksdb.hpp"
//
//      RocksDbKeyValueStore kv("/var/data/store");
//      PutObjectEncrypted(kv, "seed", wallet, cipher);   // object -> encrypt -> Base64 -> RocksDB
//
//  All the object / encrypted helpers in key_value_store.hpp work unchanged,
//  since RocksDbKeyValueStore is just another KeyValueStoreInterface.
//

#ifndef encode_decode_kv_rocksdb_hpp
#define encode_decode_kv_rocksdb_hpp

#if __has_include(<rocksdb/db.h>) && !defined(SNICHOLLS_NO_ROCKSDB)
#define SNICHOLLS_HAVE_ROCKSDB 1

#include <memory>
#include <stdexcept>

#include <rocksdb/db.h>

#include "key_value_store.hpp"

namespace snicholls {

    class RocksDbKeyValueStore : public KeyValueStoreInterface {
        std::unique_ptr<rocksdb::DB> db_;
    public:
        explicit RocksDbKeyValueStore(const std::string& path, bool createIfMissing = true) {
            rocksdb::Options options;
            options.create_if_missing = createIfMissing;
            rocksdb::DB* raw = nullptr;
            const rocksdb::Status status = rocksdb::DB::Open(options, path, &raw);
            if (!status.ok()) {
                throw std::runtime_error("RocksDbKeyValueStore: open failed: " + status.ToString());
            }
            db_.reset(raw);
        }

        bool put(const std::string& key, const std::string& value) override {
            return db_->Put(rocksdb::WriteOptions(), key, value).ok();
        }
        std::optional<std::string> get(const std::string& key) override {
            std::string value;
            const rocksdb::Status status = db_->Get(rocksdb::ReadOptions(), key, &value);
            if (status.IsNotFound()) return std::nullopt;
            if (!status.ok()) {
                throw std::runtime_error("RocksDbKeyValueStore: get failed: " + status.ToString());
            }
            return value;
        }
        bool remove(const std::string& key) override {
            return db_->Delete(rocksdb::WriteOptions(), key).ok();
        }
        bool contains(const std::string& key) override {
            std::string value;
            return db_->Get(rocksdb::ReadOptions(), key, &value).ok();
        }
        std::map<std::string, std::string> getAll() override {
            std::map<std::string, std::string> out;
            std::unique_ptr<rocksdb::Iterator> it(db_->NewIterator(rocksdb::ReadOptions()));
            for (it->SeekToFirst(); it->Valid(); it->Next()) {
                out.emplace(it->key().ToString(), it->value().ToString());
            }
            return out;
        }
    };
}

#endif /* __has_include(<rocksdb/db.h>) */
#endif /* encode_decode_kv_rocksdb_hpp */
