"""
Chaîne de mastering professionnelle pour la sortie audio
"""
import numpy as np
from scipy import signal
from ..audio.effects import Effect
from ..utils.logger import logger

class ParametricEQ(Effect):
    """
    Égaliseur paramétrique 5 bandes
    """
    
    def __init__(self):
        super().__init__("Parametric EQ")
        
        self.parameters = {
            # Bande 1 - Low Shelf (80 Hz)
            'low_gain': 0.0,      # -12 à +12 dB
            'low_freq': 80.0,     # Hz
            
            # Bande 2 - Low Mid (250 Hz)
            'low_mid_gain': 0.0,
            'low_mid_freq': 250.0,
            'low_mid_q': 1.0,     # 0.1 à 10
            
            # Bande 3 - Mid (1000 Hz)
            'mid_gain': 0.0,
            'mid_freq': 1000.0,
            'mid_q': 1.0,
            
            # Bande 4 - High Mid (4000 Hz)
            'high_mid_gain': 0.0,
            'high_mid_freq': 4000.0,
            'high_mid_q': 1.0,
            
            # Bande 5 - High Shelf (12000 Hz)
            'high_gain': 0.0,
            'high_freq': 12000.0,
        }
        
        self.filters = {}
        self.enabled = False
        
        logger.info("ParametricEQ initialisé")
    
    def _design_shelving_filter(self, freq, gain_db, sample_rate, shelf_type='low'):
        """Créer un filtre shelving (low/high)"""
        if abs(gain_db) < 0.1:
            return None
        
        gain = 10 ** (gain_db / 20)
        w0 = 2 * np.pi * freq / sample_rate
        
        A = np.sqrt(gain)
        S = 1  # Shelf slope
        alpha = np.sin(w0) / 2 * np.sqrt((A + 1/A) * (1/S - 1) + 2)
        
        cos_w0 = np.cos(w0)
        
        if shelf_type == 'low':
            b0 = A * ((A+1) - (A-1)*cos_w0 + 2*np.sqrt(A)*alpha)
            b1 = 2*A * ((A-1) - (A+1)*cos_w0)
            b2 = A * ((A+1) - (A-1)*cos_w0 - 2*np.sqrt(A)*alpha)
            a0 = (A+1) + (A-1)*cos_w0 + 2*np.sqrt(A)*alpha
            a1 = -2 * ((A-1) + (A+1)*cos_w0)
            a2 = (A+1) + (A-1)*cos_w0 - 2*np.sqrt(A)*alpha
        else:  # high
            b0 = A * ((A+1) + (A-1)*cos_w0 + 2*np.sqrt(A)*alpha)
            b1 = -2*A * ((A-1) + (A+1)*cos_w0)
            b2 = A * ((A+1) + (A-1)*cos_w0 - 2*np.sqrt(A)*alpha)
            a0 = (A+1) - (A-1)*cos_w0 + 2*np.sqrt(A)*alpha
            a1 = 2 * ((A-1) - (A+1)*cos_w0)
            a2 = (A+1) - (A-1)*cos_w0 - 2*np.sqrt(A)*alpha
        
        return ([b0/a0, b1/a0, b2/a0], [1, a1/a0, a2/a0])
    
    def _design_peaking_filter(self, freq, gain_db, Q, sample_rate):
        """Créer un filtre peaking (bell)"""
        if abs(gain_db) < 0.1:
            return None
        
        gain = 10 ** (gain_db / 20)
        w0 = 2 * np.pi * freq / sample_rate
        alpha = np.sin(w0) / (2 * Q)
        
        A = gain
        cos_w0 = np.cos(w0)
        
        b0 = 1 + alpha * A
        b1 = -2 * cos_w0
        b2 = 1 - alpha * A
        a0 = 1 + alpha / A
        a1 = -2 * cos_w0
        a2 = 1 - alpha / A
        
        return ([b0/a0, b1/a0, b2/a0], [1, a1/a0, a2/a0])
    
    def on_parameter_change(self, param_name, value):
        """Recalculer les filtres quand les paramètres changent"""
        self.filters = {}  # Reset des filtres
    
    def process(self, audio: np.ndarray, sample_rate: int) -> np.ndarray:
        """Appliquer l'égalisation"""
        if not self.enabled:
            return audio
        
        output = audio.copy()
        
        # Bande 1 - Low Shelf
        low_filter = self._design_shelving_filter(
            self.parameters['low_freq'],
            self.parameters['low_gain'],
            sample_rate,
            'low'
        )
        if low_filter:
            for ch in range(audio.shape[1]):
                output[:, ch] = signal.lfilter(low_filter[0], low_filter[1], output[:, ch])
        
        # Bande 2 - Low Mid
        low_mid_filter = self._design_peaking_filter(
            self.parameters['low_mid_freq'],
            self.parameters['low_mid_gain'],
            self.parameters['low_mid_q'],
            sample_rate
        )
        if low_mid_filter:
            for ch in range(audio.shape[1]):
                output[:, ch] = signal.lfilter(low_mid_filter[0], low_mid_filter[1], output[:, ch])
        
        # Bande 3 - Mid
        mid_filter = self._design_peaking_filter(
            self.parameters['mid_freq'],
            self.parameters['mid_gain'],
            self.parameters['mid_q'],
            sample_rate
        )
        if mid_filter:
            for ch in range(audio.shape[1]):
                output[:, ch] = signal.lfilter(mid_filter[0], mid_filter[1], output[:, ch])
        
        # Bande 4 - High Mid
        high_mid_filter = self._design_peaking_filter(
            self.parameters['high_mid_freq'],
            self.parameters['high_mid_gain'],
            self.parameters['high_mid_q'],
            sample_rate
        )
        if high_mid_filter:
            for ch in range(audio.shape[1]):
                output[:, ch] = signal.lfilter(high_mid_filter[0], high_mid_filter[1], output[:, ch])
        
        # Bande 5 - High Shelf
        high_filter = self._design_shelving_filter(
            self.parameters['high_freq'],
            self.parameters['high_gain'],
            sample_rate,
            'high'
        )
        if high_filter:
            for ch in range(audio.shape[1]):
                output[:, ch] = signal.lfilter(high_filter[0], high_filter[1], output[:, ch])
        
        return output


