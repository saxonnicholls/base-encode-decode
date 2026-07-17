//
//  encode_decode_simd_intel.hpp
//  BaseEncodeDecode
//
//  x86/x86-64 SIMD drop-in (SSSE3), included automatically by
//  encode_decode_base_whatever.hpp. Define SNICHOLLS_NO_SIMD to opt out.
//
//  Accelerates Base64, Base64Url and Base16. All other schemes use the
//  scalar path. Kernels process whole vector groups and hand tails - and
//  anything after a suspect character - back to the scalar code, which is
//  the single source of truth for validation and error reporting.
//
//  The 6-bit field extraction follows Mula & Lemire, "Faster Base64 Encoding
//  and Decoding Using AVX2 Instructions" (the SSE variant); the alphabet
//  mapping uses straightforward range comparisons so the standard and URL
//  alphabets share one implementation.
//

#ifndef encode_decode_simd_intel_hpp
#define encode_decode_simd_intel_hpp

#ifndef encode_decode_base_whatever_hpp
#error "Include encode_decode_base_whatever.hpp instead of this header directly"
#endif

#if (defined(__x86_64__) || defined(__i386__)) && defined(__SSSE3__)
#define SNICHOLLS_SIMD_INTEL 1

#include <immintrin.h>

namespace snicholls {
    namespace detail {

        // ---- Base64 family: 12 input bytes -> 16 characters per vector ----
        template<const std::array<char, 64>& Alphabet>
        struct SseBase64 {
            static constexpr bool Available = true;
            static constexpr char C62 = Alphabet[62]; // '+' or '-'
            static constexpr char C63 = Alphabet[63]; // '/' or '_'

            static void Encode(const uint8_t*& first, const uint8_t* last, char*& out) {
                while (last - first >= 16) { // reads 16 bytes, consumes 12
                    __m128i in = _mm_loadu_si128(reinterpret_cast<const __m128i*>(first));
                    // Arrange each 3-byte group as [b1 b0 b2 b1] per 32-bit lane,
                    // then split out the four 6-bit fields with two multiplies
                    in = _mm_shuffle_epi8(in, _mm_setr_epi8(1, 0, 2, 1, 4, 3, 5, 4, 7, 6, 8, 7, 10, 9, 11, 10));
                    const __m128i t0 = _mm_and_si128(in, _mm_set1_epi32(0x0fc0fc00));
                    const __m128i t1 = _mm_mulhi_epu16(t0, _mm_set1_epi32(0x04000040));
                    const __m128i t2 = _mm_and_si128(in, _mm_set1_epi32(0x003f03f0));
                    const __m128i t3 = _mm_mullo_epi16(t2, _mm_set1_epi32(0x01000010));
                    const __m128i field = _mm_or_si128(t1, t3); // one 0..63 value per byte

                    // Map 0..63 to ASCII with cumulative per-range offsets
                    const __m128i ge26 = _mm_cmpgt_epi8(field, _mm_set1_epi8(25));
                    const __m128i ge52 = _mm_cmpgt_epi8(field, _mm_set1_epi8(51));
                    const __m128i eq62 = _mm_cmpeq_epi8(field, _mm_set1_epi8(62));
                    const __m128i eq63 = _mm_cmpeq_epi8(field, _mm_set1_epi8(63));
                    __m128i ascii = _mm_add_epi8(field, _mm_set1_epi8('A'));
                    ascii = _mm_add_epi8(ascii, _mm_and_si128(ge26, _mm_set1_epi8('a' - 26 - 'A')));
                    ascii = _mm_add_epi8(ascii, _mm_and_si128(ge52, _mm_set1_epi8('0' - 52 - ('a' - 26))));
                    ascii = _mm_add_epi8(ascii, _mm_and_si128(eq62, _mm_set1_epi8(C62 - 62 - ('0' - 52))));
                    ascii = _mm_add_epi8(ascii, _mm_and_si128(eq63, _mm_set1_epi8(C63 - 63 - ('0' - 52))));

                    _mm_storeu_si128(reinterpret_cast<__m128i*>(out), ascii);
                    first += 12;
                    out += 16;
                }
            }

