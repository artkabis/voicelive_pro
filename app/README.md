# Application VoiceLive Pro (couche `app/`)

Application desktop **JUCE** qui héberge le moteur temps réel (`voicelive::engine`)
et fournit l'interface. C'est la coquille « plateforme » : le cœur audio
(core/dsp/engine) reste indépendant et testé séparément.

## ⚠️ Build à part

Cette couche **n'est pas compilée par défaut** (et n'est pas construite dans
l'environnement de développement distant, qui n'a ni JUCE ni les libs système).
Elle s'active explicitement et récupère JUCE via le réseau.

## Construire le desktop

Prérequis Linux (Ubuntu/Debian) :

```bash
sudo apt-get install -y \
  libasound2-dev libjack-jackd2-dev libfreetype-dev libfontconfig1-dev \
  libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev \
  libxrandr-dev libxrender-dev libglu1-mesa-dev mesa-common-dev
```

> macOS / Windows : aucune dépendance système à installer, JUCE utilise les
> frameworks natifs.

Configuration et compilation :

```bash
cmake -S . -B build -DVOICELIVE_BUILD_APP=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build --target VoiceLiveApp -j
# Exécutable : build/app/VoiceLiveApp_artefacts/Release/VoiceLiveApp
```

JUCE (8.0.4) est téléchargé automatiquement par CMake (FetchContent).

## Ce que fait l'app aujourd'hui

- Ouvre les périphériques audio (2 in / 2 out) via JUCE.
- Convertit l'entrée stéréo → mono (`engine::channels`, testé), passe le bloc au
  `LooperEngine`, ré-étale le mono mono → stéréo en sortie.
- UI minimale : transport de la **piste 1** (Record/Play/Stop/Clear). Les boutons
  déposent des commandes dans la file lock-free ; le thread audio les applique.

## Architecture du pont

```
JUCE AudioAppComponent (MainComponent)
   getNextAudioBlock ──► channels::downmixToMono ──► LooperEngine::process
                                                        │
   sortie ◄── channels::spreadToChannels ◄─────────────┘
   boutons UI ──► LooperEngine::post(EngineCommand)  (lock-free)
```

Seul ce câblage dépend de JUCE ; la conversion de canaux et tout le moteur sont
testés sans JUCE.

## Android / APK

Le même code compile vers Android via le chemin officiel JUCE :
**Projucer exporte un projet Android Studio → Gradle compile l'APK.**

- Projet Projucer : [`VoiceLivePro.jucer`](../VoiceLivePro.jucer) (à la racine).
- CI : [`.github/workflows/android.yml`](../.github/workflows/android.yml) —
  installe JDK 17 + SDK/NDK, construit le Projucer, exporte `Builds/Android`,
  lance `./gradlew assembleDebug`, publie l'**APK debug** en artefact.
- Déclenchement : automatique (push touchant `app/`, le cœur ou le `.jucer`) ou
  manuel (`workflow_dispatch`).

Récupérer l'APK : onglet **Actions → run « Android APK » → artefact
`VoiceLivePro-debug-apk`**, puis sideload sur l'appareil (sources inconnues
activées).

> ⚠️ **Itération 1.** Le build Android dépend de versions précises (NDK, Gradle,
> build-tools) et d'options JUCE Android ; atteindre un APK vert demande en
> général quelques ajustements **sur le runner CI réel** (impossible à vérifier
> dans le sandbox de dev). La logique métier/DSP/conversion, elle, est testée.

### Vers la « production »
- L'APK ci-dessus est **debug-signé** → suffisant pour tester sur ton téléphone.
- Pour le Play Store : signature **release** (keystore) + compte Google Play
  Developer. Le keystore se branche en secret CI (`ANDROID_KEYSTORE_*`).
