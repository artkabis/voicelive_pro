"""
Gestionnaire de projets - Sauvegarde/chargement
"""
import json
import os
import soundfile as sf
import numpy as np
from pathlib import Path
from datetime import datetime
from ..utils.logger import logger

class ProjectManager:
    """
    Gestion de la sauvegarde et du chargement de projets VoiceLive
    """
    
    def __init__(self, projects_dir: str = "projects"):
        self.projects_dir = Path(projects_dir)
        self.projects_dir.mkdir(exist_ok=True)
        logger.info(f"ProjectManager initialisé (dossier: {self.projects_dir})")
    
    def save_project(self, project_name: str, looper, effects: dict, mastering_chain, metronome) -> dict:
        """
        Sauvegarder un projet complet
        
        Returns:
            dict avec 'success', 'path', 'message'
        """
        try:
            # Créer le dossier du projet
            timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
            safe_name = "".join(c for c in project_name if c.isalnum() or c in (' ', '-', '_')).strip()
            project_folder = self.projects_dir / f"{safe_name}_{timestamp}"
            project_folder.mkdir(exist_ok=True)
            
            logger.info(f"Sauvegarde du projet '{project_name}' dans {project_folder}")
            
            # Sauvegarder les pistes audio
            tracks_data = []
            for i, track in enumerate(looper.tracks):
                if track.length > 0 and track.buffer is not None:
                    track_file = project_folder / f"track_{i}.wav"
                    
                    # Extraire l'audio de la piste
                    audio_data = track.buffer[:track.length]
                    
                    # Sauvegarder en WAV
                    sf.write(str(track_file), audio_data, looper.sample_rate, subtype='PCM_24')
                    
                    tracks_data.append({
                        'index': i,
                        'filename': track_file.name,
                        'duration': track.length / looper.sample_rate,
                        'gain': track.gain,
                        'muted': track.muted,
                    })
                    
                    logger.info(f"  Track {i} sauvegardée: {track_file.name}")
                else:
                    tracks_data.append(None)
            
            # Créer le fichier de métadonnées du projet
            project_data = {
                'project_name': project_name,
                'version': '1.0',
                'created_at': datetime.now().isoformat(),
                'sample_rate': looper.sample_rate,
                
                # Looper
                'looper': {
                    'active_track': looper.active_track,
                    'sync_mode': looper.sync_mode,
                    'master_track_index': looper.master_track_index,
                    'master_length': looper.master_length,
                    'tracks': tracks_data,
                },
                
                # Effets
                'effects': {},
                
                # Mastering
                'mastering': None,
                
                # Métronome
                'metronome': None,
            }
            
            # Sauvegarder les paramètres des effets
            if effects:
                for name, effect in effects.items():
                    project_data['effects'][name] = {
                        'enabled': effect.enabled,
                        'parameters': effect.parameters.copy() if hasattr(effect, 'parameters') else {}
                    }
            
            # Sauvegarder les paramètres du mastering
            if mastering_chain:
                project_data['mastering'] = mastering_chain.get_state()
            
            # Sauvegarder les paramètres du métronome
            if metronome:
                project_data['metronome'] = {
                    'enabled': metronome.enabled,
                    'parameters': metronome.parameters.copy()
                }
            
            # Écrire le fichier JSON
            project_file = project_folder / "project.json"
            with open(project_file, 'w', encoding='utf-8') as f:
                json.dump(project_data, f, indent=2, ensure_ascii=False)
            
            logger.info(f"✅ Projet '{project_name}' sauvegardé avec succès")
            
            return {
                'success': True,
                'path': str(project_folder),
                'message': f"Projet sauvegardé: {project_folder.name}"
            }
            
        except Exception as e:
            logger.error(f"Erreur sauvegarde projet: {e}")
            import traceback
            traceback.print_exc()
            return {
                'success': False,
                'message': f"Erreur: {str(e)}"
            }
    
    def load_project(self, project_path: str, looper, effects: dict, mastering_chain, metronome) -> dict:
        """
        Charger un projet complet
        
        Returns:
            dict avec 'success', 'message', 'project_data'
        """
        try:
            project_folder = Path(project_path)
            project_file = project_folder / "project.json"
            
            if not project_file.exists():
                return {
                    'success': False,
                    'message': f"Fichier projet non trouvé: {project_file}"
                }
            
            logger.info(f"Chargement du projet depuis {project_folder}")
            
            # Lire le fichier JSON
            with open(project_file, 'r', encoding='utf-8') as f:
                project_data = json.load(f)
            
            # Vérifier la version
            if project_data.get('version') != '1.0':
                logger.warning(f"Version du projet différente: {project_data.get('version')}")
            
            # Vider le looper actuel
            looper.clear_all()
            
            # Charger les pistes audio
            tracks_data = project_data['looper']['tracks']
            for i, track_info in enumerate(tracks_data):
                if track_info is None:
                    continue
                
                track_file = project_folder / track_info['filename']
                
                if not track_file.exists():
                    logger.warning(f"Fichier piste manquant: {track_file}")
                    continue
                
                # Charger l'audio
                audio_data, sr = sf.read(str(track_file), dtype='float32')
                
                # Vérifier le sample rate
                if sr != looper.sample_rate:
                    logger.warning(f"Sample rate différent: {sr} vs {looper.sample_rate}")
                    # TODO: Resampling si nécessaire
                
                # Charger dans la piste
                track = looper.tracks[i]
                track.length = len(audio_data)
                track._ensure_buffer(track.length)
                track.buffer[:track.length] = audio_data
                
                # Restaurer les paramètres
                track.gain = track_info.get('gain', 1.0)
                track.muted = track_info.get('muted', False)
                
                logger.info(f"  Track {i} chargée: {track_info['duration']:.2f}s")
            
            # Restaurer l'état du looper
            looper.active_track = project_data['looper'].get('active_track', 0)
            looper.sync_mode = project_data['looper'].get('sync_mode', True)
            looper.master_track_index = project_data['looper'].get('master_track_index')
            looper.master_length = project_data['looper'].get('master_length', 0)
            
            # Restaurer les effets
            if 'effects' in project_data and effects:
                for name, effect_data in project_data['effects'].items():
                    if name in effects:
                        effect = effects[name]
                        effect.enabled = effect_data.get('enabled', False)
                        
                        # Restaurer les paramètres
                        for param_name, param_value in effect_data.get('parameters', {}).items():
                            if param_name in effect.parameters:
                                effect.set_parameter(param_name, param_value)
                        
                        logger.info(f"  Effet '{name}' restauré")
            
            # Restaurer le mastering
            if 'mastering' in project_data and mastering_chain and project_data['mastering']:
                mastering_data = project_data['mastering']
                
                mastering_chain.parameters['eq_enabled'] = mastering_data.get('eq_enabled', False)
                mastering_chain.eq.enabled = mastering_data.get('eq_enabled', False)
                
                mastering_chain.parameters['compressor_enabled'] = mastering_data.get('compressor_enabled', False)
                mastering_chain.compressor.enabled = mastering_data.get('compressor_enabled', False)
                
                mastering_chain.parameters['limiter_enabled'] = mastering_data.get('limiter_enabled', True)
                mastering_chain.limiter.enabled = mastering_data.get('limiter_enabled', True)
                
                # Restaurer les paramètres EQ
                if 'eq_params' in mastering_data:
                    for param, value in mastering_data['eq_params'].items():
                        mastering_chain.eq.set_parameter(param, value)
                
                # Restaurer les paramètres Compressor
                if 'compressor_params' in mastering_data:
                    for param, value in mastering_data['compressor_params'].items():
                        mastering_chain.compressor.set_parameter(param, value)
                
                # Restaurer les paramètres Limiter
                if 'limiter_params' in mastering_data:
                    for param, value in mastering_data['limiter_params'].items():
                        mastering_chain.limiter.set_parameter(param, value)
                
                logger.info("  Mastering restauré")
            
            # Restaurer le métronome
            if 'metronome' in project_data and metronome and project_data['metronome']:
                metronome_data = project_data['metronome']
                metronome.enabled = metronome_data.get('enabled', False)
                
                for param, value in metronome_data.get('parameters', {}).items():
                    if param in metronome.parameters:
                        metronome.set_parameter(param, value)
                
                logger.info("  Métronome restauré")
            
            logger.info(f"✅ Projet '{project_data['project_name']}' chargé avec succès")
            
            return {
                'success': True,
                'message': f"Projet chargé: {project_data['project_name']}",
                'project_data': project_data
            }
            
        except Exception as e:
            logger.error(f"Erreur chargement projet: {e}")
            import traceback
            traceback.print_exc()
            return {
                'success': False,
                'message': f"Erreur: {str(e)}"
            }
    
    def list_projects(self) -> list:
        """Lister tous les projets disponibles"""
        projects = []
        
        try:
            for project_folder in self.projects_dir.iterdir():
                if not project_folder.is_dir():
                    continue
                
                project_file = project_folder / "project.json"
                if not project_file.exists():
                    continue
                
                # Lire les métadonnées
                with open(project_file, 'r', encoding='utf-8') as f:
                    project_data = json.load(f)
                
                projects.append({
                    'name': project_data.get('project_name', project_folder.name),
                    'path': str(project_folder),
                    'created_at': project_data.get('created_at', ''),
                    'tracks_count': sum(1 for t in project_data['looper']['tracks'] if t is not None),
                })
            
            # Trier par date de création (plus récent en premier)
            projects.sort(key=lambda p: p['created_at'], reverse=True)
            
        except Exception as e:
            logger.error(f"Erreur listage projets: {e}")
        
        return projects
    
    def delete_project(self, project_path: str) -> dict:
        """Supprimer un projet"""
        try:
            project_folder = Path(project_path)
            
            if not project_folder.exists():
                return {
                    'success': False,
                    'message': 'Projet non trouvé'
                }
            
            # Supprimer tous les fichiers
            import shutil
            shutil.rmtree(project_folder)
            
            logger.info(f"Projet supprimé: {project_folder}")
            
            return {
                'success': True,
                'message': 'Projet supprimé'
            }
            
        except Exception as e:
            logger.error(f"Erreur suppression projet: {e}")
            return {
                'success': False,
                'message': f"Erreur: {str(e)}"
            }
    
    def export_project_as_stems(self, project_path: str, output_folder: str) -> dict:
        """
        Exporter toutes les pistes d'un projet comme stems séparés
        """
        try:
            project_folder = Path(project_path)
            output_path = Path(output_folder)
            output_path.mkdir(exist_ok=True)
            
            project_file = project_folder / "project.json"
            with open(project_file, 'r', encoding='utf-8') as f:
                project_data = json.load(f)
            
            exported_files = []
            
            for i, track_info in enumerate(project_data['looper']['tracks']):
                if track_info is None:
                    continue
                
                src_file = project_folder / track_info['filename']
                dst_file = output_path / f"stem_{i+1}.wav"
                
                # Copier le fichier
                import shutil
                shutil.copy(src_file, dst_file)
                
                exported_files.append(dst_file.name)
            
            logger.info(f"Stems exportés: {len(exported_files)} fichiers")
            
            return {
                'success': True,
                'message': f"{len(exported_files)} stems exportés",
                'files': exported_files
            }
            
        except Exception as e:
            logger.error(f"Erreur export stems: {e}")
            return {
                'success': False,
                'message': f"Erreur: {str(e)}"
            }


