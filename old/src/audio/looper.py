"""
Looper professionnel 3 pistes avec contrôles de lecture avancés
"""
import numpy as np
from threading import Lock
from ..utils.logger import logger

class LooperTrack:
    """Une piste de loop avec contrôles avancés de lecture"""
    
    def __init__(self, max_duration: int, sample_rate: int, channels: int = 2):
        self.max_duration = max_duration
        self.sample_rate = sample_rate
        self.channels = channels
        
        self.buffer = None
        self.allocated_size = 0
        
        self.is_recording = False
        self.is_playing = False
        self.is_paused = False  # NOUVEAU
        self.is_overdubbing = False
        
        self.record_pos = 0
        self.play_pos = 0
        self.length = 0
        
        self.gain = 1.0  # NOUVEAU : Volume de la piste (0.0 à 2.0)
        self.muted = False  # NOUVEAU : Mute
        self.loop_on_export = True # Boucler cette piste à l'export
        
        self.lock = Lock()
    
    def _ensure_buffer(self, min_size: int):
        """Allouer le buffer si nécessaire"""
        if self.buffer is None or self.allocated_size < min_size:
            new_size = min_size * 2
            max_size = int(self.max_duration * self.sample_rate)
            new_size = min(new_size, max_size)
            
            logger.info(f"Track: Allocating buffer ({new_size / self.sample_rate:.1f}s)")
            
            new_buffer = np.zeros((new_size, self.channels), dtype=np.float32)
            
            if self.buffer is not None:
                copy_size = min(len(self.buffer), new_size)
                new_buffer[:copy_size] = self.buffer[:copy_size]
            
            self.buffer = new_buffer
            self.allocated_size = new_size
    
    def start_recording(self):
        """Démarrer l'enregistrement"""
        with self.lock:
            self.is_recording = True
            self.is_playing = False
            self.is_paused = False
            self.record_pos = 0
            self.length = 0
            
            initial_size = int(30 * self.sample_rate)
            self._ensure_buffer(initial_size)
            self.buffer.fill(0)
            
            logger.info("Track: Recording started")
    
    def stop_recording(self):
        """Arrêter l'enregistrement et démarrer la lecture"""
        with self.lock:
            self.is_recording = False
            self.length = self.record_pos
            self.is_playing = True
            self.is_paused = False
            self.play_pos = 0
            duration = self.length / self.sample_rate
            logger.info(f"Track: Recording stopped ({duration:.2f}s)")

    def toggle_loop_export(self):
        """Toggle loop sur export"""
        with self.lock:
            self.loop_on_export = not self.loop_on_export
            logger.info(f"Track: Loop on export {'ON' if self.loop_on_export else 'OFF'}")
    
    def start_overdub(self):
        """Démarrer l'overdub"""
        if self.length == 0:
            self.start_recording()
        else:
            with self.lock:
                self.is_overdubbing = True
                self.is_playing = True
                self.is_paused = False
                logger.info("Track: Overdub started")
    
    def stop_overdub(self):
        """Arrêter l'overdub"""
        with self.lock:
            self.is_overdubbing = False
            logger.info("Track: Overdub stopped")
    
    def start_playback(self):
        """Démarrer la lecture"""
        with self.lock:
            if self.length > 0:
                self.is_playing = True
                self.is_paused = False
                logger.info("Track: Playback started")
    
    def pause_playback(self):
        """Mettre en pause (NOUVEAU)"""
        with self.lock:
            if self.is_playing and not self.is_recording:
                self.is_paused = True
                logger.info(f"Track: Paused at {self.play_pos / self.sample_rate:.2f}s")
    
    def resume_playback(self):
        """Reprendre la lecture (NOUVEAU)"""
        with self.lock:
            if self.is_paused:
                self.is_paused = False
                logger.info("Track: Resumed")
    
    def stop_playback(self):
        """Arrêter la lecture"""
        with self.lock:
            self.is_playing = False
            self.is_paused = False
            self.play_pos = 0
            logger.info("Track: Playback stopped")
    
    def seek(self, position_seconds: float):
        """Déplacer la tête de lecture (NOUVEAU)"""
        with self.lock:
            if self.length == 0:
                return
            
            # Convertir en samples et limiter
            target_pos = int(position_seconds * self.sample_rate)
            target_pos = max(0, min(target_pos, self.length - 1))
            
            self.play_pos = target_pos
            logger.info(f"Track: Seeked to {position_seconds:.2f}s")
    
    def set_gain(self, gain: float):
        """Définir le gain (NOUVEAU)"""
        with self.lock:
            self.gain = max(0.0, min(2.0, gain))  # Limiter entre 0 et 2
            logger.info(f"Track: Gain set to {self.gain:.2f}")
    
    def toggle_mute(self):
        """Toggle mute (NOUVEAU)"""
        with self.lock:
            self.muted = not self.muted
            logger.info(f"Track: {'Muted' if self.muted else 'Unmuted'}")
    
    def clear(self):
        """Effacer la piste"""
        with self.lock:
            if self.buffer is not None:
                self.buffer.fill(0)
            self.length = 0
            self.record_pos = 0
            self.play_pos = 0
            self.is_recording = False
            self.is_playing = False
            self.is_paused = False
            self.is_overdubbing = False
            logger.info("Track: Cleared")
    
    def process(self, input_audio: np.ndarray, output_audio: np.ndarray):
        """Traiter l'audio pour cette piste - VERSION VECTORIELLE"""
        with self.lock:
            if self.buffer is None:
                return
            
            # Si en pause, ne rien faire
            if self.is_paused:
                return
            
            frames = len(input_audio)
            
            if self.is_recording:
                if self.record_pos + frames > self.allocated_size:
                    max_size = int(self.max_duration * self.sample_rate)
                    if self.record_pos >= max_size:
                        self.stop_recording()
                        return
                    else:
                        self._ensure_buffer(self.record_pos + int(30 * self.sample_rate))
                
                max_size = int(self.max_duration * self.sample_rate)
                available_space = min(self.allocated_size - self.record_pos, max_size - self.record_pos)
                frames_to_write = min(frames, available_space)
                
                if frames_to_write > 0:
                    end_pos = self.record_pos + frames_to_write
                    self.buffer[self.record_pos:end_pos] = input_audio[:frames_to_write]
                    self.record_pos = end_pos
                
                if self.record_pos >= max_size:
                    self.stop_recording()
            
            elif self.is_overdubbing and self.length > 0:
                if self.play_pos + frames <= self.length:
                    section = slice(self.play_pos, self.play_pos + frames)
                    self.buffer[section] = self.buffer[section] * 0.7 + input_audio * 0.3
                    
                    # Appliquer gain/mute
                    if not self.muted:
                        output_audio[:] += self.buffer[section] * self.gain
                    
                    self.play_pos = (self.play_pos + frames) % self.length
                else:
                    frames_before_wrap = self.length - self.play_pos
                    frames_after_wrap = frames - frames_before_wrap
                    
                    section1 = slice(self.play_pos, self.length)
                    self.buffer[section1] = self.buffer[section1] * 0.7 + input_audio[:frames_before_wrap] * 0.3
                    
                    section2 = slice(0, frames_after_wrap)
                    self.buffer[section2] = self.buffer[section2] * 0.7 + input_audio[frames_before_wrap:] * 0.3
                    
                    # Appliquer gain/mute
                    if not self.muted:
                        output_audio[:frames_before_wrap] += self.buffer[section1] * self.gain
                        output_audio[frames_before_wrap:] += self.buffer[section2] * self.gain
                    
                    self.play_pos = frames_after_wrap
            
            elif self.is_playing and self.length > 0:
                if self.play_pos + frames <= self.length:
                    # Appliquer gain/mute
                    if not self.muted:
                        output_audio[:] += self.buffer[self.play_pos:self.play_pos + frames] * self.gain
                    
                    self.play_pos = (self.play_pos + frames) % self.length
                else:
                    frames_before_wrap = self.length - self.play_pos
                    frames_after_wrap = frames - frames_before_wrap
                    
                    # Appliquer gain/mute
                    if not self.muted:
                        output_audio[:frames_before_wrap] += self.buffer[self.play_pos:self.length] * self.gain
                        output_audio[frames_before_wrap:] += self.buffer[:frames_after_wrap] * self.gain
                    
                    self.play_pos = frames_after_wrap
    
    def get_state(self):
        """Obtenir l'état de la piste (non-bloquant)"""
        if not self.lock.acquire(blocking=False):
            current_pos = self.play_pos if (self.is_playing or self.is_overdubbing) else self.record_pos
            return {
                'recording': bool(self.is_recording),
                'playing': bool(self.is_playing),
                'paused': bool(self.is_paused),
                'overdubbing': bool(self.is_overdubbing),
                'muted': bool(self.muted),
                'loop_on_export': bool(self.loop_on_export),
                'length': self.length,
                'duration': self.length / self.sample_rate if self.length > 0 else 0,
                'position': current_pos,
                'current_time': current_pos / self.sample_rate if self.length > 0 or self.is_recording else 0,
                'gain': float(self.gain)
            }
        
        try:
            current_pos = self.play_pos if (self.is_playing or self.is_overdubbing) else self.record_pos
            return {
                'recording': bool(self.is_recording),
                'playing': bool(self.is_playing),
                'paused': bool(self.is_paused),
                'overdubbing': bool(self.is_overdubbing),
                'muted': bool(self.muted),
                'length': self.length,
                'duration': self.length / self.sample_rate if self.length > 0 else 0,
                'position': current_pos,
                'current_time': current_pos / self.sample_rate if self.length > 0 or self.is_recording else 0,
                'gain': float(self.gain)
            }
        finally:
            self.lock.release()


