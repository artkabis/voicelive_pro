"""
Reverb Professionnel - Hall of Fame style
"""
import numpy as np
from scipy import signal
from ...audio.effects import Effect

class ReverbPro(Effect):
    """
    Réverbération professionnelle avec algorithme Freeverb amélioré
    """
    
    def __init__(self):
        super().__init__("ReverbPro")
        
        self.parameters = {
            'room_size': 0.5,   # 0.0 - 1.0
            'damping': 0.5,     # 0.0 - 1.0 (HF damping)
            'width': 1.0,       # Stéréo width
            'wet': 0.3,         # Wet level
            'dry': 0.7,         # Dry level
            'pre_delay': 0.02   # Pre-delay (secondes)
        }
        
        # Paramètres Freeverb
        self.comb_delays = [1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617]
        self.allpass_delays = [556, 441, 341, 225]
        
        self.comb_buffers = []
        self.comb_positions = []
        self.comb_filter_states = []
        
        self.allpass_buffers = []
        self.allpass_positions = []
        
        self.pre_delay_buffer = None
        self.pre_delay_pos = 0
    
    def process(self, audio: np.ndarray, sample_rate: int) -> np.ndarray:
        """Appliquer la réverbération"""
        if self.sample_rate != sample_rate:
            self.sample_rate = sample_rate
            self._init_reverb()
        
        if not self.comb_buffers:
            self._init_reverb()
        
        room_size = self.parameters['room_size']
        damping = self.parameters['damping']
        wet = self.parameters['wet']
        dry = self.parameters['dry']
        
        # Pre-delay
        audio_delayed = self._apply_predelay(audio)
        
        # Reverb (mono processing)
        if len(audio.shape) > 1:
            mono = (audio_delayed[:, 0] + audio_delayed[:, 1]) / 2
        else:
            mono = audio_delayed
        
        reverbed = self._freeverb_process(mono, room_size, damping)
        
        # Stéréo spread
        if len(audio.shape) > 1:
            reverbed_stereo = np.column_stack([reverbed, reverbed])
        else:
            reverbed_stereo = reverbed
        
        # Mix
        return audio * dry + reverbed_stereo * wet
    
    def _apply_predelay(self, audio: np.ndarray) -> np.ndarray:
        """Appliquer le pre-delay"""
        pre_delay_time = self.parameters['pre_delay']
        delay_samples = int(pre_delay_time * self.sample_rate)
        
        if delay_samples == 0:
            return audio
        
        if self.pre_delay_buffer is None or len(self.pre_delay_buffer) != delay_samples:
            self.pre_delay_buffer = np.zeros((delay_samples, 2), dtype=np.float32)
            self.pre_delay_pos = 0
        
        output = np.zeros_like(audio)
        
        for i in range(len(audio)):
            output[i] = self.pre_delay_buffer[self.pre_delay_pos]
            self.pre_delay_buffer[self.pre_delay_pos] = audio[i]
            self.pre_delay_pos = (self.pre_delay_pos + 1) % delay_samples
        
        return output
    
    def _freeverb_process(self, audio: np.ndarray, room_size: float, damping: float) -> np.ndarray:
        """Algorithme Freeverb simplifié"""
        output = np.zeros_like(audio)
        
        # Parallel comb filters
        for i in range(len(self.comb_delays)):
            delay = self.comb_delays[i]
            buffer = self.comb_buffers[i]
            pos = self.comb_positions[i]
            filter_state = self.comb_filter_states[i]
            
            comb_out = np.zeros_like(audio)
            
            feedback = 0.28 + room_size * 0.7
            damp1 = damping
            damp2 = 1.0 - damping
            
            for j in range(len(audio)):
                # Lire du buffer
                read_pos = (pos - delay) % len(buffer)
                delayed = buffer[read_pos]
                
                # Lowpass filter pour damping
                filter_state[0] = damp2 * filter_state[0] + damp1 * delayed
                
                # Écrire avec feedback
                buffer[pos] = audio[j] + filter_state[0] * feedback
                comb_out[j] = delayed
                
                pos = (pos + 1) % len(buffer)
            
            self.comb_positions[i] = pos
            self.comb_filter_states[i] = filter_state
            
            output += comb_out
        
        # Serial allpass filters
        signal_chain = output / len(self.comb_delays)
        
        for i in range(len(self.allpass_delays)):
            delay = self.allpass_delays[i]
            buffer = self.allpass_buffers[i]
            pos = self.allpass_positions[i]
            
            allpass_out = np.zeros_like(signal_chain)
            
            for j in range(len(signal_chain)):
                read_pos = (pos - delay) % len(buffer)
                delayed = buffer[read_pos]
                
                buffer[pos] = signal_chain[j] + delayed * 0.5
                allpass_out[j] = delayed - signal_chain[j] * 0.5
                
                pos = (pos + 1) % len(buffer)
            
            self.allpass_positions[i] = pos
            signal_chain = allpass_out
        
        return signal_chain
    
    def _init_reverb(self):
        """Initialiser les buffers de reverb"""
        # Comb filters
        self.comb_buffers = []
        self.comb_positions = []
        self.comb_filter_states = []
        
        for delay in self.comb_delays:
            buffer_size = delay + 100
            self.comb_buffers.append(np.zeros(buffer_size, dtype=np.float32))
            self.comb_positions.append(0)
            self.comb_filter_states.append(np.array([0.0]))
        
        # Allpass filters
        self.allpass_buffers = []
        self.allpass_positions = []
        
        for delay in self.allpass_delays:
            buffer_size = delay + 100
            self.allpass_buffers.append(np.zeros(buffer_size, dtype=np.float32))
            self.allpass_positions.append(0)
    
    def reset(self):
        """Réinitialiser les buffers"""
        for buffer in self.comb_buffers + self.allpass_buffers:
            buffer.fill(0)
        self.comb_positions = [0] * len(self.comb_delays)
        self.allpass_positions = [0] * len(self.allpass_delays)
        if self.pre_delay_buffer is not None:
            self.pre_delay_buffer.fill(0)
