// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Saxon Nicholls

//
//  utils/stl_support.hpp
//  BaseEncodeDecode
//
//  Optional add-on to the object-serialisation adapter: ObjectSerializer<T>
//  specialisations for the rest of the standard library, so any nesting of
//  standard containers round-trips. Include it explicitly:
//
//      #include "utils/stl_support.hpp"
//
//      std::map<std::string, std::vector<int>> m = ...;
//      std::string s = EncodeBase64Object(m);
//      auto back = DecodeBase64Object<std::map<std::string, std::vector<int>>>(s);
//
//  Covers:
//    utility    - std::pair, std::tuple, std::optional, std::variant
//    sequence   - std::array, std::deque, std::list, std::forward_list
//                 (std::vector and std::basic_string live in the object header)
//    associative- std::set, std::multiset, std::map, std::multimap
//    unordered  - std::unordered_{set,multiset,map,multimap}
//    adaptors   - std::stack, std::queue, std::priority_queue
//
//  Everything composes recursively through each element's ObjectSerializer, and
//  a container of a non-serialisable type is itself not serialisable (a clean
//  compile error, never a silent byte-copy). Add your own container the same
//  way these are written.
//
//  Same-ABI / endianness caveats from encode_decode_object.hpp apply to the
//  trivially-copyable leaves; the container framing (counts, variant tags) is
//  portable.
//

#ifndef encode_decode_stl_support_hpp
#define encode_decode_stl_support_hpp

#include <array>
#include <cstdint>
#include <deque>
#include <forward_list>
#include <iterator>
#include <list>
#include <map>
#include <optional>
#include <queue>
#include <set>
#include <span>
#include <stack>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>

#include "../encode_decode_object.hpp"

namespace snicholls {

    // --- Utility types -------------------------------------------------------

    template<ObjectSerializable A, ObjectSerializable B>
        requires(!std::is_trivially_copyable_v<std::pair<A, B>>)
    struct ObjectSerializer<std::pair<A, B>, void> {
        static Binary to_bytes(const std::pair<A, B>& p) {
            Binary out;
            ByteWriter writer(out);
            writer.template element<A>(p.first);
            writer.template element<B>(p.second);
            return out;
        }
        static std::pair<A, B> from_bytes(std::span<const uint8_t> bytes) {
            ByteReader reader(bytes);
            A first = reader.template element<A>();
            B second = reader.template element<B>();
            if (!reader.empty()) {
                throw std::invalid_argument("ObjectSerializer<pair>: trailing bytes");
            }
            return std::pair<A, B>(std::move(first), std::move(second));
        }
    };

    template<ObjectSerializable... Ts>
        requires(!std::is_trivially_copyable_v<std::tuple<Ts...>>)
    struct ObjectSerializer<std::tuple<Ts...>, void> {
        static Binary to_bytes(const std::tuple<Ts...>& t) {
            Binary out;
            ByteWriter writer(out);
            std::apply([&](const auto&... xs) {
                (writer.template element<std::decay_t<decltype(xs)>>(xs), ...);
            }, t);
            return out;
        }
        static std::tuple<Ts...> from_bytes(std::span<const uint8_t> bytes) {
            ByteReader reader(bytes);
            // Braced-init guarantees left-to-right evaluation, matching write order.
            std::tuple<Ts...> out{reader.template element<Ts>()...};
            if (!reader.empty()) {
                throw std::invalid_argument("ObjectSerializer<tuple>: trailing bytes");
            }
            return out;
        }
    };

    template<ObjectSerializable T>
        requires(!std::is_trivially_copyable_v<std::optional<T>>)
    struct ObjectSerializer<std::optional<T>, void> {
        static Binary to_bytes(const std::optional<T>& o) {
            Binary out;
            ByteWriter writer(out);
            writer.varint(o.has_value() ? 1 : 0);
            if (o.has_value()) {
                writer.template element<T>(*o);
            }
            return out;
        }
        static std::optional<T> from_bytes(std::span<const uint8_t> bytes) {
            ByteReader reader(bytes);
            const uint64_t present = reader.varint();
            std::optional<T> out;
            if (present) {
                out.emplace(reader.template element<T>());
            }
            if (!reader.empty()) {
                throw std::invalid_argument("ObjectSerializer<optional>: trailing bytes");
            }
            return out;
        }
    };

