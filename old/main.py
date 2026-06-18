"""
Point d'entrée principal de VoiceLive Pro
"""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))

from src.audio.engine import AudioEngine
from src.effects.vocal.doubling import Doubling
from src.effects.guitar.drive import Drive
from src.utils.config import Config
from src.utils.logger import logger

def main():
    print("🎸 VoiceLive Pro - Démarrage...")
    
    # Charger la configuration
    Config.load()
    
    # TODO: Lancer l'interface graphique
    # Pour l'instant, lancer le test
    from tests.test_architecture import test_engine
    test_engine()

if __name__ == "__main__":
    main()
