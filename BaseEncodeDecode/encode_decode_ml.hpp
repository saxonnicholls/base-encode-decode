// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Saxon Nicholls

//
//  encode_decode_ml.hpp
//  BaseEncodeDecode
//
//  Represent a tensor / an entire model layer as text (Base64, DNA, ...),
//  optionally encrypted, in a way that plays happily with the ML ecosystem.
//
//  Design stance: this header REPRESENTS, PACKS and TRANSPORTS tensor bytes.
//  It does NOT quantise or convert between dtypes (no fp32 -> fp8 rounding, no
//  fp16 widening) - that is the model runtime's job (GGML, libtorch, XLA).
//  Hand it already-quantised weights and it packs + encodes + encrypts + stores
//  them faithfully; it never fabricates or reinterprets numeric values.
//
//  Interop is achieved by speaking the conventions the ecosystem already uses:
//
//    * bytes are little-endian, C-contiguous, row-major  (numpy default,
//      safetensors, ggml, torch .contiguous())
//    * shape is a list of int64 dims                     (ONNX / TF allow -1)
//    * dtype names match safetensors / numpy             (F32, BF16, I8, ...)
//    * sub-byte weights (1/2/4-bit) are lane-packed MSB-first, the same bit
//      packing encode_decode_dna.hpp already does for nucleotides
//    * exotic block-quantised GGML types (Q4_K, Q6_K, ...) are carried as
//      Dtype::Opaque with the format name in `quant`, so they round-trip byte
//      for byte without this header pretending to understand them
//
//  Framework dtype cross-reference:
//    ours     safetensors   numpy      torch            ggml
//    F32      F32           <f4        float32          GGML_TYPE_F32
//    F16      F16           <f2        float16          GGML_TYPE_F16
//    BF16     BF16          (ml_dtypes)bfloat16         GGML_TYPE_BF16
//    F8_E4M3  F8_E4M3       (ml_dtypes)float8_e4m3fn    (in some IQ paths)
//    I8       I8            |i1        int8             GGML_TYPE_I8
//    I32      I32           <i4        int32            GGML_TYPE_I32
//    I4/U4    (as U8+meta)  (ml_dtypes)(quantised)      building block of Q4_*
//    OPAQUE   (n/a)         (bytes)    (n/a)            Q4_0/Q4_K/Q6_K/...
//
//  Once a Tensor is ObjectSerializable it rides the whole library for free:
//
//      Tensor w = Tensor::From<float>("mlp.w", {2, 3}, weights);
//      std::string b64 = EncodeBase64Object(w);          // layer as Base64
//      PutObjectEncrypted(kv, "mlp.w", w, cipher);        // encrypted at rest
//      Model m = { w, bias };                             // whole state_dict
//      std::string all = EncodeBase64UrlObject(m);        // one string
//
//  A Python binding down the track only needs the JSON view below plus:
//      np.frombuffer(base64.b64decode(t["data"]), dtype=NP[t["dtype"]]).reshape(t["shape"])
//

#ifndef encode_decode_ml_hpp
#define encode_decode_ml_hpp

#include <cstdint>
#include <cstring>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

#include "encode_decode_base_whatever.hpp"
#include "encode_decode_object.hpp" // Tensor rides the object pipeline

namespace snicholls::ml {

    // -----------------------------------------------------------------------
    // Dtype: the element types we describe. Names match safetensors so a value
    // is self-describing across frameworks. Sub-byte and Opaque extend it.
    // -----------------------------------------------------------------------
    enum class Dtype : uint8_t {
        Bool,                       // 1 byte (numpy/safetensors BOOL)
        U8, I8, U16, I16,
        U32, I32, U64, I64,
        F16, BF16, F32, F64,
        F8_E4M3, F8_E5M2,           // 8-bit floats (no rounding done here)
        U4, I4, U2, I2, U1, I1,     // sub-byte, lane-packed MSB-first
        Opaque,                     // block-quantised / unknown: raw bytes + `quant`
    };

