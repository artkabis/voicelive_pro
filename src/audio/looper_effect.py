"""
Wrapper pour intégrer le Looper dans la chaîne d'effets
"""
import numpy as np
from .effects import Effect
from .looper import Looper

class LooperEffect(Effect):
    """Wrapper pour utiliser le Looper comme un effet"""
    
    def __init__(self, sample_rate: int = 44100, max_duration: int = 120):  # 2 min par défaut
        super().__init__("Looper")
        
        # Créer le looper avec allocation dynamique
        self.looper = Looper(sample_rate, max_duration)
        self.enabled = True
        
        self.parameters = {
            'active_track': 0
        }
    
    def process(self, audio: np.ndarray, sample_rate: int) -> np.ndarray:
        """Traiter l'audio avec le looper"""
        if not self.enabled:
            return audio
        
        return self.looper.process(audio)
    
    def get_looper(self) -> Looper:
        """Obtenir l'instance du looper"""
        return self.looper
    
