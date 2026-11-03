# IRC

Le client IRC d'Onyx (`user/Apps/irc`) sur ARMPI, sur l'UIKit de l'OS, sans pont : aucune classe autour d'un contrôle,
ni `kapi_*`, ni uikit d'Onyx. Son interface a été refaite : proposition d'un designer, appliquée dans les limites de UIKit.

    irc                 SD0:/apps/irc.app/main

Fichiers : `SD0:/home/irc` (`servers.txt`, `nick.txt`, `config.ini` : user, realname, channel, password, server, port).
Compilation : `pibuild app /src/apps/irc irc` (GCC natif, pas d'OpenSSL) ou `wsl sh tools/office/build.sh irc`.

## Repris et écrit

| Fichier | Origine |
|---|---|
| `irc.h` | le moteur d'Onyx (conversations, analyse des lignes, numériques, LIST, commandes) ; `kapi_tcp_*` remplacés par les sockets POSIX, connexion dans un thread (le DNS et `connect` bloquent) |
| `look.h` | écrit : jetons, couleurs de pseudos, texte coupé en pixels |
| `chat.h` | écrit : les messages, une `ui_view` |
| `pm.h` | écrit : la fenêtre privée |
| `main.cpp` | écrit : fenêtre, panneaux, clavier ; `submit`, `show_rooms`, l'historique des serveurs viennent d'Onyx |

## Différences avec Onyx

* **Texte en UTF-8 du fil à l'écran** (Onyx : Latin-1). Un octet qui n'est pas de l'UTF-8 est lu comme du Latin-1. Coupure des
  lignes mesurée en pixels (Onyx comptait des caractères à chasse fixe).
* **Une fenêtre privée est une fenêtre du même programme** (`ui_window_ex`, `ui_window_use`). Onyx relançait le programme
  et parlait par boîtes aux lettres : ici plus de process ni d'IPC, la fenêtre lit le `Buf` directement.
* **Un message privé n'ouvre plus de fenêtre** : badge sur sa ligne et compte dans le titre, un clic l'ouvre. Sinon le clavier
  part ailleurs en pleine frappe.
* **Pas de connexion automatique au premier lancement** : le panneau Connexion. Onyx se connectait à `irc.libera.chat` sous le
  pseudo `onyx`. Les lancements suivants se reconnectent, et une reconnexion rejoint les salons où l'on était.
* **Interface** : plus de barre serveur ni de barre d'état. La carte en haut à gauche, un bandeau quand il y a un état à dire,
  et un seul panneau à la fois au centre (Conversation, Salons, Connexion, Aller à). Pas d'avatars dans les salons : colonnes
  heure, pseudo, texte, deux fois plus de messages à l'écran en 800x600. Arrivées et départs repliés sur une ligne, dépliée
  par un clic, ou masqués (menu Affichage). Couleurs de pseudos calculées à 5:1 sur la surface du thème.
* **Ajouts** : `Ctrl+K` Aller à, `Alt+haut/bas`, `Alt+gauche/droite`, `Alt+1..9`, `Alt+Maj+bas` (non lue suivante), `Page`,
  complétion par Tab (pseudos, `/commandes`, `#salons`), historique des lignes envoyées par haut/bas, marque « Nouveaux »,
  pastille « ↓ n nouveaux », menu contextuel d'un message (copier, répondre, message privé).
* **Mode pocket d'Onyx** (`SidePanel` en rail ou tiroir) : remplacé par le repli selon la largeur : sous 900 px les
  utilisateurs se cachent, sous 640 px les conversations.

Rien n'a été ajouté à UIKit.

## Ce qui manque

* **TLS** : comme Onyx, IRC en clair sur le port 6667. OpenSSL l'obligerait à quitter le selfhost (`docs/OPENSSL.md` : GCC
  sous ARMPI ne lie pas OpenSSL) et ajouterait 3,5 Mo. Le mot de passe NickServ passe en clair.
* Caractères hors de DejaVu (CJK, émojis) : carré.
* Appui long au tactile : pas d'équivalent du clic droit.
