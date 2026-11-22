# SHIELD Online Archive

Application audio gratuite pour 3DS, développée en priorité pour Old 3DS. Elle recherche les archives audio publiques d'Internet Archive, affiche leurs pistes et permet une préécoute ou un téléchargement sur la carte SD.

**État : version de développement 0.2.0.** Les tests Windows couvrent les parseurs et les décodeurs ; le comportement réseau, l'affichage et les performances doivent encore être validés sur le matériel avant une version stable. Le nom et les visuels ont changé depuis le prototype « SHIELD OST Archives ». Le Title ID reste `000400000A211000` pour conserver la continuité des installations.

Le profil et les deux bibliothèques du projet collaboratif ne sont pas encore configurés : leurs liens exacts sont en attente. La recherche de cette version de travail interroge les archives audio publiques. Le schéma d'interface fourni par Shield-77 reste conservé ci-dessous comme référence de conception ; il ne représente pas une capture du build actuel.

![Schéma initial de SHIELD Online Archive](https://github.com/user-attachments/assets/81fb8c60-c7c5-4f30-abda-21b30b694f53)

## Utilisation

1. Touchez **Rechercher** (ou la barre supérieure) et saisissez un titre ou un artiste.
2. Touchez **Audio** pour interroger Internet Archive. **Image** est visible mais désactivé pour le moment.
3. Choisissez une archive avec la croix et A, ou touchez son nom. L/R change de page.
4. Dans la liste des pistes, **Y** lance ou arrête une préécoute, **A** propose un téléchargement et **X** propose d'ajouter la piste à la file. **B** revient ou arrête la préécoute. **SELECT** ouvre les options, le guide et la file d'attente ; dans la file, **X** demande confirmation avant le retrait d'une piste. **START** quitte quand aucun transfert important n'est en cours.

Les fichiers téléchargés sont enregistrés sans conversion dans `sdmc:/download audio/`, un dossier partagé avec SHIELD MP3 Downloader. Un fichier partiel reste dans le dossier privé de l'application pendant le transfert et n'est renommé qu'une fois terminé. La file d'attente est conservée sur la carte SD.

Les formats affichés sont MP3, WAV, FLAC, AAC et M4A. Le décodeur rejette les fichiers invalides et les fréquences ou configurations de canaux incompatibles. Les préécoutes sont limitées à 60 secondes et à quelques mégaoctets ; un M4A volumineux ne peut pas être préécouté par téléchargement partiel. Une pochette propre à la piste n'est utilisée que si un fichier image porte le même nom de base ; sinon l'image de l'archive est utilisée. Si aucune image exploitable n'est disponible, l'interface affiche un symbole générique.

La recherche et les métadonnées proviennent des API publiques d'Internet Archive. Une réponse indisponible ou trop grande est signalée comme telle ; la liste ne crée pas de durée, date ou image fictive. Les contenus trouvés restent sous la responsabilité de leurs déposants et de leurs licences propres. Aucun morceau tiers n'est inclus dans l'application.

## Compilation

Avec devkitARM, libctru, Citro2D, les portlibs 3DS incluant libcurl, `makerom`, `bannertool` et `3dsxtool`, lancer `make`. Les sorties sont `build/ShieldOnlineArchive.cia`, `build/ShieldOnlineArchive.3dsx` et l'ELF associé. Le script `tools/create_assets.ps1` régénère l'icône, la bannière et le bref son de bannière à partir de formes dessinées pour le projet.

Sur GitHub, le code exact de cette version candidate est regroupé dans `ShieldOnlineArchive-source-0.2.0.zip`. Il faut extraire cette archive avant de compiler ; les anciens fichiers du prototype encore affichés à la racine du dépôt ne correspondent pas au CIA candidat.

Le réseau utilise HTTPS avec validation des certificats et n'ouvre que les URL construites pour Internet Archive. Une seule tâche réseau tourne à la fois pour limiter la mémoire et garder la navigation réactive. Les métadonnées et images ont des plafonds de taille. Les décodeurs audio travaillent par blocs et alimentent NDSP sans charger un morceau entier en mémoire.

## Tests effectués

- Compilation CIA et 3DSX avec avertissements traités comme erreurs pour le code de l'application.
- Test Windows du modèle de recherche, des URL et des métadonnées, avec données synthétiques et réponses réelles d'Internet Archive.
- Décodage complet de fichiers synthétiques MP3, WAV, FLAC, AAC et M4A : fin de lecture atteinte pour chaque format.
- 35 fichiers tronqués ou aléatoires essayés dans le laboratoire Windows sans crash du processus.

Ces tests ne mesurent pas les performances de la Old 3DS et ne remplacent pas un essai sur console. Les journaux et les fichiers du laboratoire restent sous `build/windows-lab/`, hors dépôt.

## Crédits et licences

Idée du projet et préparation des archives audio : **Neo Foxy76**. Conception de l'application et direction technique : **Shield-77**. Assistance au développement : **Codex / OpenAI**. Icône, bannière et son de bannière créés pour ce projet.

Code de l'application : [GPL-3.0-or-later](LICENSE). Les composants tiers et leurs licences sont détaillés dans [THIRD_PARTY.md](THIRD_PARTY.md), notamment minimp3, dr_libs, FAAD2, minimp4, stb_image, cJSON et libcurl. **Code from FAAD2 is copyright (c) Nero AG, www.nero.com**.
