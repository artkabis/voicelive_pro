"""
Serveur Web Flask pour VoiceLive Pro - Version complète avec sync et export
"""
import eventlet
eventlet.monkey_patch(socket=True, time=True, thread=False)

from flask import Flask, jsonify, send_from_directory, send_file, request
from flask_socketio import SocketIO, emit
from flask_cors import CORS
import threading
import time
import sys
import traceback
import queue
import tempfile
import os
from pathlib import Path
from datetime import datetime

sys.path.insert(0, str(Path(__file__).parent.parent.parent))

from src.audio.metronome import Metronome, MetronomeController
from src.audio.project_manager import ProjectManager, AutoSaveManager
from src.audio.tuner import ChromaticTuner
from src.audio.undo_redo import UndoRedoManager, LooperWithUndo
from src.audio.mastering_chain import MasteringChain
from src.audio.engine_optimized import AudioEngineOptimized
from src.audio.looper_effect import LooperEffect
from src.utils.logger import logger

try:
    from src.effects.vocal.harmony import Harmony
    from src.effects.vocal.reverb_pro import ReverbPro
    from src.effects.guitar.wah import Wah
    from src.effects.guitar.chorus import Chorus
    EFFECTS_AVAILABLE = True
except ImportError as e:
    logger.warning(f"Certains effets non disponibles: {e}")
    EFFECTS_AVAILABLE = False

app = Flask(__name__, static_folder='static', template_folder='templates')
app.config['SECRET_KEY'] = 'voicelive_pro_2025'
CORS(app)
socketio = SocketIO(app, cors_allowed_origins="*", async_mode='eventlet')

class AppState:
    def __init__(self):
        self.engine = None
        self.looper_effect = None
        self.looper = None
        self.effects = {}
        self.mastering_chain = None

        self.metronome = None
        self.metronome_controller = None
        self.tuner = None
        self.project_manager = ProjectManager()
        self.auto_save = None
        self.undo_manager = UndoRedoManager(max_history=20)
        self.looper_with_undo = None

        self.monitoring_active = False
        self.monitoring_thread = None
        self.monitoring_queue = queue.Queue(maxsize=10)
        self.emit_greenlet = None
        
    def reset(self):
        if self.engine and self.engine.is_running:
            self.engine.stop()

        # Arrêter le métronome
        if self.metronome:
            self.metronome.stop()
        
        # Désactiver auto-save
        if self.auto_save:
            self.auto_save.disable()

        self.engine = None
        self.looper_effect = None
        self.looper = None
        self.effects = {}
        self.stop_monitoring()
    
    def start_monitoring(self):
        if not self.monitoring_active:
            self.monitoring_active = True
            self.monitoring_thread = threading.Thread(target=self._monitor_loop, daemon=True)
            self.monitoring_thread.start()
            self.emit_greenlet = eventlet.spawn(self._emit_monitoring_data)
            logger.info("Monitoring démarré")
    
    def stop_monitoring(self):
        self.monitoring_active = False
        if self.monitoring_thread:
            self.monitoring_thread = None
        if self.emit_greenlet:
            self.emit_greenlet.kill()
            self.emit_greenlet = None
        while not self.monitoring_queue.empty():
            try:
                self.monitoring_queue.get_nowait()
            except queue.Empty:
                break
        logger.info("Monitoring arrêté")
    
    def _monitor_loop(self):
        while self.monitoring_active:
            if self.engine and self.engine.is_running:
                try:
                    data = {
                        'input_level': self.engine.get_input_level() * 100,
                        'output_level': self.engine.get_output_level() * 100,
                        'peak_input': self.engine.peak_input * 100,
                        'peak_output': self.engine.peak_output * 100
                    }
                    
                    if self.looper:
                        data['looper_states'] = self.looper.get_states()
                        data['active_track'] = self.looper.active_track
                        data['sync_info'] = self.looper.get_sync_info()
                    if self.metronome:
                        data['metronome_state'] = self.metronome.get_state()
                    # NOUVEAU : État de l'accordeur
                    if self.tuner:
                        data['tuner_state'] = self.tuner.get_state()
                    
                    try:
                        self.monitoring_queue.put_nowait(data)
                    except queue.Full:
                        pass
                        
                except Exception as e:
                    logger.error(f"Erreur monitoring: {e}")
            
            time.sleep(0.05)
    
    def _emit_monitoring_data(self):
        while self.monitoring_active:
            try:
                data = self.monitoring_queue.get(timeout=0.1)
                socketio.emit('levels_update', data)
            except queue.Empty:
                pass
            except Exception as e:
                logger.error(f"Erreur émission monitoring: {e}")
            eventlet.sleep(0.01)

