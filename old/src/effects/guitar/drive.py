"""
Effet Drive/Distortion
"""
import numpy as np
from ...audio.effects import Effect

class Drive(Effect):
    def __init__(self):
        super().__init__("Drive")
        
        self.parameters = {
            'drive': 0.5,
            'tone': 0.5,
            'level': 0.7,
            'mix': 0.5
        }
        
        self.filter_state = np.zeros(2, dtype=np.float32)
    
    def process(self, audio: np.ndarray, sample_rate: int) -> np.ndarray:
        drive = self.parameters['drive']
        tone = self.parameters['tone']
        level = self.parameters['level']
        mix = self.parameters['mix']
        
        dry = audio.copy()
        gained = audio * (1.0 + drive * 20.0)
        distorted = np.tanh(gained)
        
        output = distorted.copy()
        cutoff = 500 + tone * 4500
        alpha = self._calc_filter_alpha(cutoff, sample_rate)
        
        for i in range(len(output)):
            for ch in range(output.shape[1] if len(output.shape) > 1 else 1):
                if len(output.shape) > 1:
                    self.filter_state[ch] = alpha * output[i, ch] + (1 - alpha) * self.filter_state[ch]
                    output[i, ch] = self.filter_state[ch]
                else:
                    self.filter_state[0] = alpha * output[i] + (1 - alpha) * self.filter_state[0]
                    output[i] = self.filter_state[0]
        
        output *= level
        return dry * (1.0 - mix) + output * mix
    
    def _calc_filter_alpha(self, cutoff: float, sample_rate: int) -> float:
        rc = 1.0 / (2.0 * np.pi * cutoff)
        dt = 1.0 / sample_rate
        return dt / (rc + dt)
    
    def reset(self):
        self.filter_state.fill(0)
