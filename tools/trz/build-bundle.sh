#!/bin/sh
# Concatenates compressjs's bzip2 dependency tree into one browser-loadable
# script exposing TrzBzip2.compressFile(input, output, level).
#
# Usage: build-bundle.sh <compressjs-lib-dir> <output.js>
#
# The lib dir holds the following files, unmodified, from
# https://github.com/cscott/compressjs (lib/):
#   freeze.js Stream.js Util.js BitStream.js CRC32.js
#   HuffmanAllocator.js BWT.js Bzip2.js
#
# Minify the result (nix shell nixpkgs#nodePackages.terser -c terser ...) and
# embed it with embed-header.sh. Verify the exact artifact that ships: build
# a container with it (see make-trz.js in the same spirit as the upload page)
# and decompress with system bzip2 and the host test.
set -e
SRC=$1
OUT=$2
[ -n "$SRC" ] && [ -n "$OUT" ] || { echo "usage: $0 <compressjs-lib-dir> <output.js>" >&2; exit 1; }
for f in freeze Stream Util BitStream CRC32 HuffmanAllocator BWT Bzip2; do
  [ -f "$SRC/$f.js" ] || { echo "missing $SRC/$f.js" >&2; exit 1; }
done
{
cat <<'EOF'
/*! Bzip2 compression for the TinyReader upload page.
    compressjs by C. Scott Ananian et al. (https://github.com/cscott/compressjs)
    LGPL-2.1-or-later. Generated bundle: do not hand-edit; regenerate with
    tools/trz/build-bundle.sh. */
(function(root, factory) {
  var api = factory();
  if (typeof module !== 'undefined' && module.exports) { module.exports = api; }
  else { root.TrzBzip2 = api; }
})(typeof self !== 'undefined' ? self : this, function() {
var __registry = {};
var __name = null;
function define(deps, factory) {
  var resolved = (deps || []).map(function(d) { return __registry[d]; });
  __registry[__name] = factory.apply(null, resolved);
}
EOF
for f in freeze Stream Util BitStream CRC32 HuffmanAllocator BWT Bzip2; do
  echo "__name = './$f';"
  cat "$SRC/$f.js"
  echo
done
cat <<'EOF'
var Bzip2 = __registry['./Bzip2'];
return { compressFile: Bzip2.compressFile.bind(Bzip2) };
});
EOF
} > "$OUT"
echo "wrote $OUT ($(wc -c < "$OUT") bytes)"
