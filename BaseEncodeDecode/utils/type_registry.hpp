// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Saxon Nicholls

//
//  utils/type_registry.hpp
//  BaseEncodeDecode
//
//  Construct-and-dispatch by name, with NO std::variant, NO common base class,
//  and NO switch statement. Each type is bound to a compile-time string; a
//  runtime name is matched against those compile-time names with a
//  short-circuiting fold expression, and on the first match the type is
//  deserialised and handed to a generic callable.
//
//      #include "encode_decode_object.hpp"    // to (de)serialise your types
//      #include "utils/type_registry.hpp"
//
//      struct Object1 { ... }; struct Object2 { ... }; struct Object3 { ... };
//
//      using Registry = snicholls::TypeRegistry<
//          snicholls::Named<"object1", Object1>,
//          snicholls::Named<"object2", Object2>,
//          snicholls::Named<"object3", Object3>>;
//
//      // read { name, serialised } and dispatch the reconstructed object:
//      bool handled = Registry::dispatch(name, serialised, [](auto&& obj) {
//          // obj has its concrete type here - handle structurally (duck typing)
//          // or with `if constexpr (std::is_same_v<...>)`. No runtime switch.
//          process(obj);
//      });
//      if (!handled) { /* name was not a registered type */ }
//
//  For a per-type handler with no `if constexpr`, combine one lambda per type
//  with utils/overloaded.hpp (each registered type must be covered, or add a
//  generic `[](auto&&){}` catch-all):
//
//      Registry::dispatch(name, serialised, overloaded{
//          [](Object1&& o) { ... },
//          [](Object2&& o) { ... },
//          [](Object3&& o) { ... },
//      });
//
//  The reverse direction is just as direct: Registry::serialize(obj) returns
//  { name, Base64 } using the type's registered name (a compile-time reverse
//  lookup - referencing an unregistered type is a compile error).
//
//  Serialisation defaults to this library's Base64 object codec, but dispatch()
//  accepts a custom deserialiser (a callable (std::type_identity<T>, string) ->
//  T), so any format - JSON, your own bytes - drops in without a base class.
//

#ifndef encode_decode_type_registry_hpp
#define encode_decode_type_registry_hpp

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "../encode_decode_object.hpp"
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
    struct TypeRegistry {
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

        // Serialise an object to { registered name, Base64 }.
        template<typename T>
        static std::pair<std::string_view, std::string> serialize(const T& obj) {
            return { nameOf<T>(), EncodeBase64Object(obj) };
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

        // Same, using this library's Base64 object codec to deserialise.
        template<typename Handler>
        static bool dispatch(std::string_view name, std::string_view serialized, Handler&& handler) {
            return dispatch(name, serialized, std::forward<Handler>(handler),
                []<typename T>(std::type_identity<T>, std::string_view s) {
                    return DecodeBase64Object<T>(s);
                });
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

#endif /* encode_decode_type_registry_hpp */
