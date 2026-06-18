"""
Effet HardTune - Auto-Tune / Correction de hauteur
"""
import numpy as np
from ...audio.effects import Effect

class HardTune(Effect):
    """
    Correction de hauteur automatique (style T-Pain/Auto-Tune)
    """
    
    def __init__(self):
        super().__init__("HardTune")
        
        self.parameters = {
            'correction': 1.0,  # 0.0 = naturel, 1.0 = robot
            'key': 'C',         # Tonalité
            'scale': 'major',   # major, minor, chromatic
            'mix': 1.0
        }
        
        # Gammes (notes MIDI modulo 12)
        self.scales = {
            'major': [0, 2, 4, 5, 7, 9, 11],
            'minor': [0, 2, 3, 5, 7, 8, 10],
            'chromatic': list(range(12))
        }
        
        self.last_pitch = 0
    
    def process(self, audio: np.ndarray, sample_rate: int) -> np.ndarray:
        """Appliquer la correction de hauteur"""
        
        correction = self.parameters['correction']
        mix = self.parameters['mix']
        
        if correction < 0.01:
            return audio  # Bypass si pas de correction
        
        # Détection de pitch simplifiée (autocorrélation)
        pitch = self._detect_pitch(audio, sample_rate)
        
        if pitch > 0:
            # Trouver la note la plus proche dans la gamme
            target_pitch = self._snap_to_scale(pitch)
            
            # Calculer le ratio de correction
            if abs(target_pitch - pitch) > 0.01:
                ratio = target_pitch / pitch
                
                # Appliquer le pitch shift
                corrected = self._pitch_shift_realtime(audio, ratio, correction)
                
                # Mix
                return audio * (1.0 - mix) + corrected * mix
        
        return audio
    
    def _detect_pitch(self, audio: np.ndarray, sample_rate: int) -> float:
        """
        Détection de pitch basique via autocorrélation
        Note: Pour la production, utiliser aubio ou crepe
        """
        # Prendre le canal mono
        if len(audio.shape) > 1:
            mono = audio[:, 0]
        else:
            mono = audio
        
        # Autocorrélation
        corr = np.correlate(mono, mono, mode='full')
        corr = corr[len(corr)//2:]
        
        # Trouver le premier pic après le pic central
        min_lag = int(sample_rate / 500)  # 500 Hz max
        max_lag = int(sample_rate / 80)   # 80 Hz min
        
        if max_lag >= len(corr):
            return 0
        
        peak = np.argmax(corr[min_lag:max_lag]) + min_lag
        
        if peak > 0:
            frequency = sample_rate / peak
            return frequency
        
        return 0
    
    def _snap_to_scale(self, frequency: float) -> float:
        """Quantifier la fréquence vers la note la plus proche"""
        if frequency <= 0:
            return frequency
        
        # Convertir en MIDI
        midi = 69 + 12 * np.log2(frequency / 440.0)
        
        # Obtenir la gamme
        scale = self.scales.get(self.parameters['scale'], self.scales['chromatic'])
        
        # Trouver la note la plus proche dans la gamme
        note = round(midi) % 12
        
        # Si la note n'est pas dans la gamme, trouver la plus proche
        if note not in scale:
            distances = [abs(note - n) for n in scale]
            min_dist_idx = np.argmin(distances)
            target_note = scale[min_dist_idx]
            
            # Ajuster le MIDI
            octave = int(midi // 12)
            midi_snapped = octave * 12 + target_note
        else:
            midi_snapped = round(midi)
        
        # Reconvertir en fréquence
        target_freq = 440.0 * (2.0 ** ((midi_snapped - 69) / 12.0))
        
        return target_freq
    
    def _pitch_shift_realtime(self, audio: np.ndarray, ratio: float, amount: float) -> np.ndarray:
        """Appliquer le pitch shift avec intensité contrôlée"""
        # Interpoler le ratio selon l'intensité de correction
        ratio_adjusted = 1.0 + (ratio - 1.0) * amount
        
        # Simple time stretching pour démonstration
        output_length = len(audio)
        indices = np.arange(output_length) * ratio_adjusted
        
        shifted = np.zeros_like(audio)
        
        for ch in range(audio.shape[1] if len(audio.shape) > 1 else 1):
            if len(audio.shape) > 1:
                channel = audio[:, ch]
            else:
                channel = audio
            
            valid = indices < len(channel) - 1
            int_idx = indices[valid].astype(int)
            frac = indices[valid] - int_idx
            
            result = np.zeros(output_length)
            result[valid] = channel[int_idx] * (1 - frac) + channel[int_idx + 1] * frac
            
            if len(audio.shape) > 1:
                shifted[:, ch] = result
            else:
                shifted = result
        
        return shifted
