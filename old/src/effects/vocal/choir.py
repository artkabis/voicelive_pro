"""
Effet Choir - Simulation de choeur
"""
import numpy as np
from ...audio.effects import Effect

class Choir(Effect):
    """
    Effet de choeur avec multiples voix légèrement désaccordées
    """
    
    def __init__(self):
        super().__init__("Choir")
        
        self.parameters = {
            'voices': 8,        # Nombre de voix (4-16)
            'spread': 0.02,     # Désaccordage (0-0.1)
            'depth': 0.3,       # Profondeur de modulation
            'rate': 0.5,        # Vitesse de modulation (Hz)
            'mix': 0.5
        }
        
        self.lfo_phase = np.random.random(16) * 2 * np.pi
        self.delay_buffers = []
        self.buffer_positions = []
    
    def process(self, audio: np.ndarray, sample_rate: int) -> np.ndarray:
        """Créer l'effet de choeur"""
        if self.sample_rate != sample_rate:
            self.sample_rate = sample_rate
            self._init_buffers()
        
        if not self.delay_buffers:
            self._init_buffers()
        
        voices = int(self.parameters['voices'])
        spread = self.parameters['spread']
        depth = self.parameters['depth']
        rate = self.parameters['rate']
        mix = self.parameters['mix']
        
        output = audio.copy() * (1.0 - mix)
        
        # Créer chaque voix du choeur
        for i in range(voices):
            # LFO unique pour chaque voix
            lfo = np.sin(self.lfo_phase[i])
            self.lfo_phase[i] += 2 * np.pi * rate / sample_rate
            self.lfo_phase[i] %= 2 * np.pi
            
            # Delay modulé
            base_delay = 0.02 + (i / voices) * spread
            modulated_delay = base_delay * (1.0 + lfo * depth)
            delay_samples = int(modulated_delay * sample_rate)
            
            # Appliquer le delay
            buffer = self.delay_buffers[i]
            pos = self.buffer_positions[i]
            
            voice = np.zeros_like(audio)
            
            for frame in range(len(audio)):
                read_pos = (pos - delay_samples) % len(buffer)
                voice[frame] = buffer[read_pos]
                buffer[pos] = audio[frame]
                pos = (pos + 1) % len(buffer)
            
            self.buffer_positions[i] = pos
            
            # Ajouter au mix
            output += voice * (mix / voices)
        
        # Normaliser
        max_val = np.abs(output).max()
        if max_val > 0.9:
            output *= 0.9 / max_val
        
        return output
    
    def _init_buffers(self):
        """Initialiser les buffers de delay"""
        max_delay = 0.1  # 100ms
        buffer_size = int(max_delay * self.sample_rate)
        
        self.delay_buffers = [
            np.zeros((buffer_size, 2), dtype=np.float32)
            for _ in range(16)
        ]
        self.buffer_positions = [0] * 16
        self.lfo_phase = np.random.random(16) * 2 * np.pi
