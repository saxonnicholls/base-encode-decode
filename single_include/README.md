# The single include

`snicholls_base_encode_decode.hpp` is the whole of base-encode-decode in one file: `BaseEncodeDecode/base_encode_decode.hpp`
with every header of the library and of snicholls-core expanded in place, once each, needing nothing but the standard
library. Copy it into a project and `#include "snicholls_base_encode_decode.hpp"`. The adapters that need a third party
(nlohmann::json, OpenSSL, libsodium, RocksDB) are not in it.

- **Download it** from the [release](https://github.com/saxonnicholls/base-encode-decode/releases) for the version you
  pin. Each release's file is generated from that release's tagged commit and compiled on its own before it is attached.
- **Build it** for any other commit with `make single-include`, which writes it here (git-ignored), or with
  `python3 third_party/tooling/scripts/single_include.py --out <dir>`.

It is never committed, so it is never stale: every CMake build generates it and compiles it alone
(`base_encode_decode_single_include`), and the CTest `check.single_include` fails if a header is missing from the umbrella.
