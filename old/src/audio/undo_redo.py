"""
Système Undo/Redo pour les actions du looper
"""
import copy
import numpy as np
from collections import deque
from ..utils.logger import logger

class LooperSnapshot:
    """
    Snapshot d'une piste du looper à un instant donné
    """
    
    def __init__(self, track, track_index: int):
        self.track_index = track_index
        
        # Copier les données essentielles
        self.length = track.length
        self.gain = track.gain
        self.muted = track.muted
        
        # Copier le buffer audio (si présent)
        if track.buffer is not None and track.length > 0:
            # Ne copier que la partie utilisée pour économiser la mémoire
            self.buffer = track.buffer[:track.length].copy()
        else:
            self.buffer = None
    
    def restore_to_track(self, track):
        """Restaurer ce snapshot dans une piste"""
        track.length = self.length
        track.gain = self.gain
        track.muted = self.muted
        
        if self.buffer is not None:
            track._ensure_buffer(len(self.buffer))
            track.buffer[:len(self.buffer)] = self.buffer
        else:
            track.length = 0
        
        # Réinitialiser les positions de lecture
        track.record_pos = 0
        track.play_pos = 0
        track.is_recording = False
        track.is_playing = False
        track.is_overdubbing = False


class UndoAction:
    """
    Représente une action qui peut être annulée/refaite
    """
    
    def __init__(self, action_type: str, description: str):
        self.action_type = action_type
        self.description = description
        self.timestamp = None
        self.snapshots = []  # Liste de LooperSnapshot
        self.looper_state = {}
    
    def add_track_snapshot(self, track, track_index: int):
        """Ajouter le snapshot d'une piste"""
        snapshot = LooperSnapshot(track, track_index)
        self.snapshots.append(snapshot)
    
    def save_looper_state(self, looper):
        """Sauvegarder l'état global du looper"""
        self.looper_state = {
            'active_track': looper.active_track,
            'sync_mode': looper.sync_mode,
            'master_track_index': looper.master_track_index,
            'master_length': looper.master_length,
        }
    
    def restore_looper_state(self, looper):
        """Restaurer l'état global du looper"""
        if self.looper_state:
            looper.active_track = self.looper_state.get('active_track', 0)
            looper.sync_mode = self.looper_state.get('sync_mode', True)
            looper.master_track_index = self.looper_state.get('master_track_index')
            looper.master_length = self.looper_state.get('master_length', 0)
    
    def restore(self, looper):
        """Restaurer cet état dans le looper"""
        # Restaurer les pistes
        for snapshot in self.snapshots:
            track = looper.tracks[snapshot.track_index]
            snapshot.restore_to_track(track)
        
        # Restaurer l'état global
        self.restore_looper_state(looper)


