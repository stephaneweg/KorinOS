---
name: armpi-feuille-de-route
description: Feuille de route des chantiers ARMPI fixée par l'utilisateur le 28/10/2026 (ordre à respecter)
metadata:
  type: project
---

Ordre des chantiers demandé par l'utilisateur (28/10/2026) :
1. Découpler le code Pi 4 dans src/kernel/arch/pi4 + préparer Pi 5 d'après Onyx/Circle — **fait** (commits 658944d..57f8690 :
   arch/commun, arch/pi5 non testé, `-d ARCH=pi5`, docs/PI5.md).
2. Dispositions de clavier dans **/sd/res/keyb/** — **fait** (option keyb=NOM).
3. Bibliothèques partagées « kits » comme Onyx — **fait** (c36a954, 75c3e8a : appel 54 lib_open, /sd/lib/*.so,
   AppKit/UIKit/NetKit, une .abi par kit pour kit et programmes, docs/KITS.md).
4. GUI en espace utilisateur + bureau « Elegant » d'Onyx — plan docs/CHANTIER4-GUI.md. Décisions : noms d'Onyx
   (elegant = le SERVEUR, menubar, dock, control, theme.txt) ; PocketTerm = Elegant réduit, mais architecture
   extensible pour d'autres modes plus tard (PocketUI, console de jeu) : cœur de serveur commun + réglages par mode,
   shell= dans /sd/etc/system.ini ; ordre 4.1 noyau → 4.2 serveur user (bureau actuel) → 4.3 Elegant → 4.4 control.
   État au 28/10/2026 : 4.1 à 4.4 faits (4.4 = commits bbfdc53, 462b3e2 ; QEMU + selfhost) ; 4.4 à valider sur le PocketTerm.
**Avant le réseau** (demande du 28/10/2026) : analyser ce qui doit vraiment rester dans le noyau — l'utilisateur
vise un **noyau hybride** (pilotes, pile réseau... à sortir ou non du noyau). Analyse faite : docs/NOYAU-HYBRIDE.md
(services nommés, dev_map/dma_alloc, netd, sond ; puis STOCKAGE AUSSI en user : block devices par disque/partition, process FS par volume, VFS seulement à l'open, objet fichier bloc/flux, fichiers mappés sans copie, proc_create(image), modules de démarrage — choix de l'utilisateur, comme son OS x86 FreeBASIC ; USB reste au noyau d'abord). Binaires des services dans SD0:/sys/svc ; droits matériel = programmes de SD0:/sys ; montage = source (block device, chemin réseau + identifiants, ou ram:taille) + service FS + volume. Modules de démarrage : noyau + initramfs (fichiers .mod à la racine, bout à bout à 0x02000000, en-tête ARMPIMOD). AudioKit : anneau partagé avec sond. Étapes 1 (services nommés) et 2 (dev_map/dma_alloc/fw_call) faites le 28/10. Toute la RAM : faite le 29/10 (memmap.asm, pmm.asm, zones basse et haute, option no_highmem) ; reste à lire « RAM vue » dans le journal du Pi.
5. Activer USB/WiFi + programme de configuration WiFi comme dans Onyx.
6. Audio.
Après le réseau : volumes **FTPx:** (dossier FTP monté sous un numéro choisi, comme Onyx), en espace utilisateur.
Chemins : volumes `SD0:` `SD1:` `USB0:` `USB1:` (`SD:` / `USB:` = volume 0), plus de racine `/` (décision du 28/10).
À faire (demande) : répétition des touches maintenues (zones de texte UIKit) — prévue dans Elegant.
Après le réseau : service **rdpd** compatible avec le client **onyxremote** (bureau à distance d'Onyx).
4.4 précisé par l'utilisateur (28/10/2026) : programme de configuration (`control`) avec comme premiers applets
**dock**, **clavier et langue**, **résolution d'écran** (aujourd'hui fixée à l'assemblage du noyau : il faudra
la lire au démarrage dans /sd/etc), **thème**. Design : docs/DESIGN-ELEGANT.md (designer, style plat, Givre /
Onyx Noir) ; ombres à mesurer sur le Pi 4 (/sd/bin/mesure) — trop lourdes : contour opaque 1 px.
Demandé le 28/10/2026, place dans l'ordre à confirmer : système **stdio / terminal virtuel** avec les mêmes
commandes et utilitaires qu'Onyx ; les utilitaires en ligne de commande (fasmarm compris) dans **/sd/bin**.
Règle permanente : **toute configuration dans /sd/etc** (options.txt, demarrage.txt, wifi.txt, system.ini,
theme.txt... — déplacés le 28/10, commit 40f757b).
Ensuite seulement : adapter les apps à la taille d'écran (640x480 du PocketTerm).
Après le chantier stockage (demande du 28/10) : barre de menus **globale** comme Onyx (menubar affiche le menu de l'application active ; aujourd'hui seulement le nom de la fenêtre active).

