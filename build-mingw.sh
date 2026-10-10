#!/usr/bin/env bash
#
# Cross-builds gtaw-siren-panel.asi from Linux with MinGW-w64.
#
# build.bat is the intended path: it uses MSVC, which is what the code was written and tested
# against. This script exists so a build can be produced without a Windows machine. It emits a
# normal native PE DLL, but it is NOT byte-for-byte what build.bat produces, and it links the
# GCC runtime statically instead of the MSVC one.
#
# Point MINGW_BIN at a directory holding the cross compiler, or have it on PATH already:
#   MINGW_BIN=/path/to/toolchain/usr/bin ./build-mingw.sh
set -euo pipefail

cd "$(dirname "$0")"

if [ -n "${MINGW_BIN:-}" ]; then export PATH="$MINGW_BIN:$PATH"; fi

# Ubuntu ships the threading variants under explicit names and wires the plain name up through
# dpkg alternatives, which are absent when the packages are merely unpacked. Take whichever
# exists, preferring win32 threads: this plugin uses CreateThread directly and never std::thread,
# so it has no use for the winpthread layer.
CXX=""
for candidate in x86_64-w64-mingw32-g++-win32 x86_64-w64-mingw32-g++-posix x86_64-w64-mingw32-g++; do
  if command -v "$candidate" >/dev/null 2>&1; then CXX="$candidate"; break; fi
done
if [ -z "$CXX" ]; then
  echo "No MinGW-w64 C++ cross compiler found. Install mingw-w64, or set MINGW_BIN." >&2
  exit 1
fi
RC=x86_64-w64-mingw32-windres

# windres preprocesses the .rc by shelling out to a plain "x86_64-w64-mingw32-gcc", which is the
# same alternatives-managed name that may not exist. Point it at the matching C driver instead of
# letting it guess. The three arguments are the ones windres passes by default.
CC="${CXX/g++/gcc}"

echo "compiler: $($CXX --version | head -1)"

OUT=gtaw-siren-panel.asi
BUILD=$(mktemp -d)
trap 'rm -rf "$BUILD"' EXIT

# The resource carries the FX_ASI_BUILD stamps. Without them FiveM refuses to load the plugin on
# game build 2189 and newer, silently and with no log.
"$RC" --preprocessor="$CC" \
      --preprocessor-arg=-E --preprocessor-arg=-xc-header --preprocessor-arg=-DRC_INVOKED \
      -i gtaw-siren-panel.rc -o "$BUILD/resource.o" --output-format=coff

SOURCES=(dllmain.cpp src/core/*.cpp src/cdp/*.cpp src/hud/*.cpp)

# -fstack-protector-strong stands in for /GS. There is no GCC equivalent of /guard:cf, so the
# MSVC build gets Control Flow Guard and this one does not.
CXXFLAGS=(
  -std=c++17 -O2 -DNDEBUG -DWIN32_LEAN_AND_MEAN
  -Wall -Wextra -Wno-unknown-pragmas
  -fstack-protector-strong
  -I src
)

# -static* removes any runtime DLL dependency, the way /MT does for the MSVC build.
# --no-insert-timestamp makes the output reproducible, the way /Brepro does.
LDFLAGS=(
  -shared -static -static-libgcc -static-libstdc++
  -Wl,--exclude-all-symbols
  -Wl,--nxcompat -Wl,--dynamicbase -Wl,--high-entropy-va
  -Wl,--no-insert-timestamp
)

"$CXX" "${CXXFLAGS[@]}" "${SOURCES[@]}" "$BUILD/resource.o" -o "$OUT" "${LDFLAGS[@]}" -lwinhttp

# The header's date is the commit's, as in build.bat.
python3 tools/set_pe_time.py "$OUT" || true

echo
echo "Built $OUT"
ls -l "$OUT"
echo
echo "Copy it to: <your FiveM install>\\FiveM.app\\plugins\\"
