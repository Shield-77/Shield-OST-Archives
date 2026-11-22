# Composants tiers

Le projet SHIELD Online Archive est distribué sous GPL-3.0-or-later. Les fichiers tiers restent soumis à leurs licences respectives. Le code source des bibliothèques intégrées se trouve dans `include/` ou `vendor/` ; les bibliothèques de la chaîne de compilation sont fournies par devkitPro.

| Composant | Usage | Origine et licence |
| --- | --- | --- |
| minimp3 | Décodage MP3 par blocs | [lieff/minimp3](https://github.com/lieff/minimp3), CC0 |
| dr_wav, dr_flac | Décodage WAV et FLAC | [mackron/dr_libs](https://github.com/mackron/dr_libs), domaine public ou MIT-0, copie issue du commit `e381981afa52800b9a3864df548353ff67252fbc` |
| FAAD2 2.11.4 | Décodage AAC, version à virgule fixe pour 3DS | [knik0/faad2](https://github.com/knik0/faad2), GPL-2.0-or-later, copie issue du commit `a335040022e14ff9b6c54bbd459590d4c7edffe8`. Licence complète : `vendor/faad2/COPYING` |
| minimp4 | Lecture du conteneur M4A | [lieff/minimp4](https://github.com/lieff/minimp4), CC0. La copie utilisée comporte une limite de taille des allocations de métadonnées adaptée à Old 3DS. |
| stb_image 2.34 | Décodage des petites pochettes JPEG et PNG | [nothings/stb](https://github.com/nothings/stb), domaine public ou MIT |
| cJSON | Lecture des réponses JSON | [DaveGamble/cJSON](https://github.com/DaveGamble/cJSON), MIT, copie issue du commit `6d9f2443ab071f86e5d9b43025a40929ec41c46c`. Licence : `vendor/cjson/LICENSE` |
| libcurl et mbedTLS | HTTPS et validation des certificats | [curl](https://curl.se/) et [Mbed TLS](https://github.com/Mbed-TLS/mbedtls), via les portlibs 3DS ; leurs licences sont conservées par la distribution devkitPro |
| Autorités de certification | Validation TLS | Bundle PEM issu de l'environnement SHIELD Updater, intégré au programme ; il contient des certificats racines publics |

**Code from FAAD2 is copyright (c) Nero AG, www.nero.com**.

Les œuvres audio et leurs pochettes consultées depuis Internet Archive ne font pas partie du binaire ni de ce dépôt. Chaque élément conserve les droits et conditions indiqués par son déposant.
