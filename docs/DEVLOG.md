# Devlog — VoiceLive Pro v2

Journal de développement de la refonte (Scénario B : cœur C++/JUCE,
desktop + mobile + web). Entrées en ordre antéchronologique.

---

## 2026-06-21 — Mode split micro (sortie USB + micro téléphone) & robustesse périphériques

### Mode split : casque USB en sortie + micro intégré en entrée (`app`, +1 classe)
Besoin studio : **écouter une piste dans le casque USB tout en enregistrant
depuis le micro du téléphone** (jouer la piste 1 dans le casque sans la
repisser dans la capture de la piste 2). Impossible avec JUCE/Oboe en mode
bidirectionnel : Android ouvre **une seule session audio sur un seul HAL** —
le casque USB et le micro intégré sont sur deux HALs distincts, d'où le
`jassert juce_Oboe_android.cpp:517` (« Failed to create audio session »).

Solution : **`AndroidMicCapture`** capture le micro intégré via
`android.media.AudioRecord` (JNI, `AUDIO_SOURCE_MIC` → toujours le micro
physique même casque USB branché), **indépendamment** de JUCE/Oboe. En mode
split : `setAudioChannels(2, 0)` → Oboe n'ouvre qu'un flux **de sortie** vers
le casque USB, sans conflit de HAL.
- Thread de capture (producteur) → `getNextAudioBlock` (consommateur) via une
  file SPSC lock-free **`SampleFifo`** (transfert par blocs, ~341 ms de marge).
- `prepareToPlay()` redémarre `AndroidMicCapture` à chaque changement de
  périphérique pour rester synchronisé sur le bon `sampleRate`.
- UI : combo « Mode micro » (apparié / split). En split, le combo d'entrée JUCE
  est désactivé (l'entrée vient d'`AudioRecord`).
- Anti-larsen conservé : `headphoneMonitor` détecte toujours le casque USB en
  sortie → monitoring actif pendant l'enregistrement.

### `SampleFifo` — file SPSC d'échantillons (`engine`, +8 tests)
Nouvelle primitive lock-free dans `engine` : transfert **par blocs** d'échantillons
float entre deux threads de cadences différentes (distincte de `RingBuffer<T>`
qui transporte un élément à la fois). Capacité puissance de deux (masquage au
lieu de modulo). **Primitive pure** : `write()/read()` renvoient le nombre
transféré, sans compteur interne — la politique drop (over-run) vs
contre-pression appartient à l'appelant. Tests : vide, FIFO, lecture/écriture
partielles, wraparound long, reset, **stress concurrent 2 threads / 2 M
échantillons** (ordre FIFO vérifié, zéro corruption).

### Garde-fous & diagnostic
- `AndroidMicCapture` : anti double-`start()` (pas de thread orphelin), borne
  `sampleRate` ∈ [4 k, 192 k], compteurs **drop** (consommateur en retard) et
  **underrun** (producteur en retard → silence), loggés à l'arrêt et affichés
  en continu dans le panneau diagnostic en mode split.
- Correctif CI : `AndroidMicCapture.cpp` ajouté à `VoiceLivePro.jucer` (le build
  APK lit ses sources du `.jucer`, pas de `app/CMakeLists.txt` → sinon symboles
  non définis au link de `libjuce_jni.so`).
- Pairing USB anti-split rendu **indépendant du côté modifié** : la sortie USB
  est appariée à l'entrée USB même quand seul le combo d'entrée a changé (cas du
  nom résolu « SM-A266B built-in microphone »).

---

## 2026-06-21 — Effets guitare, transport global, sélecteur de périphériques & audit RT

### Effets guitare (`dsp`, +5 effets, +21 tests)
Cinq effets temps réel sur l'interface `Effect` (process() sans allocation,
`noexcept`), pensés pour la guitare électrique :
- **`Distortion`** : mise en forme d'onde 3 modes (soft-clip `tanh`, hard-clip,
  fuzz asymétrique) + drive, tone (passe-bas un pôle), level, mix.
- **`NoiseGate`** : porte de bruit (détecteur d'enveloppe + seuil dB, attaque/
  relâche) ; activable, supprime souffle/larsen entre les notes.
- **`Tremolo`** : modulation d'amplitude par LFO sinusoïdal (rate/depth/mix).
- **`Phaser`** : cascade de 6 passe-tout du premier ordre balayés + feedback.
- **`Flanger`** : retard court (1–7 ms) modulé + feedback + lecture fractionnaire.

Câblés dans `dsp/CMakeLists.txt`, `VoiceLivePro.jucer` **et** l'UI (panneau FX
par piste, 5 rangées). La **chaîne live a été réordonnée en signal guitare
classique** : `gate → distorsion → wah → phaser → flanger → chorus → tremolo →
delay → reverb` (le gate coupe le souffle *avant* la disto, la reverb en dernier).
Le rendu offline (`renderMix`) applique la **même séquence**. Réordonnancement
sûr : les états on/off d'effets ne sont pas persistés.

