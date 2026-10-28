# Couverture fonctionnelle et critères de développement

La référence est la famille Eos, pas Element. La lecture documentaire et l’inventaire restent partiels. Aucune parité n’est revendiquée.

| Domaine | Implémenté et testé dans 0.1 | Travail restant |
|---|---|---|
| Sélection | Circuits, plages, groupes, niveaux manuels | Query, filtres, ordre complexe, sous-fixtures |
| Palettes | Intensité, valeurs référencées | Position, couleur, beam, by-type, verrouillage, références imbriquées |
| Mémoires | Record, Update, suppression, numéros décimaux, tracking | Multipart, délais, temps par paramètre, follow/hang, liens, mark |
| Cue Only | Opération du moteur testée | Exposition dans l’interface et cas d’insertion complexes |
| Séquences | Plusieurs simultanées, priorité, Go/Back/Pause, release | Ownership Move Fade par paramètre conforme Eos, Assert, HTP, background configurable |
| Édition | Programmation Live | Blind, tableur, Undo/Redo transactionnel, preview des modifications propagées |
| Fichiers | JSON versionné, sauvegarde atomique, chargement validé | Migration, autosave, récupération, fusion sélective |
| Patch | Circuits 8 bits et plusieurs univers | GDTF intelligent, 16/24/32 bits, géométrie, MVR, remplacement d’appareils |
| GDTF | Inspection ZIP/XML et CRC | Interprétation complète des fonctions, unités, courbes, relations et cellules |
| Réseau | ArtDmx et sACN, test UDP local | ArtPoll, sACN sync/termination, interfaces réseau, RDM, redondance |
| OSC | Messages float et trois actions en réception locale | Dictionnaire complet, feedback, bundles, temporisation |
| Effets | Générateur de vague d’intensité 1D | Effets absolus/relatifs/à pas, phases, variables, courbes, tempo |
| Pixel mapping | Générateur 1D | Matrices RGB/RGBW, géométrie libre, vidéo, masques, couches, GPU |
| Ergonomie | Vue circuits, palettes, séquences, conduite et clavier virtuel | Workspaces, Magic Sheets équivalentes, tactile, accessibilité, visualisation 3D |
| Hardware | Actions accessibles à l’écran, clavier ordinateur | Interface USB HID/MIDI, firmware, mapping, encodeurs, retours lumineux |
| Fiabilité | Tests unitaires, UDP, test d’interaction Qt, lancement offscreen | Endurance, charge, matériel réel, perte réseau, multi-utilisateur et secours |

La propriété des sorties de cette version est simplifiée : une séquence reprend l’ensemble de son état suivi à chaque Go. Eos possède une sémantique plus fine fondée sur les instructions de mouvement. Le moteur doit évoluer avant toute prétention de compatibilité comportementale.

## Références officielles consultées

- [Palettes : options](https://www.etcconnect.com/WebDocs/Controls/EosFamilyOnlineHelp/en/Content/10_Palettes/Palette_Options.htm)
- [Concepts : tracking, ownership, Block / Assert](https://www.etcconnect.com/WebDocs/Controls/EosFamilyOnlineHelp/en/Content/04_System_Basics/Important_Concepts.htm)
- [Séquences multiples](https://www.etcconnect.com/WebDocs/Controls/EosFamilyOnlineHelp/en/Content/12_Cues_and_Cue_Lists/11_Multiple_Cue_Lists/Multiple_Cue_Lists.htm)
- [Propriétés des séquences](https://www.etcconnect.com/WebDocs/Controls/EosFamilyOnlineHelp/en/Content/12_Cues_and_Cue_Lists/09_Using_the_Cue_List_Index/Cue_List_Properties.htm)
- [Effets](https://www.etcconnect.com/WebDocs/Controls/EosFamilyOnlineHelp/en/Content/14_Effects/About_Effects.htm)
- [Magic Sheets](https://www.etcconnect.com/webdocs/Controls/EosFamilyOnlineHelp/en/Content/18_Magic_Sheets/About_Magic_Sheets.htm)
- [Show Control](https://www.etcconnect.com/WebDocs/Controls/EosFamilyOnlineHelp/en/Content/23_Show_Control/About_Eos_Show_Control.htm)

## Mesurer « mieux »

Scénarios à mesurer avec des utilisateurs : créer une conduite de 30 mémoires ; corriger une couleur référencée ; insérer une mémoire sans changer la suite ; retrouver pourquoi un paramètre ne répond pas ; remplacer une fixture ; reprendre une séquence après un saut ; piloter sans souris. Mesures : nombre de gestes, durée, erreurs, récupération et visibilité de la source propriétaire.
