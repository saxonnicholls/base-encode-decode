// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Saxon Nicholls

//
//  utils/basic_type_registry.hpp
//  BaseEncodeDecode
//
//  DEPRECATED: the codec-free registry lives in snicholls-core, <snicholls/core/basic_type_registry.hpp>, as
//  snicholls::basic_type_registry and snicholls::named, with name_of<T>() for nameOf<T>(). This header forwards
//  to it and keeps the old names, so code that includes it keeps working; include core's header instead. It will
//  be removed in 2.0.0.
//
//      BasicTypeRegistry<Named<"a", A>>   ->  basic_type_registry<named<"a", A>>
//      Registry::nameOf<A>()              ->  Registry::name_of<A>()
//
//  For the Base64 defaults, utils/type_registry.hpp (TypeRegistry) is not deprecated.
//

#ifndef encode_decode_basic_type_registry_hpp
#define encode_decode_basic_type_registry_hpp

#include <snicholls/core/basic_type_registry.hpp>
#include <snicholls/core/fixed_string.hpp>

namespace snicholls {

#ifndef encode_decode_named_alias
#define encode_decode_named_alias
    // Binds a compile-time name to a type: Named<"object1", Object1>. The same as core's named.
    template<basic_fixed_string Name, typename T>
    using Named = named<Name, T>;
#endif

    // core's basic_type_registry under its old name, with nameOf<T>().
    template<typename... Bindings>
    struct BasicTypeRegistry : basic_type_registry<Bindings...> {
        template<typename T>
        static consteval std::string_view nameOf() {
            return basic_type_registry<Bindings...>::template name_of<T>();
        }
    };
}

#endif /* encode_decode_basic_type_registry_hpp */
