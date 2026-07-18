// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Saxon Nicholls

//
//  test_crypto.cpp
//  BaseEncodeDecode - optional crypto / key-value-store tests
//
//  Separate from the dependency-free suite: this one links OpenSSL, libsodium
//  and (if present) RocksDB. Build with utils on the include path and the
//  libraries linked - see the Makefile `test-crypto` target.
//

#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <filesystem>
#include <map>
#include <string>

#include "encode_decode_base_whatever.hpp"
#include "encode_decode_object.hpp"
#include "encode_decode_dna.hpp"
#include "utils/stl_support.hpp"
#include "utils/secure.hpp"
#include "utils/encryption_openssl.hpp"
#include "utils/encryption_sodium.hpp"
#include "utils/key_value_store.hpp"
#include "utils/kv_rocksdb.hpp"

using namespace snicholls;

// A realistic "secret object": a balances map plus an id.
using Ledger = std::map<std::string, uint64_t>;

static void TestEncryptor(Encryptor& cipher) {
    std::printf("  algorithm: %s\n", cipher.algorithm().c_str());

    const std::string message = "the seed phrase: correct horse battery staple";
    const std::span<const uint8_t> pt(reinterpret_cast<const uint8_t*>(message.data()), message.size());

    // Round trip
    const SecureBytes blob = cipher.encrypt(pt);
    assert(blob.size() > message.size()); // nonce + tag overhead
    const SecureBytes back = cipher.decrypt(blob);
    assert(std::string(back.begin(), back.end()) == message);

    // Two encryptions of the same plaintext differ (random nonce)
    assert(cipher.encrypt(pt) != cipher.encrypt(pt));

    // Tampering is detected (flip one ciphertext byte)
    SecureBytes tampered = blob;
    tampered[tampered.size() / 2] ^= 0x01;
    bool threw = false;
    try { (void)cipher.decrypt(tampered); } catch (const EncryptionError&) { threw = true; }
    assert(threw);

    // Empty plaintext round-trips
    assert(cipher.decrypt(cipher.encrypt(std::span<const uint8_t>{})).empty());
}

static void TestWrongKey() {
    const SecureBytes k1 = SodiumEncryptor::generateKey();
    const SecureBytes k2 = SodiumEncryptor::generateKey();
    SodiumEncryptor a(k1), b(k2);
    const std::string msg = "secret";
    const SecureBytes blob = a.encrypt(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(msg.data()), msg.size()));
    bool threw = false;
    try { (void)b.decrypt(blob); } catch (const EncryptionError&) { threw = true; }
    assert(threw);
}

static void TestKeyValueStore() {
    InMemoryKeyValueStore kv;
    assert(kv.put("user:1", "alice"));
    assert(kv.get("user:1") == std::optional<std::string>("alice"));
    assert(kv.contains("user:1") && !kv.contains("missing"));
    assert(!kv.get("missing").has_value());
    kv.put("user:2", "bob");
    kv.put("acct:9", "x");
    assert(kv.getByPrefix("user:").size() == 2);
    assert(kv.remove("user:1") && !kv.contains("user:1"));

    // Plaintext object round trip
    const Ledger ledger{{"USD", 1000}, {"BTC", 2}};
    assert(PutObject(kv, "ledger", ledger));
    assert(GetObject<Ledger>(kv, "ledger") == std::optional<Ledger>(ledger));
    assert(!GetObject<Ledger>(kv, "nope").has_value());
}

static void TestEncryptedObjectStore(Encryptor& cipher) {
    InMemoryKeyValueStore kv;
    const Ledger ledger{{"USD", 999999}, {"ETH", 42}};

    assert(PutObjectEncrypted(kv, "vault", ledger, cipher));
    // Stored value is ciphertext base64 - it must NOT contain the plaintext
    const std::string stored = *kv.get("vault");
    assert(stored.find("USD") == std::string::npos);

    const auto restored = GetObjectEncrypted<Ledger>(kv, "vault", cipher);
    assert(restored == std::optional<Ledger>(ledger));
}