state = AppState()

@app.route('/')
def index():
    return send_from_directory('static', 'index.html')

@app.route('/api/status')
def get_status():
    return jsonify({
        'running': state.engine.is_running if state.engine else False,
        'effects_available': EFFECTS_AVAILABLE,
        'looper_available': state.looper is not None
    })

@app.route('/api/devices')
def get_devices():
    try:
        import sounddevice as sd
        devices = []
        for i, dev in enumerate(sd.query_devices()):
            driver_type = 'ASIO' if 'ASIO' in dev['name'] else 'Standard'
            devices.append({
                'id': i,
                'name': dev['name'],
                'inputs': dev['max_input_channels'],
                'outputs': dev['max_output_channels'],
                'default_samplerate': dev['default_samplerate'],
                'driver_type': driver_type,
                'hostapi': dev['hostapi']
            })
        return jsonify(devices)
    except Exception as e:
        logger.error(f"Erreur récupération devices: {e}")
        return jsonify({'error': str(e)}), 500

@app.route('/api/upload_audio', methods=['POST'])
def upload_audio():
    """Uploader un fichier audio dans une piste"""
    try:
        if not state.looper:
            return jsonify({'error': 'Looper non initialisé'}), 400
        
        # Vérifier qu'un fichier est présent
        if 'file' not in request.files:
            return jsonify({'error': 'Aucun fichier fourni'}), 400
        
        file = request.files['file']
        track_index = int(request.form.get('track', 0))
        
        if file.filename == '':
            return jsonify({'error': 'Nom de fichier vide'}), 400
        
        # Vérifier l'extension
        allowed_extensions = {'.wav', '.flac'}
        file_ext = Path(file.filename).suffix.lower()
        if file_ext not in allowed_extensions:
            return jsonify({'error': f'Format non supporté. Utilisez WAV ou FLAC'}), 400
        
        # Sauvegarder temporairement
        with tempfile.NamedTemporaryFile(delete=False, suffix=file_ext) as tmp:
            file.save(tmp.name)
            tmp_path = tmp.name
        
        try:
            # Charger dans le looper
            state.looper.load_audio_file(track_index, tmp_path)
            
            # Émettre l'état mis à jour
            socketio.emit('looper_updated', {
                'states': state.looper.get_states(),
                'active_track': state.looper.active_track,
                'sync_info': state.looper.get_sync_info()
            })
            
            return jsonify({
                'success': True,
                'track': track_index,
                'filename': file.filename
            })
        
        finally:
            # Nettoyer le fichier temporaire
            try:
                os.unlink(tmp_path)
            except:
                pass
        
    except Exception as e:
        logger.error(f"Erreur upload audio: {e}")
        traceback.print_exc()
        return jsonify({'error': str(e)}), 500


