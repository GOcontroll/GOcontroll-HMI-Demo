#!/bin/bash
# Regenerate splash/assets/GOcontroll_splash.png from the top-level SVG.
# Host tool only — not called during the Debian build. Re-run manually when
# the SVG changes.
#
# Deps: librsvg2-bin (rsvg-convert).

set -eu

here=$(cd "$(dirname "$0")/.." && pwd)
svg="$(cd "$here/.." && pwd)/assets/Logo GC def 2019.svg"
out="$here/assets/GOcontroll_splash.png"

if ! command -v rsvg-convert >/dev/null 2>&1; then
    echo "rsvg-convert not found. Install with: apt install librsvg2-bin" >&2
    exit 1
fi

rsvg-convert -w 600 "$svg" -o "$out"
echo "rendered $out"
