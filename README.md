# VoiceLive Pro 🎸🎤

Application professionnelle de traitement audio temps réel avec looper 3 pistes, effets vocaux/guitare, métronome, accordeur chromatique et chaîne de mastering.

![License](https://img.shields.io/badge/license-MIT-blue.svg)
![Python](https://img.shields.io/badge/python-3.8%2B-blue)
![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey)

---

## 📋 Table des matières

- [Fonctionnalités](#-fonctionnalités)
- [Prérequis](#-prérequis)
- [Installation](#-installation)
- [Lancement](#-lancement)
- [Utilisation](#-utilisation)
- [Architecture](#-architecture)
- [Configuration](#-configuration)
- [Dépannage](#-dépannage)
- [Contribution](#-contribution)
- [Licence](#-licence)

---

## ✨ Fonctionnalités

### Looper 3 Pistes
- Enregistrement/playback/overdub indépendants
- Contrôles avancés : play, pause, stop, seek
- Visualisation waveform temps réel
- Auto-sync intelligent avec quantification (1x, 2x, 4x, 1/2, 1/4)
- Import audio WAV/FLAC
- Export mixage avec bouclage automatique
- Éditeur waveform avec découpage
- Contrôle global (play/pause/stop all)
- Gain et mute individuels

### Effets Audio
**Vocaux :**
- Harmony (2-4 voix)
- Reverb Pro (room size, damping)

**Guitare :**
- Wah-Wah
- Chorus

### Outils Musicaux
- **Métronome** : 40-300 BPM, tap tempo, signatures personnalisées, pré-count
- **Accordeur chromatique** : Mode guitare folk 6 cordes, détection pitch temps réel
- **Mastering Chain** : EQ 5 bandes, compresseur, limiteur

### Gestion Projet
- Sauvegarde/chargement projets
- Undo/Redo (20 actions)
- Auto-save (5 minutes)

---

## 🔧 Prérequis

- **Système d'exploitation** : Windows 10/11, Linux, macOS
- **Python** : 3.8 ou supérieur
- **RAM** : 4 GB minimum, 8 GB recommandé
- **Interface audio** : Carte son avec drivers ASIO (recommandé pour latence < 10ms)
- **Navigateur** : Chrome, Firefox, Edge (version récente)

---

## 📦 Installation

### 1. Installation Python

#### Windows
1. Téléchargez Python depuis [python.org](https://www.python.org/downloads/)
2. Lancez l'installateur
3. ⚠️ **IMPORTANT** : Cochez "Add Python to PATH"
4. Cliquez sur "Install Now"
5. Vérifiez l'installation :

```bash
python --version
pip --version
```

#### Linux (Ubuntu/Debian)

```bash
sudo apt update
sudo apt install python3 python3-pip python3-venv
python3 --version
```

#### macOS

```bash
# Avec Homebrew
brew install python3

# Vérification
python3 --version
```

---

### 2. Installation ASIO4ALL (Windows uniquement)

ASIO4ALL permet d'obtenir une latence audio ultra-faible (< 10ms).

1. Téléchargez ASIO4ALL depuis [asio4all.org](http://www.asio4all.org/)
2. Lancez l'installateur `ASIO4ALL_v2.15.exe`
3. Suivez l'assistant d'installation (options par défaut)
4. Redémarrez votre ordinateur

**Configuration ASIO4ALL :**
1. Ouvrez le panneau ASIO4ALL
2. Sélectionnez votre carte son
3. Réglez la taille du buffer : **128 samples** (compromis latence/stabilité)
4. Activez votre interface audio

> **Note** : Si vous avez une interface audio professionnelle (Focusrite, PreSonus, etc.), utilisez ses drivers ASIO natifs au lieu d'ASIO4ALL.

---

### 3. Installation des dépendances Python

#### Cloner le dépôt

```bash
git clone https://github.com/artkabis/voicelive_pro.git
cd voicelive_pro
```

#### Créer un environnement virtuel (recommandé)

```bash
# Windows
python -m venv venv
venv\Scripts\activate

# Linux/macOS
python3 -m venv venv
source venv/bin/activate
```

#### Installer les dépendances

Le projet contient un fichier `requirements.txt` avec toutes les dépendances nécessaires :

```bash
pip install -r requirements.txt
```

**Contenu de `requirements.txt` :**
```
flask==3.0.0
flask-socketio==5.3.5
flask-cors==4.0.0
eventlet==0.33.3
sounddevice==0.4.6
numpy==1.24.3
soundfile==0.12.1
scipy==1.11.3
python-socketio==5.10.0
```

#### Vérification de l'installation

```bash
python -c "import sounddevice; print(sounddevice.query_devices())"
```

Cette commande doit afficher la liste de vos périphériques audio.

---

## 🚀 Lancement

### Démarrage du serveur

```bash
# Assurez-vous d'être dans le dossier racine du projet
python run_web.py
```

**Sortie attendue :**
```
======================================================================
🎸 VOICELIVE PRO - SERVEUR WEB
======================================================================

✅ Serveur démarré sur: http://localhost:5000
   Mode: eventlet + sync master + export audio

======================================================================
```

### Accès à l'interface

1. Ouvrez votre navigateur
2. Accédez à : **http://localhost:5000**
3. L'interface VoiceLive Pro se charge

---

## 🎵 Utilisation

### Configuration initiale

1. **Sélection des périphériques audio** :
   - Input : Sélectionnez votre microphone/interface (préférez ASIO)
   - Output : Sélectionnez vos enceintes/casque (même type que l'input)
   - ⚠️ **Important** : Input et Output doivent utiliser le même type de driver (ASIO avec ASIO, ou Standard avec Standard)

2. **Démarrer le système audio** :
   - Cliquez sur "Démarrer le système audio"
   - Vérifiez que les indicateurs de niveau s'affichent

### Workflow d'enregistrement

#### Méthode 1 : Enregistrement pas à pas

**Piste 1 (base rythmique)** :
- Sélectionnez la piste 1 (clic dessus)
- Activez le métronome si besoin
- Cliquez "Record/Stop" (bouton rouge)
- Enregistrez votre boucle
- Re-cliquez "Record/Stop" pour terminer
- La piste devient automatiquement le "master"

**Pistes 2 et 3** :
- Sélectionnez la piste suivante
- Cliquez "Record/Stop"
- Enregistrez pendant que la piste 1 joue
- L'auto-sync alignera automatiquement la longueur

**Overdub** :
- Cliquez "Overdub" pour ajouter des couches sur une piste existante

#### Méthode 2 : Import audio

1. Cliquez "📁 Importer Audio" sur une piste
2. Sélectionnez un fichier WAV ou FLAC
3. Le fichier est automatiquement chargé et devient disponible

### Contrôles globaux

**Boutons de transport global** :
- ⏮️ **Restart** : Remet toutes les pistes au début et lance la lecture
- ▶️ **Play All** : Lance toutes les pistes (synchronisées si auto-sync activé)
- ⏸️ **Pause All** : Met en pause toutes les pistes
- ⏹️ **Stop All** : Arrête toutes les pistes

### Édition de piste

1. Cliquez "✂️ Éditer" sur une piste
2. **Éditeur waveform** s'ouvre :
   - Cliquez sur la waveform pour déplacer les marqueurs start/end
   - Ajustez précisément avec les champs numériques
   - Cliquez "▶️ Prévisualiser" pour écouter
   - Cliquez "✂️ Appliquer le découpage" pour valider

### Synchronisation

**Auto-Sync** :
- Active par défaut
- Quantifie automatiquement les pistes (1x, 2x, 4x, 1/2, 1/4 du master)
- Correction si écart < 10%

**Sync Now** :
- Aligne manuellement les positions de lecture de toutes les pistes

**Nudge** :
- Décale temporellement une piste (±10ms, ±50ms)

### Export

1. Configurez les options "Loop à l'export" sur chaque piste
   - ✅ Activé : La piste boucle pendant toute la durée du mix
   - ❌ Désactivé : La piste joue une seule fois
2. Cliquez "Exporter WAV" ou "Exporter FLAC"
3. Le fichier est téléchargé automatiquement
4. Normalisation automatique à -1dB

### Métronome

1. Activez le métronome (bouton ON)
2. Réglez le BPM (slider ou Tap Tempo)
3. Choisissez la signature (4/4, 3/4, etc.)
4. Configurez le pré-count si souhaité
5. Cliquez "Start" pour démarrer

### Accordeur

1. Activez l'accordeur (bouton ON)
2. Jouez une corde à vide de votre guitare
3. La corde détectée s'illumine en bleu
4. Ajustez jusqu'à ce qu'elle devienne verte (accordée ±5 cents)

### Effets et Mastering

- **Effets** : Activez/désactivez individuellement, ajustez les paramètres en temps réel
- **Mastering** : Active les modules EQ, Compresseur, Limiteur selon vos besoins

---

## 🏗️ Architecture

```
voicelive_pro/
├── src/
│   ├── audio/              # Moteur audio et traitement
│   │   ├── engine_optimized.py
│   │   ├── looper.py
│   │   ├── metronome.py
│   │   ├── tuner.py
│   │   ├── mastering_chain.py
│   │   ├── undo_redo.py
│   │   └── project_manager.py
│   ├── effects/            # Effets audio
│   │   ├── vocal/
│   │   └── guitar/
│   ├── web/               # Serveur Flask et frontend
│   │   ├── server.py
│   │   └── static/
│   │       └── index.html
│   └── utils/
│       └── logger.py
├── run_web.py            # Point d'entrée
├── requirements.txt      # Dépendances Python
└── README.md
```

**Technologies** :
- Backend : Python, Flask, SocketIO, sounddevice
- Frontend : React 18, Tailwind CSS, Socket.IO Client
- Communication : WebSockets temps réel

---

## ⚙️ Configuration

### Latence audio

Modifier le buffer size dans l'interface :
- **128 samples** : ~3ms (recommandé avec ASIO)
- **256 samples** : ~6ms (bon compromis)
- **512 samples** : ~12ms (stable, plus de latence)

### Ports

Par défaut, le serveur écoute sur :
- **Port** : 5000
- **URL** : http://localhost:5000

Pour changer le port, modifiez `run_web.py` :

```python
socketio.run(app, host='0.0.0.0', port=8080)
```

---

## 🔍 Dépannage

### Problème : "Aucun périphérique audio détecté"

**Solution** :

```bash
python -c "import sounddevice; print(sounddevice.query_devices())"
```

Si aucun device n'apparaît, réinstallez `sounddevice` :

```bash
pip uninstall sounddevice
pip install sounddevice --no-cache-dir
```

### Problème : Latence élevée (> 50ms)

**Solutions** :
1. Utilisez des drivers ASIO (Windows)
2. Réduisez le buffer size (128 samples)
3. Fermez les applications gourmandes en CPU
4. Désactivez les effets non utilisés

### Problème : Crackling/clipping audio

**Solutions** :
1. Augmentez le buffer size (256 ou 512)
2. Réduisez le nombre d'effets actifs
3. Baissez les gains individuels des pistes
4. Vérifiez la charge CPU (< 80%)

### Problème : "ASIO device not found"

**Solutions** :
1. Vérifiez que ASIO4ALL est installé et configuré
2. Redémarrez l'application
3. Dans ASIO4ALL, vérifiez que votre carte son est activée

### Problème : Pistes désynchronisées

**Solutions** :
1. Activez "Auto-Sync" dans le panneau de synchronisation
2. Utilisez "Sync Now" pour forcer l'alignement
3. Utilisez les boutons Nudge pour ajustements fins

---

## 🤝 Contribution

Les contributions sont les bienvenues !

1. Forkez le projet
2. Créez une branche (`git checkout -b feature/AmazingFeature`)
3. Committez vos changements (`git commit -m 'Add AmazingFeature'`)
4. Pushez vers la branche (`git push origin feature/AmazingFeature`)
5. Ouvrez une Pull Request

**Guidelines** :
- Code propre et commenté
- Tests unitaires si applicable
- Documentation des nouvelles fonctionnalités

---

## 📄 Licence

Distribué sous licence MIT. Voir `LICENSE` pour plus d'informations.

---

## 🙏 Remerciements

- [sounddevice](https://python-sounddevice.readthedocs.io/) - Interface audio Python
- [Flask-SocketIO](https://flask-socketio.readthedocs.io/) - WebSockets temps réel
- [React](https://react.dev/) - Interface utilisateur
- [Tailwind CSS](https://tailwindcss.com/) - Framework CSS

---

## 📧 Contact

Lien du projet : [https://github.com/artkabis/voicelive_pro](https://github.com/artkabis/voicelive_pro)

---

**Fait avec ❤️ pour les musiciens**