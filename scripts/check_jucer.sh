#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
#
# Garde-fou anti-friction : vérifie que CHAQUE source C++ de core/dsp/engine est
# déclarée dans VoiceLivePro.jucer (le build Android énumère les sources à la
# main). Évite les erreurs « undefined symbol » au link Android lors de l'ajout
# d'un fichier.
#   Usage : ./scripts/check_jucer.sh
set -euo pipefail
cd "$(dirname "$0")/.."

JUCER="VoiceLivePro.jucer"
missing=0

while IFS= read -r src; do
    if ! grep -q "file=\"${src}\"" "$JUCER"; then
        echo "❌ Source absente de ${JUCER} : ${src}"
        missing=1
    fi
done < <(find core dsp engine -path '*/src/*' -name '*.cpp' | sort)

if [[ "$missing" -ne 0 ]]; then
    echo "→ Ajoute le(s) fichier(s) ci-dessus dans le <MAINGROUP> de ${JUCER}."
    exit 1
fi
echo "✅ Toutes les sources core/dsp/engine sont déclarées dans ${JUCER}."
