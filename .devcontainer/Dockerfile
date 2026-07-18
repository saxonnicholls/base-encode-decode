# syntax=docker/dockerfile:1.6
#
# Clean-build image for base-encode-decode.
#
# Intentionally NOT pinned to a platform: `docker build` produces an amd64 image
# on an Intel host and an arm64 image natively on a Raspberry Pi, so each machine
# builds and tests its own architecture with no QEMU cross-emulation. That is the
# whole point - it sidesteps the QEMU-on-Mac problem by building arm64 on the Pi.
#
# The library itself is header-only and needs none of these packages; they are
# only for the optional crypto / key-value drop-ins (OpenSSL, libsodium, RocksDB)
# and for building with both GCC and Clang.
FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# apt cache mounts keep downloaded packages across builds (build-time only,
# not baked into the image), so an interrupted download on a slow link is not
# wasted - a re-run reuses whatever was already fetched.
RUN --mount=type=cache,target=/var/cache/apt,sharing=locked \
    --mount=type=cache,target=/var/lib/apt,sharing=locked \
    apt-get update && apt-get install -y --no-install-recommends \
      build-essential \
      g++ \
      clang \
      make \
      cmake \
      pkg-config \
      git \
      ca-certificates \
      libssl-dev \
      libsodium-dev \
      librocksdb-dev

WORKDIR /work