            static void Decode(const char*& first, const char* last, uint8_t*& out) {
                // Consumes 16 chars -> 12 bytes per iteration but stores 16
                // bytes, so keep >= 24 chars in flight for output slack
                while (last - first >= 24) {
                    const __m128i in = _mm_loadu_si128(reinterpret_cast<const __m128i*>(first));

                    // Range classification (signed compares; bytes >= 0x80 fail all)
                    const __m128i rangeAZ = _mm_and_si128(_mm_cmpgt_epi8(in, _mm_set1_epi8('A' - 1)),
                                                          _mm_cmpgt_epi8(_mm_set1_epi8('Z' + 1), in));
                    const __m128i range_az = _mm_and_si128(_mm_cmpgt_epi8(in, _mm_set1_epi8('a' - 1)),
                                                           _mm_cmpgt_epi8(_mm_set1_epi8('z' + 1), in));
                    const __m128i range09 = _mm_and_si128(_mm_cmpgt_epi8(in, _mm_set1_epi8('0' - 1)),
                                                          _mm_cmpgt_epi8(_mm_set1_epi8('9' + 1), in));
                    const __m128i eq62 = _mm_cmpeq_epi8(in, _mm_set1_epi8(C62));
                    const __m128i eq63 = _mm_cmpeq_epi8(in, _mm_set1_epi8(C63));

                    const __m128i valid = _mm_or_si128(_mm_or_si128(rangeAZ, range_az),
                                                       _mm_or_si128(range09, _mm_or_si128(eq62, eq63)));
                    if (_mm_movemask_epi8(valid) != 0xFFFF) {
                        return; // scalar code re-examines from `first` and throws
                    }

                    // char + per-range delta -> 6-bit value (masks are disjoint)
                    __m128i delta = _mm_and_si128(rangeAZ, _mm_set1_epi8(-'A'));
                    delta = _mm_or_si128(delta, _mm_and_si128(range_az, _mm_set1_epi8(26 - 'a')));
                    delta = _mm_or_si128(delta, _mm_and_si128(range09, _mm_set1_epi8(52 - '0')));
                    delta = _mm_or_si128(delta, _mm_and_si128(eq62, _mm_set1_epi8(62 - C62)));
                    delta = _mm_or_si128(delta, _mm_and_si128(eq63, _mm_set1_epi8(63 - C63)));
                    const __m128i values = _mm_add_epi8(in, delta);

                    // Pack four 6-bit values per lane into three bytes (Mula)
                    const __m128i merged16 = _mm_maddubs_epi16(values, _mm_set1_epi32(0x01400140));
                    const __m128i merged32 = _mm_madd_epi16(merged16, _mm_set1_epi32(0x00011000));
                    const __m128i packed = _mm_shuffle_epi8(
                        merged32, _mm_setr_epi8(2, 1, 0, 6, 5, 4, 10, 9, 8, 14, 13, 12, -1, -1, -1, -1));

                    _mm_storeu_si128(reinterpret_cast<__m128i*>(out), packed);
                    first += 16;
                    out += 12;
                }
            }
        };

        // ---- Base16: 16 input bytes -> 32 characters per vector ----
        struct SseBase16 {
            static constexpr bool Available = true;

            static void Encode(const uint8_t*& first, const uint8_t* last, char*& out) {
                const __m128i lut = _mm_loadu_si128(reinterpret_cast<const __m128i*>(Base16Alphabet.data()));
                while (last - first >= 16) {
                    const __m128i in = _mm_loadu_si128(reinterpret_cast<const __m128i*>(first));
                    const __m128i hi = _mm_and_si128(_mm_srli_epi16(in, 4), _mm_set1_epi8(0x0F));
                    const __m128i lo = _mm_and_si128(in, _mm_set1_epi8(0x0F));
                    const __m128i hiChars = _mm_shuffle_epi8(lut, hi);
                    const __m128i loChars = _mm_shuffle_epi8(lut, lo);
                    _mm_storeu_si128(reinterpret_cast<__m128i*>(out), _mm_unpacklo_epi8(hiChars, loChars));
                    _mm_storeu_si128(reinterpret_cast<__m128i*>(out + 16), _mm_unpackhi_epi8(hiChars, loChars));
                    first += 16;
                    out += 32;
                }
            }

            static void Decode(const char*& first, const char* last, uint8_t*& out) {
                while (last - first >= 16) { // 16 chars -> 8 bytes, exact store
                    const __m128i in = _mm_loadu_si128(reinterpret_cast<const __m128i*>(first));
                    const __m128i range09 = _mm_and_si128(_mm_cmpgt_epi8(in, _mm_set1_epi8('0' - 1)),
                                                          _mm_cmpgt_epi8(_mm_set1_epi8('9' + 1), in));
                    const __m128i rangeAF = _mm_and_si128(_mm_cmpgt_epi8(in, _mm_set1_epi8('A' - 1)),
                                                          _mm_cmpgt_epi8(_mm_set1_epi8('F' + 1), in));
                    const __m128i valid = _mm_or_si128(range09, rangeAF);
                    if (_mm_movemask_epi8(valid) != 0xFFFF) {
                        return;
                    }
                    __m128i delta = _mm_and_si128(range09, _mm_set1_epi8(-'0'));
                    delta = _mm_or_si128(delta, _mm_and_si128(rangeAF, _mm_set1_epi8(10 - 'A')));
                    const __m128i values = _mm_add_epi8(in, delta);
                    // byte = highNibbleValue * 16 + lowNibbleValue, per char pair
                    const __m128i merged = _mm_maddubs_epi16(values, _mm_set1_epi16(0x0110));
                    _mm_storel_epi64(reinterpret_cast<__m128i*>(out), _mm_packus_epi16(merged, merged));
                    first += 16;
                    out += 8;
                }
            }
        };

        template<> struct SimdCodec<64, Base64Alphabet> : SseBase64<Base64Alphabet> {};
        template<> struct SimdCodec<64, Base64UrlAlphabet> : SseBase64<Base64UrlAlphabet> {};
        template<> struct SimdCodec<16, Base16Alphabet> : SseBase16 {};

    } // namespace detail
} // namespace snicholls

#endif /* x86 && SSSE3 */
#endif /* encode_decode_simd_intel_hpp */
