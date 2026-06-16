#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Reproduit localement TOUTES les portes de la CI. À lancer avant de pousser.
#   Usage : ./scripts/check.sh
set -euo pipefail
cd "$(dirname "$0")/.."

CLANG_FORMAT="${CLANG_FORMAT:-clang-format}"
CLANG_TIDY="${CLANG_TIDY:-clang-tidy}"
BUILD_DIR="build"

echo "▶ 1/4  Formatage (clang-format)"
mapfile -t SOURCES < <(find core dsp engine testing \( -name '*.cpp' -o -name '*.hpp' \))
"$CLANG_FORMAT" --dry-run --Werror "${SOURCES[@]}"

echo "▶ 2/4  Configuration + compilation (warnings = erreurs, sanitizers ON)"
cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug \
    -DVOICELIVE_ENABLE_SANITIZERS=ON -DVOICELIVE_WARNINGS_AS_ERRORS=ON >/dev/null
cmake --build "$BUILD_DIR" -j"$(nproc)"

echo "▶ 3/4  Tests unitaires (instrumentés ASan/UBSan)"
ctest --test-dir "$BUILD_DIR" --output-on-failure

echo "▶ 4/4  Analyse statique (clang-tidy)"
find core dsp engine -name '*.cpp' -print0 | xargs -0 -I{} "$CLANG_TIDY" -p "$BUILD_DIR" {}

echo "✅ Toutes les portes sont vertes."
