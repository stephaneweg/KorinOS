# Cartographie avant E33 : retirer les droits de /sys aux programmes de session

Relevé du 25/11/2026 (branche `security-fix`), par la lecture des sources, sans rien changer. Le journal « droits
utilisés par pid » (étape 2 de la méthode de [DECISIONS-SECURITE-25-11.md](DECISIONS-SECURITE-25-11.md)) n'existe pas
encore. **Feu vert de l'utilisateur le 26/11/2026 ; mis en œuvre le même jour (§7).**

## 1. Qui donne les droits aujourd'hui

- vfs pose `ATTR_SYS` sur tout fichier de `/sys/` (`src/svc/vfs/path.c:466`) ; AppKit `f_spawn_file` demande alors
  les droits (`appkit.c:1318`) ; le noyau ne les accorde que si le lanceur les a (`proc.c:553-554, 805-806`).
- Elegant lance `/etc/session` par `spawn_session` (uid du compte, avec les droits : `desktop.c:290`) ; voronoy et login
  avant la connexion en uid 0 avec les droits (`desktop.c:196-197`). init2 lance startup.txt de même (`init2.c:93`).
- **`INIT_OP_LAUNCH` (le plus grave)** : init2 lance n'importe quel `/sys/...` pour n'importe quel demandeur, avec son
  uid et les droits (`init2.c:502`) ; AppKit (`appkit.c:1462`) et libposix (`proc.c:772`) y passent dès qu'un process
  sans droits lance un fichier de `/sys`. **Toute app obtient donc un programme de `/sys` avec les droits.** Sans
  fermer cette voie, E33 ne sert à rien.
- Par héritage : un programme avec droits qui lance un `/sys/*` les transmet (panel → shutdown, menubar → quickmenu).

## 2. Ce qui est réservé aux droits

**Noyau du PC** : SVC_REGISTER (`svc.c:118`), SVC_MAP_CALLER (`svc.c:375`), SVC_SIGNAL (`svc.c:517`), CLOCK_SET
(`sys.c:37`), DMA_ALLOC (`mm.c:1017`), BUF_INFO physique (`mm.c:1002`), DEV_MAP, IRQ_BIND, IRQ_MSI, ACPI_TABLE,
IO_PORT, POWER (`hw.c`), KIT_REGISTER (`kits.c:160`), IMAGE_PRELOAD (`image.c:434`), image « système »
(`image.c:286`), IMAGE_FORGET de tout (`image.c:376`), MOD_LAUNCH, MOD_READ, BOOT_FILE, BOOT_READY (`boot.c`),
WS_REGISTER (`ws.c:45`), WS_SIGNAL hors DAMAGE (`ws.c:87`), CLIP_SET/GET (`ws.c:220`), PRIO_MAX (`thread.c`),
PROC_EXE et PROC_STATS d'un autre uid. Effets inverses : FORK et EXEC refusés aux process avec droits
(`proc.c:595, 656`) ; `kill(-pgid)` les épargne (`signal.c:226`). Le Pi : les mêmes règles en asm.

