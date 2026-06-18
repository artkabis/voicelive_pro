"""
Test de l'architecture complète
"""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent.parent))

from src.audio.engine import AudioEngine
from src.effects.vocal.doubling import Doubling
from src.effects.guitar.drive import Drive
import sounddevice as sd
import time

def test_engine():
    print("=" * 70)
    print("🎛️  TEST VOICELIVE PRO")
    print("=" * 70)
    
    print("\n🎤 Périphériques audio:")
    devices = sd.query_devices()
    for i, dev in enumerate(devices):
        if dev['max_input_channels'] > 0 or dev['max_output_channels'] > 0:
            print(f"  {i}: {dev['name']}")
    
    print("\n")
    try:
        input_dev = int(input("Device ENTRÉE : "))
        output_dev = int(input("Device SORTIE : "))
    except:
        input_dev = sd.default.device[0]
        output_dev = sd.default.device[1]
        print(f"Devices par défaut: {input_dev}, {output_dev}")
    
    engine = AudioEngine()
    
    doubling = Doubling()
    doubling.set_parameter('mix', 0.4)
    doubling.enable()
    
    drive = Drive()
    drive.set_parameter('drive', 0.3)
    drive.enable()
    
    engine.add_effect(doubling)
    engine.add_effect(drive)
    
    print("\n✅ Effets:")
    for effect in engine.get_effects():
        print(f"  - {effect.name}: {'ON' if effect.enabled else 'OFF'}")
    
    engine.start(input_device=input_dev, output_device=output_dev)
    
    print("\n🎸 PARLEZ OU JOUEZ (30 sec)")
    print("q - Quitter\n")
    
    start_time = time.time()
    try:
        while time.time() - start_time < 30:
            inp = engine.get_input_level()
            out = engine.get_output_level()
            print(f"\rIN: {'█' * int(inp * 50):50} OUT: {'█' * int(out * 50):50}", end='', flush=True)
            time.sleep(0.1)
    
    except KeyboardInterrupt:
        print("\n\n⏹️ Arrêt...")
    
    engine.stop()
    print("\n✅ Test terminé!")

if __name__ == "__main__":
    test_engine()