    template<ObjectSerializable... Ts>
        requires(!std::is_trivially_copyable_v<std::variant<Ts...>>)
    struct ObjectSerializer<std::variant<Ts...>, void> {
        static Binary to_bytes(const std::variant<Ts...>& v) {
            Binary out;
            ByteWriter writer(out);
            writer.varint(v.index());
            std::visit([&](const auto& x) {
                writer.template element<std::decay_t<decltype(x)>>(x);
            }, v);
            return out;
        }
        static std::variant<Ts...> from_bytes(std::span<const uint8_t> bytes) {
            ByteReader reader(bytes);
            const uint64_t index = reader.varint();
            std::optional<std::variant<Ts...>> out = Build(index, reader, std::index_sequence_for<Ts...>{});
            if (!out) {
                throw std::invalid_argument("ObjectSerializer<variant>: index out of range");
            }
            if (!reader.empty()) {
                throw std::invalid_argument("ObjectSerializer<variant>: trailing bytes");
            }
            return std::move(*out);
        }
    private:
        template<size_t... Is>
        static std::optional<std::variant<Ts...>> Build(uint64_t index, ByteReader& reader, std::index_sequence<Is...>) {
            std::optional<std::variant<Ts...>> out;
            (void)((index == Is
                        ? (out.emplace(std::in_place_index<Is>, reader.template element<Ts>()), true)
                        : false) || ...);
            return out;
        }
    };

    // --- Sequence containers -------------------------------------------------

    template<ObjectSerializable T, size_t N>
        requires(!std::is_trivially_copyable_v<std::array<T, N>>)
    struct ObjectSerializer<std::array<T, N>, void> {
        static Binary to_bytes(const std::array<T, N>& a) {
            return detail::SerializeSized<T>(a, N);
        }
        static std::array<T, N> from_bytes(std::span<const uint8_t> bytes) {
            ByteReader reader(bytes);
            const uint64_t count = reader.varint();
            if (count != N) {
                throw std::invalid_argument("ObjectSerializer<array>: element count does not match N");
            }
            std::array<T, N> out{};
            for (size_t i = 0; i < N; ++i) {
                out[i] = reader.template element<T>();
            }
            if (!reader.empty()) {
                throw std::invalid_argument("ObjectSerializer<array>: trailing bytes");
            }
            return out;
        }
    };

    // Sequence containers reconstructed via push_back (std::deque, std::list).
#define SNICHOLLS_STL_SEQUENCE(Container) \
    template<ObjectSerializable T, typename... Rest> \
    struct ObjectSerializer<Container<T, Rest...>, void> { \
        static Binary to_bytes(const Container<T, Rest...>& c) { \
            return detail::SerializeSized<T>(c, c.size()); \
        } \
        static Container<T, Rest...> from_bytes(std::span<const uint8_t> bytes) { \
            ByteReader reader(bytes); \
            const uint64_t count = reader.varint(); \
            Container<T, Rest...> out; \
            for (uint64_t i = 0; i < count; ++i) { \
                out.push_back(reader.template element<T>()); \
            } \
            if (!reader.empty()) { \
                throw std::invalid_argument("ObjectSerializer: trailing bytes"); \
            } \
            return out; \
        } \
    };

    SNICHOLLS_STL_SEQUENCE(std::deque)
    SNICHOLLS_STL_SEQUENCE(std::list)
#undef SNICHOLLS_STL_SEQUENCE

    template<ObjectSerializable T, typename... Rest>
    struct ObjectSerializer<std::forward_list<T, Rest...>, void> {
        static Binary to_bytes(const std::forward_list<T, Rest...>& c) {
            const size_t count = static_cast<size_t>(std::distance(c.begin(), c.end()));
            return detail::SerializeSized<T>(c, count);
        }
        static std::forward_list<T, Rest...> from_bytes(std::span<const uint8_t> bytes) {
            ByteReader reader(bytes);
            const uint64_t count = reader.varint();
            std::forward_list<T, Rest...> out;
            auto pos = out.before_begin();
            for (uint64_t i = 0; i < count; ++i) {
                pos = out.insert_after(pos, reader.template element<T>());
            }
            if (!reader.empty()) {
                throw std::invalid_argument("ObjectSerializer<forward_list>: trailing bytes");
            }
            return out;
        }
    };

    // --- Associative and unordered containers --------------------------------

