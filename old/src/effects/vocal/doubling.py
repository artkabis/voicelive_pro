"""
Effet de Doubling Vocal
"""
import numpy as np
from ...audio.effects import Effect

class Doubling(Effect):
    def __init__(self):
        super().__init__("Doubling")
        
        self.parameters = {
            'mix': 0.5,
            'delay': 0.03,
            'voices': 2
        }
        
        self.delay_buffers = []
        self.buffer_positions = []
    
    def process(self, audio: np.ndarray, sample_rate: int) -> np.ndarray:
        if self.sample_rate != sample_rate:
            self.sample_rate = sample_rate
            self._init_buffers()
        
        if not self.delay_buffers:
            self._init_buffers()
        
        mix = self.parameters['mix']
        num_voices = self.parameters['voices']
        
        output = audio.copy()
        
        for i in range(num_voices):
            delay_time = self.parameters['delay'] * (1.0 + i * 0.1)
            delay_samples = int(delay_time * sample_rate)
            
            if i >= len(self.delay_buffers):
                continue
            
            buffer = self.delay_buffers[i]
            pos = self.buffer_positions[i]
            
            doubled = np.zeros_like(audio)
            
            for frame in range(len(audio)):
                read_pos = (pos - delay_samples) % len(buffer)
                doubled[frame] = buffer[read_pos]
                buffer[pos] = audio[frame]
                pos = (pos + 1) % len(buffer)
            
            self.buffer_positions[i] = pos
            output += doubled * (mix / num_voices) * 0.7
        
        max_val = np.abs(output).max()
        if max_val > 0.9:
            output *= 0.9 / max_val
        
        return output
    
    def _init_buffers(self):
        max_delay = 0.1
        buffer_size = int(max_delay * self.sample_rate)
        
        num_voices = self.parameters['voices']
        self.delay_buffers = [
            np.zeros((buffer_size, 2), dtype=np.float32)
            for _ in range(num_voices)
        ]
        self.buffer_positions = [0] * num_voices
    
    def reset(self):
        for buffer in self.delay_buffers:
            buffer.fill(0)
        self.buffer_positions = [0] * len(self.delay_buffers)
