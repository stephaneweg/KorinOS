# Mail

Le client de courrier d'Onyx (`user/Apps/mail`, `user/Libs/mail`) sur ARMPI, sur l'UIKit de l'OS, sans pont : aucune classe
autour d'un contrôle, ni `kapi_*`, ni uikit d'Onyx. Une vue est un préfixe et des fonctions, un contrôle une poignée `ui_*`.
Licence MIT d'Onyx, gardée dans `util.h`.

    mail                        SD0:/apps/mail.app/main
    mail SD0:/x.eml             un fichier, en lecture seule (opens = eml)
    mail --attach <liste>       un message neuf avec les fichiers de la liste (un chemin par ligne)
    mail --compose <brouillon>  un message neuf : lignes To:, Cc:, Subject:, une ligne vide, le texte

Fichiers : `SD0:/home/mail` (`accounts.ini`, `secrets`, `key`, `recipients.tsv`, `oauth.ini` facultatif, une boîte par compte
`<id>/`, `tmp/`), `SD0:/home/documents/Contacts.card` (le fichier de Cardfile). Un message dont l'envoi échoue est gardé dans
`SD0:/home/mail/unsent-<date>.eml`.

Compilation : `wsl sh tools/office/build.sh mail`. **Pas par `pibuild`** : OpenSSL n'est pas sous `/gcc`
(`docs/OPENSSL.md`). 5 Mo.

## Repris et écrit

| Fichier | Origine |
|---|---|
| `util.h`, `mime.h`, `pop3.h`, `smtp.h`, `html_dom.h`, `html_css.h`, `html_layout.h` | d'Onyx ; seuls les messages passent par `TR`, et le nom d'EHLO |
| `imap.h` | d'Onyx. Son `idle ()` existe, mais Onyx ne l'appelle pas : relève périodique seulement, comme lui |
| `html.h` | d'Onyx ; les citations et les liens d'un texte brut prennent leur couleur du thème |
| `oauth.h` | d'Onyx ; lecture de `oauth.ini` en POSIX. L'identifiant d'application reste celui d'Onyx |
| `conn.h` | **réécrit** : sockets POSIX non bloquants, attente par `poll`, TLS direct et STARTTLS par OpenSSL, vérification par `SSL_CTX_set_default_verify_paths` (`/etc/ca-bundle`), `SSL_set1_host` ou adresse IP, délai et annulation à chaque pas |
| `accounts.h`, `contacts.h`, `store.h`, `model.h` | d'Onyx ; fichiers en POSIX, secrets en AES-256-GCM par EVP, aléa par `getentropy`. Le format de `secrets` est celui d'Onyx |
| `sync.h` | d'Onyx ; fil en `pthread`, file de résultats relue par la boucle au lieu de `kapi_post`, reprise d'une session IMAP restée ouverte par `NOOP` |
| `html_ui.h` | **écrit**, remplace `html_ft.h` : hôte sur `ui_text`, `ui_text_width`, `ui_font_get`, dessin dans la vue |
| `image.c` | **écrit** : JPEG, PNG, GIF, BMP par `stb_image` (`../media/stb_image.h`) |
| `ui.h`, `app.h` | d'Onyx pour les icônes, les dates et les avatars ; jetons `ui_jetons`, tracé par `ui_vp_*` |
| `read.h`, `compose.h`, `wizard.h`, `contactsui.h`, `main.cpp` | la logique d'Onyx, réécrite sur `ui_view`, `ui_list`, `ui_textbox`... |

Rien n'a été ajouté à UIKit.

## Différences avec Onyx

* **Trois écrans « pocket »** : pas portés. Une fenêtre sous 640 px montre un seul volet : la liste, ou la conversation, avec
  ← dans la barre ; les dossiers derrière ≡. Sous 620 px, la recherche prend la place des outils dans la liste.
* **WebKit en applet** (`webview.h`) : **pas porté**. Tout le HTML passe par le moteur de Mail (`html.h`).
* **Assistant et réglages** : des pages de la fenêtre, pas des dialogues : la boucle continue et les réponses du fil arrivent.
* **Thème sombre** : un message HTML garde sa page blanche, sur une feuille, sous ses propres couleurs ; le texte brut suit le
  thème. Couleurs d'Onyx en dur remplacées par des jetons (étoile, alertes, icônes) ; restent les bandes de compte et les
  disques des avatars, dont l'encre suit le fond.
* **Polices** : DejaVu Sans, Serif et Mono, celles de l'OS. Onyx prenait Liberation, Gelasio, Selawik.
* **Certificat refusé** : Onyx ne proposait rien à l'écran. Ici « Continuer quand même » dans l'assistant, et un clic sur
  l'erreur du pied de colonne ; case « Vérifier les certificats » dans les réglages.
* **Retrait d'un compte** : sa copie sur la carte est vraiment effacée, comme le disait le texte d'Onyx sans le faire. Sinon
  le compte suivant héritait de ses dossiers.
* **Pièces jointes** : l'application qui ouvre une extension vient des `opens =` des `app.txt`. Dossiers : `SD0:/home/documents`
  (`ui_file_save`, `ui_folder_choose`).
* **Fuseau** : `localtime` (libposix le donne d'après l'horloge du noyau) ; `system.ini` n'est plus lu.
* **Notification** : le service `notify` directement (`SYS_SVC_OPEN`, `SYS_SVC_CALL`, comme `src/inc/notify.inc`) : aucun kit ne
  l'expose. Le titre de la fenêtre compte aussi les non lus.

## Ce qui manque

* **Comptes réels** : Gmail, Outlook et OAuth, iCloud, Yahoo, GMX n'ont jamais été essayés. `oauth.h` n'a pas tourné du tout.
* **IDLE** : non utilisé, comme dans Onyx.
* **Reprendre un brouillon** : un brouillon se lit, il ne se rouvre pas pour être modifié (Onyx non plus).
* **Texte à écrire** : `ui_textarea` ne coupe pas aux mots : les longues lignes défilent à l'horizontale.
* **Une connexion qui ne répond pas** : `connect ()` bloque toujours sous ARMPI (`docs/POSIX.md`) : Annuler et la fermeture
  n'attendent pas le fil, mais un envoi vers un serveur muet met une vingtaine de secondes à échouer.
* Caractères hors de DejaVu : carré. Appui long au tactile : pas d'équivalent du clic droit.