    // Bits per element (0 for Opaque, whose size is not derivable from shape).
    inline constexpr unsigned BitsPerElement(Dtype d) noexcept {
        switch (d) {
            case Dtype::U1: case Dtype::I1: return 1;
            case Dtype::U2: case Dtype::I2: return 2;
            case Dtype::U4: case Dtype::I4: return 4;
            case Dtype::Bool:
            case Dtype::U8: case Dtype::I8:
            case Dtype::F8_E4M3: case Dtype::F8_E5M2: return 8;
            case Dtype::U16: case Dtype::I16:
            case Dtype::F16: case Dtype::BF16: return 16;
            case Dtype::U32: case Dtype::I32: case Dtype::F32: return 32;
            case Dtype::U64: case Dtype::I64: case Dtype::F64: return 64;
            case Dtype::Opaque: return 0;
        }
        return 0;
    }

    inline constexpr bool IsSubByte(Dtype d) noexcept {
        const unsigned b = BitsPerElement(d);
        return b == 1 || b == 2 || b == 4;
    }

    // Canonical name (safetensors spelling where one exists).
    inline constexpr std::string_view DtypeName(Dtype d) noexcept {
        switch (d) {
            case Dtype::Bool:    return "BOOL";
            case Dtype::U8:      return "U8";
            case Dtype::I8:      return "I8";
            case Dtype::U16:     return "U16";
            case Dtype::I16:     return "I16";
            case Dtype::U32:     return "U32";
            case Dtype::I32:     return "I32";
            case Dtype::U64:     return "U64";
            case Dtype::I64:     return "I64";
            case Dtype::F16:     return "F16";
            case Dtype::BF16:    return "BF16";
            case Dtype::F32:     return "F32";
            case Dtype::F64:     return "F64";
            case Dtype::F8_E4M3: return "F8_E4M3";
            case Dtype::F8_E5M2: return "F8_E5M2";
            case Dtype::U4:      return "U4";
            case Dtype::I4:      return "I4";
            case Dtype::U2:      return "U2";
            case Dtype::I2:      return "I2";
            case Dtype::U1:      return "U1";
            case Dtype::I1:      return "I1";
            case Dtype::Opaque:  return "OPAQUE";
        }
        return "OPAQUE";
    }

    inline Dtype DtypeFromName(std::string_view name) {
        for (int i = 0; i <= static_cast<int>(Dtype::Opaque); ++i) {
            const auto d = static_cast<Dtype>(i);
            if (DtypeName(d) == name) {
                return d;
            }
        }
        throw std::invalid_argument("snicholls::ml::DtypeFromName: unknown dtype '" + std::string(name) + "'");
    }

    // NumPy little-endian type string, or "" when numpy has no native match
    // (BF16/F8/sub-byte need the ml_dtypes package or bit-unpacking on the
    // Python side). Handy for a future binding.
    inline constexpr std::string_view NumpyTypestr(Dtype d) noexcept {
        switch (d) {
            case Dtype::Bool: return "|b1";
            case Dtype::U8:   return "|u1";
            case Dtype::I8:   return "|i1";
            case Dtype::U16:  return "<u2";
            case Dtype::I16:  return "<i2";
            case Dtype::U32:  return "<u4";
            case Dtype::I32:  return "<i4";
            case Dtype::U64:  return "<u8";
            case Dtype::I64:  return "<i8";
            case Dtype::F16:  return "<f2";
            case Dtype::F32:  return "<f4";
            case Dtype::F64:  return "<f8";
            default:          return ""; // BF16, F8_*, sub-byte, Opaque
        }
    }

    // -----------------------------------------------------------------------
    // Sub-byte lane packing: N-bit unsigned lanes (N in {1,2,4}) packed
    // MSB-first, the first value in the high bits - the same layout as the
    // 2-bit / 4-bit packing in encode_decode_dna.hpp, minus the alphabet.
    // Each input byte is one lane value in [0, 2^N). 8-bit is a straight copy.
    // -----------------------------------------------------------------------
    inline constexpr size_t PackedByteSize(size_t count, unsigned bits) noexcept {
        if (bits >= 8) {
            return count * (bits / 8); // whole-byte elements: bytes each
        }
        const unsigned per = 8 / bits;
        return (count + per - 1) / per;
    }