Piège : les effets dont le `process()` ne dépend que de `sampleRate_` doivent
défaut à **0** (et non `kStudio`) pour que la garde « passthrough avant
prepare() » fonctionne (3 tests rouges au départ, corrigés).

### Transport global (`app`)
Trois boutons au-dessus des pistes : **▶ Toutes** (stop des pistes en cours puis
play de toutes celles ayant du contenu — démarrage **synchronisé** car toutes les
commandes sont vidées dans le même `while(commands_.pop())` → ~1 bloc), **⏸ Pause**
(stop des pistes en lecture, **playhead conservé** car `TrackProcessor::stop()` ne
le réinitialise pas) et **⏹ Stop**.

Correctif CI : `pauseAllTracks` appelait `LooperTrack::isPlaying()` (inexistant —
c'est une méthode du `TrackProcessor`) → erreur de compilation Android, remplacée
par un test de `state()`.

### Sélecteur de périphériques & diagnostic casque (`app`)
- **Sélecteur** entrée/sortie : `OboeAudioIODeviceType` énumère déjà les
  périphériques physiques (JNI) et route via `setDeviceId()`. L'UI liste les noms
  exacts et applique le choix via `setAudioDeviceSetup()`. `scanForDevices()` étant
  vide en amont, un patch CI (`android.yml`) le rend ré-énumérant pour le hotplug.
- **Diagnostic** : `HeadphoneMonitor` logge désormais **tous** les types de
  périphériques (`types=[…]`, empaquetés dans un `atomic<int64>`), pas seulement le
  premier. Constante corrigée : `TYPE_AUX_LINE = 19` (et non 21 = `TYPE_IP`).

### Formes d'onde interactives (`app`)
Zoom par boutons `[+]`/`[−]` (fenêtre `[viewStart_, viewEnd_]`, cache de crêtes
invalidé au changement) et **double-clic pour déplacer la tête de lecture**
(nouvelle commande `EngineCommand::Action::Seek` → `TrackProcessor::setPlayhead`).
Note : `mouseMagnify()` (pinch) n'est **pas** implémenté par le backend Android de
JUCE 8.x → les boutons explicites sont la solution fiable sur mobile.

### Audit d'optimisation temps réel
Vérification du chemin audio : `LoopAudio` (mémoire pré-allouée, 30 s/piste),
`RingBuffer` SPSC lock-free, `TrackProcessor`/`LooperEngine`/`EffectChain`/effets —
**aucune allocation ni verrou dans le callback**, confirmé. Mémoire bornée
(~46 Mo pour 8 pistes). Seul point ajusté : `refreshDeviceList()` (ré-énumération
JNI) passait à 1 Hz dans le `timerCallback` ; ramené à **~0,33 Hz** (la détection
casque, qui pilote l'anti-larsen, reste à 1 Hz) pour alléger le thread message.

### Vérifié
- **178 tests** verts (core 41, dsp 70, engine 67), build warnings-as-errors
  propre, `clang-format` conforme.
- UI validée par le build **APK Android** (non compilable en local sans NDK/JUCE).

---

## 2026-06-19 — Correctifs sur appareil : enregistrement & détection USB-C

Tests sur appareil réel (APK) : deux régressions visibles, corrigées.

### 1. Enregistrement cassé (piste restait `Empty`)
- **Cause** : le constructeur forçait
  `setAudioDeviceSetup(bufferSize=256, treatAsChosen=true)` juste après
  `setAudioChannels(2,2)`. Sur Android, cela **rouvrait le périphérique en sortie
  seule** (device affiché `System Default (Output)`, **micro perdu** → plus rien à
  enregistrer) et déclenchait un **second `prepareToPlay`** (visible : double
  `audio pret` dans le journal). La taille 256 était de toute façon **ignorée**
  (buffer resté à 1920).
- **Fix** : suppression du bloc. Oboe négocie déjà le buffer basse latence ; la
  latence reste gérée par Oboe + flags Release `-O3 -ffast-math`.

### 2. Casque USB-C jamais détecté
- **Cause** : **mauvaise constante** `AudioDeviceInfo` — `TYPE_USB_HEADSET`
  vaut **22**, pas 8 (8 = `TYPE_BLUETOOTH_A2DP`). Un casque USB-C qui se déclare
  en type 22 n'était donc jamais reconnu.
- **Fix** : constantes corrigées (3, 4, 8, 11, 12, **22**, 26, 27) couvrant
  jack, USB, Bluetooth et BLE.

### Diagnostic ajouté
- `HeadphoneMonitor::diagnostic()` expose dans le **panneau Diag** le résultat de
  la dernière sonde JNI : code (`pas de contexte app` / `pas d'AudioManager` /
  `getDevices KO` / `ok`), nombre de sorties énumérées, et le 1ᵉʳ type rencontré.
  Un échec silencieux (contexte null, etc.) est désormais visible sur l'appareil
  sans débogueur.

---

## 2026-06-19 — Détection casque USB-C (Android), latence & durcissement CI

### Contexte
Sur Android, le voyant casque restait rouge et l'audio était coupé même avec un
casque USB-C fonctionnel (anti-larsen : le haut-parleur est coupé pendant
l'enregistrement *sauf* si un casque est détecté). Cause racine : **JUCE expose
toujours `"Android Audio"`** comme nom de périphérique quel que soit le matériel
physique branché — l'heuristique par chaîne (`engine::looksLikeHeadphones`) ne
pouvait donc jamais matcher sur mobile.

### Livré — détection casque
- **`app/HeadphoneMonitor`** : sur Android, interroge directement
  `AudioManager.getDevices(GET_DEVICES_OUTPUTS)` **via JNI** et cherche les types
  matériels réels — `TYPE_WIRED_HEADSET(3)`, `TYPE_WIRED_HEADPHONES(4)`,
  `TYPE_USB_HEADSET(8)`, `TYPE_USB_DEVICE(11)`, `TYPE_USB_ACCESSORY(12)`. Le
  chemin desktop conserve l'heuristique `engine::looksLikeHeadphones` (USB /
  headset / wired) sur le nom/type JUCE.
- **Sondage au Timer** : `poll()` est appelé à chaque tick (10 Hz) en plus du
  `ChangeListener`, car le hotplug USB-C n'est pas toujours notifié à
  l'`AudioDeviceManager`. Le voyant passe au vert < 1 s après le branchement.
- L'état est exposé via un `std::atomic<bool>` lu sans verrou dans
  `getNextAudioBlock` (anti-larsen).

### Livré — latence Android
- **Buffer 256 frames** demandé sur Android après `setAudioChannels` pour engager
  le chemin basse latence AAudio (~5 ms à 48 kHz), uniquement si le buffer par
  défaut est plus grand (évite un redémarrage inutile du périphérique).
- **Flags d'optimisation** : `.jucer` Release en `optimisation="3"` +
  `cppFlags="-ffast-math -ftree-vectorize"` (c'est ce que l'APK utilise
  réellement) ; blocs NEON/`-march=armv8-a` ajoutés dans `engine`/`dsp`
  `CMakeLists.txt` pour le chemin de cross-compilation CMake-Android.

### Durcissement JNI & audit mémoire
- Audit fuites mémoire sur tout le dépôt : `core/`, `dsp/`, `engine/` et le GUI
  sont **propres** (RAII, zéro allocation dans le callback audio, `Timer` arrêté
  et `ChangeListener` retiré au destructeur, logger débranché). Le **seul** risque
  était le code JNI nouvellement écrit.
- Correctifs JNI : `clearPendingException()` après chaque `Call*Method` (tout
  appel JNI avec une exception Java pendante est un comportement indéfini) et
  `DeleteLocalRef` sur **toutes** les sorties (pas d'accumulation de références
  locales si la boucle sur les périphériques lève).

### Pièges rencontrés (à connaître)
- **`.jucer` ≠ `CMakeLists.txt`** : un nouveau `app/src/*.cpp` doit être ajouté
  **dans les deux** — `app/CMakeLists.txt` (desktop) **et** `VoiceLivePro.jucer`
  (`grpApp`, build Android). Oublier le `.jucer` → erreur de **lien Android
  uniquement** (`undefined symbol`), invisible sur desktop.
- **`juce::getEnv()`** : déclaré dans un header natif interne de JUCE, non exposé
  par les en-têtes publics. Forward-déclaré — mais **au scope global**, pas dans
  `namespace voicelive::app` (sinon il crée un `voicelive::app::juce` qui masque
  le vrai `::juce` et casse tout le fichier).
- **`Rectangle::removeFromLeft`** est non-const : sur un `const Rectangle`, le NDK
  (clang strict) refuse — utiliser `withWidth`/`withHeight`.

### Durcissement CI (`.github/workflows/android.yml`)
La vraie erreur clang/ninja était noyée dans ~150 lignes de stacktrace Gradle.
Ajouté :
- `tee` de la sortie Gradle dans `gradle-build.log` (pipefail conservé) ;
- en cas d'échec, **extraction ciblée** des seuls signaux utiles
  (`error:` / `FAILED:` / `undefined symbol` / `ninja stopped`) + queues des logs
  `.cxx` ;
- **artefact `android-debug-logs`** (log Gradle, logs `.cxx`, manifeste,
  CMakeLists généré) téléchargeable sur échec ;
- liste systématique des sources C++ référencées par le projet généré (détecte un
  `.cpp` absent du `.jucer` avant le linker).

### Vérifié
- **CI (core)** : 8/8 verts (build, tests, ASan/UBSan, clang-tidy, clang-format).
- **Android APK** : build complet vert, **APK publié** en artefact.
- Détection casque/anti-larsen à confirmer sur appareil réel avec le nouvel APK.

---

## 2026-06-16 — `core::Error` rendu RT-safe (caveat levé)

### Changement
- `core::Error::message` passe de `std::string` à **`std::string_view`** sur des
  littéraux statiques. Produire une erreur n'alloue plus rien.
- `LooperTrack` : les messages de transition refusée, auparavant construits par
  concaténation (`std::string`), sont désormais des **littéraux statiques** par
  transition (plus précis, et sans allocation).
- Conséquence : le drain de la file de commandes dans `LooperEngine::process()`
  est maintenant **100 % sans allocation**, y compris pour une commande invalide.
  Le caveat temps réel documenté précédemment est levé.

### Vérifié
- Build + ASan/UBSan, `clang-format`, `clang-tidy` : verts.
- Tests inchangés (63), + assertion que le message d'erreur statique est bien
  renseigné. Tous les appels à `failure(...)` passent des littéraux (lifetime
  statique garanti pour la `string_view`).

---

## 2026-06-16 — Assemblage moteur : `LooperEngine` + pont `Project`

### Livré
- **`LooperEngine`** : assemblage de haut niveau du moteur temps réel.
  - Possède N `TrackProcessor` + le `Transport` partagé (réutilise `core`, pas
    de duplication de transport) + un nom + la piste sélectionnée.
  - **File de commandes lock-free** (`RingBuffer<EngineCommand>`) : l'UI dépose
    des commandes via `post()` (sans verrou) ; `process()` les draine côté thread
    audio avant de rendre. Commande invalide = no-op sûr (l'état refuse sans muter).
  - **Contrôle synchrone validé** (renvoie `Status`) pour les usages hors temps
    réel et les tests : record/finish/play/stop/overdub/clear/gain/mute/select.
  - **`process()`** : rend le mix complet (somme des pistes + limiteur) en une
    passe, buffer de rendu pré-alloué, sans allocation.
- **Réconciliation avec `core::Project`** : le moteur est l'autorité d'exécution,
  `Project` est le modèle de **réglages persistés**. `exportSettings()` produit
  un `Project` (nom, transport, gain/mute, sélection) ; `applySettings()` recharge
  ces réglages dans le moteur. Round-trip testé.

### Tests (+7, total projet : 63, 100 % verts)
- Validation du nombre de pistes, rejet d'index invalide.
- Cycle record→lecture, contrôle via la **file lock-free**, mixage de 2 pistes.
- Export/Import des réglages `LooperEngine` ↔ `Project`.

### Notes de conception
- `LooperEngine` est volontairement **non déplaçable** (contient des atomics) :
  un moteur audio ne se déplace pas une fois en service.
- Caveat temps réel résiduel : une transition *invalide* drainée sur le thread
  audio construit un message d'erreur `std::string` (allocation). Suivi : rendre
  `core::Error` RT-safe (message en `std::string_view` sur littéraux statiques)
  pour un chemin de commande 100 % sans allocation.

### Prochaines étapes
1. Rendre `core::Error` RT-safe (lever le caveat ci-dessus).
2. `dsp/` : 2e effet (delay/wah) + insertion d'une chaîne d'effets par piste
   dans `TrackProcessor`.
3. Intégration JUCE (desktop) puis cible WASM (web).

---

## 2026-06-17 — Étape 13 : effets Chorus & Wah (priorité n°5)

### Livré
- **`dsp::Chorus`** : ligne de retard courte (~15 ms) modulée par un LFO, avec
  lecture fractionnaire (interpolation linéaire) ; rate/depth/mix bornés.
- **`dsp::Wah`** : filtre state-variable (Chamberlin) passe-bande dont la
  fréquence centrale est balayée par un LFO (auto-wah) ; coefficient borné pour
  la stabilité ; fréquences/résonance/mix bornés.
- Tous deux sur l'interface `Effect` → insérables dans n'importe quelle
  `EffectChain` (démonstration de la modularité : aucun autre module modifié).
- +8 tests (total : 112) : passthrough mix=0, silence, sortie finie/stable,
  paramètres clampés.
- `Chorus.cpp` / `Wah.cpp` déclarés dans le `.jucer` (garde-fou vert).

### Note portabilité
- `-Wdouble-promotion` attrapé localement (float `ratio` × `double pi`) — casté
  explicitement, comme on le ferait pour la libc++ Android.

---

## 2026-06-17 — Étape 18 : permission micro + diagnostic copiable

### Problème rapporté
App « non fonctionnelle » sur mobile : impossible d'enregistrer, boutons sans
effet ; pas de moyen de récupérer les erreurs.

### Cause probable
La **permission micro n'était pas déclarée** (`androidMicNeeded` absent du
`.jucer`) → aucun accès à l'entrée audio → le moteur ne démarre pas → les
boutons (qui dépendent du moteur préparé) ne déclenchent rien.

### Livré
- **`.jucer` : `androidMicNeeded="1"`** → permission `RECORD_AUDIO` dans le
  manifeste, et **demande explicite au lancement** (`RuntimePermissions`), tracée.
- **Journal d'erreurs copiable** : `AppLogger` capture tous les messages JUCE
  (+ les nôtres) ; le panneau Diag les affiche, et un bouton **« Copier le
  diagnostic »** met tout dans le presse-papier (à coller pour debug).
- **Trace de chaque clic** (`Clic piste N : Record …`, + « FILE PLEINE » si la
  file sature) → distingue « UI morte » de « audio mort ».
- Trace de l'init audio et du résultat de la permission.

### Méthode de test mobile
1. Installer le nouvel APK, **accorder la permission micro** au lancement.
2. Tester (Rec/Play/Stop, métronome, EQ…).
3. En cas de souci : **« Copier le diagnostic »** → coller le texte ici.

---

## 2026-06-17 — Étape 17 : observabilité mobile (diagnostic)

### Problème
Sur mobile, l'app est une boîte noire (pas de console) → impossible de
comprendre ce qui ne va pas.

### Livré
- **`LooperEngine::diagnostics()`** : instantané d'état (fréquence, pistes,
  blocs traités, **commandes perdues** si la file sature, métronome, nb d'effets
  de mastering). Compteurs `std::atomic` alimentés par `process()`/`post()`.
  +2 tests (total : 133).
- **Panneau Diag dans l'app** : affiche en bas de l'écran, rafraîchi 10×/s,
  l'état audio (démarré ? fréquence/buffer), les compteurs moteur et l'état de
  chaque piste + version/build. Rend l'app **observable sans console**.
- **Trace d'init audio** via `juce::Logger` → visible dans `adb logcat`.
- **`docs/DEBUG_MOBILE.md`** : méthodologie en 3 niveaux (panneau Diag, compteurs
  moteur, `adb logcat` + symbolication `ndk-stack`) + checklist symptôme→cause.

---

## 2026-06-17 — Étape 16 : UI multipiste JUCE (priorité n°8)

### Livré (`app/MainComponent`)
- **UI multipiste** (3 pistes) : Rec/Play/Stop/Clear + slider de gain + Mute par
  piste, via la **file lock-free** du moteur (RT-safe).
- **Transport** : bouton métronome + slider BPM.
- **Mastering** : égaliseur 3 bandes (sliders Grave/Médium/Aigu) inséré dans le
  bus master.
- **Accordeur** : label rafraîchi par un `Timer` (10 Hz) lisant une fenêtre
  d'analyse alimentée par le callback audio → `engine.tune` → note + cents.

### Notes
- L'UI n'est **pas testable hors CI** (pas de JUCE dans l'environnement de dev) :
  validée par les builds CI **desktop** et **APK Android**.
- Réglages continus (BPM, EQ, métronome) écrits directement (course bénigne sur
  scalaires) ; actions de piste via la file lock-free. Toggle d'effets par piste
  à chaud = itération suivante (nécessite un bypass RT-safe).

---

## 2026-06-17 — Étape 15 : câblage mastering + accordeur dans le moteur

### Livré
- **Bus de mastering** : `LooperEngine::masterEffects()` expose une `EffectChain`
  appliquée au **mix complet** (après pistes + métronome, avant le limiteur) —
  on y insère compresseur/égaliseur.
- **Accordeur intégré** : `LooperEngine::tune(input)` → `std::optional<Note>`
  (détecte la fondamentale de l'entrée et la mappe sur une note).
- +2 tests (total : 131) : l'égaliseur de mastering amplifie le mix ;
  l'accordeur détecte le La joué en entrée.

### Décision technique
- Avant l'UI (non testable hors CI), on rend le **moteur complet et testé** :
  tous les modules (effets par piste, mastering, métronome, accordeur, import/
  export) sont désormais accessibles via des API de haut niveau unit-testées.
  L'UI ne sera plus qu'une fine couche de présentation.

---

## 2026-06-17 — Étape 14 : mastering (compresseur + égaliseur)

### Livré (priorité fonctionnelle n°7)
- **`dsp::Compressor`** : compresseur de dynamique (détecteur de crête,
  attaque/relâche, ratio, seuil, makeup).
- **`dsp::Equalizer`** : 3 bandes (low shelf / mid peak / high shelf) en biquads
  RBJ ; gains nuls = passthrough exact (filtres identité).
- Tous deux dérivent de `dsp::Effect` → composables en chaîne de mastering via
  `EffectChain`.
- +8 tests (total : 129) : silence, sous/au-dessus du seuil, passthrough plat,
  boost grave, sortie finie, paramètres clampés.

### Prochaine priorité
8. UI (exposer multipiste + effets), puis câblage des modules dans l'app.

---

## 2026-06-17 — Étape 13 : accordeur + 🎉 APK Android vert

### 🎉 APK Android
- Le dernier verrou (`std::from_chars` flottant absent de la libc++ du NDK,
  corrigé en `strtod`) levé → **le build Android est VERT** : l'**APK debug
  signé** (~31 Mo) est publié en artefact CI (`VoiceLivePro-debug-apk`),
  téléchargeable et sideloadable.

### Livré (priorité fonctionnelle n°6 : accordeur)
- **`core::music`** : conversion fréquence → note (12-TET, A4 = 440 Hz) + écart
  en cents, nom de note et octave. Pur, sans dépendance.
- **`dsp::PitchDetector`** : estimation de la fondamentale par fonction de
  différence carrée normalisée (NSDF / McLeod) + interpolation parabolique.
- +9 tests (total : 121) : conversion (La4, Do4, cents, fréquence invalide),
  détection (La 440, Mi grave 82 Hz, silence), mapping hauteur → note.

### Effets (rappel #5, déjà livré) : Chorus + Wah.

### Prochaine priorité
7. Mastering (EQ / compresseur). Puis #8 : UI.

---

## 2026-06-17 — Étape 12 : export du mix → WAV

### Livré
- **`LooperEngine::renderMix(frames)`** : rendu offline du mix (mono) depuis le
  début des boucles, **métronome exclu**, en réutilisant le chemin temps réel
  `process()` par blocs. **Préserve l'état de lecture** (t, têtes de lecture
  sauvegardées/restaurées) → déterministe et non destructif.
- **`exportMixToFile(path, frames)`** : rendu + écriture WAV.
- `TrackProcessor::setPlayhead` (pour sauvegarde/restauration).
- +2 tests (total : 104) : export 2 pistes → WAV relu (mix correct), rendu
  déterministe et état préservé.

La boucle import/export audio est complète : **charger un sample** et
**exporter le mix**.

---

## 2026-06-17 — Étape 11 : câblage import WAV → moteur + APK Android signé

### Livré
- **`TrackProcessor::loadContent`** : charge un buffer mono dans une piste et la
  passe en lecture (Empty → Recording → Playing en interne).
- **`LooperEngine::importTrack`** : downmixe un `wav::AudioData` (entrelacé,
  mono/stéréo) en mono et le charge dans une piste ; devient la référence
  maître s'il n'y en a pas. **`importTrackFromFile`** : lit un WAV puis importe.
- +5 tests (total : 102) : import mono, downmix stéréo, index invalide, import
  fichier, fichier absent.

### CI Android — dernier verrou levé
- Le C++ cross-compile pour `arm64-v8a` ✅, mais Gradle échouait à la
  **signature** : `~/.android/debug.keystore` absent du runner. Ajout d'une
  étape qui génère le keystore debug standard avant `assembleDebug`.

### Reste
- Export du mix (rendu offline → WAV). Puis #5 (chorus/wah).

---

## 2026-06-16 — Étape 10 : import / export audio WAV

### Livré (priorité fonctionnelle n°4)
- **`engine::wav`** : lecture/écriture de fichiers WAV (RIFF/WAVE) en C++ pur.
  - `write` : PCM 16 bits. `read` : PCM 16 bits **et** IEEE float 32 bits,
    mono/multicanal.
  - **Parseur borné et validé** (les fichiers importés = surface d'attaque,
    cf. SECURITY.md) : en-tête, chunks, tailles → `Result`/`Status`, jamais d'UB.
  - `AudioData` : échantillons entrelacés + fréquence + canaux.
- +6 tests (total : 97) : round-trip mono, stéréo, `frameCount`, paramètres
  invalides, fichier absent, données non-WAV rejetées.
- `WavFile.cpp` déclaré dans le `.jucer` (garde-fou vert).

### Reste à câbler
- Import d'un WAV dans une piste de looper ; export du mix (rendu offline du
  moteur → `AudioData` → `write`).

---

## 2026-06-16 — Étape 9 : sauvegarde / chargement de projet

### Livré (priorité fonctionnelle n°3)
- **`core::project_io`** : sérialisation des **réglages** d'un projet (nom,
  transport, gains/mute, sélection) en **format texte versionné**
  (`VOICELIVE_PROJECT v1`), sans dépendance externe (cœur portable natif+WASM).
  - `serialize` / `deserialize` (chaîne) + `saveToFile` / `loadFromFile`.
  - Parsing robuste (`std::from_chars`, sans exceptions), validation stricte :
    en-tête, champs requis, cohérence du nombre de pistes → `Result`/`Status`.
  - Les données audio (boucles) ne sont pas incluses (rôle de l'export audio).
- +6 tests (total : 91) : round-trip chaîne **et** fichier, en-tête invalide,
  champ manquant, nombre de pistes incohérent, fichier absent.
- `ProjectSerializer.cpp` déclaré dans le `.jucer` (garde-fou vert).

### Prochaine priorité
4. Import / export audio (WAV).

---

## 2026-06-16 — Étape 8 : synchronisation des boucles + garde-fou .jucer

### Livré (priorité fonctionnelle n°2)
- **`LoopAudio`** : période de boucle (`loopLength`) dissociée du contenu
  enregistré ; `setLoopLength()` permet d'aligner/compléter au silence.
- **`LooperEngine`** : la 1ʳᵉ boucle enregistrée devient la **référence
  (maître)** ; les pistes suivantes sont **alignées sur un multiple musical**
  (¼/½/1×/2×/4× via `Transport::chooseLoopMultiple`) au `FinishRecording`.
  Accès `masterLoopLength()`.
- +3 tests (total : 85).

### Garde-fou anti-friction (CI Android)
- Le build Android (Projucer) énumère les sources à la main → un fichier oublié
  = `undefined symbol` au link (cas vécu avec `Metronome.cpp`).
- Ajout de `scripts/check_jucer.sh` (échoue si une source `*/src/*.cpp` n'est
  pas déclarée dans le `.jucer`), branché dans `check.sh` **et** en job CI rapide
  (`jucer-sources`). Plus aucune dérive silencieuse possible.

### CI Android (progression)
- `gui_extra` déclaré → Projucer exporte, Gradle + NDK **cross-compilent pour
  arm64-v8a** ✅. Restait le link → corrigé par l'ajout de `Metronome.cpp` au
  `.jucer`.

---

## 2026-06-16 — Étape 7 : métronome (priorité fonctionnelle n°1)

### Livré
- **`engine/Metronome`** : générateur de clics temps réel piloté par le
  `Transport`. Clic à chaque temps, **accent sur le 1er temps** de la mesure,
  enveloppe exponentielle ; tout pré-dimensionné en `prepare()`, `process()`
  sans allocation (ajoute les clics au bloc → se mixe avec les pistes).
- Intégré à **`LooperEngine`** : `setMetronomeEnabled` / `setMetronomeGain`,
  mixé dans `process()` avant le limiteur.

### Tests (+5, total : 82)
- Désactivé → silence ; clic au 1er temps ; clics espacés d'un temps (silence
  en milieu de temps) ; **accent du 1er temps > temps normal** ; intégration
  moteur (clic présent sans aucune piste).

