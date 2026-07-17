//
//  encode_decode_format.hpp
//  BaseEncodeDecode
//
//  Optional drop-in adapter for std::format (C++20). Include it explicitly:
//
//      #include "encode_decode_format.hpp"
//
//      Binary blob = ...;
//      std::format("{}", Encoded(blob));        // Base64 (default)
//      std::format("{:b64u}", Encoded(blob));   // Base64Url, padded
//      std::format("{:b64un}", Encoded(blob));  // Base64Url, no padding (JWT)
//      std::format("{:b16}", Encoded(blob));    // hex ("hex" also accepted)
//      std::format("{:b32}", Encoded(blob));    // Base32
//      std::format("{:b2}", Encoded(blob));     // binary digits
//
//  Encoded is a cheap non-owning view over any ByteSource (std::string,
//  vector, span, ...) or a NUL-terminated string. A wrapper type is used
//  instead of specializing std::formatter for Binary directly because
//  Binary is std::vector<uint8_t>, and specializing std library templates
//  purely for std library types is not permitted (and would collide with
//  C++23's range formatters).
//
//  Unknown format specs are rejected at compile time when the format string
//  is a literal. For schemes without a spec here (Base8, Base36, ...), call
//  the Encode* functions directly.
//

#ifndef encode_decode_format_hpp
#define encode_decode_format_hpp

#include <version>

#if !defined(__cpp_lib_format)
#error "encode_decode_format.hpp requires std::format (C++20 with a complete standard library)"
#endif

#include <format>
#include <span>

#include "encode_decode_base_whatever.hpp"

namespace snicholls {

    // Lightweight non-owning view adapting any byte source for std::format
    struct Encoded {
        const uint8_t* data = nullptr;
        size_t size = 0;

        template<ByteSource R>
        Encoded(const R& source)
            : data(reinterpret_cast<const uint8_t*>(std::ranges::data(source))),
              size(std::ranges::size(source)) {}

        Encoded(const char* text)
            : data(reinterpret_cast<const uint8_t*>(text)),
              size(std::char_traits<char>::length(text)) {}
    };

} // namespace snicholls

template<>
struct std::formatter<snicholls::Encoded, char> {
    enum class Scheme { Base64, Base64Url, Base64UrlNoPad, Base32, Base16, Base2 };
    Scheme scheme = Scheme::Base64;

    constexpr auto parse(std::format_parse_context& ctx) {
        auto it = ctx.begin();
        const auto end = ctx.end();
        const auto start = it;
        while (it != end && *it != '}') {
            ++it;
        }
        const std::string_view token(start == it ? "" : std::string_view(&*start, static_cast<size_t>(it - start)));

        if (token.empty() || token == "b64") {
            scheme = Scheme::Base64;
        } else if (token == "b64u") {
            scheme = Scheme::Base64Url;
        } else if (token == "b64un") {
            scheme = Scheme::Base64UrlNoPad;
        } else if (token == "b32") {
            scheme = Scheme::Base32;
        } else if (token == "b16" || token == "hex") {
            scheme = Scheme::Base16;
        } else if (token == "b2") {
            scheme = Scheme::Base2;
        } else {
            throw std::format_error("Encoded: unknown spec (use b64, b64u, b64un, b32, b16/hex, or b2)");
        }
        return it;
    }

    template<typename FormatContext>
    auto format(const snicholls::Encoded& value, FormatContext& ctx) const {
        const std::span<const uint8_t> bytes(value.data, value.size);
        std::string encoded;
        switch (scheme) {
            case Scheme::Base64:         encoded = snicholls::EncodeBase64(bytes); break;
            case Scheme::Base64Url:      encoded = snicholls::EncodeBase64Url(bytes); break;
            case Scheme::Base64UrlNoPad: encoded = snicholls::EncodeBase64UrlNoPad(bytes); break;
            case Scheme::Base32:         encoded = snicholls::EncodeBase32(bytes); break;
            case Scheme::Base16:         encoded = snicholls::EncodeBase16(bytes); break;
            case Scheme::Base2:          encoded = snicholls::EncodeBase2(bytes); break;
        }
        return std::ranges::copy(encoded, ctx.out()).out;
    }
};

#endif /* encode_decode_format_hpp */
