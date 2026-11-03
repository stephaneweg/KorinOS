# Telegram

Le messager d'Onyx (`user/Apps/telegram`) sur ARMPI, à la façon de Windows Live Messenger, sur l'API ouverte de Telegram
(MTProto 2.0, écrit ici, sans TDLib). Sur l'UIKit de l'OS et sans pont : aucune classe autour d'un contrôle, ni `kapi_*`, ni
uikit d'Onyx. Une vue, un préfixe et des fonctions.

    telegram            SD0:/apps/telegram.app/main : le compte de session.dat, sinon la connexion
    telegram --demo     des conversations inventées, sans réseau

Fichiers : `SD0:/home/telegram` : `session.dat` (les clés d'autorisation : des secrets), `config.ini`, `seed.bin`, `log.txt`,
`cache/` (photos). Compilation : `wsl sh tools/office/build.sh telegram`, pas `pibuild` : OpenSSL n'est pas sous `/gcc`
(`docs/OPENSSL.md`). `main` : 4,1 Mo, dont l'essentiel est `libcrypto`.

`config.ini` (`[telegram]`) : `api_id`, `api_hash` (à la place de la clé de l'app, intégrée comme chez Onyx), `test`,
`pictures`, `windows`. Pour les essais, `test_host`, `test_port`, `test_rsa_n` : **tant que `test_host` est là, plus aucune
adresse ni clé intégrée ne sert** : tous les centres de données sont cet hôte, la liste `help.getConfig` est ignorée,
`session.dat` n'est pas relu si elle vient d'un autre jeu de serveurs, et `TcpTransport::open` refuse tout autre hôte.

## Repris et écrit

| Fichier | Origine |
|---|---|
| `tl.h`, `schema.inc` | d'Onyx, tels quels (couche 229) |
| `mtproto.h` | d'Onyx ; une ligne pour le grand nombre, plus de `phone_code_hash` au journal |
| `client.h` | d'Onyx ; `test_host`, `access` au lieu de relire un fichier entier pour savoir s'il existe, plus de hash au journal |
| `tgcrypto.h` | réécrit sur **OpenSSL** (EVP, BIGNUM, `PKCS5_PBKDF2_HMAC`, `RAND_bytes`) ; IGE, RSA_PAD, Pollard, SRP sont ceux d'Onyx |
| `tgplat.h` | réécrit en POSIX ; gzip par ArchiveKit (`ar_inflate_new`), aléa par `getentropy`, transport dans un thread, carte dans un thread |
| `emoticons.h` | les tables d'Onyx ; le dessin refait sur `ui_vp_disc`, `ui_vp_line`, `ui_vp_poly`, mêmes images |
| `rich.h` | d'Onyx, largeurs par `ui_text_prefix` |
| `look.h` | écrit : tout vient des jetons (ci-dessous) ; le cadre de verre, le bonhomme, les avatars d'après Onyx |
| `buddylist.h`, `pane.h`, `signin.h`, `main.cpp` | réécrits sur `ui_view` : la logique d'Onyx, plus aucun de ses widgets |
| `demo.h` | d'Onyx, sa photo est `data/demo-lake.jpg` (image de synthèse faite pour l'app) |
| `image.c` | stb_image, comme Mail ; `../media/jpegsave.h` pour envoyer une photo |

`tools/nettest/tgsrv.py` : le serveur local des essais.

## Différences avec Onyx

* **Thème** : le ciel, le verre, la sélection sont faits des jetons (`ui_jetons`, jamais `theme.txt`), mêlés jusqu'à être
  lisibles (contrastes calculés dans `look_make`). Givre, Onyx Noir et Peach essayés. Seules les sept teintes des avatars
  sans photo et les émoticônes gardent leurs couleurs : ce sont des images.
* **Une conversation dans sa fenêtre** est une fenêtre de plus du même programme (`ui_window_ex`, `ui_window_use`), comme les
  privés d'IRC. Par défaut, comme Onyx (`windows = 1`) ; menu Affichage pour les avoir à côté de la liste ; à côté de la
  liste, un double-clic ou le menu d'une ligne ouvre quand même une fenêtre.
* **Mode pocket d'Onyx** remplacé par le repli : sous 640 px, à côté de la liste, un seul volet à la fois et un bouton ←.
  Fenêtre basse (< 520 px) : ligne de saisie plus mince ; < 420 px : en-tête plus mince.
* **Carte** : Onyx lisait ses images pendant le dessin. Ici un thread lit et écrit, et la fenêtre ne l'attend jamais : le
  préchargement de Jet tient `vfs` plus de 100 s après le démarrage, une lecture pendant une peinture figeait tout.
* **Une photo cliquée** s'ouvre dans une fenêtre du programme : le Viewer d'ARMPI ne lit pas le JPEG.
* **Compteur non lu** dans le titre (« (2) Telegram ») : pas d'icône de zone d'état ; la notification est un toast de
  `notifyd`, sans action au clic.
* **Le nom du système** dans `initConnection` : `ARMPI`.

Rien n'a été ajouté à UIKit.

## Émoticônes

DejaVu n'a pas les émojis en couleur et Telegram en est plein. Comme Onyx, 32 images sont dessinées par formes, à toute taille,
et une soixantaine de points de code y sont rabattus (messages, aperçus de la liste, saisie, sélecteur). **Glyphes absents de
DejaVu, à connaître** :

* les autres émojis (drapeaux, objets, animaux...) : une pastille grise neutre, pas le caractère ;
* hors de `rich.h` (noms dans les en-têtes, barres de titre, champ de recherche, listes de contacts, notifications) un émoji
  ou un caractère CJK, arabe... s'affiche comme le glyphe manquant de DejaVu, ou pas du tout (les initiales l'ignorent).

## Ce qui manque

* **Rien n'a été essayé contre Telegram** : ni compte, ni numéro, ni serveur réel. Tout est vérifié contre `tgsrv.py`, écrit
  d'après `mtproto.h` et la documentation publique : une erreur de compréhension commune aux deux passerait. Les clés RSA de
  production et les adresses des centres de données n'ont jamais servi.
* Photo collée (`Ctrl+V` d'une image) : le presse-papiers d'ARMPI n'a que du texte. Coller du texte marche.
* Une seule instance n'est pas garantie : Onyx passait par `kapi_ipc_lookup`, ici rien ; deux lancements partageraient
  `session.dat`.
* Vidéos, vocaux, fichiers, sondages, stickers animés : une étiquette, comme Onyx. Les stickers : leur émoji.
* Sélection de texte dans la saisie : aucune, comme Onyx ; dans les messages : aucune.
* `session.dat` est en clair sur la carte, comme chez Onyx.