**Services** : hwmanager, usbd, pcid, netd IF_REGISTER, hidmanager ABONNER/PILOTE, sound DRV_*, blkserv, intelgpu,
i2chid, fsowner/smbfs/gdrivefs, oauthd, keyvault (avec droits, pas de vérification d'empreinte : `keyvault.c:932`),
init2 à l'extinction. **Aucun programme de session n'appelle ces opérations.**

**Sans lien avec les droits (rien à changer)** : vfs (par uid), busd (uid 0), Elegant (`ws_trusted`,
`ws_sysui_caller`, `ws_secure_caller` : chemin et empreinte), acpid, wifisvc, init2 REBOOT/LOGOUT/SETTING, sound
REC_OPEN (déjà au compte de la session).

## 3. Tableau

| Programme | Opération (fichier:ligne) | Pourquoi | Voie proposée |
|---|---|---|---|
| tous (UIKit, AppKit) | WS_SIGNAL DAMAGE ; KIT_LOAD par init2 | redessiner ; kits | déjà ouverts |
| voronoy | aucune op réservée ; lancé **en uid 0 avec droits** sous la connexion, gardé pour la session (`desktop.c:196, 279`) | fond d'écran | relancer sous le compte à l'ouverture (ou deux instances) ; sans droits |
| panel | SPAWN `/sys/panel`, `/sys/shutdown`, `/sys/clipboard` ; liste d'Elegant | se relancer, éteindre, widget | lancer les `/sys` hors services sans droits (étape 0) |
| menubar | SPAWN wifimenu, soundmenu, quickmenu ; ACPID_OP_PROFILE | menus rapides, énergie | comme panel ; acpid déjà au compte |
| quickmenu, soundmenu, wifimenu | SVC_REGISTER (verrou « un seul ouvert », fermer l'autre) ; SPAWN shutdown | singleton, bascule | noms de service par compte (note A), ou bascule par Elegant / bus |
| agenda | SVC_REGISTER « agenda » ; ouvert par Calendrier et Pocket QI | singleton, rappels | nom par compte |
| stickies | SVC_REGISTER « stickies » ; ouvert par Notes | singleton | nom par compte |
| clockd | SVC_REGISTER « clockd » (verrou seul) ; **ne règle pas l'heure** | un seul clockd | nom par compte, ou verrou dans `/tmp/<uid>` |
| notifyd | SVC_REGISTER « notify » ; PROC_EXE | appelé par toutes les apps, acpid, pkglib, printd | **service déguisé** (§5) |
| clipd | SVC_REGISTER « clipboard » ; **SVC_MAP_CALLER** | lit et écrit la mémoire de l'appelant | **service déguisé** (§5) |
| printd | SVC_REGISTER « print » | PrinterKit | **service déguisé** (§5) |
| shutdown | rien (REBOOT, LOGOUT déjà au compte) | — | retrait direct |
| dock (Pi) | rien de réservé | — | retrait direct |
| home, switcher, quick, widget clipboard (Pi) | SVC_REGISTER | singleton | nom par compte (le Pi n'a pas d'uid : un espace « sans droits ») |
| pkgd (session du Pi) | écrit /bin, /apps, /sys | service | dans services.txt, comme sur le PC |

**Note A, noms de service par compte** : un SVC_REGISTER sans droits est inscrit sous (nom, uid) ; SVC_OPEN cherche
le nom système, puis celui de l'uid de l'appelant ; un compte ne prend jamais un nom système ni ne voit ceux des
autres. Reste qu'une app du même compte peut prendre « agenda » la première : enjeu faible. Pas acceptable pour
« notify » et « clipboard », d'où le §5.

## 4. Ordre proposé (le moins dépendant d'abord)

0. Préalables, sans retirer de droits : journal des droits utilisés par pid ; vfs `ATTR_SYS` seulement pour
   `/sys/svc/` et une liste (elegant ; pkgd tant qu'il est dans `/sys`) ; AppKit et libposix lancent un `/sys` qui
   n'est pas un service directement, sans droits ; `INIT_OP_LAUNCH` ne donne les droits qu'à `/sys/svc/`.
1. dock (Pi), shutdown, voronoy (relancé sous le compte), panel, menubar.
2. Noms par compte (noyau du PC et du Pi), puis clockd, agenda, stickies, quickmenu, soundmenu, wifimenu ; sur le Pi
   home, switcher, quick, le widget clipboard.
3. notifyd, printd, clipd selon §5.

Un commit et un essai par programme (bureau, menus, son, batterie, Wi-Fi, notifications, fond d'écran), sous QEMU
puis sur le ZBook.

## 5. Services déguisés

- **notifyd** : vrai service, appelé par tous ; il lui faut une fenêtre sous le compte. Proposé : `/sys/svc/notifyd`,
  avec droits (pour le nom seulement), lancé par la session sous le compte.
- **clipd** (MAP_CALLER, sans fenêtre) : `/sys/svc/clipd` avec droits, sous le compte, filtre par uid gardé.
- **printd** : `/sys/svc/printd` (imprimante commune à la machine).
- **clockd, agenda, stickies** : seulement des verrous : apps avec un nom par compte.

## 6. Risques

- La voie init2 (§1) : si elle reste, tout programme sans droits les retrouve.
- voronoy en uid 0 avec droits toute la session, et lit `wallpaper.ini` (personnel) en uid 0.
- Sans droits : `kill(-pgid)` les atteint ; FORK et EXEC permis (pas de casse).
- Images créées par menubar pour quickmenu : images d'uid (Elegant vérifie l'empreinte : ça va ; keyvault refait la
  vérification, plus lente mais juste).
- Lancements détachés par init2 : père et stdio changent (`panel_respawn`) : à vérifier après l'étape 0.
- Le Pi : sans uid, `caller_admin` laisse tout passer ; seuls les tests du noyau comptent : le retrait y protège le
  matériel, pas les comptes.
- Autres programmes de `/sys` hors session (login, authcard, smbd, webd, mcp, cdmhost, linuxhost, setup) : l'étape 0
  leur retire aussi les droits ; seul mcp lit les droits (pour les afficher). À confirmer par le journal.

## 7. Mise en œuvre (26/11/2026, PC)

- **Le journal** (étape 0) : le noyau note « droits : <nom> (pid, uid) : <opération> », une fois par process et par
  opération, quand un process d'une personne (uid > 0) se sert de ses droits (`rights_used`, 26 opérations : celles
  du §2, plus SPAWN avec droits). Sous QEMU, après les changements : seuls clipd (SVC_REGISTER, SVC_MAP_CALLER) et
  notifyd (SVC_REGISTER), plus le service d'essai mapsvc.
- **vfs** (`path.c`, `sys_droits`) : `ATTR_SYS` (les droits au lancement) seulement pour `/sys/svc/` et elegant, pkgd,
  notifyd, clipd, printd. Tout le reste de `/sys` est lancé sans droits, aussi par init2 (`INIT_OP_LAUNCH` lance par
  AppKit, qui suit `ATTR_SYS`) : la voie du §1 est fermée pour ces programmes. Le Pi (sans uid) garde tout `/sys`.
- **Étape 1** : panel, menubar, shutdown, dock, voronoy, login, authcard, webd, smbd, mcp, setup sans droits ; voronoy,
  lancé en uid 0 sous l'écran de connexion, est arrêté à l'ouverture de la session et relancé sous le compte.
- **Étape 2** (note A, `svc.c`) : sans droits, un process d'une personne prend un nom à son compte ; SVC_OPEN cherche le
  nom du système puis celui du compte de l'appelant (jamais pour l'uid 0 ni un process avec droits) ; un nom pris
  une fois par le système depuis le démarrage ne peut plus être pris par un compte ; un nom de compte n'a pas d'avis
  (ni fins ni naissances de process). agenda, clockd, stickies, quickmenu, soundmenu, wifimenu en profitent.
- **Étape 3, écart** : notifyd, clipd et printd gardent leurs droits **à leur place** (`/sys/notifyd`...), par la liste
  de vfs, plutôt que d'être déplacés dans `/sys/svc/` : même effet, sans casser les `session` personnels qui les
  nomment.
- **Reste** : un process sans droits peut toujours demander à init2 de lancer un `/sys/svc/...` avec droits (les essais
  svctest et maptest s'en servent) ; à limiter à l'uid 0, les essais lancés par sudo. Le Pi.
