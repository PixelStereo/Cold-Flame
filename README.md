# Cold Flame

Jeu d’orgues de bureau en C++17 / Qt 6 pour macOS, Windows et Linux.
**Version 0.1 de développement : le périmètre Eos complet reste un objectif, pas une capacité acquise.**

![Interface](cold-flame-preview.png)

## Compiler et tester

Dépendances : CMake 3.21+, compilateur C++17, Qt 6.2+ (Core, Widgets, Network, Test), zlib.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```

Lancer `build/cold-flame` (Windows multi-configuration : `build/Release/cold-flame.exe`).
Installer les bibliothèques Qt avec l’application pour une distribution autonome ; les exécutables de développement ne sont pas encore des installateurs. La CI configure la compilation et les tests sur les trois systèmes ; seuls les résultats effectivement observés sont des validations.

## Première conduite

1. Le spectacle vierge contient 48 circuits gradateurs patchés aux adresses 1–48. Le menu **Spectacle → Patch** ajoute ou repatche un circuit.
2. Sélectionner les circuits à la souris (Ctrl / Maj pour les sélections multiples), ou saisir `Channel 1 Thru 12 At 50` puis Entrée.
3. Régler le fondu, cliquer **Record**, saisir numéro et libellé. Les valeurs manuelles sont enregistrées dans la séquence sélectionnée. Les paramètres inchangés sont suivis par tracking.
4. Cliquer **Release manuel**, puis **Go**. Les valeurs manuelles conservent la priorité tant qu’elles ne sont pas relâchées. `Ctrl+Espace` déclenche Go, `Ctrl+S` sauvegarde.
5. Ajouter d’autres séquences. Chaque séquence conserve son état et son fondu. La priorité la plus haute gagne ; à priorité égale, la dernière séquence déclenchée gagne pour les circuits qu’elle possède. Release séquence restaure les contributions encore actives.
6. Les palettes d’intensité sont enregistrées depuis la sélection et rappelées par référence. Modifier une palette met à jour sa résolution dans les mémoires lors du prochain rappel. **Update** modifie la mémoire sélectionnée avec les valeurs manuelles et le temps affiché.
7. Sauvegarder en `.cfl`. Le fichier contient patch, palettes, groupes et séquences, sans les états transitoires de restitution. L’ouverture désactive les sorties.

Commandes supplémentaires : `Group 1`, `Intensity 2`, `Cue 2/15`, `Sequence 2` (Go de cette séquence). Le clavier virtuel est accessible dans **Programmation**. Il constitue un premier accès aux actions ; le protocole matériel configurable n’est pas encore implémenté.

## Réseau et interaction

Les sorties sont **désactivées au lancement**. Configurer protocole, destination et univers initial dans **Réseau**. Art-Net utilise une numérotation base 0 ; sACN accepte 1–63999 et `multicast` comme destination. Les adresses absolues du patch sont découpées par blocs de 512 ; l’univers initial est ajouté à l’index du bloc. Fréquence cible : environ 43,5 Hz. Les fondus, l’interface et les sorties utilisent actuellement la même boucle Qt ; leur séparation en threads reste à faire.

OSC est activable dans **Interaction** sur `127.0.0.1:9000`. Messages OSC avec un seul argument float :

- `/coldflame/sequence/1/go` avec une valeur positive : Go.
- `/coldflame/master` de 0 à 1 : grand master.
- `/coldflame/blackout` : positif active, zéro désactive.

Pas encore de bundles OSC, de retours d’état ni de réception distante configurable.

**Pixel mapping** propose une vague sinusoïdale d’intensité sur les circuits sélectionnés, ordonnés par numéro, avec vitesse et répartition réglables. Sa couche passe après les séquences et avant le manuel. C’est un générateur 1D initial ; matrices RGB, couches vidéo, masques et GPU restent à développer.

**Fixtures → Inspecter un profil GDTF** lit une archive `.gdtf` (ZIP stocké ou Deflate), contrôle le CRC et affiche fabricant, modes, empreinte et attributs. Cette version ne transforme pas encore ces attributs en paramètres de projecteurs intelligents. Le patch actuel concerne des circuits 8 bits ; aucun support grandMA propriétaire n’est revendiqué.

## État et prochaines étapes

Voir [la matrice fonctionnelle](docs/FEATURES.md) et [le projet de surface physique](docs/HARDWARE.md). La présente alpha est destinée à la préparation et à l’expérimentation ; elle n’a pas encore été validée sur un parc réel, en longue durée ou en situation de spectacle.