static void TestSecureDna(Encryptor& cipher) {
    // The grand composition: a secret object -> serialise -> encrypt -> represent
    // the ciphertext as DNA -> (store / transmit) -> pack DNA -> decrypt -> object.
    const Ledger ledger{{"XMR", 7}, {"USD", 250000}};

    Binary plain = ObjectSerializer<Ledger>::to_bytes(ledger);
    SecureBytes securePlain(plain.begin(), plain.end());
    SecureWipe(plain.data(), plain.size());

    const SecureBytes ciphertext = cipher.encrypt(securePlain);
    const std::string dna = UnpackDna(ciphertext); // ciphertext as ACGT (4 bases/byte)
    assert(dna.size() == ciphertext.size() * 4);
    assert(dna.find_first_not_of("ACGT") == std::string::npos);

    // ... transmit `dna` ... then reverse
    const Binary ciphertext2 = PackDna(dna);
    const SecureBytes recovered = cipher.decrypt(ciphertext2);
    const Ledger back = ObjectSerializer<Ledger>::from_bytes(recovered);
    assert(back == ledger);
    std::printf("  ledger -> encrypt -> %zu bases of DNA -> decrypt -> ledger: OK\n", dna.size());
}

#ifdef SNICHOLLS_HAVE_ROCKSDB
static void TestRocksDb(Encryptor& cipher) {
    const auto dir = std::filesystem::temp_directory_path() / "bed_rocksdb_test";
    std::filesystem::remove_all(dir);
    {
        RocksDbKeyValueStore kv(dir.string());
        const Ledger ledger{{"USD", 100}, {"AUD", 200}};
        assert(PutObject(kv, "plain", ledger));
        assert(PutObjectEncrypted(kv, "secret", ledger, cipher));
        assert(GetObject<Ledger>(kv, "plain") == std::optional<Ledger>(ledger));
        assert(GetObjectEncrypted<Ledger>(kv, "secret", cipher) == std::optional<Ledger>(ledger));
        assert(kv.contains("plain") && !kv.contains("absent"));
    }
    std::filesystem::remove_all(dir);
    std::puts("RocksDB key-value store (object + encrypted): OK");
}
#endif

int main() {
    std::puts("OpenSSL AES-256-GCM:");
    {
        OpenSslAesGcmEncryptor cipher(OpenSslAesGcmEncryptor::generateKey());
        TestEncryptor(cipher);
        TestEncryptedObjectStore(cipher);
        TestSecureDna(cipher);
    }
    std::puts("libsodium XChaCha20-Poly1305:");
    {
        SodiumEncryptor cipher(SodiumEncryptor::generateKey());
        TestEncryptor(cipher);
        TestEncryptedObjectStore(cipher);
        TestSecureDna(cipher);
    }
    TestWrongKey();
    std::puts("Wrong-key rejection: OK");
    TestKeyValueStore();
    std::puts("Key-value store + plaintext object: OK");

    // Passphrase-derived key (Argon2id)
    {
        const std::string pw = "hunter2";
        std::array<uint8_t, crypto_pwhash_SALTBYTES> salt{};
        randombytes_buf(salt.data(), salt.size());
        SodiumEncryptor a(SodiumEncryptor::keyFromPassphrase(
            std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(pw.data()), pw.size()), salt));
        SodiumEncryptor b(SodiumEncryptor::keyFromPassphrase(
            std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(pw.data()), pw.size()), salt));
        const std::string m = "same passphrase, same salt -> same key";
        const auto blob = a.encrypt(std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(m.data()), m.size()));
        const auto out = b.decrypt(blob);
        assert(std::string(out.begin(), out.end()) == m);
        std::puts("Argon2id passphrase key derivation: OK");
    }

#ifdef SNICHOLLS_HAVE_ROCKSDB
    {
        SodiumEncryptor cipher(SodiumEncryptor::generateKey());
        TestRocksDb(cipher);
    }
#endif

    std::puts("All crypto/KV tests passed.");
    return 0;
}
