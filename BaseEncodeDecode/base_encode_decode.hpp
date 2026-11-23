// SPDX-License-Identifier: MIT
// Copyright (c) 2024 Saxon Nicholls

//
//  base_encode_decode.hpp
//  BaseEncodeDecode
//
//  The umbrella: every header of the library, sorted, so one include brings the codecs and every adapter that needs
//  nothing but the standard library (and snicholls-core, which this library pins). It is also the source of
//  single_include/snicholls_base_encode_decode.hpp, the whole library in one file (tooling's single_include.py,
//  `make single-include`).
//
//  Left out, because they need a third party (tooling.conf, single_include_opt_in); include them yourself:
//
//      #include "encode_decode_json.hpp"         // nlohmann::json
//      #include "utils/encryption_openssl.hpp"   // OpenSSL
//      #include "utils/encryption_sodium.hpp"    // libsodium
//      #include "utils/kv_rocksdb.hpp"           // RocksDB
//

#ifndef base_encode_decode_hpp
#define base_encode_decode_hpp

#include "alphabet.hpp"
#include "encode_decode_base_whatever.hpp"
#include "encode_decode_bitset.hpp"
#include "encode_decode_bitstring.hpp"
#include "encode_decode_dna.hpp"
#include "encode_decode_format.hpp"
#include "encode_decode_ml.hpp"
#include "encode_decode_mmap.hpp"
#include "encode_decode_object.hpp"
#include "encode_decode_simd_arm.hpp"
#include "encode_decode_simd_intel.hpp"
#include "encode_decode_stream.hpp"
#include "encode_decode_web.hpp"
#include "utils/basic_type_registry.hpp"
#include "utils/encryption.hpp"
#include "utils/fixed_string.hpp"
#include "utils/key_value_store.hpp"
#include "utils/overloaded.hpp"
#include "utils/secure.hpp"
#include "utils/stl_support.hpp"
#include "utils/type_registry.hpp"

#endif /* base_encode_decode_hpp */