### CI Android (en parallèle)
- Itérations successives via les logs : X11 → langage C → chemins de modules →
  dépendances. `--fix-missing-dependencies` n'existe pas en Projucer 8.0.4 →
  remplacé par la déclaration explicite du module `juce_gui_extra` dans le
  `.jucer`.

### Prochaines priorités
2. Synchronisation des boucles (piste maître + alignement).
3. Sauvegarde/chargement de projet (sérialisation disque).

---

## 2026-06-16 — Étape 6 : pipeline CI Android (APK)

### Livré
- **`VoiceLivePro.jucer`** : projet Projucer (guiapp, C++20) regroupant les
  sources core/dsp/engine/app + modules JUCE + exporter Android Studio
  (minSDK 24, targetSDK 34) avec les chemins d'en-têtes du cœur.
- **`.github/workflows/android.yml`** : CI Android par le chemin officiel JUCE —
  JDK 17 + SDK/NDK, build du Projucer, `--resave` du `.jucer` → `Builds/Android`,
  `gradlew assembleDebug`, publication de l'**APK debug** en artefact.
- Doc `app/README.md` : récupération de l'APK, sideload, et signature release.

### Honnêteté / limites
- Ce pipeline est une **itération 1** : non vérifiable dans le sandbox (pas de
  NDK/JUCE). Les builds JUCE Android demandent typiquement quelques ajustements
  de versions (NDK/Gradle/build-tools) **sur le runner réel**. La boucle vers un
  APK vert nécessite de lire les logs CI — d'où l'intérêt d'autoriser l'accès
  GitHub pour itérer rapidement.