@app.route('/api/export', methods=['POST'])
def export_mix():
    """Exporter le mixage des pistes"""
    try:
        if not state.looper:
            return jsonify({'error': 'Looper non initialisé'}), 400
        
        data = request.get_json() or {}
        format_type = data.get('format', 'wav').lower()
        normalize = data.get('normalize', True)
        
        audio_data = state.looper.export_mix(normalize=normalize)
        
        if len(audio_data) == 0:
            return jsonify({'error': 'Aucune piste à exporter'}), 400
        
        import soundfile as sf
        
        timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
        filename = f"voicelive_export_{timestamp}.{format_type}"
        
        with tempfile.NamedTemporaryFile(delete=False, suffix=f'.{format_type}') as tmp:
            if format_type == 'wav':
                sf.write(tmp.name, audio_data, state.looper.sample_rate, subtype='PCM_16')
            elif format_type == 'flac':
                sf.write(tmp.name, audio_data, state.looper.sample_rate, format='FLAC')
            else:
                return jsonify({'error': f'Format non supporté: {format_type}'}), 400
            
            tmp_path = tmp.name
        
        response = send_file(
            tmp_path,
            as_attachment=True,
            download_name=filename,
            mimetype='audio/wav' if format_type == 'wav' else 'audio/flac'
        )
        
        @response.call_on_close
        def cleanup():
            try:
                os.unlink(tmp_path)
            except:
                pass
        
        logger.info(f"Export généré: {filename}")
        return response
        
    except Exception as e:
        logger.error(f"Erreur export: {e}")
        traceback.print_exc()
        return jsonify({'error': str(e)}), 500

@socketio.on('connect')
def handle_connect():
    logger.info("Client connecté")
    emit('connection_response', {'status': 'connected'})

@socketio.on('disconnect')
def handle_disconnect():
    logger.info("Client déconnecté")

@socketio.on('start_audio')
def handle_start_audio(data):
    try:
        input_device = data.get('input_device')
        output_device = data.get('output_device')
        buffer_size = data.get('buffer_size', 128)
        
        import sounddevice as sd
        devices_list = sd.query_devices()
        
        input_dev_info = devices_list[input_device]
        output_dev_info = devices_list[output_device]
        
        input_is_asio = 'ASIO' in input_dev_info['name']
        output_is_asio = 'ASIO' in output_dev_info['name']
        
        if input_is_asio != output_is_asio:
            error_msg = "❌ Combinaison de périphériques incompatible!"
            logger.error(error_msg)
            emit('error', {'message': error_msg})
            return
        
        logger.info(f"Démarrage audio: IN={input_device}, OUT={output_device}, Buffer={buffer_size}")
        
        # ========== ÉTAPE 1 : CRÉER L'ENGINE (CRUCIAL) ==========
        state.engine = AudioEngineOptimized(sample_rate=44100, buffer_size=buffer_size)
        
        if state.engine is None:
            raise Exception("Échec de création du moteur audio")
        
        # ========== ÉTAPE 2 : CRÉER LE MÉTRONOME ==========
        state.metronome = Metronome(sample_rate=44100)
        state.metronome_controller = MetronomeController(state.metronome)
        state.engine.add_effect(state.metronome, position=0)
        logger.info("✅ Métronome créé")
        
        # ========== ÉTAPE 3 : CRÉER L'ACCORDEUR ==========
        state.tuner = ChromaticTuner(sample_rate=44100)
        state.engine.add_effect(state.tuner, position=1)
        logger.info("✅ Accordeur créé")
        
        # ========== ÉTAPE 4 : CRÉER LES EFFETS ==========
        if EFFECTS_AVAILABLE:
            state.effects = {
                'harmony': Harmony(),
                'reverb': ReverbPro(),
                'wah': Wah(),
                'chorus': Chorus()
            }
            
            # Configurer les paramètres par défaut
            state.effects['harmony'].set_parameter('voices', 2)
            state.effects['harmony'].set_parameter('mix', 0.4)
            state.effects['reverb'].set_parameter('room_size', 0.6)
            state.effects['reverb'].set_parameter('wet', 0.3)
            state.effects['wah'].set_parameter('frequency', 800)
            state.effects['chorus'].set_parameter('rate', 0.5)
            state.effects['chorus'].set_parameter('mix', 0.4)
            
            # Ajouter à l'engine
            for effect in state.effects.values():
                state.engine.add_effect(effect)
            
            logger.info(f"✅ Effets créés: {list(state.effects.keys())}")
        
        # ========== ÉTAPE 5 : CRÉER LE LOOPER ==========
        state.looper_effect = LooperEffect(sample_rate=44100, max_duration=120)
        state.looper = state.looper_effect.get_looper()
        state.engine.add_effect(state.looper_effect)
        state.looper_effect.enable()
        logger.info("✅ Looper créé")
        
        # ========== ÉTAPE 6 : CRÉER LE WRAPPER UNDO/REDO ==========
        state.looper_with_undo = LooperWithUndo(state.looper, state.undo_manager)
        logger.info("✅ Undo/Redo activé")
        
        # ========== ÉTAPE 7 : CRÉER LA CHAÎNE DE MASTERING ==========
        state.mastering_chain = MasteringChain()
        state.mastering_chain.enable()
        state.engine.add_effect(state.mastering_chain)
        logger.info("✅ Mastering chain créée")
        
        # ========== ÉTAPE 8 : INITIALISER AUTO-SAVE ==========
        state.auto_save = AutoSaveManager(state.project_manager, interval_seconds=300)
        logger.info("✅ Auto-save initialisé")
        
        # ========== ÉTAPE 9 : DÉMARRER L'ENGINE ==========
        state.engine.start(input_device=input_device, output_device=output_device)
        state.start_monitoring()
        
        # ========== ÉTAPE 10 : ÉMETTRE LES ÉTATS INITIAUX ==========
        socketio.emit('metronome_updated', state.metronome.get_state())
        socketio.emit('tuner_updated', state.tuner.get_state())
        socketio.emit('undo_redo_updated', {
            'can_undo': state.undo_manager.can_undo(),
            'can_redo': state.undo_manager.can_redo(),
            'undo_list': state.undo_manager.get_undo_list(),
            'redo_list': state.undo_manager.get_redo_list(),
        })
        
        emit('audio_started', {
            'success': True,
            'effects': list(state.effects.keys()) if state.effects else []
        })
        
        logger.info("✅ Système audio démarré avec succès")
        
    except Exception as e:
        error_msg = f"Erreur démarrage audio: {e}"
        logger.error(error_msg)
        logger.error(traceback.format_exc())
        
        # Nettoyer en cas d'erreur
        if state.engine:
            try:
                state.engine.stop()
            except:
                pass
        state.reset()
        
        emit('error', {'message': error_msg})

