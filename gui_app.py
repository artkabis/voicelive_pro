"""
VoiceLive Pro - Interface Graphique Complète
=============================================
Interface professionnelle pour contrôler tous les aspects de la pédale
"""

import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).parent))

from PyQt6.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QLabel, QPushButton, QSlider, QComboBox, QGroupBox, QGridLayout,
    QProgressBar, QDial, QSpinBox, QTabWidget, QMessageBox
)
from PyQt6.QtCore import Qt, QTimer, pyqtSignal
from PyQt6.QtGui import QPalette, QColor, QFont

from src.audio.engine_optimized import AudioEngineOptimized
from src.audio.looper_effect import LooperEffect
from src.effects.vocal.harmony import Harmony
from src.effects.vocal.reverb_pro import ReverbPro
from src.effects.vocal.doubling import Doubling
from src.effects.guitar.drive import Drive
from src.effects.guitar.chorus import Chorus
from src.effects.guitar.wah import Wah
from src.presets.manager import PresetManager

import sounddevice as sd


class VUMeter(QProgressBar):
    """VU-mètre personnalisé"""
    
    def __init__(self, orientation=Qt.Orientation.Horizontal):
        super().__init__()
        self.setOrientation(orientation)
        self.setRange(0, 100)
        self.setTextVisible(False)
        self.setMaximumHeight(20)
        
        # Style avec couleurs
        self.setStyleSheet("""
            QProgressBar {
                border: 2px solid #555;
                border-radius: 5px;
                background-color: #222;
            }
            QProgressBar::chunk {
                background: qlineargradient(
                    x1: 0, y1: 0, x2: 1, y2: 0,
                    stop: 0 #0f0,
                    stop: 0.7 #ff0,
                    stop: 0.9 #f80,
                    stop: 1.0 #f00
                );
                border-radius: 3px;
            }
        """)


