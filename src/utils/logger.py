"""
Système de logging - VERSION CORRIGÉE pour Windows
"""
import logging
from pathlib import Path
from datetime import datetime
import sys

class Logger:
    _instance = None
    
    def __new__(cls):
        if cls._instance is None:
            cls._instance = super().__new__(cls)
            cls._instance._setup_logger()
        return cls._instance
    
    def _setup_logger(self):
        log_dir = Path(__file__).parent.parent.parent / "logs"
        log_dir.mkdir(exist_ok=True)
        
        log_file = log_dir / f"voicelive_{datetime.now().strftime('%Y%m%d')}.log"
        
        self.logger = logging.getLogger('VoiceLivePro')
        self.logger.setLevel(logging.DEBUG)
        
        # Handler fichier avec UTF-8
        fh = logging.FileHandler(log_file, encoding='utf-8')
        fh.setLevel(logging.DEBUG)
        
        # Handler console avec UTF-8
        ch = logging.StreamHandler(sys.stdout)
        ch.setLevel(logging.INFO)
        
        # Format
        formatter = logging.Formatter('%(asctime)s - %(levelname)s - %(message)s')
        fh.setFormatter(formatter)
        ch.setFormatter(formatter)
        
        self.logger.addHandler(fh)
        self.logger.addHandler(ch)
    
    def _safe_log(self, level, msg):
        """Log avec gestion des erreurs d'encodage"""
        try:
            getattr(self.logger, level)(msg)
        except UnicodeEncodeError:
            # Retirer les emojis si problème d'encodage
            msg_clean = msg.encode('ascii', 'ignore').decode('ascii')
            getattr(self.logger, level)(msg_clean)
    
    def debug(self, msg):
        self._safe_log('debug', msg)
    
    def info(self, msg):
        self._safe_log('info', msg)
    
    def warning(self, msg):
        self._safe_log('warning', msg)
    
    def error(self, msg):
        self._safe_log('error', msg)
    
    def critical(self, msg):
        self._safe_log('critical', msg)

logger = Logger()