@socketio.on('metronome_command')
def handle_metronome_command(data):
    try:
        if not state.metronome:
            emit('error', {'message': 'Métronome non initialisé'})
            return
        
        command = data.get('command')
        logger.info(f"Commande métronome: {command}")
        
        if command == 'toggle_metronome':
            state.metronome.toggle()
            logger.info(f"Metronome: {'ON' if state.metronome.enabled else 'OFF'}")
        
        elif command == 'start_metronome':
            state.metronome.start()
            logger.info("Métronome démarré")
        
        elif command == 'stop_metronome':
            state.metronome.stop()
            state.metronome_controller.disable_auto_record()
            logger.info("Métronome arrêté")
        
        elif command == 'set_parameter':
            parameter = data.get('parameter')
            value = data.get('value')
            state.metronome.set_parameter(parameter, value)
            logger.info(f"Metronome: {parameter} = {value}")
        
        elif command == 'tap_tempo':
            tap_times = data.get('tap_times', [])
            if len(tap_times) >= 2:
                state.metronome.tap_tempo(tap_times)
                logger.info(f"Tap tempo: {state.metronome.parameters['bpm']:.1f} BPM")
        
        else:
            emit('error', {'message': f'Commande inconnue: {command}'})
            return
        
        # Émettre l'état mis à jour
        emit('metronome_updated', state.metronome.get_state())
        
    except Exception as e:
        logger.error(f"Erreur commande métronome: {e}")
        logger.error(traceback.format_exc())
        emit('error', {'message': str(e)})

