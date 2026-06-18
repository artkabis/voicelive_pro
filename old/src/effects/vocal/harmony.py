"""
Effet Harmony - Harmonies vocales intelligentes
"""
import numpy as np
from ...audio.effects import Effect

class Harmony(Effect):
    """
    Génère des harmonies vocales en transposant la hauteur
    Simule 1-4 voix d'harmonie
    """
    
    def __init__(self):
        super().__init__("Harmony")
        
        self.parameters = {
            'voices': 2,           # 1-4 voix
            'intervals': [4, 7],   # Intervalles en demi-tons (tierce, quinte)
            'mix': 0.5,            # 0.0 - 1.0
            'humanize': 0.3        # Variation naturelle
        }
        
        # Tables de pitch shifting (simplifiées)
        self.shift_buffers = []
        self.buffer_pos = 0
    
    def process(self, audio: np.ndarray, sample_rate: int) -> np.ndarray:
        """Générer les harmonies"""
        if self.sample_rate != sample_rate:
            self.sample_rate = sample_rate
            self._init_buffers()
        
        voices = self.parameters['voices']
        intervals = self.parameters['intervals'][:voices]
        mix = self.parameters['mix']
        humanize = self.parameters['humanize']
        
        output = audio.copy()
        
        # Pour chaque voix d'harmonie
        for i, interval in enumerate(intervals):
            # Calculer le ratio de pitch (simplifié)
            ratio = 2.0 ** (interval / 12.0)
            
            # Pitch shifting basique via resampling
            harmony = self._simple_pitch_shift(audio, ratio, humanize * (i + 1))
            
            # Ajouter au mix
            output += harmony * (mix / len(intervals))
        
        # Normaliser
        max_val = np.abs(output).max()
        if max_val > 0.9:
            output *= 0.9 / max_val
        
        return output
    
    def _simple_pitch_shift(self, audio: np.ndarray, ratio: float, detune: float) -> np.ndarray:
        """
        Pitch shifting simple via interpolation
        Note: Pour un vrai produit, utiliser librosa.effects.pitch_shift
        """
        # Ajouter une légère variation (humanize)
        ratio *= (1.0 + np.random.uniform(-detune * 0.01, detune * 0.01))
        
        # Indices pour le resampling
        output_length = len(audio)
        input_indices = np.arange(output_length) * ratio
        
        # Interpolation linéaire
        shifted = np.zeros_like(audio)
        
        for ch in range(audio.shape[1] if len(audio.shape) > 1 else 1):
            if len(audio.shape) > 1:
                channel = audio[:, ch]
            else:
                channel = audio
            
            # Interpoler
            valid_indices = input_indices < len(channel) - 1
            int_indices = input_indices[valid_indices].astype(int)
            frac = input_indices[valid_indices] - int_indices
            
            result = np.zeros(output_length)
            result[valid_indices] = (
                channel[int_indices] * (1 - frac) +
                channel[int_indices + 1] * frac
            )
            
            if len(audio.shape) > 1:
                shifted[:, ch] = result
            else:
                shifted = result
        
        return shifted
    
    def _init_buffers(self):
        """Initialiser les buffers"""
        pass
    
    def set_key(self, key: str):
        """Définir la tonalité pour les harmonies intelligentes"""
        # Mapping des tonalités vers intervalles
        key_intervals = {
            'C': [4, 7],    # Tierce majeure, Quinte
            'Cm': [3, 7],   # Tierce mineure, Quinte
            'G': [4, 7],
            'Am': [3, 7],
            # etc.
        }
        
        if key in key_intervals:
            self.parameters['intervals'] = key_intervals[key]
