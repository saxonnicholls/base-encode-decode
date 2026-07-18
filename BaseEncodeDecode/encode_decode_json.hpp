// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Saxon Nicholls

//
//  encode_decode_json.hpp
//  BaseEncodeDecode
//
//  Optional nlohmann::json adapter - include it explicitly, and only if you
//  want this behavior program-wide:
//
//      #include "encode_decode_json.hpp"
//
//      Binary blob = ...;
//      nlohmann::json j;
//      j["payload"] = blob;                       // -> "SGVsbG8sIFdvcmxkIQ=="
//      Binary back = j["payload"].get<Binary>();  // decoded
//
//  It specializes nlohmann::adl_serializer<Binary> so that Binary
//  (std::vector<uint8_t>) serializes as an RFC 4648 Base64 string instead of
//  nlohmann's default array of numbers. This also applies inside types using
//  NLOHMANN_DEFINE_TYPE_INTRUSIVE / _NON_INTRUSIVE.
//
//  Reading is tolerant: a Base64 string (padded or not), nlohmann's default
//  array-of-numbers, and the binary subtype (CBOR/MessagePack/BSON) all
//  deserialize into Binary.
//
//  ODR note: include this header in every translation unit that converts
//  Binary to or from json (e.g. from a common project header).
//

#ifndef encode_decode_json_hpp
#define encode_decode_json_hpp

#if !__has_include(<nlohmann/json.hpp>)
#error "encode_decode_json.hpp is an optional adapter and requires nlohmann/json.hpp on the include path"
#endif

#include <nlohmann/json.hpp>

#include "encode_decode_base_whatever.hpp"

namespace nlohmann {

    template<>
    struct adl_serializer<Binary> {
        static void to_json(json& j, const Binary& data) {
            j = snicholls::EncodeBase64Binary(data);
        }

        static void from_json(const json& j, Binary& data) {
            if (j.is_string()) {
                data = snicholls::DecodeBase64Binary(j.get_ref<const json::string_t&>());
            } else if (j.is_binary()) {
                const auto& bytes = j.get_binary();
                data.assign(bytes.begin(), bytes.end());
            } else if (j.is_array()) {
                // nlohmann's default representation for vector<uint8_t>
                data.clear();
                data.reserve(j.size());
                for (const auto& element : j) {
                    data.push_back(element.template get<uint8_t>());
                }
            } else {
                throw json::type_error::create(302, "expected a Base64 string, array, or binary value", &j);
            }
        }
    };

} // namespace nlohmann

// ---------------------------------------------------------------------------
// A whole JSON document as an ObjectSerializable value.
//
// When the object-serialisation layer is also present - include
// "encode_decode_object.hpp" BEFORE this header - a nlohmann::json document
// becomes a first-class citizen of the object pipeline. It serialises through
// CBOR (nlohmann's compact, endian-defined binary form), so a json document
// rides *everything* the library already offers with no bespoke code path:
//
//      nlohmann::json cfg = ...;
//      std::string s = snicholls::EncodeBase64UrlObject(cfg);   // JSON in a URL
//      auto back     = snicholls::DecodeBase64UrlObject<nlohmann::json>(s);
//      PutObjectEncrypted(kv, "config", cfg, cipher);           // encrypted JSON
//
// Because CBOR fixes the byte order, this route is portable across machines -
// unlike the host-endian trivially-copyable snapshots the primary template makes.
// ---------------------------------------------------------------------------
#ifdef encode_decode_object_hpp
namespace snicholls {

    template<>
    struct ObjectSerializer<nlohmann::json> {
        static Binary to_bytes(const nlohmann::json& j) {
            return nlohmann::json::to_cbor(j);
        }
        static nlohmann::json from_bytes(std::span<const uint8_t> bytes) {
            return nlohmann::json::from_cbor(bytes.begin(), bytes.end());
        }
    };

} // namespace snicholls
#endif // encode_decode_object_hpp

#endif /* encode_decode_json_hpp */