### Prochaines étapes
1. Faire tourner la CI Android, lire les logs, corriger jusqu'au vert (APK).
2. Signature release (keystore en secret CI) pour distribution.
3. UI multi-pistes + effets pilotés en temps réel.

---

## 2026-06-16 — Étape 5 : couche application JUCE (desktop)

### Livré
- **`engine/ChannelUtils`** (testé, sans JUCE) : `downmixToMono` et
  `spreadToChannels` — conversion buffers multicanaux ↔ mono du moteur. Isolé
  ici pour que le câblage JUCE reste une fine couche non testée.
- **`app/` (JUCE)** : `MainComponent` (= `AudioAppComponent`) qui ouvre l'audio
  (2 in/2 out), convertit stéréo→mono, appelle `LooperEngine::process`,
  ré-étale en sortie ; UI minimale de transport (piste 1) déposant des commandes
  dans la file lock-free. `Main.cpp` : fenêtre/application JUCE.
- **CMake** : option `VOICELIVE_BUILD_APP` (OFF par défaut) ; l'app récupère
  JUCE 8.0.4 via FetchContent. Le build du cœur reste autonome et inchangé.
- **CI** : job `app-desktop` (Linux) compilant `VoiceLiveApp` et publiant
  l'exécutable en artefact.
- Docs : `app/README.md` (build desktop + plan Android/APK).

