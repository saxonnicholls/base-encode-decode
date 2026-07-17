//
//  encode_decode_dna.hpp
//  BaseEncodeDecode
//
//  Optional drop-in adapter for compact DNA/RNA storage: pack a nucleotide
//  sequence into bits (4x or 2x smaller than one ASCII byte per base) and
//  unpack it losslessly. Include it explicitly:
//
//      #include "encode_decode_dna.hpp"
//
//      Binary packed = PackDna("ACGTACGT");     // 8 bases -> 2 bytes (4x)
//      std::string seq = UnpackDna(packed, 8);  // "ACGTACGT"
//
//  This is the mirror image of the base-N encoders in this library: those
//  EXPAND binary into a restricted text alphabet; this CONTRACTS a small text
//  alphabet (the bases) into packed binary. Under the hood it is the same
//  bit-group machinery with a biology alphabet.
//
//  Two widths:
//   * 2-bit  (PackDna / PackRna)           - A C G T/U only, 4 bases per byte,
//                                            4x smaller. Throws on anything
//                                            else (N, gaps, IUPAC ambiguity).
//                                            Ideal for clean/canonical data:
//                                            k-mers, QC'd references, oligos.
//   * 4-bit  (PackDnaIupac / PackRnaIupac) - full IUPAC set + N, 2 bases per
//                                            byte, 2x smaller. Handles real
//                                            FASTA. Nibble codes follow the
//                                            SAM/BAM seq_nt16 convention
//                                            (=ACMGRSVTWYHKDBN), so 4-bit
//                                            output is BAM-nibble compatible.
//
//  Input is case-insensitive (lowercase soft-masking is accepted but NOT
//  preserved - everything comes back uppercase). Invalid characters throw
//  std::invalid_argument. DNA rejects 'U' and RNA rejects 'T', so mixing the
//  two is caught rather than silently accepted.
//
//  Unpacking needs the base count, because the final byte may be partially
//  used (a sequence length not divisible by the bases-per-byte). Pass it as
//  the second argument; omit it to unpack every whole base the bytes contain
//  (which rounds up to a full byte of bases). Store the length alongside the
//  packed bytes, exactly as the .2bit / FASTA-index formats do.
//
//  Packed output is plain Binary, so it composes with the rest of the library:
//  Base64 it for a text channel, mmap it to a file, drop it in JSON, etc.
//  Encoding and decoding are constexpr.
//

#ifndef encode_decode_dna_hpp
#define encode_decode_dna_hpp

#include <array>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

#include "encode_decode_base_whatever.hpp" // for Binary

namespace snicholls {

    // Encode alphabets (index -> base character). 2-bit uses A=0,C=1,G=2,T/U=3.
    // 4-bit follows the SAM/BAM seq_nt16 order so packed nibbles match BAM.
    inline constexpr std::array<char, 4> Dna2Alphabet = {'A', 'C', 'G', 'T'};
    inline constexpr std::array<char, 4> Rna2Alphabet = {'A', 'C', 'G', 'U'};
    inline constexpr std::array<char, 16> DnaIupacAlphabet = {
        '=', 'A', 'C', 'M', 'G', 'R', 'S', 'V', 'T', 'W', 'Y', 'H', 'K', 'D', 'B', 'N'};
    inline constexpr std::array<char, 16> RnaIupacAlphabet = {
        '=', 'A', 'C', 'M', 'G', 'R', 'S', 'V', 'U', 'W', 'Y', 'H', 'K', 'D', 'B', 'N'};

    namespace detail {

        // base character -> code, case-folded; -1 for characters not in the
        // alphabet. consteval: always built at compile time.
        template<size_t N, const std::array<char, N>& Alphabet>
        consteval std::array<int8_t, 256> MakeNucleotideTable() {
            std::array<int8_t, 256> table{};
            for (auto& entry : table) {
                entry = -1;
            }
            for (size_t i = 0; i < N; ++i) {
                const char c = Alphabet[i];
                table[static_cast<unsigned char>(c)] = static_cast<int8_t>(i);
                if (c >= 'A' && c <= 'Z') { // also accept the lowercase form
                    table[static_cast<unsigned char>(c - 'A' + 'a')] = static_cast<int8_t>(i);
                }
            }
            return table;
        }

