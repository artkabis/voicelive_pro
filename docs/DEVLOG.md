# Devlog — VoiceLive Pro v2

Journal de développement de la refonte (Scénario B : cœur C++/JUCE,
desktop + mobile + web). Entrées en ordre antéchronologique.

---

## 2026-06-16 — Étapes 1 & 2 : transport, projet, premier effet DSP

### Livré
- **`core/` — Transport musical** (`Transport.hpp/.cpp`) :
  - `Bpm` (clampé 20–999) et `TimeSignature` (dénominateur puissance de deux).
  - Conversions temps↔échantillons (`samplesPerBeat`/`samplesPerBar`).
  - Quantification : `quantizeToGrid` (au temps/à la mesure) et
    `chooseLoopMultiple` (alignement ¼/½/1×/2×/4× sur une boucle de référence —
    reprend la logique d'auto-sync de la v1, désormais pure et testée).
- **`core/` — Modèle de projet** (`Project.hpp`) : agrège transport + N pistes
  (1–8) bornées, piste sélectionnée gardée, accès aux pistes borné (jamais d'UB).
- **`dsp/` — nouveau module** :
  - `Effect` : interface au **contrat temps réel strict** (alloc en `prepare`,
    `process` `noexcept` sans allocation).
  - `Reverb` : Freeverb (8 combs + 4 allpass) en C++ pur, buffers alloués une
    fois, échelonnés à la fréquence d'échantillonnage.
- **Framework de test mutualisé** : déplacé en lib partagée
  `voicelive::testing` (+ macro `CHECK_NEAR` pour le DSP).
- `Result<T>` : ajout d'un accès mutable par référence (`value() &`) — évite les
  copies inutiles de la valeur transportée.

### Tests (36 au total, 100 % verts)
- Transport : conversions, tempo, quantification, multiples de boucle.
- Project : création validée, bornes, sélection gardée, indépendance des pistes.
- Reverb : passthrough dry pur, silence→silence, paramètres clampés,
  **réponse impulsionnelle décroissante** (queue stable, jamais de NaN/Inf),
  `reset` efface la queue.

### Vérifié localement (`scripts/check.sh`)
- Build g++ + sanitizers ASan/UBSan : OK.
- `ctest` : 2 suites, 36/36 cas verts.
- `clang-format` + `clang-tidy` strict : 0 erreur.

### Prochaines étapes
1. `engine/` : squelette de boucle audio temps réel (mixage N pistes, ring
   buffer lock-free) respectant les 4 contrats RT, testable sans matériel.
2. `dsp/` : 2e effet (delay/wah) sur la même interface `Effect`.
3. Intégration JUCE (desktop) puis cible WASM (web).

---

## 2026-06-16 — Amorçage de la fondation v2

### Décisions
- **Scénario B** retenu : cœur compilé, l'audio temps réel quitte Python.
- **Stack** : C++20 / JUCE. **Cibles** : desktop + mobile + web (WASM).
- Architecture en couches `core → dsp → engine → app`, `core` sans dépendance
  (compilable natif **et** WebAssembly, testable à 100 %). Voir `ARCHITECTURE.md`.
- La v1 Python reste en place comme référence fonctionnelle (non supprimée).

### Pourquoi quitter le DSP Python (rappel de l'audit)
- Effets écrits en boucles Python échantillon-par-échantillon dans le callback
  temps réel → saturation du thread audio, craquements.
- GIL → pas de DSP multicœur. Allocations/verrous dans le callback.
- `pedalboard` (C++) présent mais inutilisé. Mobile impossible en Python.

### Livré
- Structure du dépôt v2 + build CMake (≥ 3.24, hors-source imposé).
- Module `core/` :
  - `Result<T>` / `Status` / `Error` — gestion d'erreur explicite, `[[nodiscard]]`.
  - `Gain`, `SampleRate` — types forts à invariants (états invalides
    non représentables).
  - `LooperTrack` — machine à états finie gardée (Empty/Recording/Playing/
    Overdubbing/Stopped) ; toute transition interdite est rejetée sans muter
    l'état.
- **Tests** : 16 cas couvrant transitions valides **et** interdites + bornes.
  `100% tests passed`.
- **Barrières** : `-Werror` + mur de warnings, ASan/UBSan, `clang-format`,
  `clang-tidy` strict (config assouplie pour le code de test uniquement),
  pre-commit, CI GitHub (format / build g++ & clang / sanitizers / clang-tidy),
  script `scripts/check.sh` reproduisant la CI en local.
- Docs : `ARCHITECTURE.md`, `CONVENTIONS.md`, `SECURITY.md`, ce devlog.

### Vérifié localement
- `cmake --build` : OK (g++ 13, warnings-as-errors).
- `ctest` : 1/1 suite, 16/16 cas verts.
- `clang-format --dry-run --Werror` : conforme.
- `clang-tidy` (production + tests) : 0 erreur.

### Prochaines étapes
1. `core/` : modèle de **projet** + **transport** (BPM, signature, quantif.) avec tests.
2. `dsp/` : premier effet (reverb) en C++ pur + bancs de test (réponse impulsionnelle).
3. `engine/` : squelette de boucle audio temps réel respectant les contrats RT.
4. Intégration JUCE (desktop d'abord) puis cible WASM.