    inline Binary PackBits(std::span<const uint8_t> values, unsigned bits) {
        if (bits == 8) {
            return Binary(values.begin(), values.end());
        }
        if (bits != 1 && bits != 2 && bits != 4) {
            throw std::invalid_argument("snicholls::ml::PackBits: bits must be 1, 2, 4 or 8");
        }
        const unsigned per = 8 / bits;
        const uint8_t mask = static_cast<uint8_t>((1u << bits) - 1);
        Binary out(PackedByteSize(values.size(), bits), 0);
        for (size_t i = 0; i < values.size(); ++i) {
            const size_t byte = i / per;
            const unsigned slot = static_cast<unsigned>(i % per);      // 0 = high bits
            const unsigned shift = 8 - bits * (slot + 1);
            out[byte] |= static_cast<uint8_t>((values[i] & mask) << shift);
        }
        return out;
    }

    inline std::vector<uint8_t> UnpackBits(std::span<const uint8_t> packed, size_t count, unsigned bits) {
        if (bits == 8) {
            if (count > packed.size()) {
                throw std::invalid_argument("snicholls::ml::UnpackBits: truncated input");
            }
            return std::vector<uint8_t>(packed.begin(), packed.begin() + count);
        }
        if (bits != 1 && bits != 2 && bits != 4) {
            throw std::invalid_argument("snicholls::ml::UnpackBits: bits must be 1, 2, 4 or 8");
        }
        const unsigned per = 8 / bits;
        const uint8_t mask = static_cast<uint8_t>((1u << bits) - 1);
        std::vector<uint8_t> out;
        out.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            const size_t byte = i / per;
            if (byte >= packed.size()) {
                throw std::invalid_argument("snicholls::ml::UnpackBits: truncated input");
            }
            const unsigned slot = static_cast<unsigned>(i % per);
            const unsigned shift = 8 - bits * (slot + 1);
            out.push_back(static_cast<uint8_t>((packed[byte] >> shift) & mask));
        }
        return out;
    }

    // -----------------------------------------------------------------------
    // Map a C++ element type to a Dtype (compile-time). Types without a native
    // C++ spelling (F16/BF16/F8) are constructed via FromRaw with the bytes.
    // -----------------------------------------------------------------------
    namespace detail {
        template<typename> inline constexpr bool dependent_false = false;
    }

    template<typename T>
    consteval Dtype DtypeOf() {
        if constexpr (std::is_same_v<T, float>)         return Dtype::F32;
        else if constexpr (std::is_same_v<T, double>)   return Dtype::F64;
        else if constexpr (std::is_same_v<T, int8_t>)   return Dtype::I8;
        else if constexpr (std::is_same_v<T, uint8_t>)  return Dtype::U8;
        else if constexpr (std::is_same_v<T, int16_t>)  return Dtype::I16;
        else if constexpr (std::is_same_v<T, uint16_t>) return Dtype::U16;
        else if constexpr (std::is_same_v<T, int32_t>)  return Dtype::I32;
        else if constexpr (std::is_same_v<T, uint32_t>) return Dtype::U32;
        else if constexpr (std::is_same_v<T, int64_t>)  return Dtype::I64;
        else if constexpr (std::is_same_v<T, uint64_t>) return Dtype::U64;
        else static_assert(detail::dependent_false<T>,
                           "snicholls::ml::DtypeOf: no default Dtype for this C++ type; "
                           "use Tensor::FromRaw with an explicit Dtype (e.g. F16/BF16/F8)");
    }

    // -----------------------------------------------------------------------
    // Tensor: a named, typed, shaped block of bytes. `data` holds the element
    // bytes (little-endian, row-major); sub-byte dtypes store them lane-packed.
    // `quant` is non-empty only for Dtype::Opaque (e.g. "Q4_K").
    // -----------------------------------------------------------------------
    struct Tensor {
        std::string          name;
        Dtype                dtype = Dtype::F32;
        std::vector<int64_t> shape;
        Binary               data;
        std::string          quant;

        // Number of elements = product of dims (a 0-D / empty shape is 1 scalar).
        int64_t ElementCount() const noexcept {
            int64_t n = 1;
            for (const int64_t d : shape) {
                n *= d;
            }
            return n;
        }

        unsigned Bits() const noexcept { return BitsPerElement(dtype); }

        // Bytes `data` should hold for this dtype+shape (Opaque: whatever it is).
        size_t ExpectedDataBytes() const noexcept {
            if (dtype == Dtype::Opaque) {
                return data.size();
            }
            return PackedByteSize(static_cast<size_t>(ElementCount()), Bits());
        }

        bool Valid() const noexcept {
            return dtype == Dtype::Opaque || data.size() == ExpectedDataBytes();
        }

        // ---- construction ----------------------------------------------------

        // From a typed, contiguous range (float/double/intN/uintN). LE bytes.
        template<typename T>
        static Tensor From(std::string name, std::vector<int64_t> shape, std::span<const T> values) {
            static_assert(std::is_trivially_copyable_v<T>);
            Tensor t;
            t.name  = std::move(name);
            t.dtype = DtypeOf<T>();
            t.shape = std::move(shape);
            t.data.resize(values.size() * sizeof(T));
            if (!values.empty()) {
                std::memcpy(t.data.data(), values.data(), t.data.size());
            }
            return t;
        }
        template<typename T>
        static Tensor From(std::string name, std::vector<int64_t> shape, const std::vector<T>& values) {
            return From<T>(std::move(name), std::move(shape), std::span<const T>(values));
        }

        // From already-laid-out element bytes with an explicit dtype
        // (F16/BF16/F8, or any element bytes you already hold).
        static Tensor FromRaw(std::string name, Dtype dtype, std::vector<int64_t> shape, Binary bytes) {
            Tensor t;
            t.name  = std::move(name);
            t.dtype = dtype;
            t.shape = std::move(shape);
            t.data  = std::move(bytes);
            return t;
        }

        // From sub-byte lane values (one value per input byte); they get packed.
        static Tensor FromLanes(std::string name, Dtype dtype, std::vector<int64_t> shape,
                                std::span<const uint8_t> values) {
            if (!IsSubByte(dtype)) {
                throw std::invalid_argument("snicholls::ml::Tensor::FromLanes: dtype is not sub-byte");
            }
            Tensor t;
            t.name  = std::move(name);
            t.dtype = dtype;
            t.shape = std::move(shape);
            t.data  = PackBits(values, BitsPerElement(dtype));
            return t;
        }

        // A block-quantised / opaque blob carried verbatim (e.g. GGML Q4_K).
        static Tensor Opaque(std::string name, std::string quant, std::vector<int64_t> shape, Binary bytes) {
            Tensor t;
            t.name  = std::move(name);
            t.dtype = Dtype::Opaque;
            t.shape = std::move(shape);
            t.data  = std::move(bytes);
            t.quant = std::move(quant);
            return t;
        }

        // ---- views back out --------------------------------------------------

        // Reinterpret the element bytes as a typed vector (dtype must match).
        template<typename T>
        std::vector<T> ToVector() const {
            if (DtypeOf<T>() != dtype) {
                throw std::invalid_argument("snicholls::ml::Tensor::ToVector: dtype mismatch");
            }
            if (data.size() % sizeof(T) != 0) {
                throw std::invalid_argument("snicholls::ml::Tensor::ToVector: byte count not a multiple of sizeof(T)");
            }
            std::vector<T> out(data.size() / sizeof(T));
            if (!out.empty()) {
                std::memcpy(out.data(), data.data(), data.size());
            }
            return out;
        }

        // Unpack sub-byte lanes back to one value per byte.
        std::vector<uint8_t> Lanes() const {
            if (!IsSubByte(dtype)) {
                throw std::invalid_argument("snicholls::ml::Tensor::Lanes: dtype is not sub-byte");
            }
            return UnpackBits(data, static_cast<size_t>(ElementCount()), Bits());
        }

        // Raw element bytes (use this for F16/BF16/F8/Opaque).
        std::span<const uint8_t> RawBytes() const noexcept {
            return std::span<const uint8_t>(data);
        }

        bool operator==(const Tensor&) const = default;
    };

    // An ordered collection of tensors: a layer, a state_dict, a whole model.
    // std::vector<Tensor> is already ObjectSerializable (see the specialisation
    // below composed with the built-in vector serialiser), so a Model rides the
    // pipeline as one Base64 / encrypted / DNA blob with no extra code.
    using Model = std::vector<Tensor>;

    inline const Tensor* Find(const Model& model, std::string_view name) noexcept {
        for (const Tensor& t : model) {
            if (t.name == name) {
                return &t;
            }
        }
        return nullptr;
    }

} // namespace snicholls::ml

