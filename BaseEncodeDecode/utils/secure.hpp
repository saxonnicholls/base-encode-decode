// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Saxon Nicholls

//
//  utils/secure.hpp
//  BaseEncodeDecode
//
//  Optional utilities for handling sensitive bytes (keys, seeds, passphrases)
//  alongside the codec. Include it explicitly:
//
//      #include "utils/secure.hpp"
//
//      SecureBytes key = DecodeBase64Secure(b64_secret);  // decoded into wiped storage
//      std::string b64 = EncodeBase64(key);               // SecureBytes is a ByteSource
//      if (ConstantTimeEqual(a, b)) { ... }
//
//  What this gives you
//  -------------------
//   * SecureWipe(p, n) - zero memory such that the compiler cannot elide the
//     write (volatile stores plus a compiler barrier).
//   * SecureAllocator<T> - an allocator that SecureWipes every block before it
//     is freed, so a container built on it is scrubbed on reallocation AND on
//     destruction.
//   * SecureBytes  - a drop-in for Binary (std::vector<uint8_t>) on that
//     allocator. Same API, so it works anywhere Binary does; it models
//     ByteSource, feeds Encode*/Decode* directly, and Decode*Secure decodes into
//     it with no unwiped intermediate.
//   * SecureString and a "SecureSTL" of the standard containers on the wiping
//     allocator: SecureVector, SecureDeque, SecureList, SecureSet, SecureMap,
//     SecureUnorderedSet, SecureUnorderedMap. Identical APIs to their std
//     counterparts; they also compose with the object serializer.
//   * ConstantTimeEqual - compare two buffers without a data-dependent branch.
//
//  Encoders can also target a wiped output: EncodeBase64<SecureString>(secret)
//  writes the encoded secret straight into a SecureString (no std::string
//  intermediate). The output-type template parameter defaults to std::string.
//
//  HONEST SCOPE - this is best-effort defense-in-depth, NOT a hard guarantee:
//   * It does not stop copies the compiler makes before the wipe (register
//     spills, temporaries), nor secrets paged to swap (no mlock), nor use of
//     freed pages (no guard pages).
//   * SecureBytes/SecureString are copyable, like Binary/std::string - each copy
//     is its own wiped-on-free buffer, but copies still multiply the secret in
//     memory, so move rather than copy where you can.
//   * SecureString's storage is scrubbed on free, but the small-string
//     optimisation keeps short values inline (bypassing the allocator), so short
//     strings are NOT wiped. Use SecureBytes (no SSO) for short secrets.
//   * Base-encoding a secret still produces output buffers you must handle:
//     EncodeBase64(secret) returns an ordinary std::string that also holds
//     sensitive data.
//  For hard requirements (mlock, guard pages, audited wiping) use a dedicated
//  library such as libsodium's secure-memory API. This header is the
//  lightweight, dependency-free option.
//

#ifndef encode_decode_secure_hpp
#define encode_decode_secure_hpp

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <list>
#include <map>
#include <new>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "../encode_decode_base_whatever.hpp"

namespace snicholls {

    // Zero memory so the write cannot be optimised away.
    inline void SecureWipe(void* p, size_t n) noexcept {
        if (p == nullptr || n == 0) {
            return;
        }
        volatile uint8_t* q = static_cast<volatile uint8_t*>(p);
        for (size_t i = 0; i < n; ++i) {
            q[i] = 0;
        }
#if defined(__GNUC__) || defined(__clang__)
        __asm__ __volatile__("" : : "r"(p) : "memory"); // barrier against reordering/elision
#endif
    }

    // Allocator that scrubs each block before returning it to the system, so a
    // container using it is wiped on reallocation as well as on destruction.
    template<typename T>
    struct SecureAllocator {
        using value_type = T;

        SecureAllocator() noexcept = default;
        template<typename U>
        SecureAllocator(const SecureAllocator<U>&) noexcept {}

        T* allocate(size_t n) {
            if (n > std::numeric_limits<size_t>::max() / sizeof(T)) {
                throw std::bad_alloc();
            }
            return static_cast<T*>(::operator new(n * sizeof(T)));
        }
        void deallocate(T* p, size_t n) noexcept {
            SecureWipe(p, n * sizeof(T));
            ::operator delete(p);
        }

