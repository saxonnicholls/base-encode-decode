// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Saxon Nicholls

//
//  utils/fixed_string.hpp
//  BaseEncodeDecode
//
//  A compile-time fixed-capacity string usable as a non-type template parameter
//  (C++20). Modelled on std::basic_string / std::string_view conventions and on
//  https://github.com/unterumarmung/fixed_string, but written fresh to be small,
//  clean, and DEPENDENCY-FREE (only <string_view>, <cstddef>, <functional>).
//
//      constexpr fixed_string greeting = "hello";
//      static_assert(greeting.size() == 5);
//      static_assert(greeting == "hello");
//      static_assert((greeting + fixed_string(", world")) == "hello, world");
//
//      template<basic_fixed_string Name> struct Tag {};   // NTTP: Tag<"object1">
//
//  N is the logical length (excluding the trailing '\0'), matching std::string:
//  fixed_string("abc").size() == 3. Deduction from a string literal drops the
//  literal's terminator, so `fixed_string("abc")` is `basic_fixed_string<char,3>`.
//  The character storage is a public member because a non-type template
//  parameter type must be structural; treat it as read-only.
//

#ifndef encode_decode_fixed_string_hpp
#define encode_decode_fixed_string_hpp

#include <cstddef>
#include <functional>
#include <string_view>

namespace snicholls {

    template<typename CharT, std::size_t N>
    struct basic_fixed_string {
        using value_type = CharT;
        using size_type = std::size_t;
        using view_type = std::basic_string_view<CharT>;

        CharT elems[N + 1]{}; // N characters plus a trailing '\0'; public for NTTP use

        constexpr basic_fixed_string() = default;
        constexpr basic_fixed_string(const CharT (&literal)[N + 1]) {
            for (size_type i = 0; i <= N; ++i) {
                elems[i] = literal[i];
            }
        }

        // Capacity
        static constexpr size_type size() noexcept { return N; }
        static constexpr size_type length() noexcept { return N; }
        [[nodiscard]] static constexpr bool empty() noexcept { return N == 0; }

        // Element access (read-only)
        constexpr const CharT* data() const noexcept { return elems; }
        constexpr const CharT* c_str() const noexcept { return elems; }
        constexpr const CharT& operator[](size_type i) const noexcept { return elems[i]; }
        constexpr const CharT& front() const noexcept { return elems[0]; }
        constexpr const CharT& back() const noexcept { return elems[N - 1]; }

        // Iterators
        constexpr const CharT* begin() const noexcept { return elems; }
        constexpr const CharT* end() const noexcept { return elems + N; }
        constexpr const CharT* cbegin() const noexcept { return elems; }
        constexpr const CharT* cend() const noexcept { return elems + N; }

        // Views / conversion
        constexpr view_type view() const noexcept { return view_type(elems, N); }
        constexpr operator view_type() const noexcept { return view(); }

        // All comparisons go through string_view, so a fixed string compares
        // naturally against another fixed string of ANY length, a string_view,
        // a std::string, or a literal - with no ambiguity from the implicit
        // literal constructor. (No defaulted operator== is needed: a class with
        // a public character array is already a structural type, so it works as
        // a non-type template parameter regardless.)
        constexpr bool operator==(view_type other) const noexcept { return view() == other; }
        constexpr auto operator<=>(view_type other) const noexcept { return view() <=> other; }

        // Concatenation: fixed_string<N> + fixed_string<M> -> fixed_string<N+M>
        template<size_type M>
        constexpr basic_fixed_string<CharT, N + M>
        operator+(const basic_fixed_string<CharT, M>& rhs) const noexcept {
            basic_fixed_string<CharT, N + M> out;
            for (size_type i = 0; i < N; ++i) out.elems[i] = elems[i];
            for (size_type i = 0; i < M; ++i) out.elems[N + i] = rhs.elems[i];
            out.elems[N + M] = CharT{};
            return out;
        }

        // Compile-time substring
        template<size_type Pos, size_type Count>
        constexpr basic_fixed_string<CharT, Count> substr() const noexcept {
            static_assert(Pos + Count <= N, "basic_fixed_string::substr: range out of bounds");
            basic_fixed_string<CharT, Count> out;
            for (size_type i = 0; i < Count; ++i) out.elems[i] = elems[Pos + i];
            out.elems[Count] = CharT{};
            return out;
        }
    };

    // Deduce the length from a string literal, dropping its terminator.
    template<typename CharT, std::size_t N>
    basic_fixed_string(const CharT (&)[N]) -> basic_fixed_string<CharT, N - 1>;

    // Convenience aliases mirroring the standard character-type family.
    template<std::size_t N> using fixed_string = basic_fixed_string<char, N>;
    template<std::size_t N> using wfixed_string = basic_fixed_string<wchar_t, N>;
    template<std::size_t N> using u8fixed_string = basic_fixed_string<char8_t, N>;
    template<std::size_t N> using u16fixed_string = basic_fixed_string<char16_t, N>;
    template<std::size_t N> using u32fixed_string = basic_fixed_string<char32_t, N>;
}

// Hashing, so a fixed string can key an unordered container.
template<typename CharT, std::size_t N>
struct std::hash<snicholls::basic_fixed_string<CharT, N>> {
    constexpr std::size_t operator()(const snicholls::basic_fixed_string<CharT, N>& s) const noexcept {
        return std::hash<std::basic_string_view<CharT>>{}(s.view());
    }
};

#endif /* encode_decode_fixed_string_hpp */