class LooperTrackWidget(QWidget):
    """Widget pour une piste de looper"""
    
    recordClicked = pyqtSignal(int)
    playClicked = pyqtSignal(int)
    overdubClicked = pyqtSignal(int)
    clearClicked = pyqtSignal(int)
    
    def __init__(self, track_number):
        super().__init__()
        self.track_number = track_number
        self.init_ui()
    
    def init_ui(self):
        layout = QVBoxLayout(self)
        
        # Titre
        title = QLabel(f"TRACK {self.track_number}")
        title.setAlignment(Qt.AlignmentFlag.AlignCenter)
        title.setStyleSheet("font-weight: bold; font-size: 16px; color: #0f0;")
        layout.addWidget(title)
        
        # État
        self.status_label = QLabel("EMPTY")
        self.status_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.status_label.setStyleSheet("""
            padding: 10px;
            background-color: #333;
            border: 2px solid #666;
            border-radius: 5px;
            font-size: 14px;
        """)
        layout.addWidget(self.status_label)
        
        # Durée
        self.duration_label = QLabel("0.0s")
        self.duration_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.duration_label.setStyleSheet("font-size: 12px; color: #aaa;")
        layout.addWidget(self.duration_label)
        
        # Boutons
        btn_layout = QGridLayout()
        
        self.rec_btn = QPushButton("REC")
        self.rec_btn.setCheckable(True)
        self.rec_btn.setStyleSheet("""
            QPushButton {
                background-color: #c00;
                color: white;
                font-weight: bold;
                padding: 10px;
                border-radius: 5px;
            }
            QPushButton:checked {
                background-color: #f00;
            }
        """)
        self.rec_btn.clicked.connect(lambda: self.recordClicked.emit(self.track_number))
        btn_layout.addWidget(self.rec_btn, 0, 0)
        
        self.play_btn = QPushButton("PLAY")
        self.play_btn.setCheckable(True)
        self.play_btn.setStyleSheet("""
            QPushButton {
                background-color: #0a0;
                color: white;
                font-weight: bold;
                padding: 10px;
                border-radius: 5px;
            }
            QPushButton:checked {
                background-color: #0f0;
            }
        """)
        self.play_btn.clicked.connect(lambda: self.playClicked.emit(self.track_number))
        btn_layout.addWidget(self.play_btn, 0, 1)
        
        self.overdub_btn = QPushButton("OVER")
        self.overdub_btn.setCheckable(True)
        self.overdub_btn.setStyleSheet("""
            QPushButton {
                background-color: #f80;
                color: white;
                font-weight: bold;
                padding: 10px;
                border-radius: 5px;
            }
            QPushButton:checked {
                background-color: #fa0;
            }
        """)
        self.overdub_btn.clicked.connect(lambda: self.overdubClicked.emit(self.track_number))
        btn_layout.addWidget(self.overdub_btn, 1, 0)
        
        self.clear_btn = QPushButton("CLEAR")
        self.clear_btn.setStyleSheet("""
            QPushButton {
                background-color: #666;
                color: white;
                padding: 10px;
                border-radius: 5px;
            }
            QPushButton:hover {
                background-color: #888;
            }
        """)
        self.clear_btn.clicked.connect(lambda: self.clearClicked.emit(self.track_number))
        btn_layout.addWidget(self.clear_btn, 1, 1)
        
        layout.addLayout(btn_layout)
    
    def update_state(self, state):
        """Mettre à jour l'état de la piste"""
        status = []
        
        if state['recording']:
            status.append("RECORDING")
            self.status_label.setStyleSheet("""
                padding: 10px;
                background-color: #c00;
                border: 2px solid #f00;
                border-radius: 5px;
                font-size: 14px;
                font-weight: bold;
                color: white;
            """)
            self.rec_btn.setChecked(True)
        elif state['overdubbing']:
            status.append("OVERDUB")
            self.status_label.setStyleSheet("""
                padding: 10px;
                background-color: #f80;
                border: 2px solid #fa0;
                border-radius: 5px;
                font-size: 14px;
                font-weight: bold;
                color: white;
            """)
            self.overdub_btn.setChecked(True)
        elif state['playing']:
            status.append("PLAYING")
            self.status_label.setStyleSheet("""
                padding: 10px;
                background-color: #0a0;
                border: 2px solid #0f0;
                border-radius: 5px;
                font-size: 14px;
                font-weight: bold;
                color: white;
            """)
            self.play_btn.setChecked(True)
        else:
            if state['duration'] > 0:
                status.append("STOPPED")
                self.status_label.setStyleSheet("""
                    padding: 10px;
                    background-color: #333;
                    border: 2px solid #0f0;
                    border-radius: 5px;
                    font-size: 14px;
                    color: #0f0;
                """)
                self.rec_btn.setChecked(False)
                self.play_btn.setChecked(False)
            else:
                status.append("EMPTY")
                self.status_label.setStyleSheet("""
                    padding: 10px;
                    background-color: #333;
                    border: 2px solid #666;
                    border-radius: 5px;
                    font-size: 14px;
                    color: #aaa;
                """)
                self.rec_btn.setChecked(False)
                self.play_btn.setChecked(False)
            
            self.overdub_btn.setChecked(False)
        
        self.status_label.setText(' '.join(status))
        self.duration_label.setText(f"{state['duration']:.1f}s")