class Looper:
    """Looper 3 pistes avec contrôles avancés"""
    
    def __init__(self, sample_rate: int = 44100, max_duration: int = 120):
        self.sample_rate = sample_rate
        self.max_duration = max_duration
        
        self.tracks = [
            LooperTrack(max_duration, sample_rate),
            LooperTrack(max_duration, sample_rate),
            LooperTrack(max_duration, sample_rate)
        ]
        
        self.active_track = 0
        self.enabled = True
        self.monitoring = True
        
        self.master_length = 0
        self.master_track_index = None
        self.sync_mode = True
        self.metronome_sync = False  # NOUVEAU : sync avec métronome
        self.metronome_bpm = None
        self.metronome_bars = None
        
        logger.info(f"Looper initialisé (3 pistes, {max_duration}s max, contrôles avancés)")

    def enable_metronome_sync(self, bpm: float, time_signature_beats: int):
        """
        Activer la synchronisation avec le métronome
        Les pistes seront quantifiées sur des mesures entières
        """
        self.metronome_sync = True
        self.metronome_bpm = bpm
        self.metronome_bars = time_signature_beats
        
        # Calculer la longueur d'une mesure en samples
        beat_duration = 60.0 / bpm
        bar_duration = beat_duration * time_signature_beats
        self.bar_length_samples = int(bar_duration * self.sample_rate)
        
        logger.info(f"🎵 Métronome sync activé: {bpm} BPM, {time_signature_beats}/4 (barre = {bar_duration:.2f}s)")

    def _sync_track_to_metronome(self, track_index: int):
        """Quantifier une piste sur des mesures entières du métronome"""
        if not self.metronome_sync or self.bar_length_samples == 0:
            return
        
        track = self.tracks[track_index]
        if track.length == 0:
            return
        
        # Trouver le nombre de mesures le plus proche
        num_bars = round(track.length / self.bar_length_samples)
        
        if num_bars == 0:
            num_bars = 1
        
        # Ajuster la longueur exactement
        new_length = num_bars * self.bar_length_samples
        
        with track.lock:
            track.length = new_length
        
        logger.info(f"🎵 Track {track_index + 1} quantifiée à {num_bars} mesure(s)")
    
    def set_active_track(self, track_index: int):
        """Définir la piste active"""
        if 0 <= track_index < 3:
            self.active_track = track_index
            logger.info(f"Looper: Track {track_index + 1} active")
    
    # NOUVELLES MÉTHODES DE CONTRÔLE
    
    def play_track(self, track_index: int):
        """Lecture d'une piste spécifique"""
        if 0 <= track_index < 3:
            self.tracks[track_index].start_playback()
    
    def pause_track(self, track_index: int):
        """Pause d'une piste spécifique"""
        if 0 <= track_index < 3:
            track = self.tracks[track_index]
            if track.is_paused:
                track.resume_playback()
            else:
                track.pause_playback()
    
    def stop_track(self, track_index: int):
        """Stop d'une piste spécifique"""
        if 0 <= track_index < 3:
            self.tracks[track_index].stop_playback()
    
    def seek_track(self, track_index: int, position_seconds: float):
        """Déplacer la tête de lecture d'une piste"""
        if 0 <= track_index < 3:
            self.tracks[track_index].seek(position_seconds)
    
    def set_track_gain(self, track_index: int, gain: float):
        """Définir le gain d'une piste"""
        if 0 <= track_index < 3:
            self.tracks[track_index].set_gain(gain)
    
    def toggle_track_mute(self, track_index: int):
        """Toggle mute d'une piste"""
        if 0 <= track_index < 3:
            self.tracks[track_index].toggle_mute()
    
    def toggle_track_loop_export(self, track_index: int):
        """Toggle loop sur export d'une piste"""
        if 0 <= track_index < 3:
            self.tracks[track_index].toggle_loop_export()
    
    def record_stop(self):
        """Toggle enregistrement/arrêt avec sync master"""
        track = self.tracks[self.active_track]
        
        if track.is_recording:
            track.stop_recording()
            
            if self.master_track_index is None and track.length > 0:
                self.master_track_index = self.active_track
                self.master_length = track.length
                logger.info(f"Looper: Track {self.active_track + 1} définie comme master ({self.master_length / self.sample_rate:.2f}s)")
            
            elif self.sync_mode and self.master_length > 0:
                self._sync_track_to_master(self.active_track)
                
        elif track.length == 0:
            track.start_recording()
        else:
            if track.is_playing:
                track.stop_playback()
            else:
                track.start_playback()
    
    def _sync_track_to_master(self, track_index: int):
        """Synchroniser une piste sur la longueur master - VERSION AMÉLIORÉE"""
        track = self.tracks[track_index]
        
        if track.length == 0 or self.master_length == 0:
            return
        
        ratio = track.length / self.master_length
        
        # Tolérance de base
        tolerance_samples = int(0.05 * self.sample_rate)  # 50ms
        
        # Cas 1 : Longueur quasi identique
        if abs(track.length - self.master_length) <= tolerance_samples:
            # Alignement parfait
            track.length = self.master_length
            logger.info(f"✅ Track {track_index + 1} alignée sur master (diff: {abs(track.length - self.master_length)} samples)")
            return
        
        # Cas 2 : Multiples exacts (2x, 4x, 8x)
        for multiple in [2, 4, 8]:
            target_length = self.master_length * multiple
            if abs(track.length - target_length) <= tolerance_samples * multiple:
                track.length = target_length
                logger.info(f"✅ Track {track_index + 1} quantifiée à {multiple}x le master")
                return
        
        # Cas 3 : Divisions exactes (1/2, 1/4, 1/8)
        for divisor in [2, 4, 8]:
            target_length = self.master_length // divisor
            if abs(track.length - target_length) <= tolerance_samples // divisor:
                track.length = target_length
                logger.info(f"✅ Track {track_index + 1} quantifiée à 1/{divisor}x le master")
                return
        
        # Cas 4 : Longueur non standard - TENTATIVE DE QUANTIFICATION INTELLIGENTE
        # Trouver le multiple/diviseur le plus proche
        closest_ratio = None
        closest_diff = float('inf')
        
        ratios_to_test = [1, 2, 4, 8, 0.5, 0.25, 0.125]
        for test_ratio in ratios_to_test:
            target = self.master_length * test_ratio
            diff = abs(track.length - target)
            
            if diff < closest_diff and diff < self.master_length * 0.1:  # Max 10% de différence
                closest_diff = diff
                closest_ratio = test_ratio
        
        if closest_ratio:
            track.length = int(self.master_length * closest_ratio)
            logger.info(f"⚠️ Track {track_index + 1} FORCÉE à {closest_ratio}x le master (écart corrigé: {closest_diff/self.sample_rate:.3f}s)")
        else:
            logger.warning(f"❌ Track {track_index + 1} désynchronisée (ratio: {ratio:.2f}, trop éloigné pour correction)")
    
    def nudge_track(self, track_index: int, offset_ms: float):
        """Décaler une piste de quelques millisecondes"""
        if not (0 <= track_index < 3):
            return
        
        track = self.tracks[track_index]
        if track.length == 0:
            return
        
        offset_samples = int(offset_ms * self.sample_rate / 1000)
        track.play_pos = (track.play_pos + offset_samples) % track.length
        
        logger.info(f"Looper: Track {track_index + 1} décalée de {offset_ms}ms")
    
    def reset_master(self):
        """Réinitialiser le master"""
        self.master_length = 0
        self.master_track_index = None
        logger.info("Looper: Master reset")
    
    def get_sync_info(self):
        """Obtenir les infos de synchronisation"""
        return {
            'master_track': self.master_track_index,
            'master_length': self.master_length,
            'master_duration': self.master_length / self.sample_rate if self.master_length > 0 else 0,
            'sync_mode': self.sync_mode
        }
    
    def overdub(self):
        """Toggle overdub pour la piste active"""
        track = self.tracks[self.active_track]
        
        if track.is_overdubbing:
            track.stop_overdub()
        else:
            track.start_overdub()
    
    def clear_track(self, track_index: int):
        """Effacer une piste"""
        if 0 <= track_index < 3:
            self.tracks[track_index].clear()
            
            if track_index == self.master_track_index:
                self.reset_master()
    
    def clear_all(self):
        """Effacer toutes les pistes"""
        for track in self.tracks:
            track.clear()
        self.reset_master()
        logger.info("Looper: All tracks cleared")

    def play_all_tracks(self):
        """Lecture de toutes les pistes AVEC synchronisation"""
        # D'abord synchroniser les positions
        if self.sync_mode and self.master_track_index is not None:
            self.sync_playback_positions()
        
        # Ensuite lancer la lecture
        for i in range(3):
            if self.tracks[i].length > 0:
                self.tracks[i].start_playback()
        
        logger.info("▶️ All tracks playing")

    def pause_all_tracks(self):
        """Pause de toutes les pistes"""
        for i in range(3):
            if self.tracks[i].length > 0:
                self.tracks[i].pause_playback()
        logger.info("⏸️ All tracks paused")

    def stop_all_tracks(self):
        """Stop de toutes les pistes"""
        for i in range(3):
            self.tracks[i].stop_playback()
        logger.info("⏹️ All tracks stopped")

    def restart_all_tracks(self):
        """Redémarrer toutes les pistes de manière synchronisée"""
        for i in range(3):
            track = self.tracks[i]
            if track.length > 0:
                with track.lock:
                    track.play_pos = 0
                    track.is_playing = True
                    track.is_paused = False
        
        logger.info("🔄 All tracks restarted from beginning")

    def sync_playback_positions(self):
        """Synchroniser les positions de lecture de toutes les pistes"""
        if self.master_track_index is None or self.master_length == 0:
            logger.warning("Sync positions: Pas de master défini")
            return
        
        master_track = self.tracks[self.master_track_index]
        if master_track.length == 0:
            return
        
        # Position relative du master (0.0 à 1.0)
        master_progress = master_track.play_pos / self.master_length
        
        for i, track in enumerate(self.tracks):
            if i == self.master_track_index or track.length == 0:
                continue
            
            with track.lock:
                synced_pos = int(master_progress * track.length)
                track.play_pos = synced_pos % track.length
        
        logger.info(f"🔄 Positions synchronisées (master à {master_progress*100:.1f}%)")
    
    def process(self, input_audio: np.ndarray) -> np.ndarray:
        """Traiter l'audio avec le looper"""
        if not self.enabled:
            return input_audio
        
        output = input_audio.copy()
        loop_mix = np.zeros_like(input_audio)
        
        for track in self.tracks:
            track.process(input_audio, loop_mix)
        
        has_loops = np.abs(loop_mix).max() > 0
        if has_loops:
            output = input_audio * 0.5 + loop_mix * 0.5
        
        max_val = np.abs(output).max()
        if max_val > 0.95:
            output *= 0.95 / max_val
        
        return output
    
    def get_states(self):
        """Obtenir l'état de toutes les pistes"""
        return [track.get_state() for track in self.tracks]
    
    def load_audio_file(self, track_index: int, filepath: str):
        """
        Charger un fichier audio dans une piste
        
        Args:
            track_index: Index de la piste (0-2)
            filepath: Chemin vers le fichier WAV ou FLAC
        """
        import soundfile as sf
        
        if not 0 <= track_index < len(self.tracks):
            raise ValueError(f"Index de piste invalide: {track_index}")
        
        # Lire le fichier audio
        audio_data, file_sample_rate = sf.read(filepath)
        
        # Convertir en stéréo si mono
        if len(audio_data.shape) == 1:
            audio_data = np.column_stack([audio_data, audio_data])
        
        # Resampler si nécessaire
        if file_sample_rate != self.sample_rate:
            try:
                import scipy.signal as signal_proc
                num_samples = int(len(audio_data) * self.sample_rate / file_sample_rate)
                audio_data_resampled = np.zeros((num_samples, 2), dtype=np.float32)
                # Resampler chaque canal séparément
                for ch in range(2):
                    audio_data_resampled[:, ch] = signal_proc.resample(audio_data[:, ch], num_samples)
                audio_data = audio_data_resampled
                logger.info(f"Resampling: {file_sample_rate}Hz -> {self.sample_rate}Hz")
            except ImportError:
                logger.warning("scipy non disponible, pas de resampling")
                if abs(file_sample_rate - self.sample_rate) > 100:
                    raise ValueError(f"Le fichier doit être à {self.sample_rate}Hz (trouvé: {file_sample_rate}Hz)")
        
        # Normaliser à float32 entre -1 et 1
        audio_data = audio_data.astype(np.float32)
        if np.max(np.abs(audio_data)) > 1.0:
            audio_data = audio_data / np.max(np.abs(audio_data))
        
        # Charger dans la piste (en utilisant les attributs de l'objet LooperTrack)
        track = self.tracks[track_index]
        
        with track.lock:
            # Allouer/remplacer le buffer
            track.buffer = audio_data
            track.allocated_size = len(audio_data)
            track.length = len(audio_data)
            
            # Réinitialiser l'état
            track.is_recording = False
            track.is_playing = False
            track.is_paused = False
            track.is_overdubbing = False
            track.record_pos = 0
            track.play_pos = 0
            
            duration = track.length / self.sample_rate
            logger.info(f"Fichier chargé dans piste {track_index + 1}: {len(audio_data)} samples, {duration:.2f}s")
        
        # Mettre à jour le master si nécessaire
        if self.master_track_index is None or self.sync_mode:
            self.master_track_index = track_index
            self.master_length = track.length
            logger.info(f"Piste {track_index + 1} définie comme master ({duration:.2f}s)")
    
    def export_mix(self, duration: float = None, normalize: bool = True) -> np.ndarray:
        # Début du débogage
        logger.info("=== DÉBUT EXPORT DEBUG ===")
        
        max_duration = 0
        for i, track in enumerate(self.tracks):
            if track.length > 0:
                track_duration = track.length / self.sample_rate
                max_duration = max(max_duration, track_duration)
        
        if max_duration == 0:
            return np.array([])
        
        if duration is None:
            duration = max_duration
        
        total_samples = int(duration * self.sample_rate)
        output = np.zeros((total_samples, 2), dtype=np.float32)
        
        for i, track in enumerate(self.tracks):
            if track.length == 0 or track.buffer is None:
                continue
            
            if track.muted:
                logger.warning(f"Track {i}: skipped (MUTED!) ⚠️")
                continue
            
            # ✅ NOUVEAU : Gestion du bouclage
            if track.loop_on_export and track.length < total_samples:
                # Créer un buffer bouclé
                num_loops = int(np.ceil(total_samples / track.length))
                looped_buffer = np.tile(track.buffer[:track.length], (num_loops, 1))
                track_data = looped_buffer[:total_samples] * track.gain
                logger.info(f"Track {i}: looping {num_loops}x (original: {track.length} samples)")
            else:
                # Comportement normal : copier sans boucler
                samples_to_copy = min(track.length, total_samples)
                track_data = track.buffer[:samples_to_copy] * track.gain
            
            track_max = np.abs(track_data).max()
            logger.info(f"Track {i}: copying, max={track_max:.6f}, loop={track.loop_on_export}")
            output[:len(track_data)] += track_data
        
        # Vérifier le mixage
        output_max_before_norm = np.abs(output).max()
        logger.info(f"Mixage avant normalisation: max={output_max_before_norm:.6f}")
        
        if normalize:
            max_val = np.abs(output).max()
            if max_val > 0:
                target = 0.891
                output *= target / max_val
                logger.info(f"Looper: Export normalisé ({max_val:.3f} → {target:.3f})")
            else:
                logger.error("⚠️ PROBLÈME: max_val = 0, le mixage est SILENCIEUX!")
        
        output = np.clip(output, -1.0, 1.0)
        
        final_max = np.abs(output).max()
        logger.info(f"Export final: max={final_max:.6f}, durée={duration:.1f}s")
        logger.info("=== FIN EXPORT DEBUG ===")
        
        return output
    
    def get_waveform_data(self, track_index: int, samples_per_pixel: int = 512):
        """
        Obtenir les données de forme d'onde pour visualisation
        
        Args:
            track_index: Index de la piste
            samples_per_pixel: Nombre de samples à grouper par pixel
        
        Returns:
            Liste de [min, max] pour chaque pixel
        """
        if not 0 <= track_index < len(self.tracks):
            return []
        
        track = self.tracks[track_index]
        if track.length == 0 or track.buffer is None:
            return []
        
        audio_data = track.buffer[:track.length]
        # Convertir stéréo en mono pour la visualisation
        mono = np.mean(audio_data, axis=1)
        
        num_pixels = len(mono) // samples_per_pixel
        waveform = []
        
        for i in range(num_pixels):
            start = i * samples_per_pixel
            end = start + samples_per_pixel
            chunk = mono[start:end]
            waveform.append({
                'min': float(np.min(chunk)),
                'max': float(np.max(chunk))
            })
        
        return waveform

    def trim_track(self, track_index: int, start_time: float, end_time: float):
        """
        Découper une piste entre start_time et end_time (en secondes)
        
        Args:
            track_index: Index de la piste
            start_time: Temps de début (secondes)
            end_time: Temps de fin (secondes)
        """
        if not 0 <= track_index < len(self.tracks):
            return False
        
        track = self.tracks[track_index]
        if track.length == 0 or track.buffer is None:
            return False
        
        with track.lock:
            start_sample = int(start_time * self.sample_rate)
            end_sample = int(end_time * self.sample_rate)
            
            # Valider les limites
            start_sample = max(0, min(start_sample, track.length))
            end_sample = max(start_sample, min(end_sample, track.length))
            
            # Extraire la section
            new_buffer = track.buffer[start_sample:end_sample].copy()
            new_length = len(new_buffer)
            
            # Remplacer le buffer
            track.buffer = new_buffer
            track.length = new_length
            track.allocated_size = new_length
            track.play_pos = 0
            track.record_pos = 0
            
            duration = new_length / self.sample_rate
            logger.info(f"Piste {track_index + 1} découpée: {start_time:.2f}s → {end_time:.2f}s (durée: {duration:.2f}s)")
            
            # Mettre à jour le master si nécessaire
            if track_index == self.master_track_index:
                self.master_length = new_length
            
            return True

    def get_track_preview(self, track_index: int, start_time: float, end_time: float):
        """
        Obtenir un extrait audio pour prévisualisation
        
        Returns:
            Audio data en float32 stéréo
        """
        if not 0 <= track_index < len(self.tracks):
            return np.array([])
        
        track = self.tracks[track_index]
        if track.length == 0 or track.buffer is None:
            return np.array([])
        
        start_sample = int(start_time * self.sample_rate)
        end_sample = int(end_time * self.sample_rate)
        
        start_sample = max(0, min(start_sample, track.length))
        end_sample = max(start_sample, min(end_sample, track.length))
        
        return track.buffer[start_sample:end_sample].copy()