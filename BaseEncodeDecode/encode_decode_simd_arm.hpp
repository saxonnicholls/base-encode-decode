// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Saxon Nicholls

//
//  encode_decode_simd_arm.hpp
//  BaseEncodeDecode
//
//  AArch64 NEON drop-in (Apple Silicon, Raspberry Pi 3/4/5 in 64-bit mode,
//  AWS Graviton, ...), included automatically by
//  encode_decode_base_whatever.hpp. Define SNICHOLLS_NO_SIMD to opt out.
//
//  Accelerates Base64, Base64Url and Base16. All other schemes use the
//  scalar path. Kernels process whole vector groups and hand tails - and
//  anything after a suspect character - back to the scalar code, which is
//  the single source of truth for validation and error reporting.
//
//  NEON's structured loads/stores do the 3-byte <-> 4-char plane splitting
//  for free (vld3q/vst4q), and vqtbl4q_u8 looks the entire 64-character
//  alphabet up in registers, so encoding is alphabet-agnostic.
//

#ifndef encode_decode_simd_arm_hpp
#define encode_decode_simd_arm_hpp

#ifndef encode_decode_base_whatever_hpp
#error "Include encode_decode_base_whatever.hpp instead of this header directly"
#endif

#if defined(__aarch64__) && defined(__ARM_NEON)
#define SNICHOLLS_SIMD_ARM 1

#include <arm_neon.h>

namespace snicholls {
    namespace detail {

        // ---- Base64 family: 48 input bytes -> 64 characters per iteration ----
        template<const std::array<char, 64>& Alphabet>
        struct NeonBase64 {
            static constexpr bool Available = true;
            static constexpr char C62 = Alphabet[62]; // '+' or '-'
            static constexpr char C63 = Alphabet[63]; // '/' or '_'

            static void Encode(const uint8_t*& first, const uint8_t* last, char*& out) {
                const uint8_t* alphabet = reinterpret_cast<const uint8_t*>(Alphabet.data());
                uint8x16x4_t table;
                table.val[0] = vld1q_u8(alphabet);
                table.val[1] = vld1q_u8(alphabet + 16);
                table.val[2] = vld1q_u8(alphabet + 32);
                table.val[3] = vld1q_u8(alphabet + 48);

                while (last - first >= 48) {
                    const uint8x16x3_t in = vld3q_u8(first); // planes of b0, b1, b2
                    uint8x16x4_t chars;
                    // The four 6-bit fields of each 3-byte group
                    const uint8x16_t f0 = vshrq_n_u8(in.val[0], 2);
                    const uint8x16_t f1 = vorrq_u8(vshlq_n_u8(vandq_u8(in.val[0], vdupq_n_u8(0x03)), 4),
                                                   vshrq_n_u8(in.val[1], 4));
                    const uint8x16_t f2 = vorrq_u8(vshlq_n_u8(vandq_u8(in.val[1], vdupq_n_u8(0x0F)), 2),
                                                   vshrq_n_u8(in.val[2], 6));
                    const uint8x16_t f3 = vandq_u8(in.val[2], vdupq_n_u8(0x3F));
                    // Whole-alphabet lookup: 64 entries across four table registers
                    chars.val[0] = vqtbl4q_u8(table, f0);
                    chars.val[1] = vqtbl4q_u8(table, f1);
                    chars.val[2] = vqtbl4q_u8(table, f2);
                    chars.val[3] = vqtbl4q_u8(table, f3);
                    vst4q_u8(reinterpret_cast<uint8_t*>(out), chars);
                    first += 48;
                    out += 64;
                }
            }

