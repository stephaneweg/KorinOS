# ARMPI — feuille de route

Le plan des chantiers à venir, dans l'ordre, tenu à jour au fil de l'avancée
(`[x]` fait, `[~]` en cours, `[ ]` à faire). Le détail de l'architecture est dans
[MICRO-NOYAU.md](MICRO-NOYAU.md).

## Fait

- [x] Micro-noyau, étapes 1 et 2 : services nommés (6 µs par aller-retour sur le Pi 4),
      droits de `SD0:/sys`, accès au matériel (`dev_map`, `dma_alloc`, `fw_call`).
- [x] Stockage en services : périphériques blocs, **FatFS** (FAT32 + exFAT), RamFS, `vfs`,
      pilote de la carte `sd` ; `init` + `initramfs` ; `SD0:` et `SD1:` en lecture et écriture,
      validés sur le PocketTerm.
- [x] Élagage du noyau : le mode noyau a disparu (retour possible : branche `mode-noyau`).
- [x] Démarrage par les services (`init2`, `hwmanager`), entrées par **HIDManager** (tactile GT911
      en service), **USB en service** (`usbd`, claviers, souris, clé, branchement à chaud), noyau
      sans pile USB (91 Ko), **notifyd**, **barre de menus globale**, **navigateur de fichiers**,
      éditeur avec menus — validés sur le PocketTerm (28-29/10/2026).
