"""
Presets d'usine
"""

FACTORY_PRESETS = [
    {
        "name": "Natural Vocal",
        "effects": {
            "doubling": {"enabled": True, "mix": 0.2},
            "reverb_pro": {"enabled": True, "room_size": 0.4, "wet": 0.2}
        }
    },
    {
        "name": "Church Choir",
        "effects": {
            "choir": {"enabled": True, "voices": 8, "mix": 0.6},
            "reverb_pro": {"enabled": True, "room_size": 0.9, "wet": 0.5}
        }
    },
    {
        "name": "Robot Voice",
        "effects": {
            "hardtune": {"enabled": True, "correction": 1.0},
            "vocoder": {"enabled": True, "mix": 0.5}
        }
    },
    {
        "name": "Harmony Vocals",
        "effects": {
            "harmony": {"enabled": True, "voices": 2, "intervals": [4, 7]},
            "reverb_pro": {"enabled": True, "room_size": 0.5, "wet": 0.3}
        }
    },
    {
        "name": "Rock Guitar",
        "effects": {
            "drive": {"enabled": True, "drive": 0.6, "mix": 0.7},
            "delay": {"enabled": True, "delay_time": 0.4, "mix": 0.3}
        }
    }
]
