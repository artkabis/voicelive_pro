"""
Lancement rapide du serveur web VoiceLive Pro
"""
import sys
from pathlib import Path

# Ajouter le chemin du projet
project_root = Path(__file__).parent
sys.path.insert(0, str(project_root))

# Importer et lancer le serveur
from src.web.server import main

if __name__ == '__main__':
    main()