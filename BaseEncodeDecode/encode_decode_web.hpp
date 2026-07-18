// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Saxon Nicholls

//
//  encode_decode_web.hpp
//  BaseEncodeDecode
//
//  Optional drop-in adapter for two web chores that are Base64 underneath.
//  Include it explicitly:
//
//      #include "encode_decode_web.hpp"
//
//      // RFC 2397 data URIs:
//      std::string uri = MakeDataUri("image/png", pngBytes);
//      // -> "data:image/png;base64,iVBORw0KGgo..."
//      DataUri parsed = ParseDataUri(uri);   // .mediaType, .data
//
//      // RFC 7617 HTTP Basic authentication:
//      std::string header = BasicAuthHeader("Aladdin", "open sesame");
//      // -> "Basic QWxhZGRpbjpvcGVuIHNlc2FtZQ=="
//      BasicCredentials creds = ParseBasicAuthHeader(header);
//
//  ParseDataUri handles both ;base64 and plain payloads; plain payloads are
//  returned verbatim (no percent-decoding - run your URL decoder first if
//  the producer percent-encoded them).
//
//  Reminder: Basic auth is encoding, not encryption - only use it over TLS.
//

#ifndef encode_decode_web_hpp
#define encode_decode_web_hpp

#include "encode_decode_base_whatever.hpp"

namespace snicholls {

    // ---- RFC 2397 data URIs ----

    template<ByteSource R>
    std::string MakeDataUri(std::string_view mediaType, const R& data) {
        std::string uri = "data:";
        uri += mediaType.empty() ? std::string_view{"application/octet-stream"} : mediaType;
        uri += ";base64,";
        uri += EncodeBase64(data);
        return uri;
    }

    inline std::string MakeDataUri(std::string_view mediaType, const char* data) {
        return MakeDataUri(mediaType, std::string_view{data});
    }

    struct DataUri {
        std::string mediaType;
        bool base64 = false;
        Binary data;
    };

    inline DataUri ParseDataUri(std::string_view uri) {
        constexpr std::string_view prefix = "data:";
        if (!uri.starts_with(prefix)) {
            throw std::invalid_argument("not a data: URI");
        }
        const size_t comma = uri.find(',');
        if (comma == std::string_view::npos) {
            throw std::invalid_argument("data: URI has no ',' separator");
        }

        std::string_view header = uri.substr(prefix.size(), comma - prefix.size());
        const std::string_view payload = uri.substr(comma + 1);

        DataUri result;
        constexpr std::string_view base64Suffix = ";base64";
        if (header.ends_with(base64Suffix)) {
            result.base64 = true;
            header.remove_suffix(base64Suffix.size());
        }
        // RFC 2397: an omitted mediatype defaults to text/plain;charset=US-ASCII
        result.mediaType = header.empty() ? "text/plain;charset=US-ASCII" : std::string(header);
        if (result.base64) {
            result.data = DecodeBase64Binary(payload);
        } else {
            result.data.assign(payload.begin(), payload.end()); // verbatim; no percent-decoding
        }
        return result;
    }

    // ---- RFC 7617 HTTP Basic authentication ----

    inline std::string BasicAuthHeader(std::string_view user, std::string_view password) {
        if (user.find(':') != std::string_view::npos) {
            throw std::invalid_argument("Basic auth user-id must not contain ':' (RFC 7617)");
        }
        std::string credentials;
        credentials.reserve(user.size() + 1 + password.size());
        credentials += user;
        credentials += ':';
        credentials += password;
        return "Basic " + EncodeBase64(credentials);
    }

    struct BasicCredentials {
        std::string user;
        std::string password;
    };

    inline BasicCredentials ParseBasicAuthHeader(std::string_view header) {
        constexpr std::string_view scheme = "Basic ";
        // The scheme name is case-insensitive (RFC 7235)
        if (header.size() <= scheme.size()) {
            throw std::invalid_argument("not a Basic authentication header");
        }
        for (size_t i = 0; i < scheme.size(); ++i) {
            const char a = header[i];
            const char b = scheme[i];
            const char lowerA = (a >= 'A' && a <= 'Z') ? static_cast<char>(a - 'A' + 'a') : a;
            const char lowerB = (b >= 'A' && b <= 'Z') ? static_cast<char>(b - 'A' + 'a') : b;
            if (lowerA != lowerB) {
                throw std::invalid_argument("not a Basic authentication header");
            }
        }

        const std::string decoded = DecodeBase64(header.substr(scheme.size()));
        const size_t colon = decoded.find(':');
        if (colon == std::string::npos) {
            throw std::invalid_argument("Basic credentials must be user:password");
        }
        return {decoded.substr(0, colon), decoded.substr(colon + 1)};
    }
}

#endif /* encode_decode_web_hpp */