class EffectControlWidget(QWidget):
    """Widget de contrôle d'un effet"""
    
    valueChanged = pyqtSignal(str, str, float)  # effect_name, param_name, value
    
    def __init__(self, effect_name, param_name, min_val, max_val, default_val, suffix=""):
        super().__init__()
        self.effect_name = effect_name
        self.param_name = param_name
        self.suffix = suffix
        
        layout = QVBoxLayout(self)
        layout.setContentsMargins(5, 5, 5, 5)
        
        # Label
        label = QLabel(param_name.replace('_', ' ').title())
        label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        label.setStyleSheet("font-size: 11px; color: #aaa;")
        layout.addWidget(label)
        
        # Dial
        self.dial = QDial()
        self.dial.setRange(int(min_val * 100), int(max_val * 100))
        self.dial.setValue(int(default_val * 100))
        self.dial.setNotchesVisible(True)
        self.dial.setFixedSize(60, 60)
        self.dial.valueChanged.connect(self.on_value_changed)
        layout.addWidget(self.dial, alignment=Qt.AlignmentFlag.AlignCenter)
        
        # Value display
        self.value_label = QLabel(f"{default_val:.2f}{suffix}")
        self.value_label.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.value_label.setStyleSheet("font-size: 10px; color: #0f0; font-weight: bold;")
        layout.addWidget(self.value_label)
    
    def on_value_changed(self, value):
        """Callback quand la valeur change"""
        real_value = value / 100.0
        self.value_label.setText(f"{real_value:.2f}{self.suffix}")
        self.valueChanged.emit(self.effect_name, self.param_name, real_value)


