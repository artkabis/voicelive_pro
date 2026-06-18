"""
Moteur audio OPTIMISÉ pour ASIO et latence minimale
"""
import sounddevice as sd
import numpy as np
from threading import Lock
from typing import Optional, List
from ..utils.config import Config
from ..utils.logger import logger
from .effects import Effect

class AudioEngineOptimized:
    """
    Moteur audio optimisé pour ASIO avec latence ultra-basse
    """
    
    def __init__(self, sample_rate: int = 44100, buffer_size: int = 128):
        self.sample_rate = sample_rate
        self.buffer_size = buffer_size  # Petit buffer pour latence minimale
        self.channels = Config.CHANNELS
        
        self.stream: Optional[sd.Stream] = None
        self.is_running = False
        
        self.effect_chain: List[Effect] = []
        self.lock = Lock()
        
        # Monitoring optimisé
        self.input_level = 0.0
        self.output_level = 0.0
        self.peak_input = 0.0
        self.peak_output = 0.0
        
        self.bypass = False
        
        # Stats de performance
        self.callback_count = 0
        self.underrun_count = 0
        self.overrun_count = 0
        
        logger.info(f"Moteur OPTIMISÉ initialisé (SR: {self.sample_rate} Hz, Buffer: {self.buffer_size})")
    
    def add_effect(self, effect: Effect, position: Optional[int] = None):
        with self.lock:
            if position is None:
                self.effect_chain.append(effect)
            else:
                self.effect_chain.insert(position, effect)
            logger.info(f"Effet ajouté: {effect.name}")
    
    def remove_effect(self, effect: Effect):
        with self.lock:
            if effect in self.effect_chain:
                self.effect_chain.remove(effect)
    
    def clear_effects(self):
        with self.lock:
            self.effect_chain.clear()
    
    def get_effects(self) -> List[Effect]:
        with self.lock:
            return self.effect_chain.copy()
    
    def audio_callback(self, indata, outdata, frames, time_info, status):
        """Callback optimisé pour latence minimale"""
        self.callback_count += 1
        
        # Compter les erreurs
        if status:
            if status.input_underflow or status.output_underflow:
                self.underrun_count += 1
            if status.input_overflow or status.output_overflow:
                self.overrun_count += 1
        
        try:
            # Traitement direct sur le buffer sans copie inutile
            audio = indata
            
            # Monitoring (calcul rapide)
            self.input_level = float(np.mean(np.abs(audio)))
            
            if self.bypass:
                outdata[:] = audio
                return
            
            # Appliquer les effets
            with self.lock:
                for effect in self.effect_chain:
                    if effect.enabled:
                        audio = effect.process(audio, self.sample_rate)
            
            # Monitoring sortie
            self.output_level = float(np.mean(np.abs(audio)))
            
            # Limiter (fast)
            np.clip(audio, -1.0, 1.0, out=audio)
            
            # Sortie
            outdata[:] = audio
            
        except Exception as e:
            logger.error(f"Erreur callback: {e}")
            outdata[:] = indata
    
    def start(self, input_device: Optional[int] = None, output_device: Optional[int] = None):
        """Démarrer avec configuration ASIO optimisée"""
        if self.is_running:
            return
        
        try:
            device = None
            if input_device is not None or output_device is not None:
                device = (input_device, output_device)
            
            # Configuration ASIO optimisée
            self.stream = sd.Stream(
                samplerate=self.sample_rate,
                blocksize=self.buffer_size,
                channels=self.channels,
                dtype=np.float32,
                callback=self.audio_callback,
                device=device,
                latency='low',  # Mode faible latence
                prime_output_buffers_using_stream_callback=False  # Optimisation
            )
            
            self.stream.start()
            self.is_running = True
            
            # Calculer et afficher la latence
            latency_in = self.stream.latency[0] * 1000
            latency_out = self.stream.latency[1] * 1000
            total_latency = latency_in + latency_out
            
            logger.info(f"✅ Audio démarré")
            logger.info(f"   Input latency:  {latency_in:.2f} ms")
            logger.info(f"   Output latency: {latency_out:.2f} ms")
            logger.info(f"   Total latency:  {total_latency:.2f} ms")
            
            if total_latency < 10:
                logger.info("   🎉 EXCELLENT - Latence imperceptible!")
            elif total_latency < 20:
                logger.info("   ✅ BON - Latence acceptable")
            else:
                logger.warning(f"   ⚠️ Latence élevée - Réduire le buffer size")
            
        except Exception as e:
            logger.error(f"Erreur démarrage: {e}")
            raise
    
    def stop(self):
        if not self.is_running:
            return
        
        if self.stream:
            # Afficher les stats
            if self.callback_count > 0:
                dropout_rate = (self.underrun_count + self.overrun_count) / self.callback_count * 100
                logger.info(f"📊 Stats audio:")
                logger.info(f"   Callbacks: {self.callback_count}")
                logger.info(f"   Dropouts: {self.underrun_count + self.overrun_count} ({dropout_rate:.2f}%)")
            
            self.stream.stop()
            self.stream.close()
            self.stream = None
        
        self.is_running = False
        logger.info("⏹️ Audio arrêté")
    
    def set_bypass(self, bypass: bool):
        self.bypass = bypass
    
    def get_devices(self):
        return sd.query_devices()
    
    def get_input_level(self) -> float:
        return self.input_level
    
    def get_output_level(self) -> float:
        return self.output_level
    
    def get_stats(self):
        """Obtenir les statistiques de performance"""
        return {
            'callbacks': self.callback_count,
            'underruns': self.underrun_count,
            'overruns': self.overrun_count,
            'dropout_rate': (self.underrun_count + self.overrun_count) / max(self.callback_count, 1) * 100
        }