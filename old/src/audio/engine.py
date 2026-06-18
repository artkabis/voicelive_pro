"""
Moteur audio principal
"""
import sounddevice as sd
import numpy as np
from threading import Lock
from typing import Optional, List
from ..utils.config import Config
from ..utils.logger import logger
from .effects import Effect

class AudioEngine:
    def __init__(self):
        self.sample_rate = Config.SAMPLE_RATE
        self.buffer_size = Config.BUFFER_SIZE
        self.channels = Config.CHANNELS
        
        self.stream: Optional[sd.Stream] = None
        self.is_running = False
        
        self.effect_chain: List[Effect] = []
        self.lock = Lock()
        
        self.input_level = 0.0
        self.output_level = 0.0
        self.peak_input = 0.0
        self.peak_output = 0.0
        
        self.bypass = False
        
        logger.info("Moteur audio initialisé")
    
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
                logger.info(f"Effet retiré: {effect.name}")
    
    def clear_effects(self):
        with self.lock:
            self.effect_chain.clear()
            logger.info("Effets vidés")
    
    def get_effects(self) -> List[Effect]:
        with self.lock:
            return self.effect_chain.copy()
    
    def audio_callback(self, indata, outdata, frames, time_info, status):
        if status:
            logger.warning(f"Audio: {status}")
        
        try:
            audio = indata.copy()
            
            self.input_level = float(np.abs(audio).mean())
            self.peak_input = max(self.peak_input * 0.99, float(np.abs(audio).max()))
            
            if self.bypass:
                outdata[:] = audio
                return
            
            with self.lock:
                for effect in self.effect_chain:
                    if effect.enabled:
                        try:
                            audio = effect.process(audio, self.sample_rate)
                        except Exception as e:
                            logger.error(f"Erreur {effect.name}: {e}")
            
            self.output_level = float(np.abs(audio).mean())
            self.peak_output = max(self.peak_output * 0.99, float(np.abs(audio).max()))
            
            audio = np.clip(audio, -1.0, 1.0)
            outdata[:] = audio
            
        except Exception as e:
            logger.error(f"Erreur callback: {e}")
            outdata[:] = indata
    
    def start(self, input_device: Optional[int] = None, output_device: Optional[int] = None):
        if self.is_running:
            logger.warning("Déjà en cours")
            return
        
        if input_device is None:
            input_device = Config.INPUT_DEVICE
        if output_device is None:
            output_device = Config.OUTPUT_DEVICE
        
        try:
            device = None
            if input_device is not None or output_device is not None:
                device = (input_device, output_device)
            
            self.stream = sd.Stream(
                samplerate=self.sample_rate,
                blocksize=self.buffer_size,
                channels=self.channels,
                dtype=np.float32,
                callback=self.audio_callback,
                device=device
            )
            
            self.stream.start()
            self.is_running = True
            logger.info(f"✅ Audio démarré")
            
        except Exception as e:
            logger.error(f"Erreur démarrage: {e}")
            raise
    
    def stop(self):
        if not self.is_running:
            return
        
        if self.stream:
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
    
    def get_peak_levels(self) -> tuple:
        return (self.peak_input, self.peak_output)
    
    def reset_peaks(self):
        self.peak_input = 0.0
        self.peak_output = 0.0