@socketio.on('tuner_command')
def handle_tuner_command(data):
    try:
        if not state.tuner:
            emit('error', {'message': 'Accordeur non initialisé'})
            return
        
        command = data.get('command')
        
        if command == 'toggle':
            state.tuner.toggle()
        elif command == 'set_reference':
            freq = data.get('frequency', 440.0)
            state.tuner.set_reference_pitch(freq)
        elif command == 'set_noise_gate':  # ✅ NOUVEAU
            value = data.get('value', 0.02)
            state.tuner.set_noise_gate(value)
        else:
            emit('error', {'message': f'Commande inconnue: {command}'})
            return
        
        emit('tuner_updated', state.tuner.get_state())
        
    except Exception as e:
        logger.error(f"Erreur accordeur: {e}")
        logger.error(traceback.format_exc())
        emit('error', {'message': str(e)})

@socketio.on('project_command')
def handle_project_command(data):
    try:
        command = data.get('command')
        
        if command == 'save':
            project_name = data.get('name', f'Project_{datetime.now().strftime("%Y%m%d_%H%M%S")}')
            result = state.project_manager.save_project(
                project_name,
                state.looper,
                state.effects,
                state.mastering_chain,
                state.metronome
            )
            emit('project_saved', result)
            
            # Activer auto-save pour ce projet
            if result['success']:
                state.auto_save.enable(project_name)
        
        elif command == 'load':
            project_path = data.get('path')
            result = state.project_manager.load_project(
                project_path,
                state.looper,
                state.effects,
                state.mastering_chain,
                state.metronome
            )
            
            if result['success']:
                # Émettre tous les états mis à jour
                socketio.emit('looper_updated', {
                    'states': state.looper.get_states(),
                    'active_track': state.looper.active_track,
                    'sync_info': state.looper.get_sync_info()
                })
                
                socketio.emit('mastering_updated', state.mastering_chain.get_state())
                socketio.emit('metronome_updated', state.metronome.get_state())
                
                # Vider l'historique undo/redo
                state.undo_manager.clear_history()
            
            emit('project_loaded', result)
        
        elif command == 'list':
            projects = state.project_manager.list_projects()
            emit('projects_list', {'projects': projects})
        
        elif command == 'delete':
            project_path = data.get('path')
            result = state.project_manager.delete_project(project_path)
            emit('project_deleted', result)
        
        else:
            emit('error', {'message': f'Commande inconnue: {command}'})
    
    except Exception as e:
        logger.error(f"Erreur projet: {e}")
        emit('error', {'message': str(e)})

@socketio.on('undo_redo_command')
def handle_undo_redo_command(data):
    try:
        command = data.get('command')
        
        if command == 'undo':
            success = state.undo_manager.undo(state.looper)
            if success:
                # Émettre état mis à jour
                socketio.emit('looper_updated', {
                    'states': state.looper.get_states(),
                    'active_track': state.looper.active_track,
                    'sync_info': state.looper.get_sync_info()
                })
        
        elif command == 'redo':
            success = state.undo_manager.redo(state.looper)
            if success:
                socketio.emit('looper_updated', {
                    'states': state.looper.get_states(),
                    'active_track': state.looper.active_track,
                    'sync_info': state.looper.get_sync_info()
                })
        
        # Émettre l'état undo/redo mis à jour
        emit('undo_redo_updated', {
            'can_undo': state.undo_manager.can_undo(),
            'can_redo': state.undo_manager.can_redo(),
            'undo_list': state.undo_manager.get_undo_list(),
            'redo_list': state.undo_manager.get_redo_list(),
        })
        
    except Exception as e:
        logger.error(f"Erreur undo/redo: {e}")
        emit('error', {'message': str(e)})


