//
//  encode_decode_object.hpp
//  BaseEncodeDecode
//
//  Optional drop-in adapter to serialise a whole object to a base-N string and
//  back. Include it explicitly:
//
//      #include "encode_decode_object.hpp"
//
//      struct Vec3 { float x, y, z; };
//      std::string s = EncodeBase64Object(Vec3{1, 2, 3});   // object -> string
//      Vec3 v = DecodeBase64Object<Vec3>(s);                // string -> object
//
//  How a type becomes serialisable
//  -------------------------------
//  Serialisation goes through one customization point, the trait
//  ObjectSerializer<T>, which supplies:
//
//      static Binary to_bytes(const T&);
//      static T      from_bytes(std::span<const uint8_t>);
//
//  Handled out of the box:
//   * Trivially-copyable types (scalars, enums, PODs, std::array of them) -
//     AUTOMATIC: their object representation is their bytes, via std::bit_cast
//     (no reinterpret_cast, no UB). A same-ABI memory snapshot (see SCOPE).
//   * std::basic_string (std::string, std::u16string, ...) and std::vector<T>
//     of any serialisable T - built-in specialisations below.
//   * The rest of the STL (map, set, list, deque, tuple, optional, variant,
//     stack, queue, ...) - include "utils/stl_support.hpp".
//
//  Composability: containers serialise recursively through the element's own
//  ObjectSerializer, so std::vector<std::string>, std::map<int, std::vector<T>>
//  and so on just work. A type that is neither trivially copyable nor has a
//  specialisation does NOT satisfy the ObjectSerializable concept, so
//  Encode*Object / Decode*Object are simply not callable for it (and neither is
//  any container of it) - a clean "constraints not satisfied" error, never a
//  silent byte-copy of pointers. You cannot misuse it by accident.
//
//  Adding your own type (a polymorphic hierarchy works the same way - serialise
//  its logical state, not its vtable):
//
//      struct Person { std::string name; uint32_t age; };
//      namespace snicholls {
//          template<> struct ObjectSerializer<Person> {
//              static Binary to_bytes(const Person& p) {
//                  Binary out; ByteWriter w{out};
//                  w.element<std::string>(p.name);
//                  w.element<uint32_t>(p.age);
//                  return out;
//              }
//              static Person from_bytes(std::span<const uint8_t> b) {
//                  ByteReader r{b};
//                  Person p;
//                  p.name = r.element<std::string>();
//                  p.age  = r.element<uint32_t>();
//                  return p;
//              }
//          };
//      }
//      // now Person - and std::vector<Person>, std::map<int,Person>, ... - work.
//
//  SCOPE: the trivially-copyable path copies bytes verbatim, so it is same-ABI,
//  not a portable wire format - not safe across architectures (endianness,
//  struct padding, type sizes). std::string's bytes are its characters, so that
//  one IS portable, and the container framing (varint counts/lengths) is
//  portable; only trivially-copyable leaves carry native byte order. If you
//  transmit object snapshots between machines and want a compile-time guarantee
//  the byte order agrees, define SNICHOLLS_REQUIRE_LITTLE_ENDIAN before
//  including this header (a big-endian build then fails to compile; mixed-endian
//  is always rejected). For a fully portable format, hand-write to_bytes /
//  from_bytes that fixes the byte order, or route through CBOR via the json
//  adapter: EncodeBase64(nlohmann::json(obj).to_cbor()).
//

#ifndef encode_decode_object_hpp
#define encode_decode_object_hpp

#include <array>
#include <bit>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "encode_decode_base_whatever.hpp"

namespace snicholls {

    // --- Endianness safety ---------------------------------------------------
    static_assert(std::endian::native == std::endian::little ||
                  std::endian::native == std::endian::big,
                  "encode_decode_object: mixed-endian platforms are not supported");

    inline constexpr bool NativeLittleEndian = (std::endian::native == std::endian::little);

#ifdef SNICHOLLS_REQUIRE_LITTLE_ENDIAN
    static_assert(NativeLittleEndian,
                  "SNICHOLLS_REQUIRE_LITTLE_ENDIAN is set but this build is big-endian: "
                  "object byte-snapshots would be incompatible with little-endian peers");
#endif

