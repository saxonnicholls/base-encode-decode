# Future directions

Ideas for where this library could go next. Nothing here is a commitment — it's
a menu for contributors and a record of what has been considered (including what
was deliberately left out, and why). Pull requests are welcome; see the
Contributing section of the [README](README.md).

## Guiding principles

New work should keep the project what it is:

- **Simple and header-only.** Copy the headers in; no build step, no
  configuration, no dependencies in the shipped library.
- **Correct first.** Every feature comes with tests (plain `assert()`), and
  where possible a `static_assert` for the compile-time path. Cross-validate
  against an established implementation when one exists.
- **Opt-in, not mandatory.** Extra capability lives behind an adapter header or
  a macro; the core stays small and the default behaviour obvious.
- **Only useful additions.** Breadth for its own sake is not a goal — an
  addition should remove real boilerplate a user would otherwise write.
- **One uniform surface.** Schemes live in the single `SNICHOLLS_FOR_EACH_SCHEME`
  table so every scheme gets every API; adapters reuse it.

## Performance

- **AVX2 / AVX-512 Base64 kernels.** The Intel path is SSSE3 today; AVX2 roughly
  doubles single-core Base64 throughput, and AVX-512 VBMI more. The highest-value
  performance lever. Slot in behind the existing `SimdCodec` dispatch with
  runtime CPU detection so the SSSE3 path remains the fallback.
- **SIMD for Base32 / Base32Hex / Crockford.** Only Base64/Base64Url/Base16 are
  vectorised so far; Base32's 5-bit groups are awkward but tractable.
- **Other ISAs.** ARM SVE, RISC-V Vector — same `SimdCodec` extension point.
- **Regression tracking.** Run the benchmark in CI and flag throughput drops.

## Schemes and formats

- **Whitespace-tolerant / MIME / PEM decoding.** The single biggest real-world
  interop gap: the strict decoder rejects the line breaks found in PEM
  certificates, emailed Base64, and many HTTP/API payloads. A `_mime` adapter
  with whitespace-skipping decoders plus line-wrapping (`EncodeBase64Pem`,
  `EncodeBase64Mime`) encoders would close it. High value, small effort.
- **Base85 / Ascii85 / Z85.** ~25% overhead versus Base64's ~33%; used by PDF,
  Git binary diffs, and ZeroMQ. Group-based (4 bytes → 5 chars), so it fits the
  existing machinery reasonably.
- **Crockford Base32 check symbol.** The spec defines an optional mod-37 check
  digit (computable byte-by-byte, no bignum). A natural, standards-aligned
  integrity feature for IDs and recovery codes, and Base32Crockford already
  ships.
- **Base58 / Base58Check.** Real crypto/web3 audience (Bitcoin addresses, WIF,
  IPFS CIDs). Honest caveat: 58 is not a power of two, so this is *arithmetic*
  base conversion (big-integer division), a separate and heavier implementation
  that does not reuse the bit-group core. Worth it only if the crypto use case
  is wanted.

## Adapters and integrations

- **C API (`extern "C"`).** A thin flat wrapper over the common schemes opens the
  library to C, Rust (bindgen), and Python (ctypes/cffi) callers.
- **`std::ranges` view.** A lazy `data | encode_base64` view that yields encoded
  characters without materialising a string — nice for pipelines.
- **More `ObjectSerializer` specialisations.** `std::chrono` types,
  `std::filesystem::path`, `std::bitset`, small third-party types on request.
- **Portable object serialisation.** The trivially-copyable object path is
  same-ABI (native byte order). A byte-order-normalising variant for fundamental
  types would make object snapshots portable across architectures without
  routing through CBOR.

## Ergonomics and tooling

- **Single-header amalgamation.** A `make amalgamate` target that concatenates
  the headers into one `base_encode_decode.hpp`, the way nlohmann/json ships a
  single file — pure drop-in convenience.
- **Fuzzing.** libFuzzer / OSS-Fuzz harnesses over the decoders; they are the
  parts that consume untrusted input.
- **Wider constexpr coverage** where it is currently runtime-only.

## Security

- **Constant-time decode for secrets.** `utils/secure.hpp` already provides
  wiped-on-free `SecureBytes`/`SecureString` and a `ConstantTimeEqual`; the
  remaining gap is the codec itself — the decoders use data-dependent table
  lookups and are not constant-time. A branchless decode path would matter when
  handling key material where side channels are a concern. A real, careful piece
  of work rather than a quick flag.

## Deliberately out of scope

These have been considered and set aside on purpose. Please read the reasoning
before proposing them:

- **Using OpenSSL / libsodium *for base encoding*.** Both ship their own Base64,
  but the core library keeps its own and takes no dependency on them for
  encoding. (Note: crypto is a different story and is now *done the right way* -
  `utils/encryption_*.hpp` provide optional, autodetected AES-256-GCM and
  XChaCha20-Poly1305 implementations behind the pure-virtual `Encryptor`
  interface, delegating entirely to the vetted libraries. The core stays
  dependency-free; you opt in by including the drop-in. What remains out of
  scope is implementing any cipher ourselves.)
- **Qt `QByteArray` and similar framework types.** Qt already provides
  `toBase64`; its users will not reach for this library.
- **A full genomics toolkit.** The DNA/RNA adapter is packing/unpacking only.
  Reverse-complement, GC content, alignment, and FASTA parsing belong in a
  bioinformatics library, not here.
- **ULID / TOTP generators.** These need a clock and a random source; the
  library is deliberately pure and stateless. The building blocks are present
  (Crockford Base32 is the ULID alphabet), so an *encoder* half could fit, but
  not a generator.
- **A general-purpose serialization framework.** The object/STL adapter composes
  the standard containers; going further (versioning, schema evolution,
  polymorphic type registries) is reinventing cereal/Boost.Serialization. For
  anything portable or schema-bearing, route through CBOR via the json adapter.
- **An unconditional little-endian `static_assert`.** It would break big-endian
  builds for correct same-machine use. The object adapter instead offers the
  opt-in `SNICHOLLS_REQUIRE_LITTLE_ENDIAN` guard and rejects only mixed-endian.

## Proposing something

Open an issue to discuss larger changes first — especially anything touching the
core, adding a dependency, or expanding scope. Small, well-tested additions that
follow the principles above are easy to accept.