# Ajouter un nouveau handler pour les commandes de mastering :
@socketio.on('mastering_command')
def handle_mastering_command(data):
    try:
        if not state.mastering_chain:
            emit('error', {'message': 'Mastering chain non initialisée'})
            return
        
        command = data.get('command')
        logger.info(f"Commande mastering: {command}")
        
        if command == 'toggle':
            module = data.get('module')  # 'eq', 'compressor', ou 'limiter'
            
            if module == 'eq':
                state.mastering_chain.toggle_eq()
            elif module == 'compressor':
                state.mastering_chain.toggle_compressor()
            elif module == 'limiter':
                state.mastering_chain.toggle_limiter()
            else:
                emit('error', {'message': f'Module inconnu: {module}'})
                return
        
        elif command == 'set_parameter':
            module = data.get('module')
            parameter = data.get('parameter')
            value = data.get('value')
            
            if module == 'eq':
                state.mastering_chain.set_eq_parameter(parameter, value)
            elif module == 'compressor':
                state.mastering_chain.set_compressor_parameter(parameter, value)
            elif module == 'limiter':
                state.mastering_chain.set_limiter_parameter(parameter, value)
            else:
                emit('error', {'message': f'Module inconnu: {module}'})
                return
            
            logger.info(f"Mastering: {module}.{parameter} = {value}")
        
        else:
            emit('error', {'message': f'Commande inconnue: {command}'})
            return
        
        # Envoyer l'état mis à jour
        emit('mastering_updated', state.mastering_chain.get_state())
        
    except Exception as e:
        logger.error(f"Erreur commande mastering: {e}")
        logger.error(traceback.format_exc())
        emit('error', {'message': str(e)})

# Ajouter une route API pour obtenir l'état du mastering :
@app.route('/api/mastering/state')
def get_mastering_state():
    if state.mastering_chain:
        return jsonify(state.mastering_chain.get_state())
    return jsonify({'error': 'Mastering chain non initialisée'}), 400

@socketio.on('stop_audio')
def handle_stop_audio():
    try:
        logger.info("Arrêt du système audio...")
        state.reset()
        emit('audio_stopped', {'success': True})
        logger.info("✅ Système audio arrêté")
    except Exception as e:
        logger.error(f"Erreur arrêt audio: {e}")
        emit('error', {'message': str(e)})

@socketio.on('toggle_effect')
def handle_toggle_effect(data):
    try:
        effect_name = data.get('effect')
        
        if effect_name not in state.effects:
            emit('error', {'message': f'Effet "{effect_name}" non trouvé'})
            return
        
        effect = state.effects[effect_name]
        effect.toggle()
        
        logger.info(f"Effet {effect_name}: {'ON' if effect.enabled else 'OFF'}")
        
        emit('effect_toggled', {
            'effect': effect_name,
            'enabled': effect.enabled
        })
        
    except Exception as e:
        logger.error(f"Erreur toggle effet: {e}")
        emit('error', {'message': str(e)})

@socketio.on('set_parameter')
def handle_set_parameter(data):
    try:
        effect_name = data.get('effect')
        param_name = data.get('parameter')
        value = data.get('value')
        
        if effect_name not in state.effects:
            emit('error', {'message': f'Effet "{effect_name}" non trouvé'})
            return
        
        effect = state.effects[effect_name]
        effect.set_parameter(param_name, value)
        
        logger.debug(f"Paramètre {effect_name}.{param_name} = {value}")
        
        emit('parameter_set', {
            'effect': effect_name,
            'parameter': param_name,
            'value': value
        })
        
    except Exception as e:
        logger.error(f"Erreur modification paramètre: {e}")
        emit('error', {'message': str(e)})

# Ajouter ces nouvelles commandes dans la fonction handle_looper_command() de web_server.py
# Remplacer/compléter le bloc existant