    // A type whose object representation is exactly its bytes.
    template<typename T>
    concept TriviallyCopyable = std::is_trivially_copyable_v<T>;

    // --- The customization point ---------------------------------------------
    // Primary template has no members: a type with no applicable specialisation
    // does not satisfy ObjectSerializable and cannot be serialised.
    template<typename T, typename = void>
    struct ObjectSerializer {};

    // Default: trivially-copyable types -> their raw bytes (same-ABI snapshot).
    template<TriviallyCopyable T>
    struct ObjectSerializer<T, void> {
        static constexpr Binary to_bytes(const T& obj) {
            const auto raw = std::bit_cast<std::array<std::byte, sizeof(T)>>(obj);
            Binary out(sizeof(T));
            for (size_t i = 0; i < sizeof(T); ++i) {
                out[i] = static_cast<uint8_t>(raw[i]);
            }
            return out;
        }
        static constexpr T from_bytes(std::span<const uint8_t> bytes) {
            if (bytes.size() != sizeof(T)) {
                throw std::invalid_argument("ObjectSerializer<T>: byte count does not match sizeof(T)");
            }
            std::array<std::byte, sizeof(T)> raw{};
            for (size_t i = 0; i < sizeof(T); ++i) {
                raw[i] = static_cast<std::byte>(bytes[i]);
            }
            return std::bit_cast<T>(raw);
        }
    };

    // Built-in: std::basic_string<CharT> <-> its character bytes.
    template<typename CharT, typename Traits, typename Alloc>
    struct ObjectSerializer<std::basic_string<CharT, Traits, Alloc>, void> {
        static_assert(std::is_trivially_copyable_v<CharT>);
        using String = std::basic_string<CharT, Traits, Alloc>;
        static Binary to_bytes(const String& s) {
            Binary out(s.size() * sizeof(CharT));
            for (size_t i = 0; i < s.size(); ++i) {
                const auto cb = std::bit_cast<std::array<std::byte, sizeof(CharT)>>(s[i]);
                for (size_t j = 0; j < sizeof(CharT); ++j) {
                    out[i * sizeof(CharT) + j] = static_cast<uint8_t>(cb[j]);
                }
            }
            return out;
        }
        static String from_bytes(std::span<const uint8_t> bytes) {
            if (bytes.size() % sizeof(CharT) != 0) {
                throw std::invalid_argument("ObjectSerializer<basic_string>: byte count not a multiple of sizeof(CharT)");
            }
            String out;
            out.resize(bytes.size() / sizeof(CharT));
            for (size_t i = 0; i < out.size(); ++i) {
                std::array<std::byte, sizeof(CharT)> cb{};
                for (size_t j = 0; j < sizeof(CharT); ++j) {
                    cb[j] = static_cast<std::byte>(bytes[i * sizeof(CharT) + j]);
                }
                out[i] = std::bit_cast<CharT>(cb);
            }
            return out;
        }
    };

    // A type is serialisable iff ObjectSerializer<T> provides the two hooks.
    template<typename T>
    concept ObjectSerializable = requires(const T& obj, std::span<const uint8_t> bytes) {
        { ObjectSerializer<T>::to_bytes(obj) } -> std::same_as<Binary>;
        { ObjectSerializer<T>::from_bytes(bytes) } -> std::same_as<T>;
    };

    // --- Framing: cursor-style writer/reader used by container serialisers ----
    // Counts and variable-length elements are framed with unsigned LEB128
    // varints (portable). A trivially-copyable element is fixed size, so it
    // carries no length prefix; a variable-length element is length-prefixed.

    namespace detail {
        inline void PutVarint(Binary& out, uint64_t value) {
            while (value >= 0x80) {
                out.push_back(static_cast<uint8_t>(value) | 0x80u);
                value >>= 7;
            }
            out.push_back(static_cast<uint8_t>(value));
        }
    } // namespace detail

    class ByteWriter {
        Binary& out_;
    public:
        explicit ByteWriter(Binary& out) noexcept : out_(out) {}

        void varint(uint64_t value) { detail::PutVarint(out_, value); }
        void raw(std::span<const uint8_t> bytes) { out_.insert(out_.end(), bytes.begin(), bytes.end()); }

