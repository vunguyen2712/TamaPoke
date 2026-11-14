#!/bin/bash
# Writes proto.h + sketch.cpp into the directory given (default: tools/emu).
#
# The Arduino build concatenates every .ino tab (the main sketch first, the rest
# alphabetically) and generates prototypes for them; a plain g++ build does
# neither. Shared by build.sh (the SDL emulator) and tests/run.sh, which needs
# no SDL at all -- the tests drive the sketch headlessly.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../.." && pwd)"
OUTDIR="${1:-$HERE}"
INOS=("$ROOT/TamaPoke.ino")
for f in "$ROOT"/*.ino; do [ "$f" != "$ROOT/TamaPoke.ino" ] && INOS+=("$f"); done
(cd "$OUTDIR" && python3 "$HERE/genproto.py" "${INOS[@]}")
# .ino is C++ but g++ will not compile that extension
{ echo '#include "proto.h"'; cat "${INOS[@]}"; } > "$OUTDIR/sketch.cpp"