@socketio.on('looper_command')
def handle_looper_command(data):
    try:
        if not state.looper:
            emit('error', {'message': 'Looper non initialisé'})
            return
        
        command = data.get('command')
        logger.info(f"Commande looper: {command}")
        
        # Commandes existantes
        if command == 'record_stop':
            state.looper.record_stop()
            
        elif command == 'overdub':
            state.looper.overdub()
            
        elif command == 'set_track':
            track = data.get('track', 0)
            if 0 <= track < 3:
                state.looper.set_active_track(track)
        
        elif command == 'clear_track':
            track = data.get('track')
            if track is not None:
                state.looper.clear_track(track)
            else:
                state.looper.clear_track(state.looper.active_track)
        
        elif command == 'clear_all':
            state.looper.clear_all()
        
        elif command == 'nudge_track':
            track = data.get('track')
            offset_ms = data.get('offset_ms', 0)
            if track is not None:
                state.looper.nudge_track(track, offset_ms)
        
        elif command == 'toggle_sync':
            state.looper.sync_mode = not state.looper.sync_mode
            logger.info(f"Looper: Sync mode {'ON' if state.looper.sync_mode else 'OFF'}")
        
        elif command == 'reset_master':
            state.looper.reset_master()
        
        # NOUVELLES COMMANDES DE CONTRÔLE
        elif command == 'play_track':
            track = data.get('track')
            if track is not None:
                state.looper.play_track(track)
        
        elif command == 'pause_track':
            track = data.get('track')
            if track is not None:
                state.looper.pause_track(track)
        
        elif command == 'stop_track':
            track = data.get('track')
            if track is not None:
                state.looper.stop_track(track)
        
        elif command == 'seek_track':
            track = data.get('track')
            position = data.get('position', 0)  # en secondes
            if track is not None:
                state.looper.seek_track(track, position)
        
        elif command == 'set_track_gain':
            track = data.get('track')
            gain = data.get('gain', 1.0)
            if track is not None:
                state.looper.set_track_gain(track, gain)
                logger.info(f"Looper: Track {track + 1} gain set to {gain:.2f}")
                # CRITIQUE : Émettre immédiatement l'état mis à jour
                emit('looper_updated', {
                    'states': state.looper.get_states(),
                    'active_track': state.looper.active_track,
                    'sync_info': state.looper.get_sync_info()
                })
                return  # Retour anticipé car on a déjà émis
        
        elif command == 'toggle_track_mute':
            track = data.get('track')
            if track is not None:
                state.looper.toggle_track_mute(track)
                # Émettre immédiatement pour le mute aussi
                emit('looper_updated', {
                    'states': state.looper.get_states(),
                    'active_track': state.looper.active_track,
                    'sync_info': state.looper.get_sync_info()
                })
                return
        elif command == 'clear_track':
            track = data.get('track')
            if track is not None:
                state.looper_with_undo.clear_track_with_undo(track)
            else:
                state.looper_with_undo.clear_track_with_undo(state.looper.active_track)
        elif command == 'clear_all':
            state.looper_with_undo.clear_all_with_undo()
        elif command == 'set_track_gain':
            track = data.get('track')
            gain = data.get('gain', 1.0)
            if track is not None:
                state.looper_with_undo.set_track_gain_with_undo(track, gain)
        elif command == 'toggle_track_mute':
            track = data.get('track')
            if track is not None:
                state.looper_with_undo.toggle_track_mute_with_undo(track)
        elif command == 'toggle_track_loop_export':
            track = data.get('track')
            if track is not None:
                state.looper.toggle_track_loop_export(track)
        elif command == 'play_all':
            state.looper.play_all_tracks()
        elif command == 'pause_all':
            state.looper.pause_all_tracks()
        elif command == 'stop_all':
            state.looper.stop_all_tracks()
        elif command == 'restart_all':
            state.looper.restart_all_tracks()
        elif command == 'sync_positions':
            state.looper.sync_playback_positions()
        else:
            emit('error', {'message': f'Commande inconnue: {command}'})
            return
        
        socketio.emit('undo_redo_updated', {
            'can_undo': state.undo_manager.can_undo(),
            'can_redo': state.undo_manager.can_redo(),
        })
        # Envoyer l'état mis à jour (pour toutes les autres commandes)
        emit('looper_updated', {
            'states': state.looper.get_states(),
            'active_track': state.looper.active_track,
            'sync_info': state.looper.get_sync_info()
        })
        
    except Exception as e:
        logger.error(f"Erreur commande looper: {e}")
        logger.error(traceback.format_exc())
        emit('error', {'message': str(e)})