            static void Decode(const char*& first, const char* last, uint8_t*& out) {
                while (last - first >= 64) { // 64 chars -> 48 bytes, exact store
                    const uint8x16x4_t in = vld4q_u8(reinterpret_cast<const uint8_t*>(first));

                    uint8x16_t values[4];
                    uint8x16_t allValid = vdupq_n_u8(0xFF);
                    for (int plane = 0; plane < 4; ++plane) {
                        const uint8x16_t c = in.val[plane];
                        const uint8x16_t rangeAZ = vandq_u8(vcgeq_u8(c, vdupq_n_u8('A')), vcleq_u8(c, vdupq_n_u8('Z')));
                        const uint8x16_t range_az = vandq_u8(vcgeq_u8(c, vdupq_n_u8('a')), vcleq_u8(c, vdupq_n_u8('z')));
                        const uint8x16_t range09 = vandq_u8(vcgeq_u8(c, vdupq_n_u8('0')), vcleq_u8(c, vdupq_n_u8('9')));
                        const uint8x16_t eq62 = vceqq_u8(c, vdupq_n_u8(static_cast<uint8_t>(C62)));
                        const uint8x16_t eq63 = vceqq_u8(c, vdupq_n_u8(static_cast<uint8_t>(C63)));

                        const uint8x16_t valid = vorrq_u8(vorrq_u8(rangeAZ, range_az),
                                                          vorrq_u8(range09, vorrq_u8(eq62, eq63)));
                        allValid = vandq_u8(allValid, valid);

                        // char + per-range delta -> 6-bit value (mod-256 adds)
                        uint8x16_t delta = vandq_u8(rangeAZ, vdupq_n_u8(static_cast<uint8_t>(-'A')));
                        delta = vorrq_u8(delta, vandq_u8(range_az, vdupq_n_u8(static_cast<uint8_t>(26 - 'a'))));
                        delta = vorrq_u8(delta, vandq_u8(range09, vdupq_n_u8(static_cast<uint8_t>(52 - '0'))));
                        delta = vorrq_u8(delta, vandq_u8(eq62, vdupq_n_u8(static_cast<uint8_t>(62 - C62))));
                        delta = vorrq_u8(delta, vandq_u8(eq63, vdupq_n_u8(static_cast<uint8_t>(63 - C63))));
                        values[plane] = vaddq_u8(c, delta);
                    }

                    if (vminvq_u8(allValid) == 0) {
                        return; // scalar code re-examines from `first` and throws
                    }

                    uint8x16x3_t bytes;
                    bytes.val[0] = vorrq_u8(vshlq_n_u8(values[0], 2), vshrq_n_u8(values[1], 4));
                    bytes.val[1] = vorrq_u8(vshlq_n_u8(values[1], 4), vshrq_n_u8(values[2], 2));
                    bytes.val[2] = vorrq_u8(vshlq_n_u8(values[2], 6), values[3]);
                    vst3q_u8(out, bytes);
                    first += 64;
                    out += 48;
                }
            }
        };

        // ---- Base16: 16 input bytes -> 32 characters per iteration ----
        struct NeonBase16 {
            static constexpr bool Available = true;

            static void Encode(const uint8_t*& first, const uint8_t* last, char*& out) {
                const uint8x16_t lut = vld1q_u8(reinterpret_cast<const uint8_t*>(Base16Alphabet.data()));
                while (last - first >= 16) {
                    const uint8x16_t in = vld1q_u8(first);
                    uint8x16x2_t chars;
                    chars.val[0] = vqtbl1q_u8(lut, vshrq_n_u8(in, 4));      // high nibble first
                    chars.val[1] = vqtbl1q_u8(lut, vandq_u8(in, vdupq_n_u8(0x0F)));
                    vst2q_u8(reinterpret_cast<uint8_t*>(out), chars);
                    first += 16;
                    out += 32;
                }
            }

            static void Decode(const char*& first, const char* last, uint8_t*& out) {
                while (last - first >= 32) { // 32 chars -> 16 bytes, exact store
                    const uint8x16x2_t in = vld2q_u8(reinterpret_cast<const uint8_t*>(first));

                    uint8x16_t nibbles[2];
                    uint8x16_t allValid = vdupq_n_u8(0xFF);
                    for (int plane = 0; plane < 2; ++plane) {
                        const uint8x16_t c = in.val[plane];
                        const uint8x16_t range09 = vandq_u8(vcgeq_u8(c, vdupq_n_u8('0')), vcleq_u8(c, vdupq_n_u8('9')));
                        const uint8x16_t rangeAF = vandq_u8(vcgeq_u8(c, vdupq_n_u8('A')), vcleq_u8(c, vdupq_n_u8('F')));
                        allValid = vandq_u8(allValid, vorrq_u8(range09, rangeAF));

                        uint8x16_t delta = vandq_u8(range09, vdupq_n_u8(static_cast<uint8_t>(-'0')));
                        delta = vorrq_u8(delta, vandq_u8(rangeAF, vdupq_n_u8(static_cast<uint8_t>(10 - 'A'))));
                        nibbles[plane] = vaddq_u8(c, delta);
                    }

                    if (vminvq_u8(allValid) == 0) {
                        return;
                    }

                    vst1q_u8(out, vorrq_u8(vshlq_n_u8(nibbles[0], 4), nibbles[1]));
                    first += 32;
                    out += 16;
                }
            }
        };

        template<> struct SimdCodec<64, Base64Alphabet> : NeonBase64<Base64Alphabet> {};
        template<> struct SimdCodec<64, Base64UrlAlphabet> : NeonBase64<Base64UrlAlphabet> {};
        template<> struct SimdCodec<16, Base16Alphabet> : NeonBase16 {};

    } // namespace detail
} // namespace snicholls

#endif /* __aarch64__ && __ARM_NEON */
#endif /* encode_decode_simd_arm_hpp */
