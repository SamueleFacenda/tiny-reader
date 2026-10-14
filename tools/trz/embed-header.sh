#!/bin/sh
# Wraps a bundle.js into src/Bzip2Js.h, the PROGMEM copy the portal serves
# at /bz.js. Run build-bundle.sh (and minify) first.
#
# Usage: embed-header.sh <bundle.js>
set -e
IN=$1
[ -n "$IN" ] || { echo "usage: $0 <bundle.js>" >&2; exit 1; }
OUT=src/Bzip2Js.h
{
  echo '#pragma once'
  echo
  echo '// Generated from compressjs (https://github.com/cscott/compressjs),'
  echo '// LGPL-2.1-or-later. Do not hand-edit: regenerate with'
  echo '// tools/trz/build-bundle.sh and this script.'
  echo
  echo '#include <pgmspace.h>'
  echo
  printf 'static const char BZIP2_JS[] PROGMEM = R"jsliteral('
  cat "$IN"
  printf ')jsliteral";'
  echo
} > "$OUT"
echo "wrote $OUT ($(wc -c < "$OUT") bytes)"
