# Conventions & barrières de qualité

Le but : **qu'aucune erreur ne puisse passer**. Les règles ci-dessous sont
appliquées automatiquement — elles ne reposent pas sur la discipline humaine.

## Portes automatiques

| Porte | Outil | Où | Effet si violée |
|-------|-------|-----|-----------------|
| Formatage | `clang-format` (`.clang-format`) | pre-commit + CI | commit/CI refusé |
| Analyse statique | `clang-tidy` (`.clang-tidy`) | CI | CI rouge |
| Warnings = erreurs | `-Werror -Wall -Wextra -Wpedantic -Wconversion -Wshadow …` | compilation | build échoue |
| Sanitizers | ASan + UBSan | CI + `check.sh` | tests rouges |
| Tests | `ctest` | CI + pre-push manuel | CI rouge |
| Secrets | `detect-private-key`, `check-added-large-files` | pre-commit | commit refusé |

Reproduire **toute** la CI en local : `./scripts/check.sh`.

## Style de code

- **C++20**, pas d'extensions compilateur (`-std=c++20`, pas `gnu++`).
- Indentation 4 espaces, colonnes ≤ 100 (imposé par `clang-format`).
- Nommage : `PascalCase` (types), `camelCase` (fonctions/méthodes/variables),
  `kPascalCase` (constantes), `snake_case_` (membres privés, suffixe `_`).
- Headers : `#pragma once`, en-tête SPDX en première ligne.
- Namespaces : tout sous `voicelive::…`.

## Règles de robustesse

- **Pas d'exceptions** dans le chemin métier : retourner `Result<T>` / `Status`.
- Tout retour d'erreur est `[[nodiscard]]` : impossible à ignorer silencieusement.
- **États invalides non représentables** : validation à la construction
  (fabriques `create`/`fromLinear`), invariants jamais revérifiés ensuite.
- Une transition métier interdite **ne mute pas l'état** et renvoie une erreur.

## Tests

- Tout nouveau comportement = nouveau test dans `<module>/tests/`.
- Couvrir le **chemin nominal ET les chemins d'échec** (transitions interdites,
  bornes, entrées invalides).
- Les tests doivent rester déterministes et sans dépendance externe.

## Git

- Développement sur la branche de refonte dédiée.
- Commits atomiques, message à l'impératif décrivant le « pourquoi ».
- Aucun artefact de build versionné (`build/` est ignoré).