### Tests (+5 ChannelUtils, total projet : 77, 100 % verts)
- ChannelUtils : downmix moyenne/silence, spread multi-canaux, canaux nuls,
  aller-retour mono↔stéréo.

### Important — limites de l'environnement
- La couche `app/` **n'est pas compilée ici** (pas de JUCE ni de libs système
  dans le sandbox distant) : elle est vérifiée par la CI. La logique vérifiable
  (conversion de canaux + moteur) est, elle, testée à 100 %.

### Prochaines étapes
1. CI Android : JDK + SDK/NDK + JUCE → **APK** en artefact (debug-signé pour
   sideload), puis signature release.
2. Insertion d'effets via la file de commandes (effets pilotés en RT).
3. UI : contrôles multi-pistes, niveaux, sélection d'effets.

---

## 2026-06-16 — Étape 4 : chaîne d'effets par piste + 2ᵉ effet

### Livré
- **`dsp/EffectChain`** : chaîne d'effets ordonnée. Allocation à l'ajout et
  dans `prepare()` ; `process()` enchaîne les effets sans allocation. Un effet
  ajouté après `prepare()` est préparé automatiquement.
- **`dsp/Delay`** : 2ᵉ effet (écho à ligne de retard), C++ pur. Ligne
  dimensionnée une fois dans `prepare()`, `process()` sans allocation.
  Paramètres bornés : délai, réinjection (feedback), mix dry/wet.
