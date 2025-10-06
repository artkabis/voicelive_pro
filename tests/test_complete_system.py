"""
Test du système complet avec Looper + Presets - VERSION CORRIGÉE
"""
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent.parent))

from src.audio.engine_optimized import AudioEngineOptimized
from src.audio.looper_effect import LooperEffect  # Import du wrapper
from src.effects.vocal.harmony import Harmony
from src.effects.vocal.reverb_pro import ReverbPro
from src.effects.guitar.wah import Wah
from src.effects.guitar.chorus import Chorus
from src.presets.manager import PresetManager
import sounddevice as sd
import time

def test_complete_system():
    print("=" * 70)
    print("🎸 TEST SYSTÈME COMPLET - VOICELIVE PRO")
    print("=" * 70)
    
    # Configuration
    print("\n🎤 Devices:")
    devices = sd.query_devices()
    for i, dev in enumerate(devices):
        if "ASIO" in dev['name']:
            print(f"  {i}: {dev['name']} ⭐")
        elif dev['max_input_channels'] > 0 or dev['max_output_channels'] > 0:
            print(f"  {i}: {dev['name']}")
    
    asio_device = 14  # Ton device ASIO
    buffer_size = 128
    
    print(f"\n✅ Utilisation de ASIO4ALL (Device {asio_device})")
    print(f"   Buffer: {buffer_size} samples")
    print(f"   Latence estimée: ~13ms")
    
    # Créer le moteur optimisé
    engine = AudioEngineOptimized(sample_rate=44100, buffer_size=buffer_size)
    
    # Créer le looper comme un effet
    looper_effect = LooperEffect(sample_rate=44100, max_duration=480)
    looper = looper_effect.get_looper()  # Récupérer l'instance du looper
    
    # Créer des effets
    harmony = Harmony()
    harmony.set_parameter('voices', 2)
    harmony.set_parameter('mix', 0.4)
    
    reverb = ReverbPro()
    reverb.set_parameter('room_size', 0.6)
    reverb.set_parameter('wet', 0.3)
    
    wah = Wah()
    wah.set_parameter('frequency', 800)
    wah.set_parameter('auto_wah', True)
    
    chorus = Chorus()
    chorus.set_parameter('rate', 0.5)
    chorus.set_parameter('mix', 0.4)
    
    # Gestionnaire de presets
    preset_manager = PresetManager()
    
    # Menu
    print("\n" + "=" * 70)
    print("MENU DE TEST")
    print("=" * 70)
    print("1. Test Vocal (Harmony + Reverb + Looper)")
    print("2. Test Guitare (Wah + Chorus + Looper)")
    print("3. Test Looper seul")
    print("4. Test Combo (Tous les effets)")
    
    choice = input("\nVotre choix (1-4) [3] : ").strip() or "3"
    
    # IMPORTANT : Toujours ajouter le looper en DERNIER
    # Car il doit mixer les pistes APRÈS les effets
    
    if choice == "1":
        harmony.enable()
        reverb.enable()
        engine.add_effect(harmony)
        engine.add_effect(reverb)
        engine.add_effect(looper_effect)  # Looper en dernier
        print("✅ Effets vocaux + Looper activés")
        
    elif choice == "2":
        wah.enable()
        chorus.enable()
        engine.add_effect(wah)
        engine.add_effect(chorus)
        engine.add_effect(looper_effect)
        print("✅ Effets guitare + Looper activés")
        
    elif choice == "3":
        engine.add_effect(looper_effect)
        print("✅ Mode Looper seul activé")
        
    elif choice == "4":
        harmony.enable()
        reverb.enable()
        wah.enable()
        chorus.enable()
        engine.add_effect(harmony)
        engine.add_effect(reverb)
        engine.add_effect(wah)
        engine.add_effect(chorus)
        engine.add_effect(looper_effect)
        print("✅ Tous les effets + Looper activés")
    
    # Afficher la chaîne d'effets
    print("\n📋 Chaîne d'effets:")
    for i, eff in enumerate(engine.get_effects(), 1):
        status = "🟢" if eff.enabled else "⚪"
        print(f"   {i}. {status} {eff.name}")
    
    # Démarrer
    print(f"\n🎙️ Démarrage du système...")
    engine.start(input_device=asio_device, output_device=asio_device)
    
    print("\n" + "=" * 70)
    print("🎤 SYSTÈME EN MARCHE")
    print("=" * 70)
    print("\nCONTRÔLES:")
    print("  r - Record/Stop piste active")
    print("  o - Overdub piste active")
    print("  1/2/3 - Sélectionner piste")
    print("  c - Clear piste active")
    print("  C - Clear toutes les pistes")
    print("  s - Afficher état")
    print("  m - Afficher niveaux audio")
    print("  q - Quitter")
    print()
    
    try:
        while True:
            cmd = input("> ").strip().lower()
            
            if cmd == 'q':
                break
            elif cmd == 'r':
                looper.record_stop()
                track_num = looper.active_track + 1
                track = looper.tracks[looper.active_track]
                if track.is_recording:
                    print(f"🔴 Enregistrement piste {track_num}")
                elif track.is_playing:
                    print(f"▶️ Lecture piste {track_num}")
                else:
                    print(f"⏹️ Arrêt piste {track_num}")
                    
            elif cmd == 'o':
                looper.overdub()
                track_num = looper.active_track + 1
                track = looper.tracks[looper.active_track]
                if track.is_overdubbing:
                    print(f"🟠 Overdub piste {track_num}")
                else:
                    print(f"⏹️ Overdub arrêté piste {track_num}")
                    
            elif cmd in ['1', '2', '3']:
                looper.set_active_track(int(cmd) - 1)
                print(f"✅ Piste {cmd} active")
                
            elif cmd == 'c':
                looper.clear_track(looper.active_track)
                print(f"🗑️ Piste {looper.active_track + 1} effacée")
                
            elif cmd == 'C':
                looper.clear_all()
                print("🗑️ Toutes les pistes effacées")
                
            elif cmd == 's':
                states = looper.get_states()
                print("\n📊 ÉTAT DES PISTES:")
                for i, state in enumerate(states):
                    marker = "👉" if i == looper.active_track else "  "
                    status = []
                    if state['recording']:
                        status.append("🔴 REC")
                    if state['playing']:
                        status.append("▶️ PLAY")
                    if state['overdubbing']:
                        status.append("🟠 OVER")
                    if state['duration'] > 0:
                        status.append(f"({state['duration']:.1f}s)")
                    
                    print(f"{marker} Piste {i+1}: {' '.join(status) if status else '⚪ Vide'}")
                print()
                
            elif cmd == 'm':
                # Afficher les niveaux en temps réel pendant 5 secondes
                print("\n📊 Niveaux audio (5 secondes):")
                start = time.time()
                while time.time() - start < 5:
                    inp = engine.get_input_level()
                    out = engine.get_output_level()
                    
                    inp_bar = int(inp * 40)
                    out_bar = int(out * 40)
                    
                    print(f"\rIN:  [{'█'*inp_bar:40}] {inp*100:5.1f}%  "
                          f"OUT: [{'█'*out_bar:40}] {out*100:5.1f}%", 
                          end='', flush=True)
                    time.sleep(0.05)
                print("\n")
    
    except KeyboardInterrupt:
        print("\n\n⏹️ Arrêt...")
    except Exception as e:
        print(f"\n❌ Erreur: {e}")
        import traceback
        traceback.print_exc()
    
    # Arrêter
    engine.stop()
    
    # Stats finales
    stats = engine.get_stats()
    print("\n" + "=" * 70)
    print("📊 STATISTIQUES FINALES")
    print("=" * 70)
    print(f"   Callbacks: {stats['callbacks']}")
    print(f"   Dropouts: {stats['underruns'] + stats['overruns']}")
    print(f"   Dropout rate: {stats['dropout_rate']:.2f}%")
    
    # État final des pistes
    print("\n📊 État final des pistes:")
    for i, state in enumerate(looper.get_states()):
        if state['duration'] > 0:
            print(f"   Piste {i+1}: {state['duration']:.1f}s enregistrés")
    
    print("\n✅ Test terminé!")

if __name__ == "__main__":
    test_complete_system()