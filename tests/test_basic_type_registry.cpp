// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Saxon Nicholls

//
//  test_basic_type_registry.cpp
//  BaseEncodeDecode
//
//  The codec-free registry on its own: proves snicholls-core's basic_type_registry
//  compiles without the encoder (this translation unit must not see it) and
//  dispatches with a non-Base64, caller-supplied format. Also proves the headers
//  deprecated in 1.1.0 (utils/basic_type_registry.hpp, utils/fixed_string.hpp,
//  utils/overloaded.hpp) still compile and keep their old names.
//
//  Same style as test_main.cpp: no framework, just <cassert>.
//

#undef NDEBUG

#include <cassert>
#include <cstdio>
#include <string>
#include <string_view>
#include <type_traits>

#include <snicholls/core/basic_type_registry.hpp>
#include <snicholls/core/overloaded.hpp>

// The deprecated paths, kept until 2.0.0: each forwards to snicholls-core.
#include "utils/basic_type_registry.hpp"
#include "utils/fixed_string.hpp"
#include "utils/overloaded.hpp"

// The point of the split: the generic registry must not drag in the codec.
#ifdef SNICHOLLS_FOR_EACH_SCHEME
#error "utils/basic_type_registry.hpp or core's registry pulled in the encoder (encode_decode_base_whatever.hpp)"
#endif
#ifdef encode_decode_object_hpp
#error "the registry headers pulled in encode_decode_object.hpp"
#endif

using namespace snicholls;

namespace {
    struct Celsius { int value; };
    struct Label   { std::string text; };

    // A trivial text format: "42" for Celsius, the raw string for Label.
    struct TextFormat {
        Celsius operator()(std::type_identity<Celsius>, std::string_view s) const {
            return Celsius{ std::stoi(std::string(s)) };
        }
        Label operator()(std::type_identity<Label>, std::string_view s) const {
            return Label{ std::string(s) };
        }
    };
}

using Registry = basic_type_registry<
    named<"celsius", Celsius>,
    named<"label", Label>>;

static_assert(Registry::count == 2);
static_assert(Registry::names[0] == "celsius" && Registry::names[1] == "label");
static_assert(Registry::contains("label") && !Registry::contains("kelvin"));
static_assert(Registry::name_of<Celsius>() == "celsius");
static_assert(Registry::name_of<Label>() == "label");
static_assert(std::is_same_v<named<"x", int>::type, int> && named<"x", int>::name == "x");

// The deprecated names: BasicTypeRegistry, Named and nameOf are core's types under the old names.
using LegacyRegistry = BasicTypeRegistry<
    Named<"celsius", Celsius>,
    Named<"label", Label>>;

static_assert(std::is_same_v<Named<"x", int>, named<"x", int>>);
static_assert(std::is_base_of_v<Registry, LegacyRegistry>);
static_assert(LegacyRegistry::nameOf<Label>() == "label" && LegacyRegistry::name_of<Celsius>() == "celsius");
static_assert(fixed_string("ab") + fixed_string("c") == "abc");

int main() {
    // Dispatch through the caller's format, per-type handler via overloaded.
    int degrees = 0;
    std::string text;
    const bool handled = Registry::dispatch("celsius", "21",
        overloaded{
            [&](Celsius&& c) { degrees = c.value; },
            [&](Label&& l)   { text = std::move(l.text); },
        },
        TextFormat{});
    assert(handled && degrees == 21 && text.empty());

    assert(Registry::dispatch("label", "hello",
        overloaded{
            [&](Label&& l) { text = std::move(l.text); },
            [](auto&&) {},
        },
        TextFormat{}));
    assert(text == "hello");

    // The deprecated registry dispatches exactly as core's does.
    degrees = 0;
    assert(LegacyRegistry::dispatch("celsius", "7",
        overloaded{ [&](Celsius&& c) { degrees = c.value; }, [](auto&&) {} },
        TextFormat{}));
    assert(degrees == 7);

    // Unknown name: not handled, handler and deserialiser never called.
    bool called = false;
    assert(!Registry::dispatch("kelvin", "0",
        [&](auto&&) { called = true; },
        [&]<typename T>(std::type_identity<T>, std::string_view) -> T { called = true; return T{}; }));
    assert(!called);

    // Runtime contains() agrees with the compile-time names.
    for (const auto name : Registry::names) {
        assert(Registry::contains(name));
    }

    std::puts("Basic type registry (codec-free, custom format): OK");
    return 0;
}
