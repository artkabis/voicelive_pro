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
| `app/`   | 🟢 en cours | App JUCE desktop **+ Android (APK CI)** : UI multipiste (édition, effets, EQ, spectre, accordeur, projet), détection casque (JNI Android) + anti-larsen, latence AAudio. Opt-in `-DVOICELIVE_BUILD_APP` |

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

## 6. Audio Android : mode bidirectionnel vs mode split (HAL)

Sur Android, JUCE pilote **Oboe**, qui ouvre une **session audio unique** liée à
**un seul HAL** matériel. Conséquence : on ne peut pas mélanger, dans la même
session, une **sortie** sur un HAL (casque USB) et une **entrée** sur un autre
HAL (micro intégré). Toute tentative de configuration « split » est rejetée
(`jassert juce_Oboe_android.cpp:517`, « Failed to create audio session »).

Deux modes sont donc exposés :

- **Mode apparié (par défaut)** — JUCE gère entrée + sortie sur le **même**
  périphérique (`setAudioChannels(2, 2)`). Le casque USB (in/out) ou le
  téléphone (micro + HP) fonctionnent ; un mélange est refusé par Oboe. Le
  sélecteur de périphériques apparie automatiquement la sortie USB à l'entrée
  USB (`applyDeviceSelection`).

- **Mode split** — sortie casque USB **+** micro intégré du téléphone. JUCE
  n'ouvre qu'un flux de **sortie** (`setAudioChannels(2, 0)`) ; l'entrée est
  capturée **en parallèle** par `app/src/AndroidMicCapture` via
  `android.media.AudioRecord` (JNI). C'est une API **indépendante d'Oboe** :
  deux HALs, deux sessions, aucun conflit. Le transfert capture → callback audio
  passe par `engine::SampleFifo` (SPSC lock-free). Cas d'usage : écouter une
  piste dans le casque sans la repisser dans la nouvelle prise micro.

Les contrats §5 restent respectés en mode split : le `SampleFifo` est lock-free
(contrat 2) et le callback audio ne fait que des `memcpy` bornés sans allocation
ni I/O (contrats 1 et 3) ; la capture `AudioRecord` (JNI, blocante) tourne sur
un **thread dédié**, jamais dans le callback.
