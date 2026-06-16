# Sécurité

## Surface d'attaque d'une application audio

Contrairement à une intuition courante, un logiciel audio **a** une surface
d'attaque réelle :

1. **Fichiers audio importés** (WAV/FLAC/AIFF…) : en-têtes malformés →
   débordements mémoire. C'est le vecteur n°1.
2. **Fichiers de projet** : désérialisation de données non fiables.
3. **Presets partagés** entre utilisateurs.
4. **Entrées réseau** (futur mode collaboratif/cloud).

## Mesures en place

| Mesure | Mise en œuvre |
|--------|----------------|
| Mémoire sûre | `-fsanitize=address,undefined` en CI/Debug ; pas de cast à la C (`-Wold-style-cast`) ; pas de conversions silencieuses (`-Wconversion`) |
| Pas de débordement logique | Types forts bornés (`Gain`, `SampleRate`) validés à la construction |
| Pas d'état incohérent | Machines à états gardées (`LooperTrack`) |
| Erreurs jamais ignorées | `Result`/`Status` `[[nodiscard]]`, zéro exception silencieuse |
| Pas de secret commité | `detect-private-key` en pre-commit |
| Dépendances maîtrisées | `core/` a **zéro dépendance externe** ; toute dépendance future passe par revue |

## À venir (à mesure que les couches s'ajoutent)

- Validation stricte et *fuzzing* des parseurs de fichiers audio/projet
  (entrées non fiables) avant toute lecture en mémoire.
- Limites de taille/durée sur les imports.
- Scan de dépendances (CodeQL / Dependabot) une fois les libs tierces introduites.

## Signaler une vulnérabilité

Contact : gregnicollepjms@gmail.com — merci de ne pas ouvrir d'issue publique
pour une faille non corrigée.
