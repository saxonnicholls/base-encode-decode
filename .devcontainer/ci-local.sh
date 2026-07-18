#!/usr/bin/env bash
#
# Build the devcontainer image and run the full test matrix natively for the
# HOST architecture - amd64 on an Intel machine, arm64 on a Raspberry Pi. Because
# the image is not platform-pinned, each host builds its own architecture with no
# QEMU cross-emulation. Run the same command on the Mac and on the Pi.
#
#   .devcontainer/ci-local.sh
#
set -euo pipefail
cd "$(dirname "$0")/.."

IMAGE=base-encode-decode-ci
echo "== building image ($(uname -m) host) =="
docker build -t "$IMAGE" -f .devcontainer/Dockerfile .devcontainer

echo "== running test matrix in container =="
docker run --rm -v "$PWD:/work" -w /work "$IMAGE" bash -euo pipefail -c '
  arch=$(uname -m)
  echo "container arch: $arch"
  # SSSE3 is not in the plain x86-64 baseline, so enable it to exercise the
  # Intel kernels; NEON is baseline on arm64.
  extra=""
  [ "$arch" = "x86_64" ] && extra="-mssse3"
  flags="-std=gnu++23 -O3 -Wall -Wextra $extra"

  for cxx in g++ clang++; do
    echo "--- $cxx : base suite (SIMD + scalar builds) ---"
    make clean >/dev/null
    make test CXX="$cxx" CXXFLAGS="$flags"
  done

  echo "--- g++ : crypto / key-value suite (OpenSSL + libsodium + RocksDB) ---"
  make clean >/dev/null
  make test-crypto ROCKSDB=1 CXX=g++ CXXFLAGS="$flags"
'
echo "== OK: $(uname -m) host built and tested natively =="