        template<typename U>
        bool operator==(const SecureAllocator<U>&) const noexcept { return true; }
        template<typename U>
        bool operator!=(const SecureAllocator<U>&) const noexcept { return false; }
    };

    // The standard containers on the wiping allocator - a "SecureSTL". Each is
    // the ordinary container with SecureAllocator, so it has the identical API
    // and composes with the codec and the object serializer, while its storage
    // is scrubbed on every free.
    //
    // Caveat: the allocator scrubs the CONTAINER'S OWN storage (buffer/nodes),
    // not heap owned by non-trivial element types. SecureVector<std::string>
    // wipes the vector's buffer but not each string's - use a Secure element
    // type (e.g. SecureVector<SecureString>) for fully-wiped nesting.
    template<typename T>
    using SecureVector = std::vector<T, SecureAllocator<T>>;
    template<typename T>
    using SecureDeque = std::deque<T, SecureAllocator<T>>;
    template<typename T>
    using SecureList = std::list<T, SecureAllocator<T>>;
    template<typename Key, typename Compare = std::less<Key>>
    using SecureSet = std::set<Key, Compare, SecureAllocator<Key>>;
    template<typename Key, typename Value, typename Compare = std::less<Key>>
    using SecureMap = std::map<Key, Value, Compare, SecureAllocator<std::pair<const Key, Value>>>;
    template<typename Key, typename Hash = std::hash<Key>, typename Eq = std::equal_to<Key>>
    using SecureUnorderedSet = std::unordered_set<Key, Hash, Eq, SecureAllocator<Key>>;
    template<typename Key, typename Value, typename Hash = std::hash<Key>, typename Eq = std::equal_to<Key>>
    using SecureUnorderedMap = std::unordered_map<Key, Value, Hash, Eq, SecureAllocator<std::pair<const Key, Value>>>;

    // Drop-in replacements for Binary and std::string. SecureBytes has the full
    // std::vector<uint8_t> API and is a ByteSource, so it plugs into Encode*/
    // Decode* wherever Binary would.
    using SecureBytes = SecureVector<uint8_t>;
    using SecureString = std::basic_string<char, std::char_traits<char>, SecureAllocator<char>>;

    // ------------------------------------------------------------------
    // IsSecure<T> - does T keep all of its (transitive) heap storage in wiped
    // memory? It is COMPOSABLE: a Secure container is secure iff its elements
    // are, and a pair/tuple/optional/variant is secure iff its members are.
    //
    //   IsSecure<int>                              -> true  (no heap to leak)
    //   IsSecure<SecureBytes>                      -> true
    //   IsSecure<std::string>                      -> false (std allocator)
    //   IsSecure<SecureVector<SecureString>>       -> true
    //   IsSecure<SecureVector<std::string>>        -> false (buffer wiped, not the strings)
    //   IsSecure<SecureMap<SecureString, int>>     -> true
    //
    // Use it in a static_assert or the SecureStorage concept to guarantee a type
    // cannot leave secret bytes on the heap. Specialise IsSecure for your own
    // types the same way if they own secure storage.
    // ------------------------------------------------------------------

    // Trivially-copyable types have no separate heap allocation to scrub, so
    // they satisfy the trait; any other type must opt in via a specialisation.
    template<typename T>
    struct IsSecure : std::bool_constant<std::is_trivially_copyable_v<T>> {};

    template<typename T>
    inline constexpr bool IsSecureV = IsSecure<T>::value;

    template<typename T>
    concept SecureStorage = IsSecure<T>::value;

    // Secure containers: secure iff their element / key / value types are.
    template<typename C, typename Tr>
    struct IsSecure<std::basic_string<C, Tr, SecureAllocator<C>>> : IsSecure<C> {};
    template<typename T>
    struct IsSecure<std::vector<T, SecureAllocator<T>>> : IsSecure<T> {};
    template<typename T>
    struct IsSecure<std::deque<T, SecureAllocator<T>>> : IsSecure<T> {};
    template<typename T>
    struct IsSecure<std::list<T, SecureAllocator<T>>> : IsSecure<T> {};
    template<typename K, typename Cmp>
    struct IsSecure<std::set<K, Cmp, SecureAllocator<K>>> : IsSecure<K> {};
    template<typename K, typename V, typename Cmp>
    struct IsSecure<std::map<K, V, Cmp, SecureAllocator<std::pair<const K, V>>>>
        : std::bool_constant<IsSecure<K>::value && IsSecure<V>::value> {};
    template<typename K, typename H, typename E>
    struct IsSecure<std::unordered_set<K, H, E, SecureAllocator<K>>> : IsSecure<K> {};
    template<typename K, typename V, typename H, typename E>
    struct IsSecure<std::unordered_map<K, V, H, E, SecureAllocator<std::pair<const K, V>>>>
        : std::bool_constant<IsSecure<K>::value && IsSecure<V>::value> {};