class Compressor(Effect):
    """
    Compresseur dynamique
    """
    
    def __init__(self):
        super().__init__("Compressor")
        
        self.parameters = {
            'threshold': -20.0,   # dB (-60 à 0)
            'ratio': 4.0,         # 1 à 20
            'attack': 5.0,        # ms (0.1 à 100)
            'release': 50.0,      # ms (10 à 1000)
            'knee': 0.0,          # dB (0 à 10)
            'makeup_gain': 0.0,   # dB (0 à 24)
        }
        
        self.envelope = 0.0
        self.enabled = False
        
        logger.info("Compressor initialisé")
    
    def process(self, audio: np.ndarray, sample_rate: int) -> np.ndarray:
        """Appliquer la compression"""
        if not self.enabled:
            return audio
        
        output = audio.copy()
        
        threshold = self.parameters['threshold']
        ratio = self.parameters['ratio']
        attack_ms = self.parameters['attack']
        release_ms = self.parameters['release']
        knee = self.parameters['knee']
        makeup_gain = 10 ** (self.parameters['makeup_gain'] / 20)
        
        # Convertir attack/release en coefficients
        attack_coef = np.exp(-1000 / (attack_ms * sample_rate))
        release_coef = np.exp(-1000 / (release_ms * sample_rate))
        
        for i in range(len(audio)):
            # Calculer le niveau
            input_level = np.abs(audio[i]).max()
            input_db = 20 * np.log10(input_level + 1e-10)
            
            # Envelope follower
            if input_level > self.envelope:
                self.envelope = attack_coef * self.envelope + (1 - attack_coef) * input_level
            else:
                self.envelope = release_coef * self.envelope + (1 - release_coef) * input_level
            
            envelope_db = 20 * np.log10(self.envelope + 1e-10)
            
            # Calculer la réduction de gain
            if knee > 0 and envelope_db > (threshold - knee/2) and envelope_db < (threshold + knee/2):
                # Soft knee
                overshoot = envelope_db - threshold
                gain_reduction = overshoot * (1/ratio - 1) * ((overshoot / knee + 1) / 2)
            elif envelope_db > threshold:
                # Hard knee
                overshoot = envelope_db - threshold
                gain_reduction = overshoot * (1/ratio - 1)
            else:
                gain_reduction = 0
            
            # Appliquer la réduction
            gain = 10 ** (gain_reduction / 20)
            output[i] = audio[i] * gain * makeup_gain
        
        return output


