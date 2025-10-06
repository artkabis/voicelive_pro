"""
Classe de base pour tous les effets
"""
from abc import ABC, abstractmethod
import numpy as np
from typing import Dict, Any

class Effect(ABC):
    def __init__(self, name: str):
        self.name = name
        self.enabled = False
        self.parameters: Dict[str, Any] = {}
        self.sample_rate = 44100
    
    @abstractmethod
    def process(self, audio: np.ndarray, sample_rate: int) -> np.ndarray:
        pass
    
    def set_parameter(self, param_name: str, value: Any):
        if param_name in self.parameters:
            self.parameters[param_name] = value
            self.on_parameter_change(param_name, value)
    
    def get_parameter(self, param_name: str) -> Any:
        return self.parameters.get(param_name)
    
    def on_parameter_change(self, param_name: str, value: Any):
        pass
    
    def enable(self): self.enabled = True
    def disable(self): self.enabled = False
    def toggle(self): self.enabled = not self.enabled
    
    def reset(self): pass
    
    def get_state(self) -> Dict[str, Any]:
        return {
            'name': self.name,
            'enabled': self.enabled,
            'parameters': self.parameters.copy()
        }
    
    def set_state(self, state: Dict[str, Any]):
        self.enabled = state.get('enabled', False)
        for param_name, value in state.get('parameters', {}).items():
            if param_name in self.parameters:
                self.set_parameter(param_name, value)