Contraintes : tout doit rester assemblable par FASMARM Windows ET par FASMARM natif sous ARMPI (selfhost vérifie
kernel8.img et kernel_2712.img identiques) ; réponses en français ; options de désactivation de options.txt en **`no_xxx`**
(no_gpu, no_wifi, no_ethernet, no_rng200 — demande du 29/10/2026, plus `pas_xxx`) ; « keyb » aussi en anglais.

Demande du 29/10/2026 : porter les **commandes Unix d'Onyx** (`user/BinUtils` → `SD0:/bin`, en asm) **avant** ses applications (FEUILLE-DE-ROUTE 6 ter). Réseau (section 5) fusionné le 29/10 ; reste rdpd.
Architecture des pilotes décidée le 29/10/2026 (FEUILLE-DE-ROUTE « Avant le Pi 5 ») : **un service par fonction** (usbd, ethernet, wifi…) + la partie matérielle en **module privé** (format kit mais chargé par ce seul service, pas partagé ; `SD0:/sys/drv/*.drv`) ; hwmanager seul connaît la carte et choisit les modules. Pas de pilote unique regroupé.
Son (29/10/2026) : service **`sound`** (pas sond/son), agnostique du matériel comme netd ; pilotes audio (pwmaudio, hdmiaudio, i2smic, usbaudio) chacun dans son processus, enregistrés auprès de sound comme entrées/sorties ; réglages sound.txt, options `sound=kernel`, `no_xxx`.
**Why:** priorités explicites de l'utilisateur.
**How to apply:** enchaîner dans cet ordre ; voir [[armpi-projet]], [[armpi-pocketterm]].
Stockage étape C (28/10) : mode services (initramfs avec init : init.txt, sd, vfs, fat32, appkit.so) validé sous
QEMU ; mode noyau sans init. Demandes à faire : service exfat (SD1: lecture seule) ; **remplacement des kits**
(kit reconstruit : version en mémoire marquée obsolète, gardée par ses utilisateurs jusqu'au dernier puis
libérée, nouveaux programmes prennent celle du disque — pour tous les kits) ; élaguer le noyau.
Décision (28/10) : après le stockage, init (étape 1) lance **init2** depuis le disque ; init2 lance **hwmanager**
(service de découverte du matériel qui charge les pilotes-services : usbkeyboard, usbmouse, touch...) et
**Elegant**, puis services.txt ; étape 3 : quand Elegant est prêt, les applis de demarrage.txt ; et relance les services qui tombent. Le noyau ne lance plus rien.
Idée seulement (28/10, « pour l'instant on se contente de notre système ») : personnalités (ex. Linux) servies par
un service ; c'est un **serveur exec** (pas le noyau) qui reconnaît le binaire et choisit la personnalité ; le
noyau ne fait que rediriger les appels d'un process vers le service de sa personnalité.
Nommage (28/10) : les services de systèmes de fichiers se terminent par « FS » (FatFS = fatfs : FAT32 + exFAT
reconnus au montage, exFAT en lecture seule pour l'instant ; RamFS ; plus tard FtpFS...). Mode noyau à faire
disparaître : poser d'abord une branche/étiquette git de retour (« avant-elagage »).
Ordre décidé le 29/10/2026 pour WebKit : table des appels compactée + threads (TPIDR_EL0, join, priorités)
→ posix (libposix.a, src/lib/posix, lien statique, hors selfhost) + écart UIKit comblé (tous les widgets
d'Onyx ; l'éditeur passe sur le texte multiligne avec numéros de ligne et défilement) → **préchargement**
(pages de code/lecture seule partagées, copie sur écriture du segment RW par faute de page) et **toute la
mémoire** (pmm limité au 1er Go aujourd'hui ; zone basse <1 Go pour DMA/VideoCore/V3D) → portage de WebKit
(sources dans src/extra, hors dépôt ; op Elegant « surface attachée à une fenêtre »).
Mise à jour 29/10/2026 (soir) : fait — posix, UIKit, toute la RAM + préchargement, fatfs. Ordre suivant : formats
audio en asm dans AudioKit ET MediaKit (C précompilé) + Media Player asm + op surface d'Elegant, en parallèle →
WebKit → seulement ensuite les commandes Unix d'Onyx (6 ter).
Règle (29/10/2026) : les applications d'Onyx sont portées en C/C++ hors du selfhost (comme Jet, sur posix + en-têtes C
des kits), sauf mention contraire de l'utilisateur (en asm seulement : les émulateurs d'Onyx entièrement réécrits — NES, SNES, GB, GBA, N64, NDS ; jamais GameCube ni 3DS ;
synthé MIDI SF2 inspiré de MeltySynth ; la bibliothèque multimédia = Media Player d'Onyx en C++ hors selfhost). Pas de conversion gcc -S -> FASMARM.
Juste après WebKit (décidé 29/10/2026) : service V3D (hors noyau) + portage de gpucomp d'Onyx + composition GPU de Skia/WebKit comme Onyx ; c'est moi (l'assistant) qui m'y attelle dès WebKit fini.