class AutoSaveManager:
    """
    Gestionnaire de sauvegarde automatique
    """
    
    def __init__(self, project_manager: ProjectManager, interval_seconds: int = 300):
        self.project_manager = project_manager
        self.interval_seconds = interval_seconds
        self.auto_save_enabled = False
        self.current_project_name = None
        
        import threading
        self.timer = None
    
    def enable(self, project_name: str):
        """Activer l'auto-save pour un projet"""
        self.auto_save_enabled = True
        self.current_project_name = project_name
        self._schedule_next_save()
        logger.info(f"Auto-save activé pour '{project_name}' (toutes les {self.interval_seconds}s)")
    
    def disable(self):
        """Désactiver l'auto-save"""
        self.auto_save_enabled = False
        if self.timer:
            self.timer.cancel()
        logger.info("Auto-save désactivé")
    
    def _schedule_next_save(self):
        """Planifier la prochaine sauvegarde"""
        if not self.auto_save_enabled:
            return
        
        import threading
        self.timer = threading.Timer(self.interval_seconds, self._do_auto_save)
        self.timer.daemon = True
        self.timer.start()
    
    def _do_auto_save(self):
        """Effectuer une sauvegarde automatique"""
        if not self.auto_save_enabled or not self.current_project_name:
            return
        
        logger.info(f"Auto-save: sauvegarde de '{self.current_project_name}'...")
        
        # Effectuer la sauvegarde (nécessite accès au looper, effects, etc.)
        # Cette partie sera gérée par web_server.py
        
        self._schedule_next_save()