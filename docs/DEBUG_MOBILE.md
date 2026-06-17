# Méthodologie de diagnostic mobile

Sur mobile, l'app est une **boîte noire** : pas de console, crashes opaques. Ce
guide donne une méthode en 3 niveaux pour comprendre *ce qui ne va pas*, même
sans brancher l'appareil.

---

## Niveau 1 — Le panneau Diag (dans l'app)

Un panneau de diagnostic est affiché en bas de l'écran et rafraîchi 10×/s. Il
rend visible, **sans aucun outil**, l'essentiel :

```
VoiceLive Pro v2.0.0  |  JUCE 8.0.4
Build : Jun 17 2026 09:40:11
Audio : Oboe  48000 Hz / buffer 192
Moteur : 3 pistes, 1432 blocs, 0 cmd perdues, métronome OFF, master FX 1
  Piste 1 : Playing  gain 1.00
  Piste 2 : Empty  gain 1.00  [MUTE]
  Piste 3 : Empty  gain 1.00
```

### Lecture — symptôme → cause
| Ce que tu vois | Interprétation |
|---|---|
| `Audio : NON DÉMARRÉ` | L'audio n'a pas démarré → **permission micro refusée** ou périphérique indisponible. C'est la cause n°1 de « rien ne marche ». |
| `Build :` ancien | Tu testes une **vieille version** (l'APK n'a pas été réinstallé). |
| `blocs` qui n'augmente **pas** | Le callback audio ne tourne pas → audio non démarré / suspendu. |
| `cmd perdues > 0` | La file de commandes sature (UI trop rapide / audio bloqué). |
| Piste figée en `Recording` | Le `FinishRecording` n'est jamais arrivé (bug d'UI ou de flux). |
| `gain 0.00` / `[MUTE]` | Piste muette → c'est « normal », pas un bug. |

> Première chose à regarder quand « ça ne marche pas » : **la ligne `Audio :`**.

---

## Niveau 2 — Les compteurs du moteur (`engine.diagnostics()`)

Le moteur expose un instantané **testé unitairement** (`LooperEngine::Diagnostics`)
que le panneau affiche : `blocksProcessed`, `droppedCommands`, état par piste,
métronome, nombre d'effets de mastering. Ces compteurs sont la « télémétrie »
interne : ils distinguent un problème **audio** (blocs à 0) d'un problème **UI**
(commandes perdues) ou **métier** (état de piste inattendu).

---

## Niveau 3 — Outillage hors appareil (`adb`)

Quand le panneau ne suffit pas (crash au démarrage, gel) :

### Voir les logs en direct
JUCE écrit dans **logcat** sur Android. L'app trace l'init audio
(`VoiceLive: audio prêt …` ou `ERREUR fréquence …`).

```bash
# tout le flux, filtré sur l'app
adb logcat | grep -i "voicelive\|juce\|oboe\|AndroidRuntime"

# repartir propre puis reproduire le bug
adb logcat -c && adb logcat | tee bug.log
```
- `AndroidRuntime` / `FATAL EXCEPTION` → crash côté Java/JNI.
- `oboe` → problèmes du moteur audio bas niveau (latence, périphérique).
- `VoiceLive: ERREUR …` → nos propres traces.

### Symboliser un crash natif (C++)
Un crash natif apparaît dans logcat avec une pile d'adresses (`#00 pc …`). Pour
la traduire en fonctions :

```bash
# nécessite les .so NON strippés du build (dossier symbols de l'APK)
$ANDROID_NDK/ndk-stack -sym <chemin/vers/obj/local/arm64-v8a> -dump bug.log
```

### Installer l'APK de CI et vérifier la version
```bash
adb install -r VoiceLivePro-debug.apk
# la ligne "Build :" du panneau Diag doit correspondre au commit testé
```

---

## Checklist quand « ça ne marche pas »

1. **Panneau Diag → ligne `Audio :`** : démarré ? bonne fréquence ?
2. `blocs` augmente-t-il ? (sinon : audio suspendu / permission)
3. `cmd perdues` à 0 ? (sinon : saturation)
4. États de pistes cohérents avec ce que tu as fait ?
5. Sinon → `adb logcat` pendant la repro, chercher `FATAL`, `ERREUR`, `oboe`.
6. Crash natif → symboliser avec `ndk-stack`.

---

## Améliorations CI prévues
- Publier les **symboles natifs** (`.so` non strippés) en artefact pour
  symboliser les crashes de prod.
- Build **release signé** distinct du debug.