- [x] Sources réorganisées (29/10/2026) : un dossier par programme (`src/apps`, `src/bin`, `src/sys`,
      `src/svc`, `src/kits`, `src/inc`, `src/kernel`) ; **`build.bat`** construit la carte (`sdcard\`)
      d'un double-clic, sans PowerShell ni Python ; selfhost : 21 identiques.
- [x] **Fichiers hors du noyau** (29/10/2026, section 4, sur QEMU) : chemins, descripteurs, tubes,
      lancement et kits chez AppKit, vfs et init2 ; le noyau ne lit plus aucun fichier (89 Ko).

## 1. Démarrage par les services — init2 et hwmanager

Le noyau ne lance plus rien lui-même ; tout part d'`init`.

- [x] **init2** (`SD0:/sys/svc/init2`, lancé par `init`) : lance `hwmanager`, Elegant et
      `services.txt` ; relance un service qui s'arrête sur une erreur (code de sortie non nul,
      3 fois au plus). Le noyau ne lance plus aucun programme.
- [x] **Étape 3** : quand Elegant répond (service `ws`), `init2` lance les applications de
      `startup.txt` (sans ce fichier : Bienvenue).
- [x] **hwmanager** : service de **détection** du matériel plug and play ; une table
      `SD0:/etc/drivers.txt` associe une détection à un pilote (« `i2c1:5d = SD0:/sys/svc/gt911` ») ;
      bus I2C 1 sondé (toutes les 2 s pendant une minute), puis laissé au pilote.
- [x] hwmanager : les annonces d'`usbd` (`usb:VVVV:PPPP`, `usb:cc/ss/pp`...), branchements et retraits.
- [x] hwmanager : les périphériques du SoC selon la carte (`soc:genet`, `soc:wifi` ; audio HDMI plus tard).
- Un matériel non détectable (sur des broches GPIO...) : son pilote se lance depuis
  `services.txt` (`init2` le surveille) ; `hwmanager` ne sert qu'au plug and play.

## 2. Entrées agnostiques — HIDManager

Un seul point d'arrivée pour toutes les entrées, quel que soit le matériel.

```
 pilotes (services)                 HIDManager                       Elegant
 usbclavier ─┐   événements HID     · reçoit claviers, souris,       · lit la file d'événements
 usbsouris  ─┤── normalisés  ─────▶   tactiles, manettes             · fenêtre active, curseur
 gt911      ─┘   (anneau partagé)   · applique la disposition       (anneau partagé)
                                       (SD0:/res/keyb), répétition
                                     · fusionne les claviers (comme Onyx)
```

- [x] **Format d'événement commun** (32 octets, celui d'`input.asm`) : touche (code
      HID + appui/relâche + modificateurs), pointeur relatif (souris), pointeur absolu
      (tactile), boutons, molette.
- [x] **HIDManager** (`SD0:/sys/svc/hidmanager`, service « hid », lancé par `init2`) : la
      disposition du clavier (`keymap.asm` est sorti du noyau ; `keyb=` d'`options.txt`,
      l'applet Clavier lui envoie la nouvelle), la répétition des touches (sortie d'Elegant) ;
      vers Elegant, un **anneau partagé** lu sans appel système (Elegant le vide à chaque
      tour, 8 ms au plus).
- [x] Côté pilotes : un anneau partagé par pilote en service (gt911, usbkeyboard, usbmouse :
      op 3 de HIDManager) ; la file d'entrées du noyau (appel 79) est retirée.
- [x] **Coût** : une souris ou un clavier, c'est au plus ~1 000 événements par seconde ; même
      avec un message par événement (≈ 6 µs × 2 passages), moins de 1 % d'un cœur ; avec les
      anneaux, quasi nul. Latence ajoutée : quelques µs, invisible.
- [x] **Pilote tactile GT911** en service (`SD0:/sys/svc/gt911`, lancé par hwmanager) : I2C
      logiciel sur GPIO (`dev_map`, `src/svc/inc/i2c.inc`), anneau vers HIDManager (op 3) ; le
      premier pilote d'entrée hors du noyau, validé sur le PocketTerm (28/10/2026).
- [x] Elegant lit HIDManager au lieu de l'entrée brute du noyau.
- [x] Le noyau perd `touch.asm`, puis `input.asm` (avec l'USB du noyau).

## 3. USB en service

On sépare la mécanique de la politique (détail : [USB.md](USB.md)) : **usbd** fait le bus,
**hwmanager** choisit le pilote. Cible : claviers et souris USB (ports USB-A et USB-C) et
clé USB, utilisables en même temps que le tactile.

- [x] **usbd** (`SD0:/sys/svc/usbd`, [USB.md](USB.md)) : contrôleurs DWC2 (port USB-C) et xHCI
      (ports USB-A, VL805 derrière PCIe), énumération, hubs ; il annonce chaque interface à
      **hwmanager**. Validé sur le PocketTerm (28/10/2026) : clavier intégré, clavier et souris
      Raspberry Pi sur les ports USB-A, en même temps que le tactile.
- [x] **Branchement à chaud** : usbd scrute les ports (racines et hubs) toutes les 200 ms,
      énumère ce qui arrive, détache ce qui part (les pilotes s'arrêtent, la clé est démontée).
      Validé sur le PocketTerm (28/10/2026) : clavier, souris et clé USB branchés et retirés.
- [~] Pilotes de classe en services, branchés sur usbd : **usbkeyboard**, **usbmouse** (→
      HIDManager), **usbstorage** (validés sur le PocketTerm) (→ périphérique bloc `blk.usb0`, puis FatFS : `USB0:`),
      **usbaudio**, **usbnet** (ECM).
- [~] **Montage automatique** (en partie : usbstorage fait monter `USB0:` par `vfs`, et démonter au
      retrait) : un pilote de stockage, une fois ses périphériques blocs
      enregistrés (`blk.usb0p1`...), prévient `vfs` ; `vfs` sonde chaque partition (signature
      du premier secteur : FAT32, exFAT, plus tard ext2...), choisit le service d'après
      `SD0:/etc/filesystems.txt` (`FAT32 = fatfs`, `exFAT = fatfs`), le lance et monte le volume
      sur le premier nom libre (`USB0:`, `USB1:`...). Au retrait, le pilote et son service FS
      s'arrêtent, `vfs` retire le volume (les fichiers ouverts reçoivent une erreur). Même
      mécanisme pour la carte et les images ; seul `SD0:` reste monté explicitement par
      `init.txt`.
- [x] Le noyau perd `usb_dwc2`, `usb_xhci`, `pcie`, `usb_hid`, `usb_msd`, `usb_ecm`, `usb_audio`,
      `input`, `blocks` (≈ 5 900 lignes, 30 Ko ; branche de retour `usb-noyau`) ; validé sur le PocketTerm :
      claviers, souris (y compris derrière le hub du clavier), clé, tactile, notifications.

- [x] **Navigateur de fichiers** (`SD0:/apps/files.app`, remplace l'ancien ; validé sur le PocketTerm :
      volumes, éjection, accès rapides, icônes des `.app`, barre de menus, vue en **icônes** ou en
      **liste** (menu Affichage, gardée dans `files.ini`), liste triable par **en-têtes de colonnes**)
      ; plus tard, **déplacer** fichiers et dossiers par **glisser-déposer** (vers un dossier, le
      panneau de gauche, une autre fenêtre : protocole de glisser entre fenêtres dans Elegant) : d'après le *fileviewer* d'Onyx,
      **sans la vue en colonnes « NeXT »** (choix de l'utilisateur, 29/10/2026) : à gauche, un
      panneau avec les **volumes** et les **accès rapides** (dossiers épinglés) ; la vue principale
      montre les dossiers et fichiers du dossier courant (barre de chemin au-dessus) ;
      les paquets `.app` y paraissent comme des applications : leur icône (`ui_icone`, celle du dock)
      et leur nom (`app.txt`) ;
      accès rapides : Applications (`SD0:/apps`), Documents, Musique, Images, Vidéos, puis les
      dossiers épinglés, définis dans `SD0:/etc/places.ini` (dossiers de l'utilisateur :
      `SD0:/home/documents`, `music`, `pictures`, `video`, créés par la construction) ; un
      dossier absent n'est pas montré (rien n'est créé sur la partition de l'utilisateur) ;
      Corbeille (`SD0:/home/.trash`) plus tard (il faut d'abord renommer/déplacer dans FatFS) ;
      il montre tous les volumes montés (`SD0:`, `SD1:`, `USB0:`, `RAM0:`...) avec une option pour
      **démonter** (vfs : op 21, et une op pour lister les volumes) ; l'occasion de faire la
      **barre de menus globale** (les menus de l'application active, comme Onyx).

- [x] **notifyd** (comme Onyx, `SD0:/sys/notifyd`, lancé par la session) : un service qui affiche une notification en haut à droite de
      l'écran quand on le prévient (titre, message, disparaît seule) ; commande
      `SD0:/bin/notify <titre> <message>`. Prévenu par hwmanager (périphérique branché / retiré,
      avec le nom du pilote), usbstorage (`USB0:` monté / démonté), plus tard le réseau
      (connecté, adresse) et le son.

## 4. Fichiers hors du noyau (comme QNX)

**Fait** (29/10/2026, étapes 0 à 5 sur QEMU ; docs/FICHIERS.md) : `files.asm`, `remote.asm` et
`pipe.asm` ont quitté le noyau, qui ne lit plus aucun fichier.

- [x] **Chemins et dossier courant dans `vfs`** : il connaît l'appelant (le pid du message), garde
      son dossier courant, l'oublie à sa fin ; le noyau n'analyse plus aucun chemin.
- [x] **Descripteurs dans le process** (appkit) : `open` passe par `vfs`, qui rend « service,
      numéro » ; `read`/`write` vont **directement** au service FS (un passage, sans relais du
      noyau). Reste : mise en mémoire sans copie pour un fichier chargé entier (`FS_CHARGER`).
- [x] **Lancement** : le noyau crée un process depuis une image en mémoire (`spawn_image`) ; la
      lecture de l'exécutable passe dans appkit (plus tard, un serveur exec : personnalités).
- [x] Descripteurs transmis au fils (entrée, sortie, erreur : tubes de vfs).
- [x] Transition : les appels 12–23 et 26 ont été un relais le temps de passer les programmes
      sur appkit ; ils rendent maintenant `E_NOSYS` (numéros gardés jusqu'au 6 bis).
- [x] **Kits hors du noyau** : AppKit lit `SD0:/lib/<nom>.so`, le noyau l'inscrit
      (`kit_register`, droits de `SD0:/sys`, sinon par init2) ; options et WiFi donnés au noyau
      par init (`boot_file`).
- [ ] Recharger un kit après une recompilation sous ARMPI (`kitreload`, anciennes versions
      gardées jusqu'au départ de leur dernier utilisateur).
- [ ] Entrées / sorties du demandeur pour un programme lancé par init2 (aujourd'hui le journal).
- [ ] Selfhost et PocketTerm (WiFi : fichiers donnés par init) après ce chantier.

## 5. Réseau

Pas d'abstraction poussée : tous les Pi ont la même famille de cartes.

- [x] **netd** : la pile TCP/IP du noyau (ARP, IPv4, ICMP, UDP, TCP, DHCP, DNS) dans un service,
      plusieurs threads ; NetKit lui parle (sockets) ; les programmes passent de leurs
      appels 40–52 à NetKit (fait sous QEMU ; docs/RESEAU.md, section 10).
- [x] **netdrv** : un pilote par interface (`usbnet`, `genet`, `wifi`), reliés à netd par un
      anneau de trames ; pile, trames, Wi-Fi, SDIO et GENET quittent le noyau (branche de
      retour `reseau-noyau`). **Mise au point du GENET et du Wi-Fi sur le PocketTerm à faire.**
- [x] Applet **Wi-Fi** du panneau de configuration, menu Wi-Fi de la barre (`wifimenu`), pays et
      fuseau dans l'applet Région (QEMU : Wi-Fi simulé).
- [x] **Plusieurs réseaux connus** dans `SD0:/etc/wifi.txt` (`ssid=`, `key=`, `priority=` ;
      `proto=`, `key_mgmt=` lus et ignorés), `country=` pour tous ; au démarrage, un
      balayage, puis connexion au réseau connu présent (priorité, puis signal).
- [x] **NTP** (netd, `clock.cfg`), **telnetd**, **ftpd**, `reboot`, `screenshot`, **vncd**,
      générateur aléatoire du noyau (appel 101) : sans mot de passe (QEMU).
- [x] **TLS** : d'abord `tlskit` (mbedTLS), retiré le 30/10/2026 ; aujourd'hui OpenSSL, 8 quater.
- [x] **rdpd** (port 3390, protocole d'Onyx Remote : fenêtres une à une, LZ4, 16/32 bits,
      rondes en pipeline, pointeur, touches, devant, fermer, déplacer, bureau) ; ops Elegant
      14-17 (QEMU, avec le code réseau du vrai client) ; **PocketTerm à essayer**.
- [x] **RNG200** essayé sur le PocketTerm (29/10/2026) : il répond (64 mots), pas de gel du bus
      comme sous Onyx ; actif par défaut, option `no_rng200` pour le couper.
- [ ] Ensuite : **FtpFS** (`FTPx:`).

## 6. Son

Plan et état détaillé : [SON.md](SON.md) (§9).

- [x] **sound** (`SD0:/sys/svc/sound`, service « sound ») : agnostique du matériel comme netd ;
      mixage par sortie, capture (boucle, micro), sortie principale, volumes, routage ;
      **AudioKit** par anneaux partagés.
- [x] **Pilotes** : chacun dans son process, inscrit auprès de sound (`svc/inc/sndif.inc`) :
      `pwmaudio` (prise jack, `services.txt`), `hdmiaudio` (lancé et arrêté par **hwmanager**
      d'après l'EDID et le « hot plug »), `i2smic` (micro I2S, à la demande), `usbaudio` (UAC1,
      lancé par hwmanager, classe 01 ; isochrone dans usbd). Canaux DMA « lite » distribués par
      sound. **Validé sur le PocketTerm** (29/10/2026) : jack, HDMI (haut-parleur de l'écran),
      casque USB (xHCI), capture ; reste le micro I2S.
- [x] **AudioKit** v1 (flux, capture, outils, mixer) : le seul chemin vers le son.
      Étape 9 faite : décodage et encodage **OGG** et **MP3**, synthèse **MIDI** et **FM**
      (30/10/2026).
- [x] **Sortie principale** : préférée, puis USB, HDMI, jack, recalculée à chaque branchement.
- [x] **Mixer** : volume par application et par sortie, routage d'une application vers une
      autre sortie, retenus par nom dans `SD0:/etc/sound.txt`.
- [x] **Interface du mixer** : haut-parleur de la barre de menus (volet `soundmenu`, clic droit
      « Avancé... »), application **Mixer**.
- [x] Applet **Son** du panneau.
- [x] Le son quitte le noyau (`audio.asm`, `audio_hdmi.asm`, `audio_i2s.asm` ; appels 28–35 →
      `E_NOSYS`) ; branche de retour `son-noyau`.

## 6 bis. Table des appels système renumérotée

- [x] **Fait** (29/10/2026, QEMU) : la table du noyau est refaite sans trous, **groupée par
      domaine** sur le chiffre hexadécimal de tête ; seule source `src/inc/syscalls.inc` (`SYS_*`,
      inclus par `armpi.inc` et par `syscall.asm`, macro `sysent`) ; anciens numéros non gardés,
      plus aucun numéro en dur (kit.inc, journal, fasmarm natif et `x86toa64.py`, TLSKit
      recompilé). docs/ABI.md refait (avec la correspondance des anciens numéros).

| Plage | Domaine |
|---|---|
| `0x00`–`0x0F` | mémoire (tas, tampons partagés, DMA) |
| `0x10`–`0x1F` | process et temps (créer, attendre, tuer, sommeil, heure) |
| `0x20`–`0x2F` | threads et futex (dont les appels des threads POSIX : join, TLS, priorités, noms) |
| `0x30`–`0x3F` | IPC (services nommés, messages, avis) |
| `0x40`–`0x4F` | matériel et démarrage pour `SD0:/sys` (`dev_map`, `fw_call`, modules, kits) |
| `0x50`–`0x5F` | serveur graphique (ce qu'il reste à Elegant) |
| `0x60`–`0x6F` | système (console, journal, état, aléa, `lib_open`) |
| `0x80`–`0x8F` | fichiers et lancement : servis par AppKit, pas par le noyau |

## 6 ter. Commandes Unix d'Onyx (`SD0:/bin`) — après WebKit

Ordre décidé le 29/10/2026 : formats audio d'AudioKit et MediaKit en parallèle, puis WebKit
(§8), puis ces commandes.

Demandé par l'utilisateur (29/10/2026) : porter les commandes d'Onyx (`user/BinUtils`) **avant**
ses applications. Réécriture en FASMARM d'après le C d'Onyx, sur la sortie standard et les tubes
d'AppKit (section 4) ; auto-hébergées (selfhost).

- [ ] Fichiers et texte : `ls`, `cat`, `cp`, `mv`, `rm`, `mkdir`, `touch`, `find`, `head`, `tail`,
      `wc`, `grep`, `sed`, `sort`, `uniq`, `cut`, `tr`, `nl`, `tee`, `diff`, `hexdump`, `echo`, `ed`.
- [ ] Système : `ps`, `kill`, `df`, `mount`, `uname`, `date`, `sleep`, `shutdown`, `kmsg` (journal).
- [ ] Réseau : `nslookup`, `netstat`, `wget`, `whois`, `ftp` (client).
- [ ] `sh` : tubes (`a | b`) et redirections (`>`, `<`) entre ces commandes.
- Les essais propres à Onyx (`*test`, `bench`) ne sont pas repris.

## 7. Applications d'Onyx

**Règle décidée le 29/10/2026** : les applications d'Onyx sont portées **en C/C++, hors du
selfhost**, dans **`src/extra/onyxports/<nom>/`** (suivi par git, contrairement au reste de
`src/extra` ; pas copié sur la carte), comme Jet (chaîne d'Onyx, `posix`, kits d'ARMPI par leurs en-têtes C : `uikit.h`...),
**sauf mention contraire de l'utilisateur** (en assembleur : le synthé MIDI, et **les émulateurs
d'Onyx** — `user/Emulators` : NES, SNES, GB, GBA, N64, NDS ; la GameCube en C++ et boucles du GX en asm, docs/EMULATEURS.md —, entièrement réécrits
en assembleur dans le selfhost, le C++ d'Onyx servant de modèle ; la bibliothèque multimédia,
d'abord prévue en assembleur, suit finalement la règle). Pas de conversion automatique de `gcc -S` en FASMARM : code illisible,
libstdc++ / newlib à embarquer, selfhost seulement apparent ; `gcc -S` sert au plus de brouillon
pour une routine réécrite à la main.

- [x] **Courier** (01/11/2026) : le client HTTP d'Onyx réécrit en C# sur `System.Windows.Forms`, ses interfaces en `.ui` compilés (`tools/dotnet/courier`, docs/DOTNET.md) ; trop lent à démarrer, remplacé sur la carte par le port en C++ (`src/apps/courier`) ; essais QEMU seulement.
- [x] **Applet Dock d'Onyx et espaces de travail** (31/10/2026, sous QEMU) : `dockconf` porté dans Control en widgets de UIKit, même disposition ; ce qui manquait est ajouté au dock : un tiroir par catégorie, `hidden`, `desk` (`src/inc/dockconf.inc`), et les espaces de travail dans Elegant (`WS_OP_DESK`, Ctrl+Alt+Gauche / Droite) et UIKit (`ui_desk`, `ui_win_details`, `ui_tip_at`). Reste : le menu de fenêtre « Déplacer vers… ».
- [x] **Panel** (31/10/2026, sous QEMU) : la barre du bas remplace le dock par défaut (`src/sys/panel`, `SD0:/etc/panel.ini` : `src/inc/panelconf.inc`, repli sur `dock.ini`) : toute la largeur, couleurs du thème, bouton « apps » à gauche, raccourcis centrés (12 au plus) puis applications ouvertes, à droite espaces de travail, corbeille, réglages, alimentation. Le bouton « apps » ouvre le lanceur (`launcher.inc`) : un cadre translucide, recherche sans casse ni accents dans la catégorie choisie ; les fenêtres sont cachées par le rideau d'Elegant (`WF_CURTAIN`), qui tombe aussi si le programme meurt. Applet Panel dans Control ; l'ancien dock et son applet restent dans les sources, masqués. Reste : l'essai sur le PocketTerm.
- [x] **Un seul chrome, deux thèmes** (1/11/2026, sous QEMU) : plus de barre de titre pour aucune fenêtre (`FRAME_TITLE_H` = 4). Quatre coins arrondis, les trois glyphes réduire, agrandir, fermer sur le contenu en haut à droite, sans fond, une poignée invisible à leur gauche (`FRAME_CAP_*`, `draw_framed` dans Elegant, `uk_frame_buttons` dans UIKit). Déplacement par la poignée, le haut vide du contenu (`UK_DRAG_H`) ou `ui_window_drag`. Toute application laisse `FRAME_CAP_W` libres en haut à droite. Options de fenêtre : `WF_GLYPH_LIGHT`, `WF_BODY_DARK` (émulateurs, lecteurs, Terminal, Cibles). Seuls Givre et Onyx Noir restent : les styles CDE et Milk et les autres thèmes sont supprimés. Reste : GPIO Lab, QBStudio, Media Player, Turtle Quest (contrôles sous les glyphes), `screen.inc`, l'essai sur le PocketTerm.
- [x] **Koton** (30/10/2026) : le studio d'Onyx porté **en glue C++** (`src/apps/koton`, `tools/webkit/build-koton.sh`, chaîne `aarch64-onyx-elf`, hors du selfhost), l'interface directement sur UIKit de l'OS (`ui_*`, aucun pont depuis le 30/10), synthèse et son directement par AudioKit (`ak_*`) ; dans le tiroir Création du dock. IA, plugins et MIDI matériel en stub (README du dossier).
- [x] **Circuits** (31/10/2026) : le jeu de portes logiques d'Onyx porté sur UIKit, sans pont (`src/apps/circuits`). Vérifié sous QEMU, en 1024x768, 800x600 et 640x480 ; pas essayé sur le Pi.
- [x] **Suite bureautique** (31/10/2026) : Letters, Spreadsheet, Slides, Cardfile portés d'Onyx sur UIKit, sans pont, compilés aussi par GCC sous ARMPI ; **ArchiveKit** et **DocumentKit** écrits en assembleur (docs/BUREAUTIQUE.md). Vérifié sous QEMU ; pas essayé sur le Pi.
- [x] **FMTracker** (30/10/2026) : porté **en assembleur** (`src/apps/fmtracker`, 7 300 lignes, sur UIKit et la synthèse FM d'AudioKit, selfhost, à la demande de l'utilisateur) ; lit et écrit .FMS / .FMI, export WAV.
- [ ] **Paint** (celui d'Onyx), avec ses calques : il demandera alors le **GPU** (V3D) pour
      composer les calques ; le pilote V3D (aujourd'hui dans le noyau) passera en service à ce
      moment-là.
- [x] **gamelib** (31/10/2026) : la bibliothèque de jeux, en assembleur (docs/EMULATEURS.md). Vérifiée sous QEMU ; pas essayée sur le Pi.
- [x] **Manettes** (31/10/2026, docs/EMULATEURS.md ; `usbgamepad` jamais essayé, l'applet Manette reste à écrire) : pilote `usbgamepad` (HID, par HIDManager) et **le clavier comme manette**,
      avec par défaut le mapping du PocketTerm dans Onyx : flèches = croix, touches A B X Y L R
      telles quelles, Impr. écran = Select, Pause = Start (deux touches sans événement clavier ordinaire,
      traitées à part comme dans Onyx) ; réglable dans une applet Manette (comme `padconf`).
      **HIDManager tient un anneau « manette »** (abonnement comme celui d'Elegant) : il y dépose
      l'état des manettes (pilotes `usbgamepad`) et, quand une touche du clavier est mappée, il
      l'ajoute aussi à cet anneau (elle va toujours aussi au clavier : décidé par l'utilisateur) ;
      une touche non mappée (H...) ne va qu'au clavier. Les émulateurs et les jeux ne lisent que l'anneau manette,
      quelle que soit la source.
- [x] **Émulateurs** d'Onyx (31/10/2026, docs/EMULATEURS.md ; NES et GBC en asm, SNES et GBA en C++ en attendant l'asm, vitesse du Pi non mesurée) : **NES**, **SNES**, **N64**, **GBC**, **GBA**, **NDS**. Écartés :
      la 3DS (trop complexe, pas assez rapide sur le Pi) : **levé** le 3/11/2026, `n3dsemu` fait le titre d'*A Link Between Worlds* à 36 images/s sur le GPU du Pi, docs/EMULATEURS.md.
- [x] **GameCube** (2/11/2026, docs/EMULATEURS.md) : écartée d'abord comme trop lente, sur une estimation ; Onyx mesure 46-49 trames/s sur 50 sur le Pi. `gcemu` : recompilateur d'Onyx, TEV en GLSL ES par Mesa. Mesuré sur le Pi sans son : 36 à 63 trames/s sur 50 sur Wind Waker, 18 sur le film de Mario Kart ; temps réel pas atteint partout, partie jouée pas essayée.
- [-] **Quake** en rendu logiciel : porté le 2/11/2026 puis **retiré le 5/11/2026** à la demande de l'utilisateur (ne répondait pas à ses attentes). Restent dans les kits ce qu'il avait apporté : `emu_game` / `emu_pointer` (EmuKit) et `WS_OP_POINTER_LOCK` (Elegant).
- [ ] **Émulateur PC x86** (bien plus tard, idée de l'utilisateur) : 286 puis 386 (+ x87), pour DOS
      (FreeDOS d'abord), Windows 3.11, NT 3.x. Émulateur écrit par nous ; BIOS **SeaBIOS** et
      **SeaVGABIOS** (LGPLv3, firmware séparé dans `SD0:/res/x86`, à valider : exception à la règle
      « tout propre ») ; VGA / Bochs VBE, IDE, disquette, Sound Blaster + AdLib (FM de fmsong).
      Plus tard, traduction dynamique x86 → A64 (base : `tools/x86toa64.py`).
- [ ] **Jet Browser** (le navigateur WebKit d'Onyx, LGPL-2.1 ; Onyx `docs/08-WEBKIT-PORT.md`) : **reprendre
      la chaîne d'outils d'Onyx** (`aarch64-onyx-elf`, dans WSL : `tools/toolchain/fetch.sh` →
      `/opt/toolchains` ; sysroot `user/Runtime/libc/posix`, libonyxposix sur newlib ; ports
      `tools/ports/build-all.sh`) plutôt qu'en refaire une. À porter : **newlib et sa glue**
      (appels système → AppKit, vfs, NetKit, tas, threads), puis l'affichage vers UIKit / Elegant.
      **POSIX minimal** : partir de libonyxposix (`user/Runtime/libc/posix`, ≈8 900 lignes de C,
      25 fichiers) en remplaçant ses `kapi_*` par ARMPI — fichiers, répertoires, chemins (`file`,
      `fd`, `stat`, `dir`, `path` → vfs et services FS), sockets et DNS (`socket`, `netdb`,
      `poll` → netd), threads et TLS (`pthread*`, `tls`, `sem`), mémoire (`mman`), processus
      (`proc` → V_EXEC + spawn_image, wait), temps, signaux ; `crt0posix.S` et `onyx-posix.ld`
      au format des exécutables d'ARMPI (en-tête, kits).
      Après le réseau **et quelques apps d'Onyx déjà portées** (la chaîne et le POSIX minimal
      rodés sur des programmes plus petits) ; choisir la licence d'ARMPI d'abord (Jet impose la LGPL
      à son binaire).
- [ ] Liste de l'utilisateur (29/10/2026), à porter depuis Onyx :
      - **fmsong** ;
      - la **suite bureautique** : **letter** (traitement de texte), **sheet** (tableur),
        **present** (présentations), **cardfile** (fiches) ;
      - **Paint** (ci-dessus) ; **kotonstudio** (fait : Koton, ci-dessus) ; le **lecteur multimédia** (media player) ;
      - **gamelib** et les émulateurs (ci-dessus) ;
      - **calendrier / agenda** (avec **clockd**, le service des alarmes et rappels) ;
      - **mail**, **telegram**, **irc** (après le réseau, section 5) ;
      - **archiver** (archives), **pdf** (lecteur), **shutdown** (éteindre, redémarrer),
        **screenshot** (capture d'écran) ;
      - côté système : le **presse-papiers** (comme celui d'Onyx : texte, fichiers, images,
        partagé entre applications) et les **bureaux multiples**.
- [ ] Les autres applications d'Onyx, petit à petit.

Onyx est en C++ ; ARMPI est en assembleur : un « portage » est une réécriture en
FASMARM d'après le code d'Onyx (le projet de l'utilisateur : réutilisation permise).

## 8. Couche POSIX et WebKit (le gros projet)

- [x] **Threads complets** (29/10/2026, QEMU) : `TPIDR_EL0` par thread (TLS), `thread_create_ex`
      (TLS, priorité, nom), `thread_exit(code)`, `thread_join`, `thread_self`, priorités
      (normale, haute, temps réel souple ; sans famine), noms (`thread_list`, journal F12),
      `thread_kill` différé, futex avec délai ; mutex et conditions d'AppKit (`mtx_*`, `cnd_*`) ;
      `sound` et ses pilotes en priorité haute ; essai `threadtest` (selfhost). Reste : les threads
      dans le moniteur système.
- [x] **Couche POSIX minimale** (29/10/2026, QEMU) : libonyxposix d'Onyx portée en **`posix`**
      (`src/lib/posix` : `libposix.a`, en-têtes, `crt0posix`, `posix.ld`, `posix.specs`), sur
      newlib et la chaîne d'Onyx, construite par `tools/posix/build.sh` (WSL, hors du selfhost) ;
      `kapi_*` remplacés par les appels système, AppKit, vfs et NetKit ; `posixtest` (tous les
      groupes sans échec, sauts listés) et `posixtest-cxx` (82 / 82) sur `SD0:/bin`
      ([POSIX.md](POSIX.md) : ce qui manque). Reste : environnement des fils, `mmap` du noyau,
      signaux entre process, IPC locale, dates des fichiers, cohérence de `fatfs` entre deux
      descripteurs.
- [x] **Avant le portage de WebKit proprement dit** (décidé le 29/10/2026, fait le 29/10/2026) :
      - **Préchargement des exécutables** (`preload.asm`, `SD0:/etc/preload.txt`, init2
        `preload`) : chaque lancement reprend les pages déjà chargées (code et lecture seule
        partagés, données en **copie sur écriture**, `.bss` à la demande) ; vfs fait oublier
        l'image d'un fichier réécrit. Mesuré sous QEMU : `fasmarm` 21 ms par lancement contre
        175 ms. Pas fait : le cache pour **tout** exécutable lancé plusieurs fois (il faudrait que le
        noyau garde l'image de chaque `SPAWN_IMAGE` avec son chemin, et une politique d'éviction
        quand la mémoire manque : à faire si WebKit en a besoin).
      - **Pagination à la demande** et appels `VM_*` (0x06–0x0C) pour la couche POSIX (mmap,
        munmap, mprotect, madvise) ; `.bss` et pile principale paresseux (`vmspace.asm`).
      - **Toute la mémoire** (`memmap.asm`, `pmm.asm`) : RAM du DTB (ou de la révision), noyau en
        identité sur toute la RAM, **zone basse** (< 1 Go : DMA, VideoCore, V3D) et zone haute
        (le reste, préférée) ; option `no_highmem`. QEMU : 1984 Mo vus. À voir sur le PocketTerm
        (4 ou 8 Go) : les lignes `RAM vue` et `Plages de RAM` du journal.
- [x] **Formats audio dans AudioKit, en assembleur** (SON.md étape 9 ; décidé le 29/10/2026,
      fait le 29/10/2026 : décodage WAV, FLAC, MP3, Ogg Vorbis, Ogg Opus CELT + SILK + hybride ;
      encodage WAV, MP3, Opus CELT ; API par paquets pour MediaKit ; `play`, Musique ; restent
      l'encodeur Vorbis, la dissimulation de pertes d'Opus ;
      avant WebKit) : écrits d'après les spécifications (MP3 : ISO 11172-3 couche III ; Vorbis I ;
      Opus : RFC 6716 ; FLAC), NEON pour les MDCT et bancs de filtres ; les décodeurs C d'Onyx
      (minimp3, stb_vorbis, libopus, dr_flac) servent seulement de référence d'essai (même fichier,
      même PCM). Décodage : MP3, Vorbis, Opus (CELT puis SILK), FLAC. Encodage : Opus (CELT) et MP3
      (CBR simple), Vorbis si le temps le permet.
      **MIDI et FM, fait le 30/10/2026** (SON.md 9 bis, KITS.md) ; décidé le
      29/10/2026 : **notre
      propre synthé SF2 en assembleur**, écrit d'après les spécifications SoundFont 2.04, MIDI 1.0
      et General MIDI (voix, enveloppes, filtre, chorus, réverbération ; NEON pour le mélange),
      **inspiré de MeltySynth** (celui de l'AudioKit d'Onyx : `user/Kits/audiokit/meltysynth.h`,
      `soundfont.cpp`, `synthesizer.cpp`, `voice.cpp`, `effects.cpp`) comme modèle de conception et
      référence d'essai (même .mid, même SoundFont, sons comparés), sans recopier son code ; séquenceur
      de fichiers .mid (formats 0 et 1). FM : d'après `akfm.cpp`/`fmsynth.h` d'Onyx (code de
      l'utilisateur, reprise permise). SoundFont GeneralUser GS d'Onyx dans `SD0:/res/soundfonts`.
      Nécessaire à la bibliothèque multimédia (fichiers .mid).
- [x] **Vidéo** (décidé le 29/10/2026 ; MediaKit et le lecteur faits le 29/10/2026, QEMU :
      [MEDIAKIT.md](MEDIAKIT.md) ; l'API audio par paquets d'AudioKit branchée le 29/10/2026,
      libopus retiré ; reste : un thread de décodage dans le lecteur, essais sur le PocketTerm) : les **décodeurs** dans un kit **MediaKit** en C/C++,
      précompilé comme TLSKit (chaîne d'Onyx, hors du selfhost : `prebuilt/mediakit.so`,
      `SD0:/lib/mediakit.so`), **utilisable par toutes les applications** par sa table `kit_fn` :
      libvpx (VP8/VP9), dav1d (AV1), libopus, et ce que reprend la bibliothèque **av** d'Onyx
      (`user/Libs/av` : démultiplexeurs MKV/WebM, MP4, RIFF, conversion YUV, rééchantillonnage).
      **Pas de codec audio dans MediaKit** : il sépare les pistes et confie les paquets audio aux
      décodeurs d'**AudioKit** (en assembleur, selfhost ; utilisables sans vidéo : musique,
      magnétophone, jeux). Exception : l'AAC si FFmpeg est retenu.
      Deux applications (décidé le 29/10/2026) : un **lecteur** simple en assembleur
      (`mediaplayer`, selfhost : ouvrir un fichier, lecture/pause, position, volume) ; et la
      **bibliothèque multimédia**, **portage du Media Player d'Onyx** (fait sous QEMU le 31/10/2026 : `src/apps/media` ; `user/Apps/media` : musique,
      vidéos, listes, MIDI), **en C/C++ hors du selfhost** comme les autres applications d'Onyx (§7),
      sur MediaKit (décodage vidéo), AudioKit (ses décodeurs et sa synthèse à la place de ceux
      d'Onyx) et UIKit par `uikit.h`, image par la surface attachée d'Elegant ; après MediaKit, le
      lecteur et WebKit (qui écrit `uikit.h`). WebKit s'en sert
      aussi pour la vidéo des pages (`MediaPlayerPrivateOnyx`). FFmpeg (H.264/H.265/AAC, GPL-2) :
      à décider plus tard.
- [ ] **WebKit** : partir du portage d'Onyx, n'adapter que sa « glue » propre à Onyx. Sources
      copiées dans `src/extra` (WebKit patché + 14 dépendances, hors dépôt, voir son README).
  - [x] jsc, WebCore (`wctest`), son par AudioKit (29/10/2026).
  - [x] **IPC entre process pour WebKit2** (fait le 30/10/2026, `src/kernel/ipc.asm`, appels
        `0x70`–`0x7D`, ABI.md ; POSIX.md « IPC entre process ») : `socketpair` SEQPACKET / STREAM /
        DGRAM, `SCM_RIGHTS`, `memfd_create` / `shm_open` + `mmap MAP_SHARED`, `poll` dans le noyau,
        descripteurs hérités par `posix_spawn` ; `posixtest ipc` 27 réussis (QEMU).
  - [x] **WebKit2 tourne** (30/10/2026, QEMU) : `wk2test` — process UI, web et réseau — charge et
        peint une page.
  - [ ] **Jet** : commencé (30/10/2026) — `src/extra/onyxports/jet` : moteur d'Onyx, habillage
        UIKit d'ARMPI (barre d'outils, adresse, barre d'état), page dans la surface attachée
        d'Elegant ; vérifié sous QEMU (pages locales, molette). Reste : onglets / nouvelle
        fenêtre, menus, téléchargements, console, fenêtre redimensionnable, réseau réel (https)
        à essayer, le PocketTerm.
- [ ] **Juste après WebKit : composition sur le GPU** (décidé le 29/10/2026), comme Onyx (« The
      compositor on the V3D », docs/08-WEBKIT-PORT.md d'Onyx) : (1) le pilote V3D sort du noyau
      et devient un **service** avec une API de textures et de composition (ce que le noyau d'Onyx
      offrait à `gpucomp`), validé sur le PocketTerm — fait sous QEMU le 30/10/2026 : service
      `v3d` + GpuKit, textures et `gk_render` (kapi v53–v72 d'Onyx, listes identiques à la
      référence, `docs/V3D.md`), reste l'essai sur le Pi ; (2) **portage de `gpucomp`**
      (`user/Libs/gpucomp` d'Onyx) sur ce service ; (3) **adaptation de Skia et de WebKit** : les
      tuiles peintes sur le processeur, composées par le V3D dans la surface (`LayerTreeHostOnyx`),
      repli sur le processeur sans GPU. Servira aussi à Paint (calques).
- [x] **Op d'Elegant « surface attachée à une fenêtre »** (décidé le 29/10/2026, fait le
      29/10/2026 : `WS_OP_SURFACE_ATTACH` / `WS_OP_SURFACE_DAMAGE`, UIKit `ui_surface_*`,
      DESIGN-ELEGANT.md 17 ; Jet y peint depuis le 30/10/2026 : `jet_present` de
      `src/extra/onyxports/jet/present.c` ; reste : la glue `onyxsurface.c` du compositeur) : on garde le
      schéma de Jet chez Onyx (le processus web compose dans une surface partagée à la taille de
      l'écran, puis signale la taille et les zones modifiées) ; la surface est un tampon partagé
      d'ARMPI, `onyxsurface.c` devient une glue sur ces tampons, et une op d'Elegant dit « le
      contenu de cette fenêtre est ce tampon » plus une op « zones modifiées » (les zones sales du
      compositeur) : Elegant compose directement depuis la surface, sans la recopie d'Onyx.
      Servira aussi au lecteur vidéo, aux émulateurs, aux programmes SDL portés.
- [x] **SDL** (2/11/2026) : SDL 3 avec `sdl2-compat` en statique, SuperTux et SuperTuxKart construits sans retouche ([SDL.md](SDL.md)). Reste le blocage du GPU avec tampon de profondeur.
- [ ] **Combler l'écart UIKit d'ARMPI / uikit d'Onyx** ([UIKIT-MANQUES.md](UIKIT-MANQUES.md),
      en cours le 29/10/2026) : l'habillage de Jet (onglets, barre d'adresse, barre d'outils,
      menus ; ~2 900 lignes C++ sur uikit, appkit, systemkit d'Onyx) se porte alors sur UIKit,
      appelable depuis le C (en-tête `uikit.h` d'ARMPI à écrire).
- [ ] **Décidé (28/10/2026)** : WebKit est **l'exception** à la règle d'auto-hébergement. Il est
      compilé sous Windows par une chaîne C/C++ (compilateur, éditeur de liens, bibliothèque C
      sur la couche POSIX) qui produit des ELF ARMPI ; il n'est pas construit sur le Pi. Tout le
      reste d'ARMPI continue de s'assembler sous ARMPI.

## 8 ter. GCC et G++ sous ARMPI (décidé le 30/10/2026)

L'OS reste en assembleur et dans le selfhost ; GCC est un programme ordinaire, précompilé sur le
PC, hors du selfhost comme WebKit : [GCC.md](GCC.md). `fork` n'est pas nécessaire (`posix_spawn`).

- [x] **Phase 1 : GCC et G++ sans `make`** (30/10/2026, sous QEMU) : `hello.c`, `hello.cpp` et
      `posixtest-cxx` compilés sous ARMPI ; `posixtest-cxx` ainsi compilé : 82 réussis, 0 échec.
- [x] Pile de 64 Mo pour tous les programmes (`USER_STACK_SIZE`, à la demande).
- [x] Chemins virtuels configurables (`SD0:/etc/paths.txt`, vfs).
- [x] `argv` et environnement des fils sans limite de longueur (objet partagé, libposix).
- [x] **Phase 2 : mesures sur le Pi** (30/10/2026, [GCC.md](GCC.md)) : `hello.c` 2,1 s, `hello.cpp`
      8,7 s, `posixtest-cxx` 39 s, 82 réussis ; la carte n'est pas le goulot, `ramfs` a dû
      rendre sa mémoire.
  - [ ] Coupure telnet vue deux fois sur le Pi, `RAM0:` plein, puis une fois (30/10/2026, sans
        `RAM0:` plein) : tout le réseau, ARP compris, environ deux minutes, sans redémarrage. Cause
        non trouvée.
  - [ ] `sh` jette ce qui arrive sur son entrée pendant qu'un programme tourne (Ctrl+C seul).
- [x] **Phase 3 : `make`** (30/10/2026, QEMU et Pi : `tools/gcc/pitest.py`, [GCC.md](GCC.md)).
  - [ ] Noyau : garder un pid réservé jusqu'au `wait` (aujourd'hui, il ne revient qu'après un
        tour de la table, ce qui rend le problème très rare sans le supprimer).
  - [x] Dates des fichiers (30/10/2026, QEMU) : horloge du noyau qui ne recule pas, `fatfs`,
        `ramfs`, `vfs`, `stat.c`, `utime*` ([FICHIERS.md](FICHIERS.md)). Pas encore essayé sur le Pi.
  - [x] `rename` sans copie (30/10/2026, QEMU) : fichiers et répertoires, y compris ouverts.
  - [x] Shell **en assembleur, dans le selfhost** (30/10/2026, QEMU, [SHELL.md](SHELL.md)) : sans
        `fork`, le sous-ensemble de POSIX qu'utilisent les Makefile ; `argv` et environnement vers
        les programmes C (`f_spawn_env`). Essai : 45 PASS, 0 FAIL. Reste : fonctions, `<<`,
        `sh` qui ignore les redirections de ses propres messages d'erreur.
  - [x] Outils Unix pour `make` (30/10/2026, QEMU, [OUTILS.md](OUTILS.md)) : 27 commandes en
        assembleur, `grep` et `sed` compris ; 71 PASS, 0 FAIL. Reste de la section 6 ter : `awk`,
        `diff`, `xargs`, `date`, commandes réseau. `ftw` et `nftw` dans libposix.
  - [x] Redirections vers un fichier (30/10/2026, QEMU) : `V_FILE_END`, `adddup2`, `addopen`,
        héritées par les petits-fils ; `f_redir` d'AppKit pour le shell, pas encore essayé.
        `ramfs` écrit désormais à la fin réelle en `O_APPEND`.
  - [x] GNU make 4.4.1 (30/10/2026, QEMU, [GCC.md](GCC.md)) : par `posix_spawn`, 11 PASS, 0 FAIL ;
        `-j` grâce à `SIGCHLD` émulé dans libposix et aux pid donnés en tournant (125 tours sans
        échec).
- [ ] **Signaux asynchrones** : aussi pour `present` application → Elegant.
- [x] **Les kits depuis GCC** (30/10/2026, [GCC.md](GCC.md)) : relais `/gcc/lib/lib<kit>.a`, en-têtes
      `/gcc/include/kits` (`tools/gcc/kits.sh`).
- [x] **`pibuild dossier nom`** et sa fenêtre PiBuild (30/10/2026) : assembleur, C et C++, par `make`
      et un Makefile générique. Le front end de FASMARM est retiré, et Ctrl+B de l'éditeur ouvre PiBuild.
      Reste : un fichier de réglages à la racine du dossier . `pibuild [app|bin|sys|svc|lib]` installe selon le type
      depuis le 31/10/2026 (un kit : `fasmarm source sortie` dans le terminal).
- [x] **Essai final du chantier** (30/10/2026) : Koton construit **sur le Pi** en 2 minutes, depuis
      PiBuild, et il tourne. Reste : son mode pocket, jamais essayé.
- [x] **BASIC d'Onyx** (31/10/2026, QEMU seulement, [BASIC.md](BASIC.md)) : VM et `/bin/basic`, `#import`, QBasic, QBStudio, GPIO Lab,
      Turtle Quest, GpioKit et service `gpio`. **Reste** : essai sur le Pi (GPU, GPIO réel), manettes (`WEV_PAD`).
- [x] **FreeBASIC 1.10.1 dans `/gcc`** (31/10/2026, QEMU et vrai Pi, [GCC.md](GCC.md)) : `fbc` compile sur ARMPI par
      GCC ; console, fichiers, threads, `gfxlib2` dans une fenêtre d'UIKit, kits appelés par des `.bi` générés. Reste :
      `CLS` et `LOCATE` (le terminal d'ARMPI n'a pas de séquences).
- [ ] **En fin de chantier** : toute la chaîne d'outils dans `sdcard/gcc`.

## 8 quater. OpenSSL (30/10/2026)

- [x] **OpenSSL 3.5.9 LTS** ([OPENSSL.md](OPENSSL.md)) : `libcrypto.a`, `libssl.a` statiques
      (`tools/openssl/build.sh`, hors du selfhost), assembleur AArch64 gardé, `getauxval` ajouté à
      libposix, certificats dans `/etc/ca-bundle`. QEMU : crypto 19, tls 9, threads 3, net 12
      (`https://example.com`), cxx 1, aucun échec. **Reste** : essai sur le Pi, lier depuis GCC sous
      ARMPI, le port de .NET.
- [x] **curl, WebKit et Jet sur OpenSSL** (30/10/2026, [OPENSSL.md](OPENSSL.md)) : plus de mbedTLS dans
      le C, Jet + 3,7 Mo. QEMU : https (example.com, Wikipédia), certificat expiré refusé, WebCrypto
      14 sur 14. Pi : Jet ouvre des sites https.
- [x] **TLSKit, `httpsget` et mbedTLS retirés** (30/10/2026). Les programmes en assembleur n'ont plus
      de TLS ; s'il en faut un : un service C sur OpenSSL, appelé par IPC.

## 8 bis. Kits de données et de documents (après WebKit, avec la suite bureautique)

Découpage décidé le 29/10/2026 : trois kits, chacun chargé seulement par qui en a besoin
(en assembleur, selfhost).

- [ ] **DataKit** : formats de données — JSON (lecture, écriture), XML (lecture en flux de type SAX
      et petit arbre DOM, entités, UTF-8 / UTF-16), CSV, INI, base64 ; plus tard YAML et d'autres.
      Pour : réglages, NetKit (API HTTP), RSS, SVG, listes de lecture, paquets.
- [ ] **ArchiveKit** (le filekit d'Onyx) : zip et tar, deflate / inflate, CRC32 ; l'inflate d'UIKit
      (PNG) y passe pour n'en garder qu'un. Pour : paquets, explorateur, DocumentKit.
- [ ] **DocumentKit** : docx / odt, xlsx / ods, pptx / odp (Office Open XML et OpenDocument : un zip
      de XML) vers un modèle de document commun (paragraphes et styles, feuilles et cellules,
      diapositives), sur ArchiveKit et DataKit. Pour : suite bureautique, aperçu de l'explorateur,
      `richtextbox` d'UIKit.
- WebKit garde ses propres libxml2 et JSON (JavaScriptCore).
- [ ] **SqlKit** (plus tard, décidé le 29/10/2026) : connecteurs SQL, kit à part (client réseau :
      NetKit, TLS à prévoir [plus de TLSKit, 8 quater], authentification SCRAM / sha2). API commune (à la JDBC / ODBC) : préparer, lier,
      parcourir les lignes typées, transactions ; un **pilote par base** chargé selon le fournisseur
      (module privé du kit) : SQLite local (moteur C de `src/extra`, précompilé comme MediaKit),
      PostgreSQL, MySQL / MariaDB en assembleur. Deux façons de se connecter :
      - **connexion nommée** : un applet du panneau de configuration crée des connexions (nom,
        fournisseur, hôte, port, base, identifiants, options) ; le programme ouvre par le nom
        (`sql_open_named("compta")`) sans connaître les identifiants ;
      - **connexion directe** : `sql_connect(fournisseur, hôte, port, base, utilisateur, mot de
        passe, options)` (ou une URL `postgres://…`).
      Les mots de passe des connexions nommées ne doivent pas être en clair dans un fichier lisible
      par tous : à concevoir avec l'authentification (chantier de la fin), par exemple un service
      qui garde les secrets et ouvre la connexion pour le programme.

## Chantier perf (remplace le chantier IPC, 30/10/2026)

- [x] **Mot de signaux par service** (`SVC_SIGNAL`, `WS_SIGNAL`), `EV_WAIT` qui dort vraiment.
- [x] **Present asynchrone** : `ui_update` sans attendre Elegant, zone salie en fin de tampon.
- [x] **`WEV_FRAME`** (`ui_frame_request`) : le contenu a été pris.
- [x] **Signaux POSIX** (`src/kernel/signal.asm`), ce qu'il faut à la CLR Core : fautes avec
      `si_addr`, `ucontext` modifiable, `pthread_kill` vers un thread, `sigaltstack`, masques par
      thread ; **`membarrier`**. Vérifié sous QEMU, à essayer sur le PocketTerm.
- [x] **IRQ des pilotes** (`src/kernel/irq.asm`, ABI.md `0x49`, option `no_irq`) : DMA d'Elegant,
      `v3d`, `pwmaudio`, `hdmiaudio`, `genet`. Noyau essayé sous QEMU (`irqtest`) ; **pilotes à
      essayer sur le PocketTerm** : le DMA de QEMU finit aussitôt, les autres blocs n'y sont pas.
- [x] **Wi-Fi** : 3 ms, 7,6 et 5,8 Mo/s, 10 au mieux (0,7 avant) ; économie d'énergie recoupée,
      5 GHz, bus à 50 MHz (RESEAU.md, « Performances du Wi-Fi »). Cœur réservé essayé, sans gain :
      retiré ; `netd` et les pilotes restent des process séparés.
- [x] **TCP de `netd`** : NewReno, retransmission rapide, RTO de 1 s, accusés retardés, *window
      scaling*, tampons de 256 Ko.
- [x] **IRQ de `gt911`** : INT sur le GPIO 4 (mesurée avec `gpiowatch`), rien au repos.
- [x] **`netd` sans sondage** : il dort dans le service `net.rx`, le pilote qui pose une trame le réveille
      (`NR_SLEEP`, `NR_WAKE`). 0,7 % d'un cœur au repos.
- [x] **IRQ du Wi-Fi** : interruption SDIO, ligne 158 partagée avec la carte (`IRQ_SHARED`), attente de
      200 ms au repos ; `wifi` passe de 2 % à 0,7 %. USB : pas de gain sur DWC2, voir ABI.md.
- [x] **Mémoire de l'appelant par mappage** : `SVC_MAP_CALLER` (`src/kernel/mapcaller.asm`), en chaîne,
      pages épinglées par `PTE_SHARED`. Les lectures de fichier de 64 Ko et plus (`FS_LIRE_D`) vont de
      la carte à la mémoire du programme sans copie ; repli sur le tampon partagé.
- [x] **DMA de la SD** (ADMA2, fin par interruption), lecture et écriture : `sd` consomme 40 à 130 fois
      moins de processeur ; débit inchangé en lecture (le bus à 50 MHz est la limite). Atteint la
      mémoire jusqu'à 2,5 Go (`dmareach`). `init2` le demande une fois les options lues, donc le
      démarrage reste en PIO ; essai au passage, repli sur le PIO, option `no_sd_dma`.
- [x] **Écriture de fichier** (`fatfs`) : un cluster neuf coûtait 13 écritures (zéros secteur par
      secteur, 4 de FAT, les données) et une commande par cluster. Un cluster écrit en entier n'est
      plus mis à zéro, FAT et bitmap partent une fois à la fin de la requête, les clusters contigus en
      une commande. Sur le Pi (`writebench`, FAT32) : fichier neuf de 0,23-0,29 à 6,4-7,1 Mo/s, sur
      place de 2,47 à 11,3-11,7 Mo/s. exFAT reçoit le même code, essayé sous QEMU seulement. `rm` et la
      troncature écrivent de même FAT et bitmap une fois : 8 Mo en 65 à 77 ms sur le Pi.
- [x] **Vraies pages de fichier partagées pour `mmap`** : un fichier de 64 Ko à 128 Mo projeté en
      lecture garde ses pages dans un objet nommé d'après lui, une seule fois pour tous les process,
      en copie sur écriture (`VM_COW`), et part avec le dernier process qui le projette ; seul
      `file_pin` le garde. Les DLL cochées dans Réglages > .NET sont épinglées.
- [x] `SIGCHLD` envoyé par le noyau, à la place de l'émulation dans `poll`.
- [x] **Boucle locale** : 127.0.0.0/8, sa propre adresse et `localhost`, essayés en TCP et ICMP ; UDP
      prend le même chemin (`lo_put`, `lo_drain`).


## .NET sous ARMPI (but de l'utilisateur, 30/10/2026)

Un compilateur et un runtime .NET dans l'OS, la GUI liée à UIKit, et un système « à la WPF » sur UIKit :
un fichier qui décrit la GUI d'une fenêtre, d'un user control. Moteur : CoreCLR. Essais sous QEMU
jusqu'au hello world en C#.

- [x] Prérequis du noyau : signaux, `membarrier`, régions recollées (`VM_PROTECT`, `VM_MAP`).
- [x] Prérequis de libposix : piles de threads sous une page de garde, `dl_iterate_phdr`,
      `.eh_frame_hdr`, `dladdr`, `/proc/self/exe`, `sched_getaffinity`, `statfs`, `getgrouplist`,
      `SIGRTMIN`.
- [x] **Hello world en C#** (31/10/2026, QEMU, [DOTNET.md](DOTNET.md)) : CoreCLR de .NET 10.0.14 et son
      JIT dans un hôte statique, notre GCC, bibliothèques C# de `linux-arm64`. `dotnetcheck` : 33
      réussis, 3 échecs (sockets, cryptographie, `Process.Start`).
- [x] OpenSSL lié à l'hôte, en lien direct (cryptographie, HTTPS) : `dotnetcheck` 38 réussis, 0 échec.
- [x] `epoll` sur `poll`, `Process.Start` sur `posix_spawn`.
- [ ] `SIGCHLD` du noyau pour `Process`, à la place de l'émulation.
- [ ] ICU à l'exécution, ou le mode invariant assumé.
- [x] **Assemblys `System.<Kit>`** (`System.UIKit`, `AudioKit`, `NetKit`, `GpuKit`, `MediaKit`) : le lien entre les applications C# et les kits, un par kit
      (`System.UIKit`, `System.AudioKit`, puis ceux à venir ; `System.NetKit` peut-être inutile, le
      réseau de .NET passant par libposix). Générés depuis les `.abi`, comme `tools/kits/abi2c.py` ;
      appels directs au kit, avec une API C# par-dessus.
- [x] `System.DocumentKit` (31/10/2026, QEMU) : le kit (`DK`) et `Doc`, en types C#.
- [ ] Une assembly pour manipuler les documents, dans ce même `System.DocumentKit` ou un `System.Interop.Office`.
- [x] **`System.Windows.Forms.dll` à nous** (31/10/2026, QEMU) : la même API que WinForms, chaque contrôle étant un widget
      d'UIKit (thème du bureau compris), sur `System.UIKit`. Pas de code de WinForms ni de Mono.
      `Form.Show` non modal, `OnPaint` et souris / clavier sur les vues d'UIKit (`ui_view`), `Graphics`,
      `AutoSize`.
- [x] Système à la WPF : les fichiers `.ui` (fenêtre, user control), lus par `UiLoader`.
- [x] Le compilateur C# (Roslyn) sous ARMPI, les `.cs` dans `pibuild`.
- [x] Runtime partagé dans `/dotnet` (le « shared framework », l'équivalent du GAC).
- [x] **Préchargement des DLL** : `file_pin` garde les pages de fichier partagées de `mmap`, applet
      Réglages > .NET avec une case par assembly. `hello` : de 23 à 12 s sous QEMU.
## IPC : canaux, files par service, droits d'accès, notifications (remplacé)

Plan abandonné le 30/10/2026 au profit du chantier perf ci-dessus ; décidé avec l'utilisateur (29/10/2026), après la comparaison avec QNX, Minix 3, seL4, Zircon et
Genode ; conception : [IPC.md](IPC.md).

- [ ] **Canaux par process** : `SVC_CALL` refuse un service non ouvert (masque de 64 bits dans le
      PCB, `SVC_MAX` = 64), `SVC_CLOSE`, `SVC_OPEN` réveillé par l'enregistrement.
- [ ] **Une file et un verrou par service** au lieu de `svc_lock` ; **quotas** de requêtes en
      cours par process (8 sans droits, 32 pour `SD0:/sys`).
- [ ] **Niveaux d'accès** : public, `SVF_SYSTEM` (`blk.*`, `usbd`...), `SVF_PRIVATE` +
      `SVC_GRANT` ; dans FatFS et RamFS, ops par chemin réservées à vfs.
- [ ] **Priorités** : requête la plus prioritaire d'abord, héritage de priorité jusqu'à la réponse.
- [ ] `SVC_REPLY_NEXT`, `SVC_POST` (avis des programmes), `SVC_CALL_EX` (échéance choisie).
- [ ] **Notifications** (`NOTIFY_*`, `SVC_BIND_NOTIFY`) : réveil entre process ; HIDManager et
      Elegant ne sondent plus leurs anneaux.
- [ ] **Mise à jour des fenêtres sans attente** : zone salie dans l'en-tête de la surface,
      événement « image affichée ».
- [ ] **Données privées des applications** (décidé le 29/10/2026, après les niveaux d'accès) :
      - **identité fiable** : le noyau joint à chaque requête, en plus du pid, le **chemin canonique
        de l'exécutable** relevé au lancement (celui des images partagées) ;
      - **règle de vfs** : `X.app/data/` n'est ouvert qu'aux process dont l'exécutable est
        `X.app/main` ; même règle pour les services système (`SD0:/sys/svc/<nom>.data/`) ;
      - **dossiers des programmes en lecture seule** hors de `data/` : seul l'installateur (futur
        gestionnaire de paquets) écrit dans `SD0:/apps` et `SD0:/sys` (sinon remplacer `main`
        donnerait son identité) ;
      - **plus d'accès direct aux secteurs** : `blk.*` réservé aux services de systèmes de fichiers ;
      - **aucune exception** pour ftpd, telnet, l'explorateur : ils passent par vfs avec leur
        identité (un mode administrateur viendra avec l'authentification).
      Usage : un **service des secrets** (connexions nommées de SqlKit, Wi-Fi, comptes) garde les
      identifiants en clair dans son dossier privé et ne les rend jamais aux applications (il ouvre
      la connexion et la passe, ou la relaie). **Pas de chiffrement pour l'instant** : une clé sur la
      même carte ne protège de rien ; plus tard, seulement avec une clé hors de la carte (mot de
      passe de session dérivé par Argon2 / scrypt, OTP du BCM2711, ou une puce de sécurité), contre
      le vol de la carte.

## Avant le Pi 5 : pilotes = services par fonction + modules matériels

Décidé avec l'utilisateur (29/10/2026), d'après la pratique des micronoyaux (QNX : `io-usb-otg`
\+ `devu-hcd-*.so` ; Fuchsia : *board driver* ; Genode : *platform driver*) : on garde **un
service par fonction** (isolation, relance par init2 / hwmanager), et la **partie propre au
matériel** devient un **module privé** chargé par ce seul service.

- [ ] **Module de pilote** : même format qu'un kit (code sans adresse absolue, table `.abi`),
      mais **privé** : lu par le service (vfs), mappé exécutable dans ce seul processus (nouvel
      appel du genre `module_map`), pas inscrit dans la table des kits, libéré à la fin du
      service ; ses variables lui sont propres. Un service relancé relit le module sur le disque
      (une recompilation est prise sans `kitreload`). Rangement : `SD0:/sys/drv/*.drv`.
- [ ] **hwmanager** = seul à connaître la carte (révision) : il choisit les modules
      (`ethernet drv=genet`, `wifi chip=cyw43 bus=sdio_pi4`, `usbd hcd=xhci_pi4,dwc2`) ; une table
      par carte. Une seule carte SD pour tous les Pi (tous les modules dessus).
- [ ] **usbd** : la pile USB dans le service ; contrôleurs en modules (`hcd_xhci_pi4.drv` :
      PCIe + VL805, `hcd_dwc2.drv` : USB-C ; plus tard `hcd_xhci_rp1.drv`). La logique xHCI
      commune peut être partagée entre VL805 et RP1, seul le démarrage du contrôleur change.
- [ ] **ethernet** : un service par interface (netif, anneau, lien, compteurs, relance,
      `no_ethernet`) ; modules `eth_genet.drv` (Pi 4), `eth_macb.drv` (Pi 5), `eth_ecm.drv`
      (Ethernet sur USB : remplace `usbnet`).
- [ ] **wifi** : le service (wifi.txt, réseaux connus, connexion auto, pays, `wlan0`) ; module de
      la puce (`wl_cyw43.drv` : firmware, ioctl, événements, WPA) ; module du bus
      (`sdio_pi4.drv`, plus tard `sdio_pi5.drv`).
- [ ] Ensuite **sd** (SDHCI en module) et **gt911** (I²C en module : BSC Pi 4, DesignWare Pi 5).
- Porter vers le Pi 5 = `arch/pi5` du noyau (PCIe, RP1) + quelques modules (`hcd_xhci_rp1`,
  `eth_macb`, `sdio_pi5`, I²C) + la table de la carte dans hwmanager ; les services ne changent pas.

## Terminal ANSI, `fork`, bash, git, ripgrep (vers Claude Code, étude du 10/11/2026)

But : les fondations pour faire tourner Claude Code (puis Node.js) et les programmes Unix interactifs. Étude, décisions
à prendre et essais d'acceptation : [TERMINAL-ET-FORK.md](TERMINAL-ET-FORK.md).

- [x] Décisions (10/11/2026, §7) : pty dans un service `vterm`, `fork` par copie immédiate d'abord, position des
      fichiers côté service, `rg` en C si possible, bit d'exécution par règles de chemin et index tenu par vfs (`chmod +x`/`-x`), un shell à l'ouverture du
      Terminal.
- [x] 1. Terminal VT (10/11/2026, QEMU ; **à essayer sur le PocketTerm**) : grille de cellules (256 × 128 au plus,
      1 000 lignes d'historique), analyseur VT500 (CSI, SGR 16/256/24 bits, écran alternatif, zone de défilement,
      séquences inconnues avalées), dessin des seules lignes changées, fenêtre redimensionnable, `key_encode` prêt pour
      `vterm` ; commande `ansitest`. En attendant `vterm` : LF vaut aussi CR, pas de réponse à `CSI 6n`/`CSI c`, pas de
      table de largeur (CJK, emoji sur une cellule).
- [ ] 2. Service `vterm` (pty) : `termios`, `isatty` exact, taille et SIGWINCH, Ctrl+C en SIGINT au groupe de premier plan,
      `EINTR` ; groupes de process minimaux ; Terminal et telnetd (mode caractère, NAWS) sur pty.
- [ ] 3. `fork` (copie immédiate) : avis aux services, entrées à plusieurs propriétaires (vfs, vterm, fatfs, ramfs, netd),
      `fork()` et `pthread_atfork` dans `libposix`, recyclage des ASID.
- [ ] 4. `exec` réel : même pid, même PCB.
- [ ] 5. bash 5.2 (sans contrôle des tâches) : bit d'exécution (règles de chemin et index dans vfs), `#!` dans `V_EXEC`, SIGALRM.
- [ ] 6. Contrôle des tâches : SIGTSTP, état arrêté, `WUNTRACED`, `tcsetpgrp`.
- [ ] 7. git (HTTPS par libcurl, réglages pour FAT et exFAT).
- [ ] 8. `rg` en C sur PCRE2 (d'abord : les options que Claude Code emploie) ; cible Rust en repli.
- [ ] 9. Copie sur écriture au `fork`, compteurs de références des pages.

## À glisser entre les chantiers

- [ ] **Dialogue de fichiers sur tous les volumes** (demandé le 29/10/2026) : `ui_file_open`,
      `ui_file_save`, `ui_folder_choose` permettent de choisir le volume (`SD0:`, `SD1:`, `RAM0:`,
      `USB0:`... : la liste montée par vfs), pas seulement de remonter dans `SD0:`.
- [ ] **Associations de fichiers** (demandé le 29/10/2026) :
      - le **manifeste** de chaque application (`app.txt`) dit les extensions qu'elle ouvre (par ex.
        `types = .webm .mp4 .mkv`) ;
      - l'**explorateur** ouvre un fichier avec l'application associée ; si **aucune ou plusieurs**
        applications le prennent en charge (et pas de choix retenu), un dialogue comme sous Windows
        propose celles qui le gèrent, avec les boutons **« Une seule fois »**, **« Toujours »** et
        **« Annuler »** ; une entrée **« Autre... »** affiche toutes les autres applications, avec une
        **barre de recherche** au-dessus de la liste ;
      - les choix « Toujours » sont gardés dans `SD0:/etc/fileassoc.txt` (section `[default]` :
        `.webm = mediaplayer`) ; quand on choisit une application qui ne déclarait pas
        l'extension, elle est **retenue comme la gérant** (section `[types]` : `editor = .log
        .conf`), ajoutée aux extensions de son manifeste quand on cherche qui ouvre un type ; le
        manifeste lui-même n'est pas modifié (dossiers des applications en lecture seule, et une
        mise à jour l'écraserait) ; un applet du panneau montre et efface ces choix ; menu
        « Ouvrir avec » dans l'explorateur.

- [ ] **Remplacement des kits** : un kit reconstruit sur le disque sert les programmes lancés
      ensuite ; l'ancien, marqué obsolète, reste à ceux qui l'utilisent, puis est libéré.
- [x] **Barre de menus globale** (les menus de l'application active, comme Onyx) : `ui_menu`,
      ops 10-12 d'Elegant, `WEV_MENU` (docs/DESIGN-ELEGANT.md 6.2).
- [ ] **Terminal virtuel / stdio** ; les commandes d'Onyx dans `SD0:/bin` : section 6 ter.
- [x] **Écran en 640x480** (28/10/2026) : lignes verticales. Cause : depuis le démarrage par les
      services, Elegant prenait l'écran en 800x600 pendant que le noyau passait en 640x480
      (`ecran=`). `demarrage_pret` attend maintenant les options et l'écran ; `fb_set` renvoie
      un message neuf ; le journal donne taille, pas et profondeur obtenus.
- [~] **Changer la résolution à chaud** (31/10/2026, DESIGN-ELEGANT.md 16.1) : l'applet Écran
      l'applique tout de suite ; `SYS_WS_MODE` (serveur seulement) change le framebuffer, Elegant
      (`elegant/screen.inc`) replace les fenêtres et envoie `WEV_SCREEN` : barre de menus, dock,
      fond, panneau se refont ; fenêtre agrandie : `WEV_RESIZE` ; `rdpd` envoie SCREEN, `vncd`
      coupe (le client revient à la nouvelle taille). Liste selon l'EDID de l'écran ; « Garder ? »
      15 s, sinon retour, minuteur dans Elegant (panneau mort ou écran noir : retour quand même) ;
      gardée dans `ecran=` de `SD0:/etc/options.txt`. Validé sous QEMU, DMA 2D compris
      (`dispdma_qemu`) ; **reste** : le valider sur le PocketTerm.
- [ ] **Facteur d'échelle (HiDPI)**, bien plus tard (inutile en 800x600) : un facteur entier
      (×1, ×2) comme Retina, les programmes en points, UIKit dessine polices, icônes et formes en
      pixels × facteur (net, pas un zoom de l'image). Écrans 4K.
- [ ] **Localisation** (28/10/2026) : l'applet « Clavier, pays et langue » du panneau de
      configuration règle la disposition du clavier, le **pays** (code pays du Wi-Fi, fuseau
      horaire pour NTP) et la **langue d'affichage**. Les applications qui le peuvent affichent
      leurs textes dans cette langue ; sans traduction, dans leur langue par défaut.
- [~] **mkinitramfs** (idée de l'utilisateur 29/10/2026 ; la commande est faite et validée sous QEMU :
      ses `.mod` sont identiques à ceux de la construction Windows) : une **commande** `SD0:/bin/mkinitramfs`
      qui lit `SD0:/etc/initramfs.txt` (un fichier à inclure par ligne : services de
      `SD0:/sys/svc`, kits de `SD0:/lib`, et `init.txt`), crée l'enveloppe de chacun (en-tête
      `ARMPIMOD`), écrit les `.mod` à la racine de `SD0:` et met à jour la ligne `initramfs` de
      `config.txt`. Une **application graphique** ne fait que lire et écrire `initramfs.txt` (cases
      à cocher) puis appeler la commande ; `init.txt` reste à éditer à la main. Facilite
      l'auto-hébergement : un service corrigé sous ARMPI repart au démarrage suivant, sans PC.
- [x] **Front end de FASMARM**, remplacé par PiBuild le 30/10/2026 (section 8 ter). Il était ainsi
      (comme celui de MenuetOS ; fait le 29/10/2026 sur QEMU :
      `src/apps/assembler`, nom affiché « FASMARM », catégorie Système) : champs **Source** et
      **Sortie** (déduite de la source : `x.asm` → `x.elf`, tant qu'on ne l'a pas modifiée), bouton
      **Assembler** (Entrée, Ctrl+B ; « Arrêter » pendant l'assemblage, Ctrl+C), journal défilant
      (molette, Page préc./suiv., barre), menu Fichier (Assembler, Arrêter, Effacer la sortie,
      Fermer). `SD0:/bin/fasmarm` lancé par `spawn_pipe` avec les chemins rendus absolus, **dans le
      dossier du source** (comme `build.ps1` : le noyau s'assemble), tube lu sans attendre depuis
      la boucle d'événements ; `wait` donne le code ; ligne de bilan verte ou rouge. L'éditeur
      l'ouvre sur son fichier (Fichier → Assembler, Ctrl+B). **Reste** : le bouton **Choisir…**
      à droite de Source (place laissée) quand le dialogue de fichiers existera ; les vrais champs
      texte d'UIKit (curseur au milieu, sélection) ; des chemins avec espaces (arguments de
      fasmarm séparés par des espaces) ; aller à la ligne d'une erreur dans l'éditeur.
- [ ] **`x86toa64` natif** (plus tard, après le réseau et le son) : porter `tools/x86toa64.py`
      (≈3 100 lignes de Python) en commande ARMPI, avec un ordre des tables fixe (sortie identique
      au script, octet pour octet) : ARMPI régénère `fasmarm.asm` depuis les sources x86 de fasm et
      FASMARM, puis s'assemble lui-même — auto-hébergement complet, sans PC.
- [ ] **Champ texte d'UIKit** : un vrai curseur, se déplacer au milieu (flèches, Début, Fin),
      cliquer au milieu pour y insérer.
- [x] **Formes du curseur** : Elegant connaît la flèche, ↔ et ↕ (redimensionner : colonnes, bords
      des fenêtres), la barre I (texte), la main (liens, boutons), l'attente ; une opération
      « forme du curseur au-dessus de ma fenêtre » et `ui_curseur(forme)` dans UIKit (le navigateur
      de fichiers l'utilise sur les séparateurs de colonnes, l'éditeur et les champs texte la barre I).
      Comme sur Onyx : déplacement, diagonales et interdit en plus, `Control.Cursor` en C#, et `rdpd`
      transmet chaque forme à OnyxRemote (message CURSOR, lue par `WS_OP_CURSOR_GET`).
- [~] **Changer la résolution à chaud**, comme sur Onyx : fait sous QEMU, à valider sur le PocketTerm
      (voir plus haut).
- [ ] **Dialogue modal de fichiers** (Ouvrir / Enregistrer) d'UIKit, pour toutes les applications.
- [~] **Recomposition d'Elegant : plusieurs zones sales et DMA 2D de l'écran** (29/10/2026,
      comme Onyx) : liste de 16 rectangles fusionnés quand l'union coûte peu ; présentation par
      un canal DMA du BCM2711 en mode 2D, asynchrone (DESIGN-ELEGANT.md 15). Validé sous QEMU
      (option `dispdma_qemu`) ; **reste** : le valider sur le PocketTerm (journal : « copie vers
      l'écran par DMA 2D asynchrone, canal 6 »), mesurer `bench` avec et sans `no_dispdma`.
- [x] **Toute la RAM** : fait le 29/10/2026 (memmap.asm, pmm.asm, zones basse et haute, voir plus haut).
- [ ] Application **Journal** (le journal du noyau en direct).
- [ ] Idée seulement : personnalités (serveur exec) — par exemple des binaires Linux.