// ---------------------------------------------------------------------------
// Make Tensor ObjectSerializable so it (and Model = vector<Tensor>, and
// map<string,Tensor>, ...) flows through EncodeBase64Object / PutObjectEncrypted
// / DNA. Framing: name | dtype | shape(zigzag) | quant | data, via the same
// ByteWriter/ByteReader the rest of the object layer uses.
// ---------------------------------------------------------------------------
namespace snicholls {

    template<>
    struct ObjectSerializer<ml::Tensor> {
        static Binary to_bytes(const ml::Tensor& t) {
            Binary out;
            ByteWriter w(out);
            w.element(t.name);
            w.varint(static_cast<uint64_t>(t.dtype));
            w.varint(t.shape.size());
            for (const int64_t d : t.shape) {
                w.varint((static_cast<uint64_t>(d) << 1) ^ static_cast<uint64_t>(d >> 63)); // zigzag
            }
            w.element(t.quant);
            w.varint(t.data.size());
            w.raw(t.data);
            return out;
        }
        static ml::Tensor from_bytes(std::span<const uint8_t> bytes) {
            ByteReader r(bytes);
            ml::Tensor t;
            t.name = r.element<std::string>();
            const uint64_t rawDtype = r.varint();
            if (rawDtype > static_cast<uint64_t>(ml::Dtype::Opaque)) {
                throw std::invalid_argument("ObjectSerializer<ml::Tensor>: unknown dtype tag");
            }
            t.dtype = static_cast<ml::Dtype>(rawDtype);
            const uint64_t nd = r.varint();
            t.shape.reserve(static_cast<size_t>(nd));
            for (uint64_t i = 0; i < nd; ++i) {
                const uint64_t zz = r.varint();
                t.shape.push_back(static_cast<int64_t>(zz >> 1) ^ -static_cast<int64_t>(zz & 1)); // unzigzag
            }
            t.quant = r.element<std::string>();
            const uint64_t dn = r.varint();
            const auto slice = r.take(static_cast<size_t>(dn));
            t.data.assign(slice.begin(), slice.end());
            return t;
        }
    };

} // namespace snicholls

