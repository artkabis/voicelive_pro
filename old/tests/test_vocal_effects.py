"""
Test des effets vocaux avancés
"""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent.parent))

from src.audio.engine import AudioEngine
from src.effects.vocal.harmony import Harmony
from src.effects.vocal.hardtune import HardTune
from src.effects.vocal.vocoder import Vocoder
from src.effects.vocal.choir import Choir
from src.effects.vocal.reverb_pro import ReverbPro
import sounddevice as sd
import time

def test_vocal_effects():
    print("=" * 70)
    print("🎤 TEST DES EFFETS VOCAUX AVANCÉS")
    print("=" * 70)
    
    # Lister devices
    print("\n🎤 Périphériques audio:")
    devices = sd.query_devices()
    for i, dev in enumerate(devices):
        if dev['max_input_channels'] > 0 or dev['max_output_channels'] > 0:
            print(f"  {i}: {dev['name']}")
    
    print("\n")
    try:
        input_dev = int(input("Device ENTRÉE (micro) : "))
        output_dev = int(input("Device SORTIE : "))
    except:
        input_dev = sd.default.device[0]
        output_dev = sd.default.device[1]
    
    # Créer le moteur
    engine = AudioEngine()
    
    # Menu de sélection d'effet
    print("\n" + "=" * 70)
    print("Choisissez l'effet à tester:")
    print("=" * 70)
    print("1. Harmony - Harmonies vocales (tierce + quinte)")
    print("2. HardTune - Auto-Tune robot")
    print("3. Vocoder - Voix robot extrême")
    print("4. Choir - Effet de choeur (8 voix)")
    print("5. ReverbPro - Grande salle")
    print("6. Combo - Harmony + Reverb")
    print("7. ALL - Tous les effets en série!")
    
    choice = input("\nVotre choix (1-7) : ").strip()
    
    if choice == "1":
        # Harmony
        harmony = Harmony()
        harmony.set_parameter('voices', 2)
        harmony.set_parameter('intervals', [4, 7])  # Tierce + Quinte
        harmony.set_parameter('mix', 0.5)
        harmony.enable()
        engine.add_effect(harmony)
        print("✅ Harmony activé : Tierce majeure + Quinte")
        
    elif choice == "2":
        # HardTune
        hardtune = HardTune()
        hardtune.set_parameter('correction', 1.0)  # Max correction
        hardtune.set_parameter('key', 'C')
        hardtune.set_parameter('scale', 'major')
        hardtune.enable()
        engine.add_effect(hardtune)
        print("✅ HardTune activé : Mode robot maximum")
        
    elif choice == "3":
        # Vocoder
        vocoder = Vocoder()
        vocoder.set_parameter('bands', 16)
        vocoder.set_parameter('carrier_type', 'saw')
        vocoder.set_parameter('carrier_freq', 220)
        vocoder.set_parameter('mix', 0.9)
        vocoder.enable()
        engine.add_effect(vocoder)
        print("✅ Vocoder activé : 16 bandes, son sawtooth")
        
    elif choice == "4":
        # Choir
        choir = Choir()
        choir.set_parameter('voices', 8)
        choir.set_parameter('spread', 0.02)
        choir.set_parameter('mix', 0.6)
        choir.enable()
        engine.add_effect(choir)
        print("✅ Choir activé : 8 voix")
        
    elif choice == "5":
        # Reverb Pro
        reverb = ReverbPro()
        reverb.set_parameter('room_size', 0.8)
        reverb.set_parameter('damping', 0.4)
        reverb.set_parameter('wet', 0.5)
        reverb.enable()
        engine.add_effect(reverb)
        print("✅ ReverbPro activé : Grande salle")
        
    elif choice == "6":
        # Combo Harmony + Reverb
        harmony = Harmony()
        harmony.set_parameter('mix', 0.4)
        harmony.enable()
        
        reverb = ReverbPro()
        reverb.set_parameter('room_size', 0.6)
        reverb.set_parameter('wet', 0.3)
        reverb.enable()
        
        engine.add_effect(harmony)
        engine.add_effect(reverb)
        print("✅ Combo : Harmony + Reverb")
        
    elif choice == "7":
        # TOUS LES EFFETS!
        hardtune = HardTune()
        hardtune.set_parameter('correction', 0.5)
        hardtune.enable()
        
        harmony = Harmony()
        harmony.set_parameter('mix', 0.3)
        harmony.enable()
        
        choir = Choir()
        choir.set_parameter('voices', 6)
        choir.set_parameter('mix', 0.4)
        choir.enable()
        
        reverb = ReverbPro()
        reverb.set_parameter('room_size', 0.7)
        reverb.set_parameter('wet', 0.4)
        reverb.enable()
        
        engine.add_effect(hardtune)
        engine.add_effect(harmony)
        engine.add_effect(choir)
        engine.add_effect(reverb)
        print("✅ TOUS les effets activés!")
    
    # Afficher la chaîne d'effets
    print("\n📋 Chaîne d'effets:")
    for i, effect in enumerate(engine.get_effects(), 1):
        print(f"   {i}. {effect.name}")
    
    # Démarrer
    print(f"\n🎙️ Démarrage...")
    engine.start(input_device=input_dev, output_device=output_dev)
    
    print("\n" + "=" * 70)
    print("🎤 CHANTEZ DANS LE MICRO ! (30 secondes)")
    print("=" * 70)
    print("Essayez de chanter des notes soutenues...")
    print("Ctrl+C pour arrêter\n")
    
    start_time = time.time()
    try:
        while time.time() - start_time < 30:
            inp = engine.get_input_level()
            out = engine.get_output_level()
            
            inp_bar = '█' * int(inp * 40)
            out_bar = '█' * int(out * 40)
            
            print(f"\rIN:  [{inp_bar:40}] {inp*100:5.1f}%  "
                  f"OUT: [{out_bar:40}] {out*100:5.1f}%", 
                  end='', flush=True)
            
            time.sleep(0.05)
    
    except KeyboardInterrupt:
        print("\n\n⏹️ Arrêt...")
    
    engine.stop()
    print("\n✅ Test terminé!")

if __name__ == "__main__":
    test_vocal_effects()