@app.route('/api/waveform/<int:track_index>', methods=['GET'])
def get_waveform(track_index):
    """Obtenir la forme d'onde d'une piste"""
    try:
        if not state.looper:
            return jsonify({'error': 'Looper non initialisé'}), 400
        
        samples_per_pixel = int(request.args.get('samples_per_pixel', 512))
        waveform = state.looper.get_waveform_data(track_index, samples_per_pixel)
        
        return jsonify({
            'waveform': waveform,
            'duration': state.looper.tracks[track_index].length / state.looper.sample_rate if state.looper.tracks[track_index].length > 0 else 0
        })
    except Exception as e:
        logger.error(f"Erreur waveform: {e}")
        return jsonify({'error': str(e)}), 500

@socketio.on('trim_track')
def handle_trim_track(data):
    """Découper une piste"""
    try:
        track_index = data.get('track')
        start_time = data.get('start_time', 0)
        end_time = data.get('end_time', 0)
        
        success = state.looper.trim_track(track_index, start_time, end_time)
        
        if success:
            socketio.emit('looper_updated', {
                'states': state.looper.get_states(),
                'active_track': state.looper.active_track,
                'sync_info': state.looper.get_sync_info()
            })
            emit('trim_success', {'track': track_index})
        else:
            emit('error', {'message': 'Échec du découpage'})
            
    except Exception as e:
        logger.error(f"Erreur trim: {e}")
        emit('error', {'message': str(e)})

@app.route('/api/preview_trim/<int:track_index>', methods=['POST'])
def preview_trim(track_index):
    """Prévisualiser un découpage"""
    try:
        data = request.get_json()
        start_time = data.get('start_time', 0)
        end_time = data.get('end_time', 0)
        
        preview_audio = state.looper.get_track_preview(track_index, start_time, end_time)
        
        if len(preview_audio) == 0:
            return jsonify({'error': 'Aucun audio à prévisualiser'}), 400
        
        import soundfile as sf
        with tempfile.NamedTemporaryFile(delete=False, suffix='.wav') as tmp:
            sf.write(tmp.name, preview_audio, state.looper.sample_rate, subtype='PCM_16')
            tmp_path = tmp.name
        
        response = send_file(tmp_path, mimetype='audio/wav')
        
        @response.call_on_close
        def cleanup():
            try:
                os.unlink(tmp_path)
            except:
                pass
        
        return response
        
    except Exception as e:
        logger.error(f"Erreur preview: {e}")
        return jsonify({'error': str(e)}), 500

@socketio.on('get_full_status')
def handle_get_full_status():
    try:
        status = {
            'running': state.engine.is_running if state.engine else False,
            'effects': {},
            'looper': {
                'states': [],
                'active_track': 0
            }
        }
        
        if state.effects:
            for name, effect in state.effects.items():
                status['effects'][name] = {
                    'enabled': effect.enabled,
                    'parameters': effect.parameters.copy()
                }
        
        if state.looper:
            status['looper'] = {
                'states': state.looper.get_states(),
                'active_track': state.looper.active_track
            }
        
        emit('status_update', status)
        
    except Exception as e:
        logger.error(f"Erreur récupération statut: {e}")
        emit('error', {'message': str(e)})

def main():
    print("=" * 70)
    print("🎸 VOICELIVE PRO - SERVEUR WEB")
    print("=" * 70)
    print()
    print("✅ Serveur démarré sur: http://localhost:5000")
    print("   Mode: eventlet + sync master + export audio")
    print()
    print("=" * 70)
    print()
    
    try:
        socketio.run(app, host='0.0.0.0', port=5000, debug=False)
    except KeyboardInterrupt:
        print("\n\n⏹️ Arrêt du serveur...")
        state.reset()
    except Exception as e:
        logger.error(f"Erreur serveur: {e}")
        logger.error(traceback.format_exc())

if __name__ == '__main__':
    main()