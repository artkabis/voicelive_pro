import sounddevice as sd

# Lister les devices
devices = sd.query_devices()
print("\nDevices ASIO disponibles:")
for i, dev in enumerate(devices):
    if 'ASIO' in dev['name']:
        print(f"{i}: {dev['name']}")

# Tester l'ouverture du device ASIO
print("\nTest ouverture ASIO4ALL (device 14)...")
try:
    stream = sd.Stream(
        samplerate=44100,
        blocksize=128,
        channels=2,
        device=(14, 14)
    )
    stream.start()
    print("✅ ASIO ouvert avec succès!")
    stream.stop()
    stream.close()
except Exception as e:
    print(f"❌ Erreur: {e}")