#!/bin/sh
# Installs what jetio needs to build and test on Debian and Ubuntu (apt). The Docker image of
# tools/docker runs this script, so a native setup and the image have the same requirements.
set -eu

if ! command -v apt-get >/dev/null 2>&1; then
    echo "linux-apt.sh needs apt-get; see docs/development.md for what to install" >&2
    exit 1
fi

if [ "$(id -u)" -eq 0 ]; then
    sudo=""
else
    sudo="sudo"
fi

# gcc, g++, clang and clang-tidy are the default versions of the distribution; clang-22 and
# libc++-22 are for the libcxx presets, libclang-rt-dev and libclang-rt-22-dev for the sanitizers
# and libFuzzer of both clangs.
packages="
    ca-certificates git python3
    cmake ninja-build
    gcc g++
    clang clang-format clang-tidy libclang-rt-dev
    clang-22 libc++-22-dev libc++abi-22-dev libclang-rt-22-dev
"

export DEBIAN_FRONTEND=noninteractive
$sudo apt-get update
# shellcheck disable=SC2086
$sudo apt-get install -y --no-install-recommends $packages
