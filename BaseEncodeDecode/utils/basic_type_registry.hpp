// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Saxon Nicholls

//
//  utils/basic_type_registry.hpp
//  BaseEncodeDecode
//
//  The codec-independent core of the type registry: construct-and-dispatch by
//  name with NO std::variant, NO common base class and NO switch statement.
//  Each type is bound to a compile-time string; a runtime name is matched
//  against those names with a short-circuiting fold expression, and on the
//  first match the payload is deserialised with a caller-supplied deserialiser
//  and handed to a generic callable.
//
//  Depends only on fixed_string.hpp - no encoder, no SIMD - so it can be used
//  with any format (JSON, protobuf, your own bytes). For the Base64 defaults
//  (Registry::serialize(obj), Registry::dispatch(name, base64, handler)) use
//  utils/type_registry.hpp, whose TypeRegistry extends this one.
//
//      #include "utils/basic_type_registry.hpp"
//
//      using Registry = snicholls::BasicTypeRegistry<
//          snicholls::Named<"object1", Object1>,
//          snicholls::Named<"object2", Object2>>;
//
//      bool handled = Registry::dispatch(name, payload,
//          [](auto&& obj) { process(obj); },                         // handler
//          []<typename T>(std::type_identity<T>, std::string_view s) { // deserialiser
//              return my_format::parse<T>(s);
//          });
//

#ifndef encode_decode_basic_type_registry_hpp
#define encode_decode_basic_type_registry_hpp

#include <array>
#include <cstddef>
#include <string_view>
#include <type_traits>

#include "fixed_string.hpp"

namespace snicholls {

    // Binds a compile-time name to a type: Named<"object1", Object1>.
    template<basic_fixed_string Name, typename T>
    struct Named {
        using type = T;
        static constexpr std::string_view name = Name.view();
    };

    // A closed set of (name, type) bindings, all known at compile time.
    template<typename... Bindings>
    struct BasicTypeRegistry {
        static constexpr size_t count = sizeof...(Bindings);
        static constexpr std::array<std::string_view, count> names{ Bindings::name... };

        // Is `name` one of the registered names? (runtime)
        static constexpr bool contains(std::string_view name) {
            return ((name == Bindings::name) || ...);
        }

        // The registered name for a type. Referencing an unregistered type is a
        // compile error (the consteval throw cannot be a constant expression).
        template<typename T>
        static consteval std::string_view nameOf() {
            std::string_view result{};
            const bool found = ((std::is_same_v<typename Bindings::type, T>
                                     ? (result = Bindings::name, true) : false) || ...);
            if (!found) {
                throw "TypeRegistry::nameOf<T>: type is not registered";
            }
            return result;
        }

        // Match `name`, deserialise the bound type from `serialized`, and invoke
        // handler(object). Returns true iff a registered type matched. The fold
        // short-circuits at the first match - no switch, no variant, no base
        // class. `deserialize` maps (std::type_identity<T>, string) -> T.
        template<typename Handler, typename Deserialize>
        static bool dispatch(std::string_view name, std::string_view serialized,
                             Handler&& handler, Deserialize&& deserialize) {
            return (tryOne<Bindings>(name, serialized, handler, deserialize) || ...);
        }

    private:
        template<typename Binding, typename Handler, typename Deserialize>
        static bool tryOne(std::string_view name, std::string_view serialized,
                           Handler& handler, Deserialize& deserialize) {
            if (name != Binding::name) {
                return false;
            }
            handler(deserialize(std::type_identity<typename Binding::type>{}, serialized));
            return true;
        }
    };
}

#endif /* encode_decode_basic_type_registry_hpp */
