# Changelog

All notable changes to base-encode-decode. The format follows Keep a Changelog; versions follow Semantic Versioning.

## 1.1.0 - 2026-11-10

base-encode-decode now builds on [snicholls-core](https://github.com/saxonnicholls/core), pinned at v0.5.1 as the
submodule `third_party/core` (CMake: `snicholls::core`, `snicholls_core_require(0.5.0)`; the Makefile and the Xcode
project add `third_party/core/include`). Clone with `--recurse-submodules`, or run
`git submodule update --init` in an existing clone.

### Deprecated

These headers moved to snicholls-core. Each now only forwards to its replacement, so code that includes it still
compiles unchanged. They will be removed in 2.0.0.

| Deprecated header | Replacement |
|---|---|
| `utils/overloaded.hpp` | `<snicholls/core/overloaded.hpp>` (adds `first_match` and function-pointer elements) |
| `utils/fixed_string.hpp` | `<snicholls/core/fixed_string.hpp>` (the same interface) |
| `utils/basic_type_registry.hpp` | `<snicholls/core/basic_type_registry.hpp>`: `BasicTypeRegistry` -> `basic_type_registry`, `Named` -> `named`, `nameOf<T>()` -> `name_of<T>()` |

### Changed

- `utils/type_registry.hpp` (`TypeRegistry`, the Base64 defaults) stays, and is not deprecated. It is now built on
  core's `basic_type_registry` and includes core's headers directly, so it does not reach the deprecated ones.
  `Named` and `nameOf<T>()` keep working with it.
- The tests use the core headers. `tests/test_basic_type_registry.cpp` also includes every deprecated path, to
  prove they still compile and keep their old names.

### Not changed

- `utils/secure.hpp` stays. It is not a duplicate of core's `secret.hpp`: `secure.hpp` wipes memory
  (`SecureWipe`, `SecureAllocator`, `SecureBytes` and the wiped containers, `IsSecure<T>`) and compares in constant
  time, while core's `secret<T>` only keeps a value from being printed or logged.