    // Value composites hold their members inline: secure iff every member is.
    template<typename A, typename B>
    struct IsSecure<std::pair<A, B>> : std::bool_constant<IsSecure<A>::value && IsSecure<B>::value> {};
    template<typename... Ts>
    struct IsSecure<std::tuple<Ts...>> : std::bool_constant<(IsSecure<Ts>::value && ...)> {};
    template<typename T>
    struct IsSecure<std::optional<T>> : IsSecure<T> {};
    template<typename... Ts>
    struct IsSecure<std::variant<Ts...>> : std::bool_constant<(IsSecure<Ts>::value && ...)> {};

    // Branchless equality: time depends on the length, never on the contents.
    inline bool ConstantTimeEqual(const uint8_t* a, const uint8_t* b, size_t n) noexcept {
        uint8_t acc = 0;
        for (size_t i = 0; i < n; ++i) {
            acc |= static_cast<uint8_t>(a[i] ^ b[i]);
        }
        return acc == 0;
    }
    inline bool ConstantTimeEqual(std::span<const uint8_t> a, std::span<const uint8_t> b) noexcept {
        if (a.size() != b.size()) {
            return false; // the length itself is not secret
        }
        return ConstantTimeEqual(a.data(), b.data(), a.size());
    }

    // Decode straight into wiped storage, avoiding an unwiped Binary temporary.
    // Same fast path as the ordinary decoder: SIMD kernels always, and the
    // auto thread count (0) splits large inputs across threads exactly like
    // Decode*Parallel (small secrets stay single-threaded below the threshold).
#define SNICHOLLS_DEFINE_SECURE_SCHEME(Name, BitGroupSize, AlphabetSize, Alphabet, Padded) \
    template<ByteSource R> \
    SecureBytes Decode##Name##Secure(const R& input, unsigned threadCount = 0) { \
        return BaseDecodeParallelImpl<SecureBytes, BitGroupSize, AlphabetSize, Alphabet, Padded>( \
            reinterpret_cast<const char*>(std::ranges::data(input)), std::ranges::size(input), threadCount); \
    } \
    inline SecureBytes Decode##Name##Secure(const char* input, unsigned threadCount = 0) { \
        return Decode##Name##Secure(std::string_view{input}, threadCount); \
    }

    SNICHOLLS_FOR_EACH_SCHEME(SNICHOLLS_DEFINE_SECURE_SCHEME)

#undef SNICHOLLS_DEFINE_SECURE_SCHEME

#ifdef encode_decode_object_hpp
    // Secure object deserialisation (available when encode_decode_object.hpp is
    // included before this header): decode the serialised form into wiped
    // SecureBytes, then reconstruct - no plaintext Binary temporary. Reconstruct
    // into a Secure type (e.g. a SecureVector) for a fully-wiped result; see
    // IsSecure<T>. Uses the same fast SIMD/parallel decode path.
#define SNICHOLLS_DEFINE_OBJECT_SECURE(Name, BitGroupSize, AlphabetSize, Alphabet, Padded) \
    template<ObjectSerializable T, ByteSource R> \
    T Decode##Name##ObjectSecure(const R& input, unsigned threadCount = 0) { \
        return ObjectSerializer<T>::from_bytes(Decode##Name##Secure(input, threadCount)); \
    } \
    template<ObjectSerializable T> \
    T Decode##Name##ObjectSecure(const char* input, unsigned threadCount = 0) { \
        return Decode##Name##ObjectSecure<T>(std::string_view{input}, threadCount); \
    }

    SNICHOLLS_FOR_EACH_SCHEME(SNICHOLLS_DEFINE_OBJECT_SECURE)

#undef SNICHOLLS_DEFINE_OBJECT_SECURE
#endif /* encode_decode_object_hpp */
}

#endif /* encode_decode_secure_hpp */
