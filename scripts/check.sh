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

echo "▶ 0/5  Cohérence des sources du projet Android (.jucer)"
./scripts/check_jucer.sh

echo "▶ 1/5  Formatage (clang-format)"
mapfile -t SOURCES < <(find core dsp engine testing \( -name '*.cpp' -o -name '*.hpp' \))
"$CLANG_FORMAT" --dry-run --Werror "${SOURCES[@]}"

echo "▶ 2/5  Configuration + compilation (warnings = erreurs, sanitizers ON)"
cmake -S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Debug \
    -DVOICELIVE_ENABLE_SANITIZERS=ON -DVOICELIVE_WARNINGS_AS_ERRORS=ON >/dev/null
cmake --build "$BUILD_DIR" -j"$(nproc)"

echo "▶ 3/5  Tests unitaires (instrumentés ASan/UBSan)"
ctest --test-dir "$BUILD_DIR" --output-on-failure

echo "▶ 4/5  Build Release (attrape les warnings sensibles à l'optimisation)"
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release \
    -DVOICELIVE_WARNINGS_AS_ERRORS=ON >/dev/null
cmake --build build-release -j"$(nproc)"
ctest --test-dir build-release --output-on-failure

echo "▶ 5/5  Analyse statique (clang-tidy)"
find core dsp engine -name '*.cpp' -print0 | xargs -0 -I{} "$CLANG_TIDY" --quiet -p "$BUILD_DIR" {}

echo "✅ Toutes les portes sont vertes."