        template<size_t N, const std::array<char, N>& Alphabet>
        inline constexpr std::array<int8_t, 256> NucleotideTable = MakeNucleotideTable<N, Alphabet>();

        // Pack a sequence: BitsPerBase bits per base, first base in the high
        // bits of each byte. The last byte is zero-padded in its unused low bits.
        template<size_t BitsPerBase, size_t AlphabetSize, const std::array<char, AlphabetSize>& Alphabet>
        constexpr Binary PackNucleotides(std::string_view seq) {
            constexpr size_t perByte = 8 / BitsPerBase;
            const auto& reverse = NucleotideTable<AlphabetSize, Alphabet>;

            Binary out((seq.size() + perByte - 1) / perByte, 0);
            for (size_t i = 0; i < seq.size(); ++i) {
                const int code = reverse[static_cast<unsigned char>(seq[i])];
                if (code < 0) {
                    throw std::invalid_argument("PackNucleotides: character is not a valid base for this alphabet");
                }
                const size_t shift = (perByte - 1 - (i % perByte)) * BitsPerBase;
                out[i / perByte] = static_cast<uint8_t>(out[i / perByte] | (static_cast<unsigned>(code) << shift));
            }
            return out;
        }

        template<size_t BitsPerBase, size_t AlphabetSize, const std::array<char, AlphabetSize>& Alphabet>
        constexpr std::string UnpackNucleotides(std::span<const uint8_t> packed, size_t nbases) {
            constexpr size_t perByte = 8 / BitsPerBase;
            constexpr uint8_t mask = static_cast<uint8_t>((1u << BitsPerBase) - 1);

            const size_t available = packed.size() * perByte;
            const size_t count = nbases < available ? nbases : available;

            std::string out;
            out.reserve(count);
            for (size_t i = 0; i < count; ++i) {
                const size_t shift = (perByte - 1 - (i % perByte)) * BitsPerBase;
                out.push_back(Alphabet[(packed[i / perByte] >> shift) & mask]);
            }
            return out;
        }

    } // namespace detail

    // Number of packed bytes for a given base count and width
    constexpr size_t PackedDnaSize(size_t nbases) noexcept { return (nbases + 3) / 4; }       // 2-bit
    constexpr size_t PackedDnaIupacSize(size_t nbases) noexcept { return (nbases + 1) / 2; }  // 4-bit

    // ---- 2-bit: canonical bases only, 4x smaller ----

    constexpr Binary PackDna(std::string_view seq) {
        return detail::PackNucleotides<2, 4, Dna2Alphabet>(seq);
    }
    constexpr std::string UnpackDna(std::span<const uint8_t> packed, size_t nbases = SIZE_MAX) {
        return detail::UnpackNucleotides<2, 4, Dna2Alphabet>(packed, nbases);
    }

    constexpr Binary PackRna(std::string_view seq) {
        return detail::PackNucleotides<2, 4, Rna2Alphabet>(seq);
    }
    constexpr std::string UnpackRna(std::span<const uint8_t> packed, size_t nbases = SIZE_MAX) {
        return detail::UnpackNucleotides<2, 4, Rna2Alphabet>(packed, nbases);
    }

    // ---- 4-bit: full IUPAC set incl. N, 2x smaller, BAM-nibble compatible ----

    constexpr Binary PackDnaIupac(std::string_view seq) {
        return detail::PackNucleotides<4, 16, DnaIupacAlphabet>(seq);
    }
    constexpr std::string UnpackDnaIupac(std::span<const uint8_t> packed, size_t nbases = SIZE_MAX) {
        return detail::UnpackNucleotides<4, 16, DnaIupacAlphabet>(packed, nbases);
    }

    constexpr Binary PackRnaIupac(std::string_view seq) {
        return detail::PackNucleotides<4, 16, RnaIupacAlphabet>(seq);
    }
    constexpr std::string UnpackRnaIupac(std::span<const uint8_t> packed, size_t nbases = SIZE_MAX) {
        return detail::UnpackNucleotides<4, 16, RnaIupacAlphabet>(packed, nbases);
    }
}

#endif /* encode_decode_dna_hpp */
