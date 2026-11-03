# Courier

Le client HTTP d'Onyx (`user/Apps/courier`), à la Postman, sur ARMPI : C++ sur l'UIKit de l'OS, sans pont, ni `kapi_*`, ni
uikit d'Onyx. Il remplace sur la carte la version C# (`tools/dotnet/courier`), trop lente à démarrer.

    courier                     SD0:/apps/courier.app/main
    courier <fichier>           importe une collection, un environnement Postman, une commande cURL

Fichiers : `SD0:/home/courier` (`collections/`, `environments/`, `globals.json`, `history.json`, `state.json`, `cookies.json`),
aux formats de Postman, les mêmes que la version C# et qu'Onyx (`insecure` en plus dans le membre `onyx`).

Compilation : `wsl sh tools/office/build.sh courier`. **Pas par `pibuild`** : OpenSSL n'est pas sous `/gcc` (`docs/OPENSSL.md`).
4,8 Mo.

## Repris et écrit

| Fichier | Origine |
|---|---|
| `vars.h`, `tools.h` | d'Onyx ; les messages passent par `TR` |
| `model.h`, `util.h` | d'Onyx ; fichiers en POSIX, membre `insecure` ; `json.hpp` est celui de Koton (`../koton/ui`) |
| `net.h` | la logique d'Onyx ; **réécrits** : connexion (sockets POSIX non bloquantes, `poll`, OpenSSL, comme `mail/conn.h`), fil en `pthread`, fin des envois relevée par la boucle (`jobs_poll`) au lieu de `kapi_post`, gzip et deflate par `stbi_zlib_*` |
| `image.c` | comme Mail : JPEG, PNG, GIF, BMP par `stb_image` (`../media/stb_image.h`) |
| `app.h`, `rail.h`, `main.cpp` | d'Onyx ; `rail.h` et les barres dessinés dans des `ui_view`, commandes reprises telles quelles |
| `ui.h` | comme celui de Mail : jetons `ui_jetons`, tracé par `ui_vp_*` |
| `widgets.h` | **écrit** : barres d'onglets à compteurs, onglets de documents, table clé / valeur, panneau d'autorisation |
| `views.h`, `dialogs.h` | la logique d'Onyx, réécrite sur `ui_*` ; un widget par contrôle, fait une fois et relié à l'onglet montré |

Rien n'a été ajouté à UIKit. La table clé / valeur est celle de la version C# : une réserve de vrais champs pour les lignes qui
tiennent, reliés aux données quand on défile.

## Repris de la version C#

* La vérification du certificat : `/etc/ca-bundle`, un interrupteur dans Settings par requête (membre `onyx.insecure`). Onyx ne
  vérifiait rien.
* `User-Agent: Courier/1.0 (ARMPI)`, les fichiers dans `/home/courier`, l'import après l'affichage de la fenêtre.
* Le corps montré coupé à 900 000 octets (un `ui_textarea` tient 1 Mio), Save garde tout.
* Annuler rend la main tout de suite : `connect ()` bloque sous ARMPI, le fil finit seul et la boucle le reprend.
* Les boutons de la fenêtre laissés libres (`FRAME_CAP_W`), le temps d'envoi affiché.

## Différences avec Onyx

* Pas de couleurs de syntaxe : `ui_textarea` ne connaît que l'assembleur. `{{variables}}` non colorées dans les champs, pas
  d'infobulle de leur valeur ni de complétion : `ui_textbox` ne dessine que du texte.
* Pas d'indicateur pendant la frappe du Bulk Edit, les dialogues « Delete » ont OK / Annuler (`ui_message`).
* Coller une commande cURL dans l'URL se reconnaît à un texte qui grandit d'un coup : UIKit n'a pas de notification de collage.
* Glisser-déposer : fichier ou texte, importés à la fin du tampon (64 Ko). Pas essayé.
* Les dates de l'historique en `jj/mm/aaaa`.

## Essais

Sous QEMU seulement, contre `tools/dotnet/courier/test/server.py` et sa collection `seed/` (jamais internet sans cible locale ;
`ntp=10.0.2.2` dans `/etc/options.txt`). Givre et Onyx Noir, 1024 x 768 et une fenêtre de 800 x 540.
