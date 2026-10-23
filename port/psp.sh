#!/usr/bin/env bash
# Build the PSP-2000 port (docs/PSP.md) in the pspdev container. The result is
# port/build/psp/EBOOT.PBP, with lang/ and glyphs/ beside it.
#
#   port/psp.sh            build
#   CLEAN=1 port/psp.sh    remove port/build/psp first
#   JOBS=N port/psp.sh     parallel jobs (default: all cores)
set -euo pipefail

cd "$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

IMAGE=${PSPDEV_IMAGE:-pspdev/pspdev:latest}
[ "${CLEAN:-0}" = 1 ] && rm -rf port/build/psp
[ -f tools/kazarov/include/kazarov/port.h ] || git submodule update --init tools/kazarov

# MSYS_NO_PATHCONV keeps Git Bash on Windows from rewriting the container paths.
MSYS_NO_PATHCONV=1 exec docker run --rm -v "$PWD:/work" -w /work -e "JOBS=${JOBS:-}" "$IMAGE" sh -euc '
    command -v python3 >/dev/null || apk add --no-cache python3 >/dev/null
    cmake -S . -B port/build/psp -G "Unix Makefiles" -DPLATFORM=PSP -DCMAKE_BUILD_TYPE=MinSizeRel \
        -DCMAKE_TOOLCHAIN_FILE="$PSPDEV/psp/share/pspdev.cmake"
    cmake --build port/build/psp -j"${JOBS:-$(nproc)}"
'
