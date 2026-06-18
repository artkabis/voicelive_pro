"""
Configuration globale de l'application VoiceLive Pro
"""
import json
from pathlib import Path

class Config:
    # Chemins
    BASE_DIR = Path(__file__).parent.parent.parent
    DATA_DIR = BASE_DIR / "data"
    PRESETS_DIR = DATA_DIR / "presets"
    IMPULSES_DIR = DATA_DIR / "impulses"
    
    # Audio
    SAMPLE_RATE = 44100
    BUFFER_SIZE = 128
    CHANNELS = 2
    INPUT_DEVICE = None
    OUTPUT_DEVICE = None
    
    # Looper
    MAX_LOOP_DURATION = 480  # 8 minutes
    NUM_TRACKS = 3
    MAX_PRESETS = 50
    
    # MIDI
    MIDI_CHANNEL = 1
    MIDI_DEVICE = None
    
    # Interface
    WINDOW_WIDTH = 1200
    WINDOW_HEIGHT = 800
    
    @classmethod
    def load(cls, config_file="config.json"):
        config_path = cls.BASE_DIR / config_file
        if config_path.exists():
            try:
                with open(config_path, 'r') as f:
                    data = json.load(f)
                    for key, value in data.items():
                        if hasattr(cls, key):
                            setattr(cls, key, value)
                print(f"✅ Configuration chargée")
            except Exception as e:
                print(f"⚠️ Erreur config: {e}")
    
    @classmethod
    def save(cls, config_file="config.json"):
        config_path = cls.BASE_DIR / config_file
        data = {
            'SAMPLE_RATE': cls.SAMPLE_RATE,
            'BUFFER_SIZE': cls.BUFFER_SIZE,
            'CHANNELS': cls.CHANNELS,
            'INPUT_DEVICE': cls.INPUT_DEVICE,
            'OUTPUT_DEVICE': cls.OUTPUT_DEVICE,
            'MAX_LOOP_DURATION': cls.MAX_LOOP_DURATION,
            'NUM_TRACKS': cls.NUM_TRACKS,
            'MAX_PRESETS': cls.MAX_PRESETS,
            'MIDI_CHANNEL': cls.MIDI_CHANNEL,
            'MIDI_DEVICE': cls.MIDI_DEVICE
        }
        try:
            with open(config_path, 'w') as f:
                json.dump(data, f, indent=4)
            print(f"✅ Configuration sauvegardée")
        except Exception as e:
            print(f"❌ Erreur sauvegarde: {e}")
    
    @classmethod
    def setup_directories(cls):
        cls.PRESETS_DIR.mkdir(parents=True, exist_ok=True)
        cls.IMPULSES_DIR.mkdir(parents=True, exist_ok=True)

Config.setup_directories()
