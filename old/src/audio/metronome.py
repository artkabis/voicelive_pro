"""
Métronome professionnel avec pré-count et accents
"""
import numpy as np
from threading import Lock
from ..audio.effects import Effect
from ..utils.logger import logger

class Metronome(Effect):
    """
    Métronome avec click haute/basse, pré-count, et accents
    """
    
    def __init__(self, sample_rate: int = 44100):
        super().__init__("Metronome")
        
        self.sample_rate = sample_rate
        
        self.parameters = {
            'bpm': 120.0,              # 40 à 300 BPM
            'time_signature_beats': 4,  # Beats par mesure
            'time_signature_note': 4,   # Valeur de note (4 = noire)
            'volume': 0.5,             # 0.0 à 1.0
            'pre_count_bars': 1,       # Nombre de mesures de pré-count (0-4)
            'accent_first_beat': True, # Accent sur premier temps
        }
        
        # État du métronome
        self.is_running = False
        self.is_pre_counting = False
        self.pre_count_remaining = 0
        
        # Position dans le temps
        self.sample_position = 0
        self.beat_position = 0  # 0, 1, 2, 3 pour une mesure 4/4
        self.bar_count = 0
        
        # Samples par beat
        self.samples_per_beat = 0
        self._update_timing()
        
        # Générateur de sons de click
        self._generate_click_sounds()
        
        self.lock = Lock()
        self.enabled = False
        
        logger.info(f"Metronome initialisé ({self.parameters['bpm']} BPM)")
    
    def _update_timing(self):
        """Recalculer les timings basés sur le BPM"""
        bpm = self.parameters['bpm']
        beats_per_second = bpm / 60.0
        self.samples_per_beat = int(self.sample_rate / beats_per_second)
        logger.debug(f"Timing updated: {self.samples_per_beat} samples/beat @ {bpm} BPM")
    
    def _generate_click_sounds(self):
        """Générer les sons de click (haute fréquence = accent, basse = temps normal)"""
        duration_ms = 50  # Durée du click en ms
        duration_samples = int(duration_ms * self.sample_rate / 1000)
        
        # Click aigu pour accent (premier temps)
        freq_high = 1200  # Hz
        t = np.linspace(0, duration_ms / 1000, duration_samples, False)
        
        # Onde sinusoïdale avec envelope
        envelope = np.exp(-t * 50)  # Decay rapide
        self.click_high = np.sin(2 * np.pi * freq_high * t) * envelope
        
        # Click grave pour temps normaux
        freq_low = 800  # Hz
        self.click_low = np.sin(2 * np.pi * freq_low * t) * envelope
        
        # Normaliser
        self.click_high = self.click_high * 0.8
        self.click_low = self.click_low * 0.5
    
    def start(self):
        """Démarrer le métronome avec pré-count"""
        with self.lock:
            pre_count_bars = self.parameters['pre_count_bars']
            
            if pre_count_bars > 0:
                self.is_pre_counting = True
                self.pre_count_remaining = pre_count_bars
                logger.info(f"Metronome: Starting with {pre_count_bars} bar(s) pre-count")
            else:
                self.is_pre_counting = False
            
            self.is_running = True
            self.sample_position = 0
            self.beat_position = 0
            self.bar_count = 0
            
            self._update_timing()
            
            logger.info(f"Metronome started ({self.parameters['bpm']} BPM, {self.parameters['time_signature_beats']}/{self.parameters['time_signature_note']})")
    
    def stop(self):
        """Arrêter le métronome"""
        with self.lock:
            self.is_running = False
            self.is_pre_counting = False
            logger.info("Metronome stopped")
    
    def tap_tempo(self, tap_times):
        """
        Calculer BPM à partir de taps (list de timestamps en secondes)
        Au moins 2 taps nécessaires
        """
        if len(tap_times) < 2:
            return
        
        # Calculer intervals entre taps
        intervals = []
        for i in range(1, len(tap_times)):
            intervals.append(tap_times[i] - tap_times[i-1])
        
        # BPM moyen
        avg_interval = sum(intervals) / len(intervals)
        bpm = 60.0 / avg_interval
        
        # Limiter entre 40 et 300 BPM
        bpm = max(40, min(300, bpm))
        
        self.set_parameter('bpm', bpm)
        logger.info(f"Tap tempo: {bpm:.1f} BPM")
    
    def on_parameter_change(self, param_name, value):
        """Réagir aux changements de paramètres"""
        if param_name == 'bpm':
            self._update_timing()
        elif param_name.startswith('time_signature'):
            self._update_timing()
    
    def process(self, audio: np.ndarray, sample_rate: int) -> np.ndarray:
        """Mixer le click du métronome avec l'audio"""
        if not self.enabled or not self.is_running:
            return audio
        
        with self.lock:
            output = audio.copy()
            frames = len(audio)
            volume = self.parameters['volume']
            beats_per_bar = self.parameters['time_signature_beats']
            
            for i in range(frames):
                # Vérifier si on doit jouer un click
                if self.sample_position == 0:
                    # Déterminer quel click jouer
                    is_first_beat = (self.beat_position == 0)
                    
                    # Choisir le son approprié
                    if is_first_beat and self.parameters['accent_first_beat']:
                        click_sound = self.click_high
                    else:
                        click_sound = self.click_low
                    
                    # Ajouter le click à l'audio
                    click_len = len(click_sound)
                    end_idx = min(i + click_len, frames)
                    click_samples = end_idx - i
                    
                    # Mixer le click (stéréo)
                    click_stereo = np.column_stack([
                        click_sound[:click_samples] * volume,
                        click_sound[:click_samples] * volume
                    ])
                    
                    output[i:end_idx] += click_stereo
                
                # Incrémenter position
                self.sample_position += 1
                
                # Nouveau beat ?
                if self.sample_position >= self.samples_per_beat:
                    self.sample_position = 0
                    self.beat_position += 1
                    
                    # Nouvelle mesure ?
                    if self.beat_position >= beats_per_bar:
                        self.beat_position = 0
                        self.bar_count += 1
                        
                        # Pré-count terminé ?
                        if self.is_pre_counting:
                            self.pre_count_remaining -= 1
                            if self.pre_count_remaining <= 0:
                                self.is_pre_counting = False
                                logger.info("Metronome: Pre-count finished, recording can start")
            
            return output
    
    def is_pre_count_finished(self):
        """Vérifier si le pré-count est terminé"""
        with self.lock:
            return not self.is_pre_counting
    
    def get_state(self):
        """Obtenir l'état du métronome"""
        with self.lock:
              return {
                'enabled': bool(self.enabled),
                'is_running': bool(self.is_running),
                'is_pre_counting': bool(self.is_pre_counting),
                'pre_count_remaining': self.pre_count_remaining,
                'bpm': self.parameters['bpm'],
                'time_signature': f"{self.parameters['time_signature_beats']}/{self.parameters['time_signature_note']}",
                'current_beat': self.beat_position + 1,
                'current_bar': self.bar_count + 1,
                'volume': self.parameters['volume'],
            }


