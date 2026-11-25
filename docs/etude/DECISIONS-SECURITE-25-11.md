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