class Limiter(Effect):
    """
    Limiteur (brick wall)
    """
    
    def __init__(self):
        super().__init__("Limiter")
        
        self.parameters = {
            'threshold': -1.0,    # dB (-12 à 0)
            'release': 50.0,      # ms (10 à 500)
        }
        
        self.envelope = 0.0
        self.enabled = False
        
        logger.info("Limiter initialisé")
    
    def process(self, audio: np.ndarray, sample_rate: int) -> np.ndarray:
        """Appliquer la limitation"""
        if not self.enabled:
            return audio
        
        output = audio.copy()
        
        threshold = 10 ** (self.parameters['threshold'] / 20)
        release_ms = self.parameters['release']
        release_coef = np.exp(-1000 / (release_ms * sample_rate))
        
        # Attack très rapide (1 sample)
        for i in range(len(audio)):
            input_level = np.abs(audio[i]).max()
            
            # Envelope follower
            if input_level > self.envelope:
                self.envelope = input_level  # Attack instantané
            else:
                self.envelope = release_coef * self.envelope + (1 - release_coef) * input_level
            
            # Calculer le gain
            if self.envelope > threshold:
                gain = threshold / self.envelope
            else:
                gain = 1.0
            
            output[i] = audio[i] * gain
        
        return output


class MasteringChain(Effect):
    """
    Chaîne de mastering complète
    EQ → Compressor → Limiter
    """
    
    def __init__(self):
        super().__init__("Mastering Chain")
        
        self.eq = ParametricEQ()
        self.compressor = Compressor()
        self.limiter = Limiter()
        
        self.enabled = False
        
        # Paramètres accessibles
        self.parameters = {
            'eq_enabled': False,
            'compressor_enabled': False,
            'limiter_enabled': True,  # Limiter activé par défaut pour protection
        }
        
        logger.info("MasteringChain initialisée")
    
    def process(self, audio: np.ndarray, sample_rate: int) -> np.ndarray:
        """Traiter l'audio avec la chaîne complète"""
        if not self.enabled:
            return audio
        
        output = audio
        
        # Ordre de traitement : EQ → Compressor → Limiter
        if self.parameters['eq_enabled']:
            output = self.eq.process(output, sample_rate)
        
        if self.parameters['compressor_enabled']:
            output = self.compressor.process(output, sample_rate)
        
        if self.parameters['limiter_enabled']:
            output = self.limiter.process(output, sample_rate)
        
        return output
    
    def get_eq(self):
        return self.eq
    
    def get_compressor(self):
        return self.compressor
    
    def get_limiter(self):
        return self.limiter
    
    def set_eq_parameter(self, param, value):
        self.eq.set_parameter(param, value)
    
    def set_compressor_parameter(self, param, value):
        self.compressor.set_parameter(param, value)
    
    def set_limiter_parameter(self, param, value):
        self.limiter.set_parameter(param, value)
    
    def toggle_eq(self):
        self.parameters['eq_enabled'] = not self.parameters['eq_enabled']
        self.eq.enabled = self.parameters['eq_enabled']
        logger.info(f"EQ: {'ON' if self.eq.enabled else 'OFF'}")
    
    def toggle_compressor(self):
        self.parameters['compressor_enabled'] = not self.parameters['compressor_enabled']
        self.compressor.enabled = self.parameters['compressor_enabled']
        logger.info(f"Compressor: {'ON' if self.compressor.enabled else 'OFF'}")
    
    def toggle_limiter(self):
        self.parameters['limiter_enabled'] = not self.parameters['limiter_enabled']
        self.limiter.enabled = self.parameters['limiter_enabled']
        logger.info(f"Limiter: {'ON' if self.limiter.enabled else 'OFF'}")
    
    def get_state(self):
        return {
            'enabled': self.enabled,
            'eq_enabled': self.parameters['eq_enabled'],
            'eq_params': self.eq.parameters.copy(),
            'compressor_enabled': self.parameters['compressor_enabled'],
            'compressor_params': self.compressor.parameters.copy(),
            'limiter_enabled': self.parameters['limiter_enabled'],
            'limiter_params': self.limiter.parameters.copy(),
        }