        // Append one serialisable element (length-prefixed unless fixed size).
        template<ObjectSerializable E>
        void element(const E& value) {
            const Binary bytes = ObjectSerializer<E>::to_bytes(value);
            if constexpr (!TriviallyCopyable<E>) {
                varint(bytes.size());
            }
            raw(bytes);
        }
    };

    class ByteReader {
        std::span<const uint8_t> in_;
    public:
        explicit ByteReader(std::span<const uint8_t> in) noexcept : in_(in) {}

        bool empty() const noexcept { return in_.empty(); }
        size_t remaining() const noexcept { return in_.size(); }

        uint64_t varint() {
            uint64_t value = 0;
            int shift = 0;
            while (true) {
                if (in_.empty()) {
                    throw std::invalid_argument("ByteReader: truncated varint");
                }
                const uint8_t byte = in_.front();
                in_ = in_.subspan(1);
                value |= static_cast<uint64_t>(byte & 0x7F) << shift;
                if ((byte & 0x80) == 0) {
                    break;
                }
                shift += 7;
                if (shift >= 64) {
                    throw std::invalid_argument("ByteReader: varint too long");
                }
            }
            return value;
        }

        std::span<const uint8_t> take(size_t count) {
            if (count > in_.size()) {
                throw std::invalid_argument("ByteReader: truncated input");
            }
            const auto slice = in_.subspan(0, count);
            in_ = in_.subspan(count);
            return slice;
        }

        // Read one serialisable element written by ByteWriter::element.
        template<ObjectSerializable E>
        E element() {
            size_t count;
            if constexpr (TriviallyCopyable<E>) {
                count = sizeof(E);
            } else {
                count = static_cast<size_t>(varint());
            }
            return ObjectSerializer<E>::from_bytes(take(count));
        }
    };

    namespace detail {
        // size + each element, for any forward range of serialisable elements
        template<ObjectSerializable E, typename Range>
        Binary SerializeSized(const Range& range, size_t count) {
            Binary out;
            ByteWriter writer(out);
            writer.varint(count);
            for (const auto& element : range) {
                writer.template element<E>(element);
            }
            return out;
        }
    } // namespace detail

    // Built-in: std::vector<T> <-> count + elements (tightly packed for
    // trivially-copyable T, length-prefixed per element otherwise).
    template<ObjectSerializable T, typename Alloc>
    struct ObjectSerializer<std::vector<T, Alloc>, void> {
        static Binary to_bytes(const std::vector<T, Alloc>& v) {
            return detail::SerializeSized<T>(v, v.size());
        }
        static std::vector<T, Alloc> from_bytes(std::span<const uint8_t> bytes) {
            ByteReader reader(bytes);
            const uint64_t count = reader.varint();
            std::vector<T, Alloc> out;
            out.reserve(static_cast<size_t>(count));
            for (uint64_t i = 0; i < count; ++i) {
                out.push_back(reader.template element<T>());
            }
            if (!reader.empty()) {
                throw std::invalid_argument("ObjectSerializer<vector>: trailing bytes");
            }
            return out;
        }
    };

    // Object <-> bytes, the shared entry the per-scheme wrappers build on.
    template<ObjectSerializable T>
    constexpr Binary ToBytes(const T& obj) {
        return ObjectSerializer<T>::to_bytes(obj);
    }
    template<ObjectSerializable T>
    constexpr T FromBytes(std::span<const uint8_t> bytes) {
        return ObjectSerializer<T>::from_bytes(bytes);
    }

    // Per-scheme object serialisation, generated from the one scheme table.
#define SNICHOLLS_DEFINE_OBJECT_SCHEME(Name, BitGroupSize, AlphabetSize, Alphabet, Padded) \
    template<ObjectSerializable T> \
    constexpr std::string Encode##Name##Object(const T& obj) { \
        return Encode##Name(ObjectSerializer<T>::to_bytes(obj)); \
    } \
    template<ObjectSerializable T> \
    constexpr T Decode##Name##Object(std::string_view encoded) { \
        return ObjectSerializer<T>::from_bytes(Decode##Name##Binary(encoded)); \
    }

    SNICHOLLS_FOR_EACH_SCHEME(SNICHOLLS_DEFINE_OBJECT_SCHEME)

#undef SNICHOLLS_DEFINE_OBJECT_SCHEME
}

#endif /* encode_decode_object_hpp */
