#!/usr/bin/env bash
# Builds web/js/refcore.js: RefCore (the same C++ rules as the Unreal module) + web/wasm/RcWebApi.cpp
# compiled to WebAssembly with the WASI SDK, then to plain JavaScript with Binaryen's wasm2js.
#   WASI_SDK=/path/to/wasi-sdk BINARYEN=/path/to/binaryen web/tools/build_core.sh
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
: "${WASI_SDK:?set WASI_SDK to an extracted wasi-sdk (https://github.com/WebAssembly/wasi-sdk/releases)}"
: "${BINARYEN:?set BINARYEN to an extracted binaryen release (https://github.com/WebAssembly/binaryen/releases)}"
SRC="$ROOT/RefereeCareer/Source/RefereeCareer"
OUT="$ROOT/web/build"
mkdir -p "$OUT"
"$WASI_SDK/bin/clang++" --target=wasm32-wasip1 -O2 -std=c++20 -fno-exceptions -fno-rtti -mexec-model=reactor \
  -I"$SRC/Public" "$SRC"/Private/RefCore/*.cpp "$ROOT/web/wasm/RcWebApi.cpp" \
  -Wl,--strip-all -Wl,-z,stack-size=1048576 -o "$OUT/refcore.wasm"
FEATURES=(--enable-bulk-memory --enable-sign-ext --enable-nontrapping-float-to-int --enable-mutable-globals)
"$BINARYEN/bin/wasm-opt" "${FEATURES[@]}" -O2 "$OUT/refcore.wasm" -o "$OUT/refcore.opt.wasm"
"$BINARYEN/bin/wasm2js" "${FEATURES[@]}" -O2 "$OUT/refcore.opt.wasm" -o "$OUT/refcore.wasm2js.mjs"
python3 "$ROOT/web/tools/wrap_core.py" "$OUT/refcore.wasm2js.mjs" "$ROOT/web/js/refcore.js"
echo "built web/js/refcore.js"
