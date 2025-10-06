"""
Compresseur dynamique
"""
import numpy as np
from ...audio.effects import Effect

class Compressor(Effect):
    """Compresseur avec attack/release"""
    
    def __init__(self):
        super().__init__("Compressor")
        
        self.parameters = {
            'threshold': -20,  # dB
            'ratio': 4.0,      # 1:1 to 20:1
            'attack': 0.005,   # secondes
            'release': 0.1,    # secondes
            'makeup': 0,       # dB
            'mix': 1.0
        }
        
        self.envelope = 0.0
    
    def process(self, audio: np.ndarray, sample_rate: int) -> np.ndarray:
        threshold = self.parameters['threshold']
        ratio = self.parameters['ratio']
        attack_time = self.parameters['attack']
        release_time = self.parameters['release']
        makeup = 10 ** (self.parameters['makeup'] / 20)
        mix = self.parameters['mix']
        
        attack_coef = np.exp(-1.0 / (attack_time * sample_rate))
        release_coef = np.exp(-1.0 / (release_time * sample_rate))
        
        threshold_lin = 10 ** (threshold / 20)
        
        output = np.zeros_like(audio)
        
        for i in range(len(audio)):
            # Niveau d'entrée
            input_level = np.abs(audio[i]).max()
            
            # Suiveur d'enveloppe
            if input_level > self.envelope:
                self.envelope = attack_coef * self.envelope + (1 - attack_coef) * input_level
            else:
                self.envelope = release_coef * self.envelope + (1 - release_coef) * input_level
            
            # Calcul du gain
            if self.envelope > threshold_lin:
                # Au-dessus du seuil, compresser
                excess = self.envelope / threshold_lin
                gain_reduction = excess ** ((1 / ratio) - 1)
            else:
                gain_reduction = 1.0
            
            # Appliquer le gain
            output[i] = audio[i] * gain_reduction * makeup
        
        return audio * (1 - mix) + output * mix
