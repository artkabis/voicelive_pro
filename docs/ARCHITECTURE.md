# Architecture — VoiceLive Pro v2

> Refonte « Scénario B » : cœur compilé C++, cible **desktop + mobile + web**.

## 1. Principe directeur

Une seule règle gouverne toute l'architecture :

> **La logique métier et le DSP sont du C++ pur, sans aucune dépendance à
> l'interface ni au système. Ils compilent à l'identique en natif et en
> WebAssembly, et sont testables à 100 % en isolation.**

La couche temps réel ne doit jamais être à la merci d'un ramasse-miettes, d'un
verrou ou d'une allocation imprévisible (les pièges de la v1 Python). En la
gardant compilée et pure, on garantit à la fois la **latence**, la
**portabilité** (desktop/mobile/web) et la **testabilité**.

## 2. Couches

```
┌───────────────────────────────────────────────────────────────┐
│  app/      UI & I/O audio                                      │
│            • natif  : JUCE (Windows/macOS/Linux/iOS/Android)   │
│            • web    : AudioWorklet + WebAssembly               │
├───────────────────────────────────────────────────────────────┤
│  engine/   Orchestration temps réel (transport, mix, routing) │
│            sans allocation/lock dans le callback audio         │
├───────────────────────────────────────────────────────────────┤
│  dsp/      Effets (reverb, wah, chorus…) — C++ pur, SIMD       │
├───────────────────────────────────────────────────────────────┤
│  core/  ◀── IMPLÉMENTÉ — logique métier pure, zéro dépendance  │
│            machine à états du looper, types forts, projet      │
└───────────────────────────────────────────────────────────────┘
```

Chaque couche ne dépend **que** des couches inférieures. `core/` ne dépend de
rien — c'est ce qui le rend compilable en WASM et testable sans matériel audio.

## 3. État d'avancement

| Couche   | État        | Contenu                                              |
|----------|-------------|------------------------------------------------------|
| `core/`  | 🟢 en cours | `LooperTrack`, `Transport` (tempo/quantif.), `Project`, types forts, `Result/Status` |
| `dsp/`   | 🟢 en cours | `Effect` (contrat RT) · `Reverb` · `Delay` · `EffectChain` (chaîne par piste) |
| `engine/`| 🟢 en cours | `LooperEngine` (N pistes + transport + file de commandes + mix), `RingBuffer`, `LoopAudio`, `Mixer`, `TrackProcessor` |
| `app/`   | 🟡 amorcé   | App JUCE desktop (pont moteur ↔ audio + UI mini), opt-in `-DVOICELIVE_BUILD_APP` |

## 4. Choix techniques

- **Langage** : C++20 (portable GCC/Clang/MSVC, et Emscripten pour le web).
- **Build** : CMake ≥ 3.24, hors-source obligatoire.
- **Tests** : micro-framework header-only sans dépendance (migration Catch2
  triviale ultérieurement). Voir `core/tests/testing.hpp`.
- **Gestion d'erreur** : `Result<T>` / `Status` explicites, **sans exceptions**
  dans le chemin métier. `[[nodiscard]]` interdit d'ignorer une erreur.
- **Mobile** : assuré par JUCE (iOS/Android) au-dessus du même `core`.
- **Web** : le `core`/`dsp` compilés en WebAssembly, pilotés par un AudioWorklet.

## 5. Contrats non négociables du temps réel (à respecter dès `engine/`)

1. Aucune allocation mémoire dans le callback audio.
2. Aucun verrou bloquant dans le callback (structures lock-free).
3. Aucune I/O (fichier, log, réseau) dans le callback.
4. États invalides non représentables (cf. `LooperTrack`, types forts).
