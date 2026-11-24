#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${TMPDIR:-/tmp}/star_pet_core_test"

"${CC:-cc}" -std=c11 -Wall -Wextra \
  -I"$ROOT/components/pet_core/include" \
  "$ROOT/components/pet_core/src/pet_core.c" \
  "$ROOT/tests/test_pet_core.c" \
  -o "$OUT"

"$OUT"
