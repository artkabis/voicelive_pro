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

## Android / APK (prochaine étape)

JUCE compile le même code vers Android. Le pipeline cible :
1. CI dédiée (JDK + Android SDK/NDK + JUCE) compilant un **APK** publié en artefact.
2. APK **debug-signé** pour sideload (test direct sur appareil).
3. Signature **release** (keystore) pour une distribution Play Store — nécessite
   un compte Google Play Developer.

> L'APK ne peut pas être produit dans l'environnement de dev distant (pas de
> NDK) : il sera généré par la CI.
