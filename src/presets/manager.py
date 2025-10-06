"""
Gestionnaire de presets
"""
import json
from pathlib import Path
from typing import Dict, Any, List, Optional
from ..utils.config import Config
from ..utils.logger import logger

class PresetManager:
    """
    Gestion des presets (50 emplacements)
    """
    
    def __init__(self):
        self.presets_dir = Config.PRESETS_DIR
        self.current_preset = 0
        self.presets: List[Optional[Dict]] = [None] * 50
        
        # Charger les presets existants
        self.load_all_presets()
        
        logger.info("Preset Manager initialisé")
    
    def save_preset(self, slot: int, name: str, effects_state: List[Dict], looper_state: Dict):
        """
        Sauvegarder un preset
        
        Args:
            slot: Emplacement (0-49)
            name: Nom du preset
            effects_state: État de tous les effets
            looper_state: État du looper
        """
        if not 0 <= slot < 50:
            logger.error(f"Slot invalide: {slot}")
            return False
        
        preset = {
            'name': name,
            'slot': slot,
            'effects': effects_state,
            'looper': looper_state
        }
        
        self.presets[slot] = preset
        
        # Sauvegarder sur disque
        filename = self.presets_dir / f"preset_{slot:02d}.json"
        try:
            with open(filename, 'w') as f:
                json.dump(preset, f, indent=4)
            logger.info(f"Preset sauvegardé: {name} (slot {slot})")
            return True
        except Exception as e:
            logger.error(f"Erreur sauvegarde preset: {e}")
            return False
    
    def load_preset(self, slot: int) -> Optional[Dict]:
        """Charger un preset"""
        if not 0 <= slot < 50:
            return None
        
        # D'abord essayer depuis la mémoire
        if self.presets[slot] is not None:
            self.current_preset = slot
            logger.info(f"Preset chargé: {self.presets[slot]['name']} (slot {slot})")
            return self.presets[slot]
        
        # Sinon charger depuis le disque
        filename = self.presets_dir / f"preset_{slot:02d}.json"
        if filename.exists():
            try:
                with open(filename, 'r') as f:
                    preset = json.load(f)
                self.presets[slot] = preset
                self.current_preset = slot
                logger.info(f"Preset chargé: {preset['name']} (slot {slot})")
                return preset
            except Exception as e:
                logger.error(f"Erreur chargement preset: {e}")
        
        return None
    
    def delete_preset(self, slot: int):
        """Supprimer un preset"""
        if not 0 <= slot < 50:
            return
        
        self.presets[slot] = None
        
        filename = self.presets_dir / f"preset_{slot:02d}.json"
        if filename.exists():
            filename.unlink()
        
        logger.info(f"Preset supprimé (slot {slot})")
    
    def load_all_presets(self):
        """Charger tous les presets depuis le disque"""
        for slot in range(50):
            filename = self.presets_dir / f"preset_{slot:02d}.json"
            if filename.exists():
                try:
                    with open(filename, 'r') as f:
                        self.presets[slot] = json.load(f)
                except:
                    pass
    
    def get_preset_list(self) -> List[Dict]:
        """Obtenir la liste des presets disponibles"""
        presets_list = []
        for i, preset in enumerate(self.presets):
            if preset is not None:
                presets_list.append({
                    'slot': i,
                    'name': preset['name']
                })
            else:
                presets_list.append({
                    'slot': i,
                    'name': 'Empty'
                })
        return presets_list
    
    def export_preset(self, slot: int, filepath: Path):
        """Exporter un preset"""
        preset = self.presets[slot]
        if preset is None:
            return False
        
        try:
            with open(filepath, 'w') as f:
                json.dump(preset, f, indent=4)
            logger.info(f"Preset exporté: {filepath}")
            return True
        except Exception as e:
            logger.error(f"Erreur export: {e}")
            return False
    
    def import_preset(self, filepath: Path, slot: int):
        """Importer un preset"""
        try:
            with open(filepath, 'r') as f:
                preset = json.load(f)
            
            preset['slot'] = slot
            self.presets[slot] = preset
            
            # Sauvegarder
            self.save_preset(
                slot, 
                preset['name'], 
                preset['effects'], 
                preset['looper']
            )
            
            logger.info(f"Preset importé: {preset['name']} → slot {slot}")
            return True
        except Exception as e:
            logger.error(f"Erreur import: {e}")
            return False
