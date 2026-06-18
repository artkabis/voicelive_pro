"""
Accordeur chromatique professionnel
"""
import numpy as np
from threading import Lock
from ..audio.effects import Effect
from ..utils.logger import logger

class ChromaticTuner(Effect):
    """
    Accordeur chromatique avec détection de fréquence par autocorrélation
    """
    
    # Notes et fréquences de référence (A4 = 440 Hz)
    NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
    A4_FREQ = 440.0
    
    def __init__(self, sample_rate: int = 44100):
        super().__init__("Chromatic Tuner")
        
        self.sample_rate = sample_rate
        self.buffer_size = 8192  # ✅ AUGMENTÉ de 4096 à 8192 pour mieux capter les basses fréquences
        
        self.parameters = {
            'reference_pitch': 440.0,  # A4 de référence (Hz)
            'sensitivity': 0.5,         # 0.0 à 1.0
            'noise_gate': 0.015,  # ✅ RÉDUIT de 0.02 à 0.015 (1.5%) pour mieux capter les cordes
        }
        
        # État actuel
        self.current_frequency = 0.0
        self.current_note = None
        self.current_octave = 0
        self.cents_offset = 0.0  # Décalage en cents (-50 à +50)
        self.confidence = 0.0    # Confiance de la détection (0 à 1)
        
        # Buffer circulaire pour l'analyse
        self.analysis_buffer = np.zeros(self.buffer_size)
        self.buffer_index = 0

        # ✅ NOUVEAU: Historique pour stabilité
        self.freq_history = []
        self.max_history = 5
        
        self.lock = Lock()
        self.enabled = False
        
        logger.info("ChromaticTuner initialisé")
    
    def _freq_to_note(self, frequency: float) -> tuple:
        """
        Convertir une fréquence en note + octave + cents
        
        Returns:
            (note_name, octave, cents_offset)
        """
        if frequency <= 0:
            return (None, 0, 0.0)
        
        # Calculer la distance en demi-tons depuis A4
        semitones_from_a4 = 12 * np.log2(frequency / self.parameters['reference_pitch'])
        
        # Note MIDI
        midi_note = 69 + semitones_from_a4  # A4 = MIDI 69
        
        # Note arrondie
        note_index = int(round(midi_note)) % 12
        note_name = self.NOTE_NAMES[note_index]
        
        # Octave
        octave = int(round(midi_note)) // 12 - 1
        
        # Cents (différence fine)
        cents = (semitones_from_a4 - round(semitones_from_a4)) * 100
        
        return (note_name, octave, cents)
    
    def _calculate_rms(self, signal: np.ndarray) -> float:
        """
        Calculer le RMS (Root Mean Square) pour mesurer la puissance du signal
        """
        return np.sqrt(np.mean(signal**2))
    
    def _autocorrelation(self, signal: np.ndarray) -> float:
        """
        Détection de pitch par autocorrélation améliorée (YIN-like algorithm)
        
        Returns:
            frequency en Hz, ou 0.0 si pas détectée
        """
        # Normaliser le signal
        signal = signal - np.mean(signal)
        
        # Vérifier le RMS
        rms = self._calculate_rms(signal)
        noise_gate = self.parameters.get('noise_gate', 0.02)
        
        if rms < noise_gate:
            return 0.0
        
        # Autocorrélation
        corr = np.correlate(signal, signal, mode='full')
        corr = corr[len(corr)//2:]
        
        # Normaliser
        if corr[0] > 0:
            corr = corr / corr[0]
        else:
            return 0.0
        
        # ✅ CORRECTION CRITIQUE: Définir la plage de recherche pour guitare
        # E2 (82 Hz) nécessite ~538 samples de période à 44100 Hz
        # E6 (1318 Hz) nécessite ~33 samples de période
        min_period = int(self.sample_rate / 1500)  # ~29 samples pour 1500 Hz
        max_period = int(self.sample_rate / 70)     # ~630 samples pour 70 Hz
        
        # Limiter à la taille du buffer
        max_period = min(max_period, len(corr) - 1)
        
        if max_period <= min_period:
            return 0.0
        
        # ✅ AMÉLIORATION: Différence cumulée normalisée (méthode YIN)
        # Cela aide à trouver la vraie fondamentale au lieu d'une harmonique
        diff = np.zeros(max_period)
        
        for tau in range(1, max_period):
            for i in range(len(signal) - max_period):
                diff[tau] += (signal[i] - signal[i + tau]) ** 2
        
        # Normalisation cumulative
        cumsum = 0
        normalized_diff = np.zeros(max_period)
        normalized_diff[0] = 1
        
        for tau in range(1, max_period):
            cumsum += diff[tau]
            if cumsum == 0:
                normalized_diff[tau] = 1
            else:
                normalized_diff[tau] = diff[tau] / (cumsum / tau)
        
        # ✅ Chercher le premier minimum significatif
        threshold = 0.1  # Seuil de confiance
        
        for tau in range(min_period, max_period):
            if normalized_diff[tau] < threshold:
                # Chercher le minimum local autour de ce point
                local_min_tau = tau
                local_min_val = normalized_diff[tau]
                
                # Vérifier les voisins
                for delta in range(-5, 6):
                    check_tau = tau + delta
                    if min_period <= check_tau < max_period:
                        if normalized_diff[check_tau] < local_min_val:
                            local_min_val = normalized_diff[check_tau]
                            local_min_tau = check_tau
                
                # Interpolation parabolique pour plus de précision
                if 0 < local_min_tau < max_period - 1:
                    alpha = normalized_diff[local_min_tau - 1]
                    beta = normalized_diff[local_min_tau]
                    gamma = normalized_diff[local_min_tau + 1]
                    
                    if alpha - 2*beta + gamma != 0:
                        peak_tau = local_min_tau + 0.5 * (alpha - gamma) / (alpha - 2*beta + gamma)
                    else:
                        peak_tau = local_min_tau
                else:
                    peak_tau = local_min_tau
                
                # Calculer la fréquence
                frequency = self.sample_rate / peak_tau
                
                # ✅ Vérifier que c'est une fréquence de guitare valide
                if 70 < frequency < 1500:
                    # ✅ Vérification supplémentaire : le pic doit être fort
                    if local_min_val < 0.2:  # Bonne corrélation
                        return frequency
                
                # Si le premier minimum n'est pas bon, continuer la recherche
                break
        
        return 0.0
    
    def _is_stable_frequency(self, freq: float) -> bool:
        """
        ✅ NOUVEAU: Vérifier si la fréquence est stable
        """
        if len(self.freq_history) < 2:
            return True  # Pas assez d'historique
        
        # Calculer la variation
        variation = np.std(self.freq_history)
        # Accepter si variation < 3 Hz
        return variation < 3.0
    
    def process(self, audio: np.ndarray, sample_rate: int) -> np.ndarray:
        """
        Analyser l'audio en entrée pour détecter la note
        L'accordeur n'altère PAS l'audio
        """
        if not self.enabled:
            return audio
        
        with self.lock:
            # Prendre le canal gauche (mono)
            mono_audio = audio[:, 0] if len(audio.shape) > 1 else audio
            
            # Remplir le buffer circulaire
            for sample in mono_audio:
                self.analysis_buffer[self.buffer_index] = sample
                self.buffer_index = (self.buffer_index + 1) % self.buffer_size
            
            # Analyser toutes les 512 samples au lieu de 256 pour les basses fréquences
            if self.buffer_index % 512 == 0:
                # ✅ NOUVEAU: Calculer le RMS pour la confidence
                rms = self._calculate_rms(self.analysis_buffer)

                # Détecter la fréquence
                freq = self._autocorrelation(self.analysis_buffer)
                
                if freq > 0:
                    # ✅ NOUVEAU: Ajouter à l'historique
                    self.freq_history.append(freq)
                    if len(self.freq_history) > self.max_history:
                        self.freq_history.pop(0)
                  # ✅ NOUVEAU: Ne mettre à jour que si stable
                    if self._is_stable_frequency(freq):
                        self.current_frequency = freq
                        note, octave, cents = self._freq_to_note(freq)
                        self.current_note = note
                        self.current_octave = octave
                        self.cents_offset = cents
                        
                        # ✅ AMÉLIORATION: Confidence basée sur RMS + stabilité
                        stability = 1.0 - min(1.0, np.std(self.freq_history) / 10.0) if len(self.freq_history) > 1 else 0.5
                        self.confidence = min(1.0, rms * 10) * stability
                    else:
                        # Fréquence instable = probablement du bruit
                        self.confidence = max(0.0, self.confidence - 0.2)
                else:
                    # Pas de signal suffisant
                    self.current_frequency = 0.0
                    self.current_note = None
                    self.confidence = 0.0
                    self.freq_history.clear()
        
        return audio  # L'accordeur ne modifie pas l'audio
    
    def set_noise_gate(self, threshold: float):
        """✅ NOUVEAU: Définir le seuil de noise gate"""
        self.parameters['noise_gate'] = max(0.001, min(0.1, threshold))
        logger.info(f"Tuner: Noise gate set to {self.parameters['noise_gate']:.3f}")

    def get_state(self):
        """Obtenir l'état actuel de l'accordeur"""
        with self.lock:
            return {
                'enabled': bool(self.enabled),  # Convertir en bool natif
                'frequency': round(self.current_frequency, 2),
                'note': self.current_note,
                'octave': self.current_octave,
                'cents': round(self.cents_offset, 1),
                'confidence': round(self.confidence, 2),
                'in_tune': bool(abs(self.cents_offset) < 5 if self.current_note else False),  # Convertir en bool natif
                'reference_pitch': self.parameters['reference_pitch'],
            }
    
    def set_reference_pitch(self, frequency: float):
        """Définir la fréquence de référence (A4)"""
        self.parameters['reference_pitch'] = max(400, min(480, frequency))
        logger.info(f"Tuner: Reference pitch set to {self.parameters['reference_pitch']:.1f} Hz")


class TunerDisplay:
    """
    Générateur de données pour l'affichage visuel de l'accordeur
    """
    
    @staticmethod
    def get_needle_position(cents: float) -> float:
        """
        Convertir cents en position d'aiguille
        
        Returns:
            Position de -1.0 (très grave) à +1.0 (très aigu)
            0.0 = parfaitement accordé
        """
        # Limiter à ±50 cents
        cents = max(-50, min(50, cents))
        return cents / 50.0
    
    @staticmethod
    def get_color_for_cents(cents: float) -> str:
        """
        Retourner une couleur en fonction de l'écart
        
        Returns:
            Code couleur: 'green', 'yellow', 'red'
        """
        abs_cents = abs(cents)
        
        if abs_cents < 5:
            return 'green'  # Parfaitement accordé
        elif abs_cents < 15:
            return 'yellow'  # Proche
        else:
            return 'red'  # Désaccordé
    
    @staticmethod
    def get_frequency_spectrum(frequency: float, reference: float = 440.0) -> dict:
        """
        Générer des données pour un affichage spectral
        """
        if frequency <= 0:
            return {'valid': False}
        
        # Calculer les harmoniques
        harmonics = [frequency * i for i in range(1, 5)]
        
        # Différence avec la référence
        freq_diff = frequency - reference
        
        return {
            'valid': True,
            'fundamental': frequency,
            'harmonics': harmonics,
            'diff_from_reference': freq_diff,
        }