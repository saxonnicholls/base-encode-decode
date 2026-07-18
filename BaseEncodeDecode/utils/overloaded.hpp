// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Saxon Nicholls

//
//  utils/overloaded.hpp
//  BaseEncodeDecode
//
//  The classic lambda-overload-set helper: combine several callables into one
//  whose operator() is the overload set of all of them. Overload resolution
//  picks the right one at compile time - a clean alternative to a switch or an
//  `if constexpr` chain, and the usual companion to std::visit.
//
//      auto handler = overloaded{
//          [](const Object1& o) { handleOne(o); },
//          [](const Object2& o) { handleTwo(o); },
//          [](const auto& o)    { handleDefault(o); },   // optional catch-all
//      };
//
//  Paired with utils/type_registry.hpp, this dispatches a reconstructed object
//  to a per-type handler with no variant, no base class, and no switch:
//
//      Registry::dispatch(name, serialised, overloaded{
//          [](Object1&& o) { ... },
//          [](Object2&& o) { ... },
//          [](Object3&& o) { ... },
//      });
//

#ifndef encode_decode_overloaded_hpp
#define encode_decode_overloaded_hpp

namespace snicholls {

    template<typename... Fs>
    struct overloaded : Fs... {
        using Fs::operator()...;
    };

    // CTAD guide (redundant under C++20 aggregate CTAD, kept for clarity/C++17).
    template<typename... Fs>
    overloaded(Fs...) -> overloaded<Fs...>;
}

#endif /* encode_decode_overloaded_hpp */
