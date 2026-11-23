# base-encode-decode

Instructions for agents working in this repository. The README describes the library.

## The family: placement, versions and standards (MUST)

This repository is one of the snicholls libraries. The family's standards (how we write C++; tests, demos and benchmarks against known libraries; interfaces as a concept and its virtual twin; documentation; Docker; the security model) are in tooling's [STANDARDS.md](https://github.com/saxonnicholls/tooling/blob/main/STANDARDS.md). Which repository owns what, and the one version of every library, are kept in the owner's private `internal/` documents (`REPO_LAYOUT.md`, `LIBRARIES.md`): read them before adding code if you have access.

- **Owns:** byte codecs: base-N (Base16 bulk on core's hex digits, Base32, Base64, base64url), RLP, SIMD bulk paths.
- **Never:** hashes (core's), base58 and bech32 (crypto's).
- **Pins:** core.
- **Code is written once.** Search the family before writing anything. A copy, in a demo, a test, a script or a Dockerfile, is a defect: the best version wins and the others are deleted.
- **One version of every library, ours and third-party:** pinned by tag only, never a branch tip or an untagged commit, at the same tag as everywhere else in the family. A repository without a release takes `v0.1.0` first.
- **Interfaces twice:** every pure virtual interface has its concept twin, with adapters both ways and one conformance kit.
- **Always tests, a demo and a benchmark**, tested against known libraries wherever possible; as much as possible at compile time.
- **Docker** from tooling's base images; **secrets** in the git-ignored `.credentials/<service>.env`, with committed `.env.example` templates; the rest of the security model and the documentation standard as STANDARDS.md says.

