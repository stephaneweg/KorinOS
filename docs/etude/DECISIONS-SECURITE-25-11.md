# Décisions du 25/11/2026 sur la sécurité (suite)

## Décision : apps de l'utilisateur dans HOME:/apps (avant E72)

- qbasic et qbstudio installent les apps créées dans `HOME:/apps/<nom>.app` (plus dans `/apps`), sans élévation.
- `/apps` : écriture par l'uid 0 seulement, dans tous les modes (ajouter `/apps` aux marques de vfs une fois qbasic et qbstudio adaptés).
- Déplacer un dossier vers `/apps` (Fichiers, `mv`) : par la carte d'élévation.
- À adapter :
  - la liste des apps (panel, grille, dock, applist de Réglages, Fichiers) lit `/apps` et `HOME:/apps` ;
  - vfs : exécution permise de `HOME:/apps/*.app/main` pour son compte (vérifier les règles d'exécution : une copie dans /home ne se lançait pas dans les essais) ;
  - notifyd (`app_from_sender`) et les autres identités par chemin : une app de HOME:/apps ne passe pas pour l'app système de même nom ;
  - en cas de même nom : l'app système d'abord.
## Décision : E33 accepté (revient sur le « non » du 25/11)

Les programmes de la session graphique (panel, menubar, voronoy, agenda, stickies, clockd, quickmenu, soundmenu, wifimenu, et tout programme de /sys lancé dans la session) tournent comme des apps : uid de l'utilisateur, SANS les droits de /sys. Les droits restent aux services (/sys/svc), à Elegant, init, vfs, authd et aux pilotes.

À faire :
1. Relever, programme par programme, ce que chacun fait aujourd'hui grâce aux droits (SYS_PROC_RIGHTS, caller_rights dans les services, appels noyau réservés aux droits).
2. Pour chaque usage : passer par un service qui accepte le compte de la session (caller_session_or_admin), ou par la liste d'Elegant (chemin + empreinte), ou le supprimer.
3. Ne plus donner les droits au lancement de ces programmes (init2, Elegant, spawn de session).
4. Conséquence : seuls les services et le démarrage créent des images préchargées « système » (point 8) ; préchargement limité à /apps, choisi par élévation.
5. Essais : bureau complet, menus, son, batterie, Wi-Fi, notifications, fond d'écran, sous QEMU puis sur le ZBook.

## Cartographie avant E33 : retirer les droits de /sys aux programmes de session

But : les programmes de session (panel, menubar, voronoy, agenda, stickies, clockd, quickmenu, soundmenu, wifimenu, notifyd, clipd ; dock, home, switcher sur le Pi) tournent sans les droits de /sys, sans rien casser et sans ouvrir de trou.

1. Relever tout ce qui dépend des droits :
   - noyau : chaque test de p->rights (SVC_SIGNAL, PROC_EXE, PROC_STATS, BUF_INFO, IMAGE_CREATE/image « système », DEV_MAP, IO_PORT, presse-papiers…) ;
   - services : chaque caller_rights / caller_admin / SYS_PROC_RIGHTS ;
   - vfs et préchargement.
2. Pour chaque programme de session : quelles opérations il appelle (recherche dans ses sources, puis journal « droits utilisés par pid » sous QEMU et sur le ZBook pendant une session normale).
3. Tableau : programme | opération | pourquoi | voie proposée, parmi :
   - service + compte de la session (caller_session_or_admin) ;
   - liste par chemin + empreinte (comme ws_trusted d'Elegant) ;
   - op en lecture seule sans données sensibles ;
   - élévation ;
   - suppression.
4. Règle : jamais de voie qui rende l'anneau 0 ou contourne vfs (DMA, ports, blk.*, fs.*, mémoire physique, images partagées) ; ces opérations restent aux services.
5. Retirer les droits programme par programme (un commit et un essai chacun), en commençant par ceux qui en ont le moins besoin.
6. Montrer le tableau à l'utilisateur avant de changer le code.
## Décision : Réglages sans élévation pour réseau, Wi-Fi, clavier, énergie

- Pour l'utilisateur de la session (caller_session_or_admin, comme E32), sans carte d'élévation.
- Réglages n'écrit pas /etc lui-même : il passe par le service (uid 0) qui écrit le fichier :
  - Wi-Fi et réseau : wifisvc écrit /etc/wifi.txt (lisible par l'uid 0 seulement). Corriger P12 (SSID filtré avant d'écrire).
  - Énergie : acpid (PROFILE) écrit /etc/power.ini.
  - Clavier : hidmanager KEYMAP ; la disposition du compte dans son dossier personnel ; celle de l'écran de connexion dans system.ini, écrite par un service (pas par Réglages directement).
- Gardent l'élévation : access=, autologin, shell, comptes, remote.txt, paquets.
- Puis : sortir de system.ini ce qui est personnel, et protéger system.ini en écriture (uid 0) avant enforce (E72).


## Décisions (suite, 25/11/2026)

- Date, heure, fuseau horaire : par élévation (NTP de netd continue sans rien demander).
- Affichage (résolution, échelle) : réglage de la machine, commun à tous les comptes, par élévation.
- Écran de connexion : en cliquant sur un compte, sa disposition de clavier est chargée (lue dans son dossier, hidmanager KEYMAP). La disposition par défaut (avant le choix d'un compte) se change par élévation.
- Même nom dans /apps et HOME:/apps : les deux sont affichées, celle de l'utilisateur précédée de « (u) » ; identités distinctes (notifyd, coffre, liste d'Elegant).
- sudo : un mot de passe est toujours exigé, comme pour tout compte qui peut administrer (plus de sudo sans mot de passe, console comprise).
- Un compte qui peut gérer le système (elevate.ini, groupe admin) doit avoir un mot de passe : imposé par osinstall, Réglages (création ou passage admin) et authd.
- oauthd : un service par compte (oauth.<uid>), lancé à l'ouverture de la session de l'utilisateur, avec ses propres jetons.
- Moniteur du noyau : seulement dans les builds de test (ktest…), exclu des builds « release », sans rien à la place.
- Disques : / et /home en ext4 à l'installation (osinstall, installateur) ; FAT/exFAT réservés aux disques amovibles ; pas de chiffrement. Corriger CLAUDE.md, AGENTS.md, FICHIERS.md (« / est en FAT32 »).
- Préchargement : seulement des apps de /apps, choisi par élévation ; seul le démarrage (init/vfs) crée des images partagées ; le noyau refuse SYS_IMAGE_CREATE d'une image partagée à tout autre process, et hors de /apps (avec E33 accepté).
- K3 (clé de signature) : pas pour l'instant ; on continue d'utiliser la clé de l'utilisateur (ONYX_PKG_KEY).
- E34 accepté : tuer ou signaler un process qui n'est pas à soi (autre compte, ou système : uid 0, services) demande l'élévation (sudo kill, ou la carte depuis le Gestionnaire des tâches) ; ses propres process restent libres. Avant d'implémenter : relire la section E34 du plan (constats D2, N27) et ce que font aujourd'hui le noyau (SYS_KILL, signaux de groupe) et sudo.