class MetronomeController:
    """
    Contrôleur pour gérer le métronome et son intégration avec le looper
    """
    
    def __init__(self, metronome: Metronome):
        self.metronome = metronome
        self.auto_record_after_precount = False
        self.record_callback = None
    
    def enable_auto_record(self, callback):
        """
        Activer l'enregistrement automatique après le pré-count
        callback: fonction à appeler quand le pré-count est terminé
        """
        self.auto_record_after_precount = True
        self.record_callback = callback
        logger.info("Auto-record activé après pré-count")
    
    def disable_auto_record(self):
        """Désactiver l'enregistrement automatique"""
        self.auto_record_after_precount = False
        self.record_callback = None
    
    def start_with_precount(self):
        """Démarrer le métronome avec pré-count"""
        self.metronome.start()
        
        if self.auto_record_after_precount and self.record_callback:
            # Le callback sera appelé dans check_and_trigger_record()
            pass
    
    def check_and_trigger_record(self):
        """
        À appeler périodiquement pour vérifier si le pré-count est fini
        et déclencher l'enregistrement
        """
        if (self.auto_record_after_precount and 
            self.record_callback and 
            self.metronome.is_running and 
            self.metronome.is_pre_count_finished()):
            
            # Déclencher l'enregistrement
            logger.info("Déclenchement auto-record")
            self.record_callback()
            
            # Désactiver pour ne pas déclencher plusieurs fois
            self.disable_auto_record()
    
    def stop(self):
        """Arrêter le métronome"""
        self.metronome.stop()
        self.disable_auto_record()