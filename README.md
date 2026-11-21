# SHIELD OST Archives

SHIELD OST Archives est une application musicale pour 3DS, conçue en priorité pour Old 3DS. Elle présentera les deux bibliothèques **3DS** et **Wii U** d'un profil Internet Archive précis, permettra de chercher dans les deux à la fois, d'écouter et de télécharger les MP3 que notre collaborateur y mettra à disposition.

**État actuel : développement.** L'adresse du profil et les identifiants des bibliothèques ne nous ont pas encore été communiqués. Aucun lien n'est inventé ni intégré. Le CIA de travail affiche « Profil en attente de configuration » ; il n'annonce pas de recherche ou de téléchargement en ligne fonctionnel. Aucune release officielle n'est publiée à ce stade.

## Interface préparée

- Écran supérieur : présentation de l'archive sélectionnée, plateforme, collection, taille et progression lorsqu'elles sont connues.
- Écran inférieur : barre de recherche tactile globale, onglets 3DS/Wii U, résultats, commandes.
- Thèmes **Bleu cyber** (par défaut) et **Noir et blanc**, dans les options ouvertes avec SELECT.
- Rendu Citro2D avec panneaux arrondis et texte tronqué pour rester dans les écrans.

Les commandes prévues sont : croix haut/bas pour la liste, A pour lire, X pour pause/reprise, B pour arrêter, SELECT pour les options, Y pour actualiser et START pour quitter. Les onglets et la liste acceptent aussi le tactile. Les commandes de lecture ne peuvent encore être éprouvées avec une archive distante : aucun lien n'est configuré.

Le futur dossier de téléchargement proposé est `sdmc:/download audio`, comme dans SHIELD MP3 Downloader. L'application **n'y écrit rien pour le moment**. Quand le téléchargement sera opérationnel, les MP3 enregistrés devront être des fichiers standards, lisibles ailleurs.

## Développement et labo

Le moteur audio `source/audio.c` adapte le chemin de décodage progressif minimp3 et les tampons NDSP de SHIELD DJ Lite, sans effets DJ. Le catalogue et la recherche sont séparés dans `source/catalog.c` ; le rendu et les commandes sont dans `source/main.c`.

Le mode de labo `OST_LOCAL_LAB` ajoute deux entrées fictives et permet des essais sur des MP3 locaux. Il est **désactivé dans le CIA normal** et n'est pas le produit visé. Le test Windows du moteur décode un MP3 de test, vérifie la pause, la reprise, la fin de lecture et la gestion d'un fichier absent. Le test Windows du catalogue vérifie la recherche sur deux bibliothèques et l'insensibilité aux accents. Les tests et caches sont placés sur le HDD de développement.

Pour compiler, installer devkitARM, libctru/Citro2D, `makerom`, `bannertool` et `3dsxtool`, puis lancer `make`. Le script `tools/create_assets.ps1` régénère l'icône, la bannière et son bref son sans média tiers. Les sorties sont `build/ShieldOSTArchives.cia`, `build/ShieldOSTArchives.3dsx` et l'ELF correspondant. Le Title ID réservé est `000400000A211000` (`UniqueId 0xA2110`). Les exécutables des outils de compilation ne font pas partie du dépôt.

## Suite nécessaire

Lorsque les liens arriveront, il faudra vérifier la structure exacte du profil et des deux bibliothèques, leurs métadonnées et les droits de téléchargement, puis intégrer et tester la récupération du catalogue et des MP3 sur une vraie Old 3DS. L'entrée SHIELD Updater suivra une release validée, afin que son statut de version reste exact.

## Crédits et licence

Idée du projet et préparation de la mise en ligne des audios : **Neo Foxy76**. Conception de l'application et direction technique : **Shield-77**. Assistance au développement : **Codex / OpenAI**. Moteur adapté de **SHIELD DJ Lite**. Décodeur MP3 : **minimp3** (CC0). Bibliothèques de la plateforme : **devkitPro/libctru/Citro2D**. L'icône, la bannière et le son de bannière sont générés pour ce projet. Aucune musique commerciale n'est incluse.

Le code de l'application est sous **GPL-3.0-or-later** ; voir [LICENSE](LICENSE). Les composants tiers conservent leurs licences respectives.
