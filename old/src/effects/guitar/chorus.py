"""
Effet Chorus - Corona style
"""
import numpy as np
from ...audio.effects import Effect

class Chorus(Effect):
    """Chorus classique avec LFO"""
    
    def __init__(self):
        super().__init__("Chorus")
        
        self.parameters = {
            'rate': 0.5,      # Hz (0.1-5)
            'depth': 0.5,     # 0-1
            'delay': 0.02,    # 20ms base
            'feedback': 0.3,
            'mix': 0.5
        }
        
        max_delay = 0.05  # 50ms
        self.buffer_size = int(max_delay * 44100) + 100
        self.delay_buffer = np.zeros((self.buffer_size, 2), dtype=np.float32)
        self.write_pos = 0
        
        self.lfo_phase = 0.0
    
    def process(self, audio: np.ndarray, sample_rate: int) -> np.ndarray:
        if self.sample_rate != sample_rate:
            self.sample_rate = sample_rate
            self.buffer_size = int(0.05 * sample_rate) + 100
            self.delay_buffer = np.zeros((self.buffer_size, 2), dtype=np.float32)
        
        rate = self.parameters['rate']
        depth = self.parameters['depth']
        base_delay = self.parameters['delay']
        feedback = self.parameters['feedback']
        mix = self.parameters['mix']
        
        output = np.zeros_like(audio)
        
        for i in range(len(audio)):
            # LFO
            self.lfo_phase += 2 * np.pi * rate / sample_rate
            if self.lfo_phase > 2 * np.pi:
                self.lfo_phase -= 2 * np.pi
            
            lfo = np.sin(self.lfo_phase)
            
            # Delay modulé
            modulated_delay = base_delay * (1.0 + lfo * depth)
            delay_samples = int(modulated_delay * sample_rate)
            delay_samples = np.clip(delay_samples, 0, self.buffer_size - 1)
            
            # Lire depuis le buffer
            read_pos = (self.write_pos - delay_samples) % self.buffer_size
            delayed = self.delay_buffer[read_pos]
            
            # Écrire dans le buffer avec feedback
            self.delay_buffer[self.write_pos] = audio[i] + delayed * feedback
            
            output[i] = delayed
            self.write_pos = (self.write_pos + 1) % self.buffer_size
        
        return audio * (1 - mix) + output * mix