class VoiceLiveGUI(QMainWindow):
    """Fenêtre principale de VoiceLive Pro"""
    
    def __init__(self):
        super().__init__()
        
        # Audio engine
        self.engine = None
        self.looper_effect = None
        self.looper = None
        self.preset_manager = PresetManager()
        
        # Effets
        self.effects = {}
        
        # Timer pour mise à jour
        self.update_timer = QTimer()
        self.update_timer.timeout.connect(self.update_meters)
        
        self.init_ui()
        self.setup_audio()
    
    def init_ui(self):
        """Initialiser l'interface"""
        self.setWindowTitle("VoiceLive Pro - Pédale Multi-Effets")
        self.setGeometry(100, 100, 1400, 900)
        
        # Style sombre
        self.setStyleSheet("""
            QMainWindow {
                background-color: #1a1a1a;
            }
            QWidget {
                background-color: #1a1a1a;
                color: #fff;
            }
            QGroupBox {
                border: 2px solid #444;
                border-radius: 8px;
                margin-top: 10px;
                font-weight: bold;
                padding-top: 10px;
            }
            QGroupBox::title {
                color: #0f0;
                subcontrol-origin: margin;
                left: 10px;
                padding: 0 5px 0 5px;
            }
        """)
        
        # Widget central
        central_widget = QWidget()
        self.setCentralWidget(central_widget)
        main_layout = QVBoxLayout(central_widget)
        
        # En-tête
        header = self.create_header()
        main_layout.addWidget(header)
        
        # Tabs
        tabs = QTabWidget()
        tabs.setStyleSheet("""
            QTabWidget::pane {
                border: 2px solid #444;
                background-color: #1a1a1a;
            }
            QTabBar::tab {
                background-color: #333;
                color: #aaa;
                padding: 10px 20px;
                border: 1px solid #444;
            }
            QTabBar::tab:selected {
                background-color: #0a0;
                color: white;
                font-weight: bold;
            }
        """)
        
        tabs.addTab(self.create_looper_tab(), "LOOPER")
        tabs.addTab(self.create_vocal_effects_tab(), "VOCAL EFFECTS")
        tabs.addTab(self.create_guitar_effects_tab(), "GUITAR EFFECTS")
        tabs.addTab(self.create_presets_tab(), "PRESETS")
        
        main_layout.addWidget(tabs)
        
        # VU-mètres
        meters = self.create_meters()
        main_layout.addWidget(meters)
    
    def create_header(self):
        """Créer l'en-tête"""
        header = QWidget()
        layout = QHBoxLayout(header)
        
        # Logo
        logo = QLabel("VOICELIVE PRO")
        logo.setStyleSheet("""
            font-size: 32px;
            font-weight: bold;
            color: #0f0;
            padding: 20px;
        """)
        layout.addWidget(logo)
        
        layout.addStretch()
        
        # Contrôles audio
        audio_group = QGroupBox("AUDIO")
        audio_layout = QHBoxLayout(audio_group)
        
        # Device selector
        audio_layout.addWidget(QLabel("Device:"))
        self.device_combo = QComboBox()
        self.device_combo.setMinimumWidth(200)
        devices = sd.query_devices()
        for i, dev in enumerate(devices):
            if "ASIO" in dev['name']:
                self.device_combo.addItem(f"{i}: {dev['name']}", i)
        self.device_combo.setCurrentIndex(0)
        audio_layout.addWidget(self.device_combo)
        
        # Start/Stop
        self.start_btn = QPushButton("START")
        self.start_btn.setStyleSheet("""
            QPushButton {
                background-color: #0a0;
                color: white;
                font-weight: bold;
                font-size: 16px;
                padding: 15px 30px;
                border-radius: 8px;
            }
            QPushButton:hover {
                background-color: #0c0;
            }
        """)
        self.start_btn.clicked.connect(self.toggle_audio)
        audio_layout.addWidget(self.start_btn)
        
        layout.addWidget(audio_group)
        
        return header
    
    def create_looper_tab(self):
        """Créer l'onglet looper"""
        widget = QWidget()
        layout = QHBoxLayout(widget)
        
        # 3 pistes
        for i in range(3):
            track_widget = LooperTrackWidget(i + 1)
            track_widget.recordClicked.connect(self.on_track_record)
            track_widget.playClicked.connect(self.on_track_play)
            track_widget.overdubClicked.connect(self.on_track_overdub)
            track_widget.clearClicked.connect(self.on_track_clear)
            layout.addWidget(track_widget)
            
            if i == 0:
                self.track1_widget = track_widget
            elif i == 1:
                self.track2_widget = track_widget
            else:
                self.track3_widget = track_widget
        
        return widget
    
    def create_vocal_effects_tab(self):
        """Créer l'onglet effets vocaux"""
        widget = QWidget()
        layout = QVBoxLayout(widget)
        
        # Harmony
        harmony_group = QGroupBox("HARMONY")
        harmony_layout = QHBoxLayout(harmony_group)
        
        self.harmony_enable = QPushButton("ON/OFF")
        self.harmony_enable.setCheckable(True)
        self.harmony_enable.clicked.connect(lambda: self.toggle_effect('harmony'))
        harmony_layout.addWidget(self.harmony_enable)
        
        harmony_mix = EffectControlWidget("harmony", "mix", 0, 1, 0.4)
        harmony_mix.valueChanged.connect(self.on_effect_param_changed)
        harmony_layout.addWidget(harmony_mix)
        
        layout.addWidget(harmony_group)
        
        # Reverb
        reverb_group = QGroupBox("REVERB PRO")
        reverb_layout = QHBoxLayout(reverb_group)
        
        self.reverb_enable = QPushButton("ON/OFF")
        self.reverb_enable.setCheckable(True)
        self.reverb_enable.clicked.connect(lambda: self.toggle_effect('reverb'))
        reverb_layout.addWidget(self.reverb_enable)
        
        reverb_room = EffectControlWidget("reverb", "room_size", 0, 1, 0.5)
        reverb_room.valueChanged.connect(self.on_effect_param_changed)
        reverb_layout.addWidget(reverb_room)
        
        reverb_wet = EffectControlWidget("reverb", "wet", 0, 1, 0.3)
        reverb_wet.valueChanged.connect(self.on_effect_param_changed)
        reverb_layout.addWidget(reverb_wet)
        
        layout.addWidget(reverb_group)
        
        # Doubling
        doubling_group = QGroupBox("DOUBLING")
        doubling_layout = QHBoxLayout(doubling_group)
        
        self.doubling_enable = QPushButton("ON/OFF")
        self.doubling_enable.setCheckable(True)
        self.doubling_enable.clicked.connect(lambda: self.toggle_effect('doubling'))
        doubling_layout.addWidget(self.doubling_enable)
        
        doubling_mix = EffectControlWidget("doubling", "mix", 0, 1, 0.3)
        doubling_mix.valueChanged.connect(self.on_effect_param_changed)
        doubling_layout.addWidget(doubling_mix)
        
        layout.addWidget(doubling_group)
        
        layout.addStretch()
        
        return widget
    
    def create_guitar_effects_tab(self):
        """Créer l'onglet effets guitare"""
        widget = QWidget()
        layout = QVBoxLayout(widget)
        
        # Drive
        drive_group = QGroupBox("DRIVE")
        drive_layout = QHBoxLayout(drive_group)
        
        self.drive_enable = QPushButton("ON/OFF")
        self.drive_enable.setCheckable(True)
        self.drive_enable.clicked.connect(lambda: self.toggle_effect('drive'))
        drive_layout.addWidget(self.drive_enable)
        
        drive_amount = EffectControlWidget("drive", "drive", 0, 1, 0.5)
        drive_amount.valueChanged.connect(self.on_effect_param_changed)
        drive_layout.addWidget(drive_amount)
        
        drive_mix = EffectControlWidget("drive", "mix", 0, 1, 0.5)
        drive_mix.valueChanged.connect(self.on_effect_param_changed)
        drive_layout.addWidget(drive_mix)
        
        layout.addWidget(drive_group)
        
        # Chorus
        chorus_group = QGroupBox("CHORUS")
        chorus_layout = QHBoxLayout(chorus_group)
        
        self.chorus_enable = QPushButton("ON/OFF")
        self.chorus_enable.setCheckable(True)
        self.chorus_enable.clicked.connect(lambda: self.toggle_effect('chorus'))
        chorus_layout.addWidget(self.chorus_enable)
        
        chorus_rate = EffectControlWidget("chorus", "rate", 0.1, 5, 0.5, "Hz")
        chorus_rate.valueChanged.connect(self.on_effect_param_changed)
        chorus_layout.addWidget(chorus_rate)
        
        chorus_mix = EffectControlWidget("chorus", "mix", 0, 1, 0.4)
        chorus_mix.valueChanged.connect(self.on_effect_param_changed)
        chorus_layout.addWidget(chorus_mix)
        
        layout.addWidget(chorus_group)
        
        layout.addStretch()
        
        return widget
    
    def create_presets_tab(self):
        """Créer l'onglet presets"""
        widget = QWidget()
        layout = QVBoxLayout(widget)
        
        # Boutons de presets
        presets_grid = QGridLayout()
        
        for i in range(10):
            for j in range(5):
                slot = i * 5 + j
                btn = QPushButton(f"Preset {slot + 1}")
                btn.setMinimumHeight(50)
                btn.clicked.connect(lambda checked, s=slot: self.load_preset(s))
                presets_grid.addWidget(btn, i, j)
        
        layout.addLayout(presets_grid)
        
        # Sauvegarder preset
        save_layout = QHBoxLayout()
        save_layout.addWidget(QLabel("Sauvegarder au slot:"))
        
        self.preset_slot = QSpinBox()
        self.preset_slot.setRange(0, 49)
        save_layout.addWidget(self.preset_slot)
        
        save_btn = QPushButton("SAVE")
        save_btn.clicked.connect(self.save_preset)
        save_layout.addWidget(save_btn)
        
        layout.addLayout(save_layout)
        
        return widget
    
    def create_meters(self):
        """Créer les VU-mètres"""
        meters_widget = QWidget()
        layout = QVBoxLayout(meters_widget)
        
        # Input
        in_layout = QHBoxLayout()
        in_layout.addWidget(QLabel("INPUT:"))
        self.input_meter = VUMeter()
        in_layout.addWidget(self.input_meter)
        layout.addLayout(in_layout)
        
        # Output
        out_layout = QHBoxLayout()
        out_layout.addWidget(QLabel("OUTPUT:"))
        self.output_meter = VUMeter()
        out_layout.addWidget(self.output_meter)
        layout.addLayout(out_layout)
        
        return meters_widget
    
    def setup_audio(self):
        """Configurer l'audio"""
        self.engine = AudioEngineOptimized(sample_rate=44100, buffer_size=128)
        
        # Créer les effets
        self.looper_effect = LooperEffect(sample_rate=44100, max_duration=120)
        self.looper = self.looper_effect.get_looper()
        
        self.effects['harmony'] = Harmony()
        self.effects['reverb'] = ReverbPro()
        self.effects['doubling'] = Doubling()
        self.effects['drive'] = Drive()
        self.effects['chorus'] = Chorus()
        
        # Ajouter au moteur
        for effect in self.effects.values():
            self.engine.add_effect(effect)
        
        self.engine.add_effect(self.looper_effect)
    
    def toggle_audio(self):
        """Démarrer/arrêter l'audio"""
        if not self.engine.is_running:
            device_idx = self.device_combo.currentData()
            try:
                self.engine.start(input_device=device_idx, output_device=device_idx)
                self.start_btn.setText("STOP")
                self.start_btn.setStyleSheet("""
                    QPushButton {
                        background-color: #c00;
                        color: white;
                        font-weight: bold;
                        font-size: 16px;
                        padding: 15px 30px;
                        border-radius: 8px;
                    }
                """)
                self.update_timer.start(50)
            except Exception as e:
                QMessageBox.critical(self, "Erreur", f"Impossible de démarrer l'audio: {e}")
        else:
            self.engine.stop()
            self.start_btn.setText("START")
            self.start_btn.setStyleSheet("""
                QPushButton {
                    background-color: #0a0;
                    color: white;
                    font-weight: bold;
                    font-size: 16px;
                    padding: 15px 30px;
                    border-radius: 8px;
                }
            """)
            self.update_timer.stop()
    
    def update_meters(self):
        """Mettre à jour les VU-mètres et états"""
        if self.engine.is_running:
            # Niveaux
            inp = self.engine.get_input_level()
            out = self.engine.get_output_level()
            
            self.input_meter.setValue(int(inp * 100))
            self.output_meter.setValue(int(out * 100))
            
            # États des pistes
            states = self.looper.get_states()
            self.track1_widget.update_state(states[0])
            self.track2_widget.update_state(states[1])
            self.track3_widget.update_state(states[2])
    
    def on_track_record(self, track_num):
        """Record sur une piste"""
        self.looper.set_active_track(track_num - 1)
        self.looper.record_stop()
    
    def on_track_play(self, track_num):
        """Play sur une piste"""
        self.looper.set_active_track(track_num - 1)
        self.looper.record_stop()
    
    def on_track_overdub(self, track_num):
        """Overdub sur une piste"""
        self.looper.set_active_track(track_num - 1)
        self.looper.overdub()
    
    def on_track_clear(self, track_num):
        """Clear une piste"""
        self.looper.clear_track(track_num - 1)
    
    def toggle_effect(self, effect_name):
        """Activer/désactiver un effet"""
        if effect_name in self.effects:
            self.effects[effect_name].toggle()
    
    def on_effect_param_changed(self, effect_name, param_name, value):
        """Changement de paramètre d'effet"""
        if effect_name in self.effects:
            self.effects[effect_name].set_parameter(param_name, value)
    
    def save_preset(self):
        """Sauvegarder un preset"""
        slot = self.preset_slot.value()
        name = f"Preset {slot + 1}"
        
        effects_state = [eff.get_state() for eff in self.engine.get_effects()]
        looper_state = self.looper.get_states()
        
        self.preset_manager.save_preset(slot, name, effects_state, looper_state)
        QMessageBox.information(self, "Preset", f"Preset sauvegardé au slot {slot}")
    
    def load_preset(self, slot):
        """Charger un preset"""
        preset = self.preset_manager.load_preset(slot)
        if preset:
            QMessageBox.information(self, "Preset", f"Preset {preset['name']} chargé")
        else:
            QMessageBox.warning(self, "Preset", f"Slot {slot} vide")


def main():
    app = QApplication(sys.argv)
    
    # Style global
    app.setStyle('Fusion')
    
    window = VoiceLiveGUI()
    window.show()
    
    sys.exit(app.exec())


if __name__ == '__main__':
    main()
    