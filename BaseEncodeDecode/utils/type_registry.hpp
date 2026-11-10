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
//  with snicholls::overloaded from <snicholls/core/overloaded.hpp> (each registered type must be covered, or add a
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
//  TypeRegistry is snicholls-core's basic_type_registry
//  (<snicholls/core/basic_type_registry.hpp>: names, contains, name_of, dispatch
//  with a custom deserialiser - no codec) plus nameOf and the Base64 defaults
//  below. Include core's header directly when you do not want the encoder
//  pulled in. Named is core's named under this library's name.
//

#ifndef encode_decode_type_registry_hpp
#define encode_decode_type_registry_hpp

#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

#include "../encode_decode_object.hpp"
#include <snicholls/core/basic_type_registry.hpp>
#include <snicholls/core/fixed_string.hpp>

namespace snicholls {

#ifndef encode_decode_named_alias
#define encode_decode_named_alias
    // Binds a compile-time name to a type: Named<"object1", Object1>. The same as core's named.
    template<basic_fixed_string Name, typename T>
    using Named = named<Name, T>;
#endif

    // core's basic_type_registry plus this library's Base64 object codec as the
    // default serialiser / deserialiser.
    template<typename... Bindings>
    struct TypeRegistry : basic_type_registry<Bindings...> {
        using Base = basic_type_registry<Bindings...>;
        using Base::dispatch;

        // The registered name for a type; a compile error for an unregistered one.
        template<typename T>
        static consteval std::string_view nameOf() {
            return Base::template name_of<T>();
        }

        // Serialise an object to { registered name, Base64 }.
        template<typename T>
        static std::pair<std::string_view, std::string> serialize(const T& obj) {
            return { nameOf<T>(), EncodeBase64Object(obj) };
        }

        // Match `name`, deserialise the bound type from Base64 `serialized` with
        // this library's object codec, and invoke handler(object).
        template<typename Handler>
        static bool dispatch(std::string_view name, std::string_view serialized, Handler&& handler) {
            return Base::dispatch(name, serialized, std::forward<Handler>(handler),
                []<typename T>(std::type_identity<T>, std::string_view s) {
                    return DecodeBase64Object<T>(s);
                });
        }
    };
}

#endif /* encode_decode_type_registry_hpp */