    // set-like: one key per entry, reconstructed via insert. `Rest` absorbs the
    // comparator/hash/allocator so ordered and unordered variants share code.
#define SNICHOLLS_STL_SET(Container) \
    template<ObjectSerializable Key, typename... Rest> \
    struct ObjectSerializer<Container<Key, Rest...>, void> { \
        static Binary to_bytes(const Container<Key, Rest...>& s) { \
            return detail::SerializeSized<Key>(s, s.size()); \
        } \
        static Container<Key, Rest...> from_bytes(std::span<const uint8_t> bytes) { \
            ByteReader reader(bytes); \
            const uint64_t count = reader.varint(); \
            Container<Key, Rest...> out; \
            for (uint64_t i = 0; i < count; ++i) { \
                out.insert(reader.template element<Key>()); \
            } \
            if (!reader.empty()) { \
                throw std::invalid_argument("ObjectSerializer: trailing bytes"); \
            } \
            return out; \
        } \
    };

    SNICHOLLS_STL_SET(std::set)
    SNICHOLLS_STL_SET(std::multiset)
    SNICHOLLS_STL_SET(std::unordered_set)
    SNICHOLLS_STL_SET(std::unordered_multiset)
#undef SNICHOLLS_STL_SET

    // map-like: key + mapped value per entry, reconstructed via emplace.
#define SNICHOLLS_STL_MAP(Container) \
    template<ObjectSerializable Key, ObjectSerializable Val, typename... Rest> \
    struct ObjectSerializer<Container<Key, Val, Rest...>, void> { \
        static Binary to_bytes(const Container<Key, Val, Rest...>& m) { \
            Binary out; \
            ByteWriter writer(out); \
            writer.varint(m.size()); \
            for (const auto& kv : m) { \
                writer.template element<Key>(kv.first); \
                writer.template element<Val>(kv.second); \
            } \
            return out; \
        } \
        static Container<Key, Val, Rest...> from_bytes(std::span<const uint8_t> bytes) { \
            ByteReader reader(bytes); \
            const uint64_t count = reader.varint(); \
            Container<Key, Val, Rest...> out; \
            for (uint64_t i = 0; i < count; ++i) { \
                Key key = reader.template element<Key>(); \
                Val val = reader.template element<Val>(); \
                out.emplace(std::move(key), std::move(val)); \
            } \
            if (!reader.empty()) { \
                throw std::invalid_argument("ObjectSerializer: trailing bytes"); \
            } \
            return out; \
        } \
    };

    SNICHOLLS_STL_MAP(std::map)
    SNICHOLLS_STL_MAP(std::multimap)
    SNICHOLLS_STL_MAP(std::unordered_map)
    SNICHOLLS_STL_MAP(std::unordered_multimap)
#undef SNICHOLLS_STL_MAP

    // --- Container adaptors --------------------------------------------------

    namespace detail {
        // Read the protected underlying container of a stack/queue/priority_queue.
        template<typename Adaptor>
        const typename Adaptor::container_type& AdaptorContainer(const Adaptor& a) noexcept {
            struct Access : Adaptor {
                static const typename Adaptor::container_type& get(const Adaptor& x) noexcept {
                    return x.*&Access::c;
                }
            };
            return Access::get(a);
        }
    } // namespace detail

    template<typename T, typename Container>
        requires ObjectSerializable<Container>
    struct ObjectSerializer<std::stack<T, Container>, void> {
        static Binary to_bytes(const std::stack<T, Container>& s) {
            return ObjectSerializer<Container>::to_bytes(detail::AdaptorContainer(s));
        }
        static std::stack<T, Container> from_bytes(std::span<const uint8_t> bytes) {
            return std::stack<T, Container>(ObjectSerializer<Container>::from_bytes(bytes));
        }
    };

    template<typename T, typename Container>
        requires ObjectSerializable<Container>
    struct ObjectSerializer<std::queue<T, Container>, void> {
        static Binary to_bytes(const std::queue<T, Container>& q) {
            return ObjectSerializer<Container>::to_bytes(detail::AdaptorContainer(q));
        }
        static std::queue<T, Container> from_bytes(std::span<const uint8_t> bytes) {
            return std::queue<T, Container>(ObjectSerializer<Container>::from_bytes(bytes));
        }
    };

    template<typename T, typename Container, typename Compare>
        requires ObjectSerializable<Container>
    struct ObjectSerializer<std::priority_queue<T, Container, Compare>, void> {
        static Binary to_bytes(const std::priority_queue<T, Container, Compare>& q) {
            return ObjectSerializer<Container>::to_bytes(detail::AdaptorContainer(q));
        }
        static std::priority_queue<T, Container, Compare> from_bytes(std::span<const uint8_t> bytes) {
            Container c = ObjectSerializer<Container>::from_bytes(bytes);
            return std::priority_queue<T, Container, Compare>(Compare{}, std::move(c));
        }
    };
}

#endif /* encode_decode_stl_support_hpp */
