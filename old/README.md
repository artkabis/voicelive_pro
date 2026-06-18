# Legacy — VoiceLive Pro v1 (Python)

Version antérieure du projet (looper temps réel en **Python** : Flask/SocketIO +
sounddevice, effets, métronome, accordeur, mastering). **Conservée pour
référence** après le merge — non maintenue et **non construite par la CI**.

La **v2** (cœur C++ pur + JUCE, à la racine du dépôt) la remplace pour la
latence, la qualité DSP et le support mobile.

## Lancer la v1 (si besoin)
```bash
pip install -r requirements.txt
python run_web.py     # interface web
# ou
python gui_app.py     # interface PyQt
```
