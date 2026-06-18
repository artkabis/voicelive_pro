"""
Effet Flanger
"""
import numpy as np
from ...audio.effects import Effect

class Flanger(Effect):
    """Flanger avec feedback"""
    
    def __init__(self):
        super().__init__("Flanger")
        
        self.parameters = {
            'rate': 0.2,      # Hz
            'depth': 0.8,     # 0-1
            'feedback': 0.7,  # 0-0.95
            'delay': 0.003,   # 3ms base
            'mix': 0.5
        }
        
        self.buffer_size = 1000
        self.delay_buffer = np.zeros((self.buffer_size, 2), dtype=np.float32)
        self.write_pos = 0
        self.lfo_phase = 0.0
    
    def process(self, audio: np.ndarray, sample_rate: int) -> np.ndarray:
        rate = self.parameters['rate']
        depth = self.parameters['depth']
        base_delay = self.parameters['delay']
        feedback = self.parameters['feedback']
        mix = self.parameters['mix']
        
        output = np.zeros_like(audio)
        
        for i in range(len(audio)):
            # LFO
            self.lfo_phase += 2 * np.pi * rate / sample_rate
            lfo = np.sin(self.lfo_phase) % (2 * np.pi)
            
            # Delay très court modulé
            modulated_delay = base_delay * (1.0 + lfo * depth)
            delay_samples = int(modulated_delay * sample_rate)
            delay_samples = np.clip(delay_samples, 1, self.buffer_size - 1)
            
            read_pos = (self.write_pos - delay_samples) % self.buffer_size
            delayed = self.delay_buffer[read_pos]
            
            # Feedback élevé pour effet jet plane
            self.delay_buffer[self.write_pos] = audio[i] + delayed * feedback
            
            output[i] = delayed
            self.write_pos = (self.write_pos + 1) % self.buffer_size
        
        return audio * (1 - mix) + output * mix