class UndoRedoManager:
    """
    Gestionnaire d'historique Undo/Redo
    """
    
    def __init__(self, max_history: int = 20):
        self.max_history = max_history
        self.undo_stack = deque(maxlen=max_history)
        self.redo_stack = deque(maxlen=max_history)
        
        # Statistiques
        self.total_actions = 0
        
        logger.info(f"UndoRedoManager initialisé (max {max_history} actions)")
    
    def can_undo(self) -> bool:
        """Vérifier si undo est possible"""
        return len(self.undo_stack) > 0
    
    def can_redo(self) -> bool:
        """Vérifier si redo est possible"""
        return len(self.redo_stack) > 0
    
    def capture_state(self, looper, action_type: str, description: str, track_indices: list = None):
        """
        Capturer l'état actuel avant une modification
        
        Args:
            looper: Instance du looper
            action_type: Type d'action ('clear', 'record', 'gain_change', etc.)
            description: Description lisible de l'action
            track_indices: Liste des indices de pistes à capturer (None = toutes)
        """
        import datetime
        
        action = UndoAction(action_type, description)
        action.timestamp = datetime.datetime.now()
        
        # Capturer les pistes concernées
        if track_indices is None:
            # Capturer toutes les pistes
            track_indices = range(len(looper.tracks))
        
        for idx in track_indices:
            if 0 <= idx < len(looper.tracks):
                action.add_track_snapshot(looper.tracks[idx], idx)
        
        # Capturer l'état global du looper
        action.save_looper_state(looper)
        
        # Ajouter à la pile undo
        self.undo_stack.append(action)
        
        # Vider la pile redo (nouvelle branche d'historique)
        self.redo_stack.clear()
        
        self.total_actions += 1
        
        logger.info(f"État capturé: {description} (undo stack: {len(self.undo_stack)})")
    
    def undo(self, looper) -> bool:
        """
        Annuler la dernière action
        
        Returns:
            True si undo effectué, False sinon
        """
        if not self.can_undo():
            logger.warning("Aucune action à annuler")
            return False
        
        # Récupérer l'action à annuler
        action = self.undo_stack.pop()
        
        # Avant d'appliquer le undo, capturer l'état actuel pour le redo
        redo_action = UndoAction(action.action_type, f"Redo: {action.description}")
        
        for snapshot in action.snapshots:
            redo_action.add_track_snapshot(looper.tracks[snapshot.track_index], snapshot.track_index)
        
        redo_action.save_looper_state(looper)
        self.redo_stack.append(redo_action)
        
        # Appliquer le undo
        action.restore(looper)
        
        logger.info(f"Undo: {action.description}")
        return True
    
    def redo(self, looper) -> bool:
        """
        Refaire la dernière action annulée
        
        Returns:
            True si redo effectué, False sinon
        """
        if not self.can_redo():
            logger.warning("Aucune action à refaire")
            return False
        
        # Récupérer l'action à refaire
        action = self.redo_stack.pop()
        
        # Capturer l'état actuel pour undo
        undo_action = UndoAction(action.action_type, action.description.replace("Redo: ", ""))
        
        for snapshot in action.snapshots:
            undo_action.add_track_snapshot(looper.tracks[snapshot.track_index], snapshot.track_index)
        
        undo_action.save_looper_state(looper)
        self.undo_stack.append(undo_action)
        
        # Appliquer le redo
        action.restore(looper)
        
        logger.info(f"Redo: {action.description}")
        return True
    
    def get_undo_list(self) -> list:
        """Obtenir la liste des actions undo disponibles"""
        return [
            {
                'description': action.description,
                'type': action.action_type,
                'timestamp': action.timestamp.isoformat() if action.timestamp else None
            }
            for action in reversed(self.undo_stack)
        ]
    
    def get_redo_list(self) -> list:
        """Obtenir la liste des actions redo disponibles"""
        return [
            {
                'description': action.description,
                'type': action.action_type,
                'timestamp': action.timestamp.isoformat() if action.timestamp else None
            }
            for action in reversed(self.redo_stack)
        ]
    
    def clear_history(self):
        """Vider tout l'historique"""
        self.undo_stack.clear()
        self.redo_stack.clear()
        logger.info("Historique Undo/Redo vidé")
    
    def get_memory_usage(self) -> dict:
        """Estimer l'utilisation mémoire de l'historique"""
        total_bytes = 0
        
        for action in list(self.undo_stack) + list(self.redo_stack):
            for snapshot in action.snapshots:
                if snapshot.buffer is not None:
                    total_bytes += snapshot.buffer.nbytes
        
        return {
            'total_mb': total_bytes / (1024 * 1024),
            'undo_actions': len(self.undo_stack),
            'redo_actions': len(self.redo_stack),
        }


# Intégration avec le Looper
class LooperWithUndo:
    """
    Wrapper du Looper avec support Undo/Redo
    """
    
    def __init__(self, looper, undo_manager: UndoRedoManager):
        self.looper = looper
        self.undo_manager = undo_manager
    
    def clear_track_with_undo(self, track_index: int):
        """Effacer une piste avec support undo"""
        # Capturer l'état avant
        self.undo_manager.capture_state(
            self.looper,
            'clear_track',
            f"Effacer piste {track_index + 1}",
            [track_index]
        )
        
        # Effectuer l'action
        self.looper.clear_track(track_index)
    
    def clear_all_with_undo(self):
        """Effacer toutes les pistes avec support undo"""
        # Capturer l'état avant
        self.undo_manager.capture_state(
            self.looper,
            'clear_all',
            "Effacer toutes les pistes",
            None  # Toutes les pistes
        )
        
        # Effectuer l'action
        self.looper.clear_all()
    
    def set_track_gain_with_undo(self, track_index: int, gain: float):
        """Modifier le gain avec support undo"""
        # Ne capturer l'état que si le changement est significatif
        current_gain = self.looper.tracks[track_index].gain
        if abs(current_gain - gain) > 0.05:
            self.undo_manager.capture_state(
                self.looper,
                'gain_change',
                f"Gain piste {track_index + 1}: {current_gain:.2f} → {gain:.2f}",
                [track_index]
            )
        
        # Effectuer l'action
        self.looper.set_track_gain(track_index, gain)
    
    def toggle_track_mute_with_undo(self, track_index: int):
        """Toggle mute avec support undo"""
        current_muted = self.looper.tracks[track_index].muted
        
        self.undo_manager.capture_state(
            self.looper,
            'mute_toggle',
            f"{'Unmute' if current_muted else 'Mute'} piste {track_index + 1}",
            [track_index]
        )
        
        # Effectuer l'action
        self.looper.toggle_track_mute(track_index)
    
    def record_stop_with_undo(self):
        """
        Record/Stop avec support undo (seulement pour stop/clear)
        Note: L'enregistrement lui-même ne crée pas de undo automatiquement
        """
        track = self.looper.tracks[self.looper.active_track]
        
        # Si on arrête un enregistrement, capturer
        if track.is_recording:
            # Ne pas capturer ici car l'enregistrement n'est pas terminé
            pass
        
        # Effectuer l'action
        self.looper.record_stop()