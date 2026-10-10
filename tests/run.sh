#!/usr/bin/env bash
#
# Runs the test suite on Linux. The plugin itself is a Windows DLL and is built with build.bat on
# Windows, but most of it is portable C++ and one script, so the logic can be checked here first.
#
# The stub/ headers stand in for the Win32 and WinHTTP surfaces, using the documented signatures,
# so a mismatch here is a mismatch on MSVC too. Needs g++ and node; nothing is downloaded.
set -u

cd "$(dirname "$0")"
SRC=../src
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

CXXFLAGS=(-std=c++17 -Wall -Wextra -O1 -fsanitize=address,undefined
          -include msvc_shim.h -Istub "-I$SRC")

pass=0
fail=0

step() {
  local name="$1"; shift
  echo
  echo "###############################################################################"
  echo "# $name"
  echo "###############################################################################"
  if "$@"; then pass=$((pass + 1)); else fail=$((fail + 1)); echo "  >>> $name FAILED"; fi
}

# ---- every source file must typecheck ---------------------------------------------------------
syntax_check() {
  local bad=0
  for f in ../dllmain.cpp "$SRC"/core/*.cpp "$SRC"/cdp/*.cpp "$SRC"/hud/*.cpp; do
    local out
    out=$(g++ -std=c++17 -fsyntax-only -Wall -Wextra -Wno-unknown-pragmas \
              -include msvc_shim.h -Istub "-I$SRC" "$f" 2>&1)
    if [ -n "$out" ]; then echo "  ISSUES ${f#../}"; echo "$out"; bad=1
    else echo "  clean  ${f#../}"; fi
  done
  return $bad
}
step "every source file typechecks" syntax_check

# ---- the files compiled into the .asi as resources -----------------------------------------
# Every panel layer is a well-formed PNG carrying pixels and nothing else, and every one the .rc
# names is there.
step "the panel images" python3 -I ../tools/check_assets.py

# ---- C++ unit tests ---------------------------------------------------------------------------
run_json() {
  g++ "${CXXFLAGS[@]}" "$SRC/cdp/json.cpp" json_test.cpp -o "$OUT/json_test" || return 1
  "$OUT/json_test"
}
step "the JSON reader" run_json

# The reader is the only component that parses bytes the plugin did not produce. Pass a bigger
# number as the first argument to run it for longer: tests/run.sh 1000000
run_fuzz() {
  g++ "${CXXFLAGS[@]}" -fno-sanitize-recover=all "$SRC/cdp/json.cpp" json_fuzz.cpp \
      -o "$OUT/json_fuzz" || return 1
  ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1}" "$OUT/json_fuzz" "${FUZZ_ITERATIONS:-200000}"
}
step "fuzzing the JSON reader" run_fuzz

run_wsurl() {
  g++ "${CXXFLAGS[@]}" "$SRC/cdp/client.cpp" "$SRC/cdp/json.cpp" "$SRC/core/util.cpp" \
      stub_transport.cpp stub_win32.cpp wsurl_test.cpp -o "$OUT/wsurl_test" || return 1
  "$OUT/wsurl_test"
}
step "the loopback guard on the debugger endpoint" run_wsurl

run_settings() {
  mkdir -p "$OUT/settings"
  g++ "${CXXFLAGS[@]}" "$SRC/core/settings.cpp" "$SRC/core/util.cpp" \
      settings_test.cpp stub_win32.cpp -o "$OUT/settings_test" || return 1
  "$OUT/settings_test" "$OUT/settings"
}
step "the settings file" run_settings

run_layout() {
  mkdir -p "$OUT/layout"
  g++ "${CXXFLAGS[@]}" "$SRC/core/layout.cpp" "$SRC/core/util.cpp" \
      layout_test.cpp stub_win32.cpp -o "$OUT/layout_test" || return 1
  "$OUT/layout_test" "$OUT/layout"
}
step "the layout file" run_layout

run_logger() {
  mkdir -p "$OUT/logger"
  g++ "${CXXFLAGS[@]}" "$SRC/core/logger.cpp" "$SRC/core/util.cpp" \
      stub_win32.cpp logger_test.cpp -o "$OUT/logger_test" || return 1
  "$OUT/logger_test" "$OUT/logger"
}
step "the startup log writes byte text" run_logger

# ---- the injected script ----------------------------------------------------------------------
extract_script() {
  node -e '
    const fs = require("fs");
    const src = fs.readFileSync(process.argv[1], "utf8");
    const a = src.indexOf("R\"JS(") + 5;
    const b = src.indexOf(")JS\"", a);
    if (a < 5 || b < 0) { console.error("could not find the script in payload.cpp"); process.exit(1); }
    fs.writeFileSync(process.argv[2], src.slice(a, b));
  ' "$SRC/hud/payload.cpp" "$OUT/script.js"
}

run_payload() {
  extract_script || return 1
  node --check "$OUT/script.js" 2>/dev/null || {
    # A bare function declaration is not a valid program on its own; wrap it to check syntax.
    printf 'var f = (%s);\n' "$(cat "$OUT/script.js")" > "$OUT/script_wrapped.js"
    node --check "$OUT/script_wrapped.js" || return 1
  }
  node payload_test.js "$OUT/script.js"
}
step "the injected panel script" run_payload

run_generated() {
  g++ "${CXXFLAGS[@]}" "-DSTUB_ASSET_ROOT=\"$(cd .. && pwd)\"" \
      "$SRC/hud/payload.cpp" "$SRC/hud/images.cpp" "$SRC/hud/assets.cpp" \
      "$SRC/core/settings.cpp" "$SRC/core/layout.cpp" "$SRC/core/util.cpp" "$SRC/cdp/json.cpp" \
      payload_dump.cpp stub_win32.cpp stub_resources.cpp \
      -o "$OUT/payload_dump" || return 1
  "$OUT/payload_dump" > "$OUT/install.js" || return 1
  echo "  generated $(wc -c < "$OUT/install.js") bytes"
  node generated_test.js "$OUT/install.js"
}
step "the expression the plugin actually injects" run_generated

echo
echo "###############################################################################"
if [ "$fail" -eq 0 ]; then
  echo "# all $pass groups passed"
else
  echo "# $fail of $((pass + fail)) groups FAILED"
fi
echo "###############################################################################"
exit $((fail > 0))
