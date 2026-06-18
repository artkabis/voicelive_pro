"""
Effet Vocoder - Voix robot
"""
import numpy as np
from scipy import signal
from ...audio.effects import Effect

class Vocoder(Effect):
    """
    Vocoder classique avec banque de filtres
    Transforme la voix en son robotique
    """
    
    def __init__(self):
        super().__init__("Vocoder")
        
        self.parameters = {
            'bands': 16,        # Nombre de bandes (8-32)
            'carrier_type': 'saw',  # saw, square, sine
            'carrier_freq': 220.0,  # Fréquence porteuse (Hz)
            'mix': 0.8
        }
        
        self.filters = []
        self.carrier_phase = 0.0
        self.envelope_followers = []
    
    def process(self, audio: np.ndarray, sample_rate: int) -> np.ndarray:
        """Appliquer l'effet vocoder"""
        if self.sample_rate != sample_rate:
            self.sample_rate = sample_rate
            self._init_vocoder()
        
        if not self.filters:
            self._init_vocoder()
        
        mix = self.parameters['mix']
        
        # Générer le signal porteur (carrier)
        carrier = self._generate_carrier(len(audio), sample_rate)
        
        # Analyser le signal modulateur (voix)
        modulator = audio
        
        # Appliquer le vocoder
        vocoded = self._vocode(modulator, carrier, sample_rate)
        
        # Mix
        return audio * (1.0 - mix) + vocoded * mix
    
    def _generate_carrier(self, length: int, sample_rate: int) -> np.ndarray:
        """Générer le signal porteur"""
        carrier_type = self.parameters['carrier_type']
        freq = self.parameters['carrier_freq']
        
        t = np.arange(length) / sample_rate
        phase = 2 * np.pi * freq * t + self.carrier_phase
        
        if carrier_type == 'saw':
            carrier = 2 * (phase / (2 * np.pi) % 1.0) - 1
        elif carrier_type == 'square':
            carrier = np.sign(np.sin(phase))
        else:  # sine
            carrier = np.sin(phase)
        
        # Mettre à jour la phase pour continuité
        self.carrier_phase = phase[-1] % (2 * np.pi)
        
        # Stéréo si nécessaire
        if length > 0:
            carrier = np.column_stack([carrier, carrier])
        
        return carrier
    
    def _vocode(self, modulator: np.ndarray, carrier: np.ndarray, sample_rate: int) -> np.ndarray:
        """Processus de vocoding"""
        bands = self.parameters['bands']
        
        # Créer les bandes de fréquences (logarithmiques)
        freqs = np.logspace(np.log10(80), np.log10(8000), bands + 1)
        
        output = np.zeros_like(modulator)
        
        # Pour chaque bande
        for i in range(bands):
            low_freq = freqs[i]
            high_freq = freqs[i + 1]
            
            # Filtrer le modulateur
            mod_filtered = self._bandpass_filter(modulator[:, 0], low_freq, high_freq, sample_rate)
            
            # Filtrer le carrier
            car_filtered = self._bandpass_filter(carrier[:, 0], low_freq, high_freq, sample_rate)
            
            # Extraire l'enveloppe du modulateur
            envelope = self._envelope_follower(np.abs(mod_filtered))
            
            # Appliquer l'enveloppe au carrier
            vocoded_band = car_filtered * envelope
            
            # Ajouter à la sortie
            output[:, 0] += vocoded_band
            output[:, 1] += vocoded_band  # Copier en stéréo
        
        # Normaliser
        max_val = np.abs(output).max()
        if max_val > 0.9:
            output *= 0.9 / max_val
        
        return output
    
    def _bandpass_filter(self, audio: np.ndarray, low: float, high: float, sample_rate: int) -> np.ndarray:
        """Filtre passe-bande"""
        nyquist = sample_rate / 2.0
        low_norm = low / nyquist
        high_norm = high / nyquist
        
        # Limiter les fréquences
        low_norm = np.clip(low_norm, 0.001, 0.999)
        high_norm = np.clip(high_norm, 0.001, 0.999)
        
        if high_norm <= low_norm:
            return audio
        
        # Filtre butterworth
        sos = signal.butter(4, [low_norm, high_norm], btype='band', output='sos')
        filtered = signal.sosfilt(sos, audio)
        
        return filtered
    
    def _envelope_follower(self, audio: np.ndarray, attack: float = 0.01, release: float = 0.1) -> np.ndarray:
        """Suiveur d'enveloppe"""
        envelope = np.zeros_like(audio)
        current = 0.0
        
        attack_coef = np.exp(-1.0 / (attack * self.sample_rate))
        release_coef = np.exp(-1.0 / (release * self.sample_rate))
        
        for i in range(len(audio)):
            if audio[i] > current:
                current = attack_coef * current + (1 - attack_coef) * audio[i]
            else:
                current = release_coef * current + (1 - release_coef) * audio[i]
            
            envelope[i] = current
        
        return envelope
    
    def _init_vocoder(self):
        """Initialiser le vocoder"""
        bands = self.parameters['bands']
        self.envelope_followers = [0.0] * bands
        self.carrier_phase = 0.0