// ---------------------------------------------------------------------------
// Optional JSON view (include "encode_decode_json.hpp" BEFORE this header).
// A tensor becomes { name, dtype, shape, data(base64) [, quant] } - the exact
// shape a numpy/torch binding wants:
//     np.frombuffer(base64.b64decode(t["data"]), dtype=NP[t["dtype"]]).reshape(t["shape"])
// `data` is a Base64 string because the json adapter maps Binary that way.
// ---------------------------------------------------------------------------
#ifdef encode_decode_json_hpp
namespace snicholls::ml {

    inline void to_json(nlohmann::json& j, const Tensor& t) {
        j = nlohmann::json{
            {"name", t.name},
            {"dtype", DtypeName(t.dtype)},
            {"shape", t.shape},
            {"data", t.data}, // Binary -> Base64 via the json adapter
        };
        if (!t.quant.empty()) {
            j["quant"] = t.quant;
        }
    }

    inline void from_json(const nlohmann::json& j, Tensor& t) {
        t.name  = j.value("name", std::string{});
        t.dtype = DtypeFromName(j.at("dtype").get<std::string>());
        t.shape = j.at("shape").get<std::vector<int64_t>>();
        t.data  = j.at("data").get<Binary>();
        t.quant = j.value("quant", std::string{});
    }

} // namespace snicholls::ml
#endif // encode_decode_json_hpp

#endif /* encode_decode_ml_hpp */
