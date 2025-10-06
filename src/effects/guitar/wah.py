"""
Effet Wah-Wah
"""
import numpy as np
from scipy import signal
from ...audio.effects import Effect

class Wah(Effect):
    """Effet Wah-Wah classique"""
    
    def __init__(self):
        super().__init__("Wah")
        
        self.parameters = {
            'frequency': 500,   # 200-2000 Hz
            'resonance': 8.0,   # Q factor
            'mix': 1.0,
            'auto_wah': False,  # Auto-wah basé sur l'enveloppe
            'speed': 1.0        # Vitesse de l'auto-wah
        }
        
        self.filter_state = np.zeros(4, dtype=np.float32)
        self.envelope = 0.0
        self.lfo_phase = 0.0
    
    def process(self, audio: np.ndarray, sample_rate: int) -> np.ndarray:
        if self.sample_rate != sample_rate:
            self.sample_rate = sample_rate
        
        frequency = self.parameters['frequency']
        resonance = self.parameters['resonance']
        auto_wah = self.parameters['auto_wah']
        speed = self.parameters['speed']
        mix = self.parameters['mix']
        
        output = np.zeros_like(audio)
        
        for i in range(len(audio)):
            # Suiveur d'enveloppe pour auto-wah
            if auto_wah:
                input_level = np.abs(audio[i]).mean()
                self.envelope = 0.9 * self.envelope + 0.1 * input_level
                
                # Moduler la fréquence
                freq_mod = 200 + self.envelope * 1800
            else:
                # LFO pour wah manuel
                self.lfo_phase += 2 * np.pi * speed / sample_rate
                lfo = (np.sin(self.lfo_phase) + 1) / 2
                freq_mod = 200 + lfo * (frequency - 200) * 2
            
            # Filtre passe-bande résonant
            nyquist = sample_rate / 2
            freq_norm = np.clip(freq_mod / nyquist, 0.01, 0.99)
            
            # Calcul des coefficients (simplifié)
            w0 = 2 * np.pi * freq_norm
            alpha = np.sin(w0) / (2 * resonance)
            
            # Appliquer le filtre sur chaque canal
            for ch in range(audio.shape[1] if len(audio.shape) > 1 else 1):
                if len(audio.shape) > 1:
                    sample = audio[i, ch]
                else:
                    sample = audio[i]
                
                # Filtre biquad
                filtered = sample * alpha + self.filter_state[ch * 2]
                self.filter_state[ch * 2] = sample * alpha - filtered * alpha + self.filter_state[ch * 2 + 1]
                self.filter_state[ch * 2 + 1] = filtered
                
                if len(audio.shape) > 1:
                    output[i, ch] = filtered
                else:
                    output[i] = filtered
        
        return audio * (1 - mix) + output * mix
