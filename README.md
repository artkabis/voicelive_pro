# VoiceLive Pro 🎸🎤

**Moteur audio temps réel multiplateforme** (looper multipiste, effets, mixage)
bâti sur un **cœur C++ pur, testé et portable** — desktop, mobile (Android) et,
à terme, web (WebAssembly).

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Language](https://img.shields.io/badge/C%2B%2B-20-blue)
![Tests](https://img.shields.io/badge/tests-77%20✓-success)
![Platform](https://img.shields.io/badge/platform-desktop%20%7C%20Android%20%7C%20web-lightgrey)

> ℹ️ **v2 — refonte C++.** Ce README décrit l'architecture refactorisée. La v1
> Python (looper en temps réel via Python) est conservée comme **référence
> fonctionnelle** (voir [Historique v1](#-historique--v1-python)). La raison de
> la refonte : le DSP temps réel en Python (boucles échantillon par échantillon,
> GIL, allocations dans le callback) plafonnait la latence/qualité et fermait la
> porte du mobile. Le cœur compilé lève ces verrous.

---

## 📐 Architecture

Une règle gouverne tout : **la logique métier et le DSP sont du C++ pur, sans
dépendance à l'UI ni au système.** Ils compilent à l'identique en natif et en
WebAssembly, et sont testables à 100 % en isolation.

```
┌──────────────────────────────────────────────────────────────┐
│ app/      UI + I/O audio (JUCE desktop/mobile, shell web WASM) │
├──────────────────────────────────────────────────────────────┤
│ engine/   Temps réel : mixage, boucles, file de commandes      │
│           lock-free, processeurs de piste                      │
├──────────────────────────────────────────────────────────────┤
│ dsp/      Effets temps réel (Reverb, Delay, chaîne d'effets)   │
├──────────────────────────────────────────────────────────────┤
│ core/     Logique métier pure, zéro dépendance                 │
└──────────────────────────────────────────────────────────────┘
```

Chaque couche ne dépend **que** des couches inférieures. Détails :
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

| Couche    | État        | Tests |
|-----------|-------------|-------|
| `core/`   | 🟢 en cours | 30    |
| `dsp/`    | 🟢 en cours | 13    |
| `engine/` | 🟢 en cours | 34    |
| `app/`    | 🟡 amorcé (JUCE) | via CI |

---

## ✨ Fonctionnalités disponibles

### 🔁 Looper / pistes (`core`, `engine`)
- **Machine à états de piste** stricte (Empty → Recording → Playing →
  Overdubbing → Stopped) : impossible d'atteindre un état incohérent.
- **Multipiste** (1 à 8 pistes) avec mixage et limiteur de sortie.
- **Synchronisation** : la 1ʳᵉ boucle définit la référence (maître), les
  suivantes sont alignées sur un multiple musical (¼/½/1×/2×/4×).
- **Enregistrement, lecture en boucle, overdub** (superposition de couches).
- **Stockage de boucle à capacité fixe** (zéro allocation en temps réel).
- **Gain & mute** par piste ; piste sélectionnée.

### ⏱️ Transport musical (`core`)
- **Tempo (BPM)** borné, **signature rythmique** validée.
- Conversions **temps musical ↔ échantillons** (samples/temps, samples/mesure).
- **Quantification** au temps ou à la mesure.
- **Alignement de boucle** sur multiples musicaux (¼ / ½ / 1× / 2× / 4×).
- **Métronome** (`engine`) : clic temps réel, accent sur le 1er temps, mixé à la
  sortie ; activable/désactivable, gain réglable.

### 🎚️ Effets (`dsp`)
- **Reverb** (algorithme Freeverb : 8 combs + 4 allpass).
- **Delay** (écho à ligne de retard : délai, feedback, mix).
- **Chaîne d'effets par piste** (`EffectChain`) : effets ordonnés, insérables à
  chaud, interface `Effect` commune à contrat temps réel strict.

### 🧩 Moteur temps réel (`engine`)
- **`LooperEngine`** : assemble N pistes + transport + mixage.
- **File de commandes lock-free** (`RingBuffer` SPSC) UI → thread audio.
- **Pont de réglages** avec `core::Project` (export/import : nom, transport,
  gains/mute, sélection).
- **Sauvegarde / chargement de projet** (`core::project_io`) : sérialisation des
  réglages en fichier texte versionné (`serialize`/`deserialize`/`saveToFile`/
  `loadFromFile`).
- **Conversion de canaux** stéréo ↔ mono testée (`ChannelUtils`).
- **Import / export WAV** (`engine::wav`) : lecture (PCM 16 bits / float 32 bits)
  et écriture (PCM 16 bits), parseur borné et validé.

### 🖥️ Application (`app`, JUCE)
- App desktop : ouverture audio (2 in / 2 out), pont moteur, **UI de transport**
  (Record/Play/Stop/Clear), commandes via la file lock-free.
- Pipelines CI : **binaire desktop** et **APK Android (debug)** en artefacts.

### 🛡️ Robustesse transverse
- Gestion d'erreur **explicite sans exceptions** (`Result<T>` / `Status`,
  `[[nodiscard]]` : impossible d'ignorer une erreur).
- **Types forts à invariants** (`Gain`, `SampleRate`…) : états invalides non
  représentables.
- **Contrats temps réel** : aucune allocation / verrou / I/O dans le callback.

---

## 🧱 Modularité & extension

L'ajout de fonctionnalités est volontairement **à faible friction et testé** :

| Pour ajouter… | On touche à… | Effort |
|---------------|--------------|--------|
| un **effet** | un fichier `dsp/` + son test | ~30 min |
| une **commande** UI→audio | `EngineCommand::Action` + `applyCommand` | ~10 min |
| un **type métier** | un fichier `core/` + son test | isolé |

**Exemple — ajouter un effet :**
1. Créer `dsp/include/voicelive/dsp/MonEffet.hpp` + `dsp/src/MonEffet.cpp`
   héritant de `voicelive::dsp::Effect` (`prepare` / `process` / `reset`).
2. L'ajouter à `dsp/CMakeLists.txt`.
3. Écrire `dsp/tests/test_mon_effet.cpp`.
4. Il est immédiatement insérable :
   `engine.effectsForTrack(i)->add(std::make_unique<MonEffet>());` — sans
   modifier `engine` ni `app`.

Chaque ajout passe par les mêmes **barrières automatiques** (ci-dessous) : une
régression ou une erreur d'intégration est attrapée avant le merge.

Conventions complètes : [`docs/CONVENTIONS.md`](docs/CONVENTIONS.md).

---

## 🛡️ Barrières de qualité

| Porte | Outil | Où |
|-------|-------|-----|
| Tests unitaires (**77**, chemins nominaux **et** d'erreur) | `ctest` | local + CI |
| Build **Debug + Release**, 2 compilateurs | g++ / clang | CI |
| Warnings = erreurs | `-Werror -Wall -Wextra -Wconversion …` | compilation |
| Mémoire / UB | **ASan + UBSan** | local + CI |
| Formatage | `clang-format` | pre-commit + CI |
| Analyse statique | `clang-tidy` (strict) | CI |
| Secrets / gros fichiers | pre-commit hooks | commit |

Tout reproduire en local : `./scripts/check.sh`.

---

## 🚀 Construire & tester

### Cœur (core + dsp + engine) — partout, sans dépendance

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

Prérequis : CMake ≥ 3.24, un compilateur C++20 (GCC 13+/Clang 16+).

### Application desktop (JUCE)

Voir [`app/README.md`](app/README.md). JUCE est récupéré automatiquement
(FetchContent) ; l'app est **opt-in** :

```bash
cmake -S . -B build -DVOICELIVE_BUILD_APP=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --target VoiceLiveApp -j
```

### APK Android

Généré par la CI ([`.github/workflows/android.yml`](.github/workflows/android.yml)) :
**Actions → « Android APK » → artefact `VoiceLivePro-debug-apk`**, puis sideload.
Le build Android ne se fait pas en local sans NDK ; détails dans
[`app/README.md`](app/README.md).

---

## 🗺️ Roadmap

- [ ] Stabiliser le pipeline APK Android (en cours).
- [ ] 3ᵉ effet (chorus / wah) + insertion d'effets pilotée en temps réel.
- [ ] UI multipiste complète (niveaux, sélection d'effets).
- [ ] Sauvegarde/chargement de projet (sérialisation de `core::Project`).
- [ ] Cible **web** (cœur en WebAssembly + AudioWorklet).
- [ ] Signature release Android (Play Store).

---

## 📁 Structure du dépôt

```
voicelive_pro/
├── core/        # logique métier pure (LooperTrack, Transport, Project…)
├── dsp/         # effets (Effect, Reverb, Delay, EffectChain)
├── engine/      # temps réel (RingBuffer, LoopAudio, Mixer, LooperEngine…)
├── app/         # application JUCE (desktop/mobile)
├── testing/     # micro-framework de test partagé
├── cmake/       # modules CMake (warnings, sanitizers)
├── docs/        # ARCHITECTURE, CONVENTIONS, DEVLOG
├── .github/     # CI (cœur, desktop, Android)
├── scripts/     # check.sh (reproduit la CI en local)
├── VoiceLivePro.jucer  # projet Projucer (export Android)
└── src/, gui_app.py…   # v1 Python (legacy, référence)
```

---

## 🕰️ Historique — v1 (Python)

La première version (looper 3 pistes, effets, métronome, accordeur, mastering)
était écrite en **Python** (Flask/SocketIO + sounddevice). Elle reste dans le
dépôt (`src/`, `gui_app.py`, `run_web.py`) comme **référence fonctionnelle**,
mais n'est plus la cible de développement — la v2 C++ la remplace pour la
latence, la qualité DSP et le support mobile. Le journal de la refonte est dans
[`docs/DEVLOG.md`](docs/DEVLOG.md).

---

## 📄 Licence

Distribué sous licence **MIT**. Voir [`LICENSE`](LICENSE).

**Fait avec ❤️ pour les musiciens.**