- **Intégration `engine/TrackProcessor`** : chaîne d'effets par piste, appliquée
  à la lecture (après lecture de la boucle, avant gain/mute). `prepare()` prend
  désormais la fréquence + la taille de bloc max (surcharge de commodité
  conservée).
- **`engine/LooperEngine`** : accesseur `effectsForTrack(i)` pour insérer des
  effets dans une piste (hors temps réel). Le moteur lie désormais `dsp`.

### Tests (+9, total projet : 72, 100 % verts)
- Delay : passthrough mix=0, **retard d'impulsion vérifié**, paramètres clampés.
- EffectChain : passthrough à vide, effet nul ignoré, effet appliqué.
- TrackProcessor : **chaîne d'effets appliquée en lecture** (impulsion retardée).
- LooperEngine : chaîne d'effets par piste, index borné.

### Prochaines étapes
1. Couche `app/` : intégration JUCE (desktop d'abord) — projet CMake + JUCE,
   pont moteur ↔ I/O audio (`AudioDeviceManager`) et UI minimale.
2. Pipeline CI Android (JUCE + NDK) produisant un **APK en artefact** téléchargeable.
3. 3ᵉ effet (wah/chorus) et insertion d'effets via la file de commandes RT.

---

## 2026-06-16 — Étape 3 : squelette du moteur temps réel (`engine/`)

### Livré (nouveau module `engine/`, dépend uniquement de `core`)
- **`RingBuffer<T>`** : file SPSC lock-free (canal UI → thread audio sans
  verrou) — application directe du contrat « aucun verrou en temps réel ».
- **`LoopAudio`** : stockage mono de boucle à capacité fixe ; mémoire allouée
  une seule fois dans `prepare()`, opérations temps réel (`append`/`readLooped`/
  `overdub`) sans allocation.
- **`Mixer`** : sommation pondérée (`addScaled`) + limiteur dur (`limit`),
  fonctions `noexcept` sans allocation.
- **`TrackProcessor`** : relie l'état métier (`core::LooperTrack`) aux données
  audio + tête de lecture. Les transitions encapsulent état ET audio pour
  garantir leur cohérence ; `process()` rend un bloc selon l'état
  (enregistrement / lecture bouclée / overdub / silence).

### Tests (20 nouveaux, total projet : 56, 100 % verts)
- RingBuffer : FIFO, plein/vide, wraparound.
- LoopAudio : capacité bornée, lecture bouclée, overdub, clear.
- Mixer : accumulation, mixage multi-sources, limiteur.
- TrackProcessor (intégration) : capture, lecture bouclée, gain/mute, overdub,
  stop, et **mixage de deux pistes** de bout en bout.

### Outillage
- `scripts/check.sh` et la CI couvrent désormais `core` + `dsp` + `engine`
  (format + clang-tidy), au lieu de `core` seul.

### Note d'architecture
- `engine::TrackProcessor` possède aujourd'hui sa propre `LooperTrack`. La
  réconciliation avec `core::Project` (qui modélise aussi les pistes) sera
  traitée lors de l'assemblage du moteur de haut niveau.

### Prochaines étapes
1. Assemblage moteur : `LooperEngine` (N `TrackProcessor` + `Transport` partagé,
   file de commandes via `RingBuffer`) et réconciliation avec `Project`.
2. `dsp/` : 2e effet (delay/wah) sur l'interface `Effect`, insérable par piste.
3. Intégration JUCE (desktop) puis cible WASM (web).

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
