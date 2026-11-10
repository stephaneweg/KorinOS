# Les kits d'ARMPI : bibliothèques partagées

Même modèle qu'Onyx (`docs/06-KITS-GUIDE.md`, `docs/02-KERNEL-INTERNALS.md` § *Shared libraries*) :
un programme n'embarque pas de copie du code commun, il s'appuie sur des **kits**, une
bibliothèque partagée par domaine, chargée **une fois pour tout le système** et liée à chaque
programme qui l'utilise.

| Kit | Domaine | Sur la carte | Sources (`src/kits/<kit>/`) | Version |
|---|---|---|---|---|
| **AppKit** | ce qui fait tourner un programme : console, nombres, chaînes, UTF-8, chemins, tas, **client de fichiers** (`fichier_sys`, `f_*`, descripteurs 64–127 ; tubes, entrées / sorties standard, lancement des programmes, **chargement des kits** `f_kit_load` : [FICHIERS.md](FICHIERS.md)), **mutex et variables de condition** (`mtx_*`, `cnd_*`, ci-dessous) | `SD0:/lib/appkit.so` (et le module `appkit.so` d'initramfs) | `appkit.asm`, `appkit.abi` (+ `inc/lib.inc`, `appsA_util.inc`, `fileclient.inc`, `sync.inc`) | 50 |
| **UIKit** | l'interface : fenêtre (servie par Elegant), titre, événements, dessin, texte TrueType (rastériseur dans le kit) ; **importe AppKit** (polices, thème lus par son client de fichiers) | `SD0:/lib/uikit.so` | `uikit.asm` (+ `uikit_theme`, `uikit_frame`, `uikit_icons`, `uikit_widgets`, `uikit_list.inc`...), `uikit.abi`, `uikit.inc` | 201 |
| **GpioKit** | le connecteur à 40 broches ([BASIC.md](BASIC.md)) : modes, niveaux, PWM, fronts, I2C, SPI, par le service `gpio` ; simulateur en mémoire (SSD1306, BME280) sous QEMU ou sur demande. Préfixe `gp_` : `gk_` est à GpuKit | `SD0:/lib/gpiokit.so` | `gpiokit.asm`, `gpiokit.abi`, `gpiokit.inc` | 34 |
| **MediaKit** | la vidéo ([MEDIAKIT.md](MEDIAKIT.md)) : conteneurs WebM / Matroska, MP4, RIFF (bibliothèque av d'Onyx), VP8 / VP9 (libvpx), AV1 (dav1d), YUV → XRGB8888 à l'échelle, son par l'API par paquets d'AudioKit (Opus, Vorbis, MP3, FLAC : `av/av_audiokit.c`) ; **précompilé** (C, chaîne d'Onyx, `tools/mediakit`), hors du selfhost ; tas et pile des décodeurs par `VM_MAP`, fichiers par AppKit | `SD0:/lib/mediakit.so` | `prebuilt/mediakit.so`, `src/kits/mediakit/mediakit.abi`, `mediakit.inc` | 15 |
| **NetKit** | le réseau : sockets, DNS, ping, état, heure (service `net` : netd), Wi-Fi (service `wifi`), attente du DHCP, adresses IPv4 ; fonctions qui gardent tous les registres sauf x0 (comme les anciens appels 36–52 ; macro `netcall`) | `SD0:/lib/netkit.so` | `netkit.asm`, `netkit.abi`, `netkit.inc` | 33 |
| **AudioKit** | le son : flux de lecture (`ak_open` dans n'importe quel format PCM 8/16/24/32 bits, 1 à 8 canaux, 4 à 192 kHz : conversion et rééchantillonnage dans le process de l'application), capture (micro, boucle d'une sortie), outils (rééchantillonnage, mélange, gain, courbe de volume, en-tête WAV), mixer (`ak_state` : le tampon d'état de sound ; volumes des sorties et des applications, routage, sortie préférée) ; service `sound` (docs/SON.md) ; **formats** (décodage WAV, FLAC, MP3, Ogg Vorbis, Ogg Opus : `ak_dec_*`), **lecteur** en arrière-plan (`ak_play*`), **API par paquets** pour MediaKit (`ak_codec_*`), **encodage** WAV, MP3, Ogg Opus (`ak_enc_*`), **synthèse FM** (`ak_fm_*`) et **MIDI** (`ak_synth_*`, SoundFont 2 ou banque FM ; fichiers .mid et .fms par `ak_dec_*`), ci-dessous | `SD0:/lib/audiokit.so` | `audiokit.asm`, `audiokit.abi`, `audiokit.inc` (constantes publiques), `ak_codecs.inc` et `ak_*.inc` (formats, communs au banc d'essai du PC `tools/audiotest`) (+ `inc/sound.inc`) | 81 |
| **DocumentKit** | la plomberie des formats bureautiques, en entiers et sur des tampons : lecteur XML par événements et échappement, UTF-8, CP1252, Latin-1, longueurs Office et ODF, couleurs, dates civiles et séries Excel, CSV, reconnaissance d'un paquet, relations OOXML, enveloppes OOXML et ODF, ci-dessous ; sans kit importé ni tas ; le ZIP est à ArchiveKit | `SD0:/lib/documentkit.so` | `documentkit.asm`, `documentkit.abi`, `documentkit.inc` | 60 |
| **ArchiveKit** | compression et archives : CRC-32, Adler-32, deflate (niveaux 0 à 9, Huffman dynamique), inflate, zlib, gzip ; archives lues sur la carte ou en mémoire (ZIP, ZIP64, tar, tar.gz, gz), écrites (ZIP, tar, tar.gz, gz), ci-dessous ; **importe AppKit** (fichiers) ; pas de chiffrement | `SD0:/lib/archivekit.so` | `archivekit.asm`, `ar_deflate.inc`, `ar_inflate.inc`, `ar_read.inc`, `ar_write.inc`, `archivekit.abi`, `archivekit.inc` | 36 |
| **PrinterKit** | l'impression ([IMPRESSION.md](IMPRESSION.md)) : le dialogue, un travail et ses pages (`print_*`), confiés au service `print` (printd), PDF ou imprimante réseau IPP ; **importe AppKit, UIKit, ArchiveKit** | `SD0:/lib/printerkit.so` | `printerkit.asm`, `printerkit.abi`, `printerkit.inc`, `printd.inc`, `include/` | 35 |
| **BusKit** | le bus d'événements ([BUS.md](BUS.md)) : publier une valeur sous un nom de sujet, s'abonner à un nom ou à un préfixe, lire l'état courant ; les événements `WEV_BUS` arrivent dans la file d'`ui_event` (service `bus` : busd) | `SD0:/lib/buskit.so` | `buskit.asm`, `buskit.abi`, `buskit.inc` | 9 |
| **EmuKit** | le socle des émulateurs ([EMULATEURS.md](EMULATEURS.md)) : `emu_run` et un descripteur `EMD_*` ; fenêtre, ROM, thread de la machine, cadence, son, sauvegarde, vues ; **importe AppKit, UIKit, AudioKit** | `SD0:/lib/emukit.so` | `emukit.asm`, `emukit.abi`, `emukit.inc`, `include/emukit.h` | 6 |

AudioKit est le seul chemin des programmes vers le son ([SON.md](SON.md)).

### UIKit : l'association des fichiers (`uikit_assoc.inc`, version 209)

Comme `fileassoc.h` de SystemKit d'Onyx. Chaque application déclare dans son manifeste les extensions qu'elle ouvre
(`opens = rtf docx odt`, [ABI.md](ABI.md)) ; le choix par défaut d'une extension est dans `SD0:/etc/fileassoc.ini`
(« `ext = app` » par ligne, `#` : commentaire ; `ext =` sans rien : l'Éditeur), qui l'emporte. Sans ligne, la première
application dont le manifeste déclare l'extension ; sans application, l'Éditeur (le texte et les fichiers inconnus). Le
fichier de la carte (`skel/sd0/etc/fileassoc.ini`, `config` du paquet `armpi`) ne donne que les extensions que plusieurs
applications déclarent (`txt` à l'Éditeur, les images à la Visionneuse d'images, `imageview`). Fichiers, Archiver, Mail et le Lecteur PDF ouvrent
par ces fonctions ; Fichiers et Archiver ont « Ouvrir avec… » au clic droit, Mail sur une pièce jointe.

| Fonction | Arguments | Résultat |
|---|---|---|
| `ui_assoc_app` | chemin, tampon, taille | 1 : le `main` de l'application qui ouvre le fichier dans le tampon ; 0 : aucune, celui de l'Éditeur |
| `ui_assoc_open` | chemin | pid (< 0 : erreur) : un dossier s'ouvre dans Fichiers, un paquet `.app` et un programme ELF se lancent, un fichier dans son application |
| `ui_assoc_set` | extension (`docx` ou `.docx`), application (paquet sans `.app` ; 0 : retirer) | 1 : `fileassoc.ini` réécrit, ses autres lignes gardées |
| `ui_open_with` | chemin | pid (0 : Annuler) : le dialogue « Ouvrir avec » (applications proposées, celles qui déclarent l'extension et l'Éditeur, puis les autres ; la case « Toujours » appelle `ui_assoc_set`) |
| `ui_arg` | ligne d'arguments | la même, son premier argument sans guillemets : un chemin avec des espaces est passé entre guillemets (`\"` et `\`, comme `posix_spawn`) ; les programmes en assembleur qui prennent un chemin l'appellent au départ (Fichiers, Éditeur, Visionneuse) |

### UIKit : l'écran pris, la fenêtre réduite (`uikit_frame.inc`)

Pour Capture d'écran (`src/apps/screenshot`) : `ui_screen_grab (pixels, largeur, hauteur)` recopie l'écran tel qu'il est
montré, pointeur compris (l'op `WS_OP_SNAPSHOT` d'Elegant, celle de la commande `screenshot` et de `vncd` ; un tampon partagé
de la taille de l'écran, accordé au programme au premier appel) -> 1. `ui_minimize ()` réduit la fenêtre courante comme le
bouton de son cadre (`WS_OP_MINIMIZE`, 36) ; `ui_raise` la rend. Pour un écran sans pointeur, l'app pose d'abord une fenêtre
transparente sur tout l'écran (`WF_FRAMELESS | WF_TOP | WF_GRAB | WF_ALPHA`) et y prend le pointeur (`ui_pointer_lock`).

### UIKit : le presse-papiers (`uikit_clip.inc`, service `clipd`)

`clipd` (`src/sys/clipd`, lancé par `SD0:/etc/session`, protocole dans `inc/clip.inc`) garde les 10 dernières
copies en mémoire, chacune avec ses représentations (un format, ses octets), et un **curseur**, ce que Ctrl+V colle.
`ui_clip_set` / `ui_clip_get` passent par lui sans rien changer aux applications ; s'il ne tourne pas, ils repassent
par Elegant (un seul texte). Les données restent chez l'appelant : clipd les lit ou les écrit par `SVC_MAP_CALLER`.
Le widget `src/sys/clipboard`, ouvert par le bouton du panel, montre l'anneau et déplace le curseur.

| Fonction | Arguments | Résultat |
|---|---|---|
| `ui_clip_put` | format, données, octets, copie à compléter (0 : nouvelle) | id de la copie, 0 : refusée |
| `ui_clip_take` | formats `"rtf text  "` (le préféré d'abord), tampon (0 : taille seule), taille | x0 octets, 0 : rien ; x1 rang du format |
| `ui_clip_set_image`, `ui_clip_get_image` | pixels `0x00RRGGBB`, l, h ; tampon, taille | 1 ; x0 l, x1 h, x2 octets |
| `ui_clip_set_files`, `ui_clip_get_files` | chemins séparés par `
`, 1 si coupés ; tampon, taille | 1 ; x0 octets, x1 1 si coupés |
| `ui_clip_done_cut` | | les fichiers coupés ont été déplacés : la copie s'en va |

Formats : `text`, `image` (l et h sur 32 bits, puis les pixels), `files`, `files-cut`, `url`, `rtf`, `x-<app>`. Essai :
`SD0:/bin/cliptest`.

### AppKit : mutex et variables de condition (version 39, `sync.inc`)

Sur les futex du noyau (`SYS_FUTEX_WAIT`, `SYS_FUTEX_WAKE` : [ABI.md](ABI.md), threads). Un
**mutex** et une **condition** sont chacun un mot de **32 bits à 0**, aligné sur 4 octets
(`dw 0` en FASMARM), dans la mémoire du process (partagée par ses threads). Fonctions AAPCS
(x0-x15 non gardés ; ni x16-x18 ni les registres FP touchés).

| Fonction | Arguments | Résultat |
|---|---|---|
| `mtx_lock` | x0 = &mutex | le prend (attend s'il est pris) |
| `mtx_trylock` | x0 = &mutex | 0 (pris) ou `E_BUSY` (-16) |
| `mtx_unlock` | x0 = &mutex | le rend ; réveille un thread s'il en attend un |
| `cnd_wait` | x0 = &condition, x1 = &mutex (pris) | 0 : rend le mutex, attend un signal, reprend le mutex |
| `cnd_timedwait` | x0 = &condition, x1 = &mutex, x2 = ms (0 : sans fin) | 0, ou `E_TIMEDOUT` (-110) ; le mutex est repris dans les deux cas |
| `cnd_signal` | x0 = &condition | réveille un thread qui attend |
| `cnd_broadcast` | x0 = &condition | les réveille tous |

Mutex : 0 libre, 1 pris, 2 pris avec des threads en attente (sans concurrence, ni prise ni rendu
ne font d'appel système). Ni récursif ni lié à un propriétaire. Condition : un compteur de
signaux ; comme en POSIX, les réveils parasites sont possibles : attendre dans une boucle sur la
condition elle-même :

```asm
	adr	x0,verrou
	bl	mtx_lock
.att:	adr	x0,pret			; la condition protégée par le mutex
	ldr	w0,[x0]
	cbnz	w0,.ok
	adr	x0,cond
	adr	x1,verrou
	bl	cnd_wait
	b	.att
.ok:	...
	adr	x0,verrou
	bl	mtx_unlock
``` Gain mesuré : les 28 programmes passent de 184 Ko à 99 Ko, plus 9 Ko de kits
partagés (le rastériseur TrueType n'est plus recopié dans chaque application graphique).

Règles (celles d'Onyx) : le code réutilisable va dans le kit de son domaine, pas dans une copie
par application ; ce qu'un seul programme utilise reste à côté de lui.

### AppKit et NetKit : fork (AppKit version 50, NetKit version 33, `fileclient.inc`)

`SYS_FORK` ([ABI.md](ABI.md), `0x6A`) recopie la mémoire du process, variables des kits
comprises : le fils hérite des tables d'AppKit (descripteurs, tampons d'échange) et de NetKit.
Les services donnent au fils ce que tenait le père, aux mêmes numéros (avis `SVC_OP_FORK`) ; il
reste à remettre en état ce qui appartient au **pid** du père ou à ses autres threads.

| Fonction (rang) | Arguments | Résultat |
|---|---|---|
| `f_fork` (46) | — | pid du fils (père), 0 (fils), erreur. Avant `SYS_FORK` : la position de chaque fichier ouvert passe au service (`FS_POSITION`), le descripteur devient **partagé** (`FC_PARTAGE` : read, write et seek passent ensuite par la position du service, partagée par père et fils) ; verrous du kit tenus pendant l'appel. Dans le fils : `f_fork_child`. La macro `sys SYS_FORK` d'un programme lié à AppKit y mène |
| `f_fork_child` (47) | — | 0. Dans le fils, juste après un `SYS_FORK` fait par `svc 0` : verrous du kit (`fc_verrou`, `fc_sverrou`, un par tampon) rendus, descripteurs en cours d'ouverture oubliés, tampons d'échange du père démappés et oubliés (le service les avait accordés au pid du père : le fils en demande un au premier gros transfert). Sans `f_fork`, les positions ne sont pas partagées |
| `f_lire_a`, `f_ecrire_a` (48, 49) | x0 descripteur, x1 tampon, x2 octets, x3 position | octets : `pread` / `pwrite`, la position du descripteur ne bouge pas (tube : `E_SPIPE`) |
| `net_fork_child` (NetKit, 32) | — | 0. Dans le fils : le tampon du process (128 Ko, accordé au père par netd) démappé et oublié, zones rendues ; les sockets restent (netd les donne au fils) |

Les descripteurs et le cache des entrées / sorties standard (`fc_stdio`) restent justes dans le
fils : vfs lui donne les bouts de tubes du père, fatfs et ramfs ses fichiers. `fork()` de
libposix appelle `f_fork`, puis `net_fork_child` si NetKit est ouvert.

### AppKit : exec (AppKit version 53, `exec.inc`)

`SYS_EXEC` ([ABI.md](ABI.md), `0x6B`) garde le pid : les services tiennent toujours les fichiers,
bouts de tubes et de pty, sockets. Mais l'espace d'adressage est neuf, variables des kits
comprises : AppKit passe lui-même ses descripteurs au nouveau programme.

| Fonction (rang) | Arguments | Résultat |
|---|---|---|
| `f_exec` (50) | x0 chemin, x1 argv (argv[0] compris), x2 envp (0 : aucun), x3 &fds[3] (comme `f_spawn_stdio` ; 0 : entrées / sorties inchangées) | erreur ; sinon ne revient pas. Chemin résolu par vfs (`V_EXEC`, comme spawn), argv et environnement dans un objet partagé (`BLOB_TAG`, comme `f_spawn_env`), argv[1..] en ligne d'arguments. Ne ferme rien |
| `f_exec_file` (51) | x0 réponse de `V_EXEC`, x1 params `EXP_*` (72 o, `syscalls.inc`) | erreur ; sinon ne revient pas. Pour libposix, qui remplit la ligne, les entrées / sorties et ses marques ; AppKit ajoute le nom, son état et l'image (celle du chemin, sinon construite : `IMAGE_CREATE`, lecture, `IMAGE_SEAL` ; sans images : l'ELF lu dans un tampon partagé, `EXP_IMAGE`). Le fichier est fermé avant `SYS_EXEC`. `SD0:/sys` (`ATTR_SYS`) : `E_PERM` (exec n'accorde pas les droits) ; DLL .NET (`MZ`) : `E_NOEXEC` ; ce qui n'est pas un ELF : `E_NOEXEC`. Paramètres de l'appelant rendus intacts en cas d'erreur |
| `f_exec_reprise` (52) | — | (tous les registres gardés). Reprend l'état d'avant un exec ; fait seul au premier usage d'un descripteur (`fc_reserver`, `fc_desc_de`, `f_fork`) ; libposix l'appelle au démarrage, avant de lire les marques |

État d'AppKit : les descripteurs 64..127 ouverts (fichiers, répertoires, bouts de tubes ; pas ceux
des entrées / sorties standard, que le nouveau programme redemande à vfs) vont dans un objet
partagé d'une page marqué `EXEC_FC_TAG | 1`. Si `proc_flags` dit `PROC_EXEC`, le nouveau programme
les remet **aux mêmes numéros**, puis ferme l'objet. La position suit (`FCD_POS`, ou gardée par le
service après un fork) ; les tampons d'échange sont redemandés (le service rend le même à ce pid).
NetKit ne passe rien : ses sockets restent à netd jusqu'à la fin du process (libposix les ferme
avant l'exec).

### DocumentKit : la plomberie des formats bureautiques (`documentkit.inc`)

Ce que Letter, Sheet, Present et Cardfile ont en commun : le lot A de l'analyse d'Onyx (`IDEAS.md`,
« DocumentKit »), du code dupliqué dans leurs `xml.h`, `docx.h`, `odt.h`, `xlsx.h`, `ods.h`, `pptx.h`, `odp.h`.

**Pourquoi de l'assembleur, et pourquoi pas plus.** Tout le contenu est entier, sans état caché et sans
structure partagée avec les apps : l'assembleur n'y coûte rien en interface, il s'assemble dans le selfhost et
il n'importe rien (ni AppKit ni tas : `dk_xml_open` prend une des 64 cases du process). Restent dans les apps :
* `dk_doc` / `dk_book` (lot C) : un seul client chacun, flottants, grosses structures C++ (`wr::Doc`, `ss::Book`)
  qui gèleraient l'ABI ;
* `sheet/numfmt.h` et `sheet/input.h` (lot B) : seul Sheet s'en sert, le tout en `double` ; le partagé, les dates
  civiles et la série Excel avec son 29 février 1900, est ici (`date_serial`, `serial_date`, `civil_from_days` de
  `core.h`), sans flottant ; la partie décimale d'un numéro de série reste à l'appelant ;
* le ZIP : à ArchiveKit, ici seulement la lecture des noms du répertoire central pour `dk_doc_kind`. DocumentKit
  n'importe pas ArchiveKit : l'appelant extrait la partie et passe le tampon.

**Conventions.** Fonctions AAPCS64, entiers et pointeurs, poignées opaques (un petit entier, pas une adresse),
erreurs négatives (`DKE_NOTFOUND` -2, `DKE_BADF` -9, `DKE_NOMEM` -12, `DKE_INVAL` -22, `DKE_CORRUPT` -74).
**Sortie texte comme `snprintf`** : `out`, `cap` en fin d'arguments, résultat = longueur **complète** sans le 0 final,
au plus `cap`-1 octets écrits puis un 0 ; `cap` = 0 ne donne que la longueur, pour dimensionner. En C : `#include
<documentkit_abi.h>`, `documentkit_kit_open ()`, `-ldocumentkit`.

**Lecteur XML** (comportement de `XmlReader` d'Onyx, repris y compris sur le XML mal formé) :

```c
long r = dk_xml_open ((long) buf, n);
for (long e; (e = dk_xml_next (r)) != DKX_EOF; )
	if (e == DKX_START && dk_xml_is (r, (long) "p"))		/* w:p, text:p, a:p */
		len = dk_xml_attr (r, (long) "val", (long) v, sizeof v);	/* < 0 : absent */
	else if (e == DKX_TEXT)
		len = dk_xml_text (r, (long) t, sizeof t);
dk_xml_close (r);
```

| Fonction | Arguments | Résultat |
|---|---|---|
| `dk_xml_open` | tampon, octets | lecteur ; le tampon reste à l'appelant jusqu'à `dk_xml_close` ; `DKE_NOMEM` : 64 ouverts dans le process |
| `dk_xml_next`, `dk_xml_event` | lecteur | `DKX_EOF` 0, `DKX_START` 1, `DKX_END` 2, `DKX_TEXT` 3 ; `<a/>` : START puis END ; commentaires, déclarations, DOCTYPE sautés ; CDATA : un TEXT à lui, brut |
| `dk_xml_is`, `dk_xml_isq` | lecteur, nom | 1 : nom sans son préfixe (`w:p` = `p`), ou exact (`text:p`) ; valables sur START et END |
| `dk_xml_empty` | lecteur | 1 : ce START vient d'un `<a/>` |
| `dk_xml_name`, `dk_xml_text` | lecteur, out, cap | longueur ; texte décodé en UTF-8 (`&lt;` et compagnie, références numériques ; entité inconnue laissée telle quelle ; `&#0;` retiré ; référence invalide : U+FFFD) |
| `dk_xml_attr` | lecteur, nom, out, cap | longueur décodée, `DKE_NOTFOUND` ; nom avec préfixe : exact ; sans préfixe : aussi sous tout préfixe sauf `xmlns` |
| `dk_xml_attr_int`, `dk_xml_attr_bool`, `dk_xml_attr_on` | lecteur, nom, défaut (pas pour `_on`) | entier ; 1 si commence par `1`, `t`, `T` ; valeur on/off de Word (absent : 1 ; nom 0 : `val`) |
| `dk_xml_skip` | lecteur | 0 ; après un START, jusqu'à son END compris |
| `dk_xml_pos` | lecteur | octets lus |
| `dk_xml_esc`, `dk_xml_esc_attr` | texte, octets (< 0 : jusqu'au 0), out, cap | longueur ; `& < > "` échappés, contrôles retirés ; `_attr` : tab, LF, CR en `&#n;` pour survivre à la normalisation des attributs |

**UTF-8, CP1252, Latin-1** : `dk_utf8_encode`, `dk_utf8_decode` (strict : ni suite trop longue, ni surrogate, ni au-delà
de U+10FFFF), `dk_utf8_valid`, `dk_utf8_count`, `dk_utf8_to_cp`, `dk_cp_to_utf8`, `dk_latin1_to_utf8`, `dk_cp1252_to_utf8`,
`dk_utf8_to_latin1`, `dk_utf8_to_cp1252` (remplaçant en dernier argument, 0 : retiré ; un octet invalide est pris pour du
Latin-1), `dk_cp1252_cp`, `dk_cp_cp1252`. Les 5 trous de CP1252 valent eux-mêmes (WHATWG).

**Longueurs** : `DKU_EMU`, `DKU_TWIP`, `DKU_HMM` (1/100 mm), `DKU_PT`, `DKU_IN`, `DKU_CM`, `DKU_MM`, `DKU_PC`, `DKU_PX`.
`dk_len_parse (texte, unité voulue, unité d'un nombre nu, défaut)` (« 2.54cm », « 3,5mm » ; pas de nombre ou un `%` :
défaut ; suffixe inconnu : nombre nu), `dk_len_conv (valeur, de, vers)`, `dk_len_write (valeur, unité, unité écrite,
décimales, out, cap)`. Conversions exactes, arrondies au plus près, en passant par l'EMU (`len_twips` d'Onyx comptait
567 twips au centimètre : 10cm y valaient 5670, ici 5669). **Couleurs** : `dk_color_parse (texte, défaut)` (`#RRGGBB`,
`RRGGBB`, `AARRGGBB` ; `auto` : défaut), `dk_color_write (couleur, DKC_HASH | DKC_LOWER | DKC_ARGB, out)`.

**Dates** : `dk_days_from_civil`, `dk_civil_from_days`, `dk_weekday` (0 dimanche), jours depuis le 1970-01-01, grégorien
proleptique ; mois et jours hors bornes reportés comme `DATE`. `dk_excel_from_civil (a, m, j, 1904 ?)` et
`dk_civil_from_excel (série, 1904 ?, &a, &m, &j)` : la série Excel avec son 29 février 1900 fictif (60) et le 0 janvier
1900 (0), années 0 à 1899 comptées depuis 1900.

**CSV** : `dk_csv_open (tampon, octets, séparateur)` (BOM sauté ; 0 : devine parmi `,` `;` tab sur 20 lignes, comme Sheet),
`dk_csv_next` -> 0 à la fin, sinon `DKV_FIELD` + `DKV_EOL` (dernier de l'enregistrement) + `DKV_QUOTED` ; `dk_csv_text`,
`dk_csv_sep`, `dk_csv_close`, `dk_csv_esc (texte, octets, séparateur, out, cap)`. Un guillemet n'ouvre qu'en début de champ
(RFC 4180) ; un champ non fermé va jusqu'à la fin.

**Paquets** :

| Fonction | Résultat |
|---|---|
| `dk_doc_kind (fichier, octets)` | `DKK_DOCX`, `_XLSX`, `_PPTX` (noms du répertoire central + `[Content_Types].xml`), `_ODT`, `_ODS`, `_ODP` (`mimetype` en tête, non compressé), `_RTF`, `_OLE` (ancien Office, non pris en charge), `_ZIP` (autre ZIP, ou `mimetype` compressé : l'extraire et `dk_doc_kind_mime`), `_UNKNOWN` |
| `dk_doc_kind_mime (type, octets)`, `dk_doc_mime (genre)` | `DKK_*` d'un type MIME (`-template` accepté, blancs de fin ignorés) ; type MIME d'un genre (le contenu de `mimetype` à écrire) |
| `dk_part_path (base, cible, out, cap)`, `dk_rels_path (partie, out, cap)` | `word/document.xml` + `../x.png` ; `word/_rels/document.xml.rels` |
| `dk_rel_count`, `dk_rel_get (rels, octets, rang, DKR_ID / _TYPE / _TARGET / _MODE, out, cap)` | parcours d'un `.rels` ; `DKR_TYPE` : dernier composant (`slideMaster`) ; cible brute |
| `dk_rel_target (rels, octets, id, out, cap)`, `dk_rel_by_type (rels, octets, type, out, cap)` | cible d'un `rId` ; première cible interne d'un type (sans casse) |
| `dk_env_text (DKS_*)` | les textes fixes des enveloppes : déclaration XML, ouverture et fin de `Relationships`, de `Types` (avec les `Default` de `rels` et `xml`), du manifeste ODF |
| `dk_rel_write (n, type, cible, externe, out, cap)` | `<Relationship Id="rIdn" .../>` ; un type sans `:` reçoit le préfixe officeDocument |
| `dk_opc_default (ext, type, out, cap)`, `dk_opc_override (partie, type, out, cap)` | lignes de `[Content_Types].xml` |
| `dk_odf_entry (chemin, type, out, cap)` | `<manifest:file-entry .../>` (`/` : avec `manifest:version`) |

`dk_hash (tampon, octets)` : le FNV-1a de `doc_hash` (`docguard.h` d'Onyx), pour « modifié depuis l'enregistrement ? ».

**Essai** : `tools/gcc/test/dktest.c` (`dkvec.py` pour les vecteurs, `dktest.sh` pour construire). Vraies parties DOCX,
XLSX, PPTX, ODP contre expat ; XML abîmé contre le lecteur C++ d'Onyx compilé sur le PC (`dkref.cpp`) ; dates, longueurs,
UTF-8, CP1252 contre Python ; toute entrée finit contre une page `PROT_NONE`.

### ArchiveKit : compression et archives (version 36, `archivekit.inc`)

**Pourquoi en assembleur** : le kit est du calcul pur sur des octets, de petite taille (29 Ko), sans
dépendance ; en FASMARM il entre dans le selfhost et se reconstruit sous ARMPI sans GCC, comme
AudioKit, et sert aux outils écrits en assembleur (`arc`) autant qu'aux applications C. Le natif
produit les mêmes octets que FASMARM Windows (vérifié sous QEMU). Les boucles qui comptent tiennent
en quelques registres et les instructions CRC d'ARMv8 sont directes.

AAPCS64, seulement des entiers, des pointeurs et des **poignées opaques** : appelable depuis le C
(`#include <archivekit_abi.h>`, `-larchivekit`, chaque argument en `long`). Erreurs négatives
(`AR_E_CORRUPT` -74, `AR_E_CRC` -84, `AR_E_FORMAT` -95, `AR_E_SPACE` -28 : sortie trop petite,
`AR_E_METHOD` -38, `AR_E_CRYPT` -13, `AR_E_INVAL` -22, `AR_E_NOMEM` -12, `AR_E_IO` -5, `AR_E_FBIG`
-27, ou celle d'AppKit) ; `ar_strerror` les dit en français. Aucune variable globale hors de la
liaison à AppKit : deux threads peuvent travailler sur deux poignées (une archive ne sert qu'à un
thread à la fois). Un fichier abîmé rend une erreur : tailles et positions sont bornées par la
source, le contenu est vérifié par son CRC.

**Sommes et bloc de mémoire** (enveloppe `AR_RAW` 0, `AR_ZLIB` 1, `AR_GZIP` 2 ; `AR_AUTO` 3 en
lecture ; niveau 0..9, -1 : 6) :

| Fonction | Arguments | Résultat |
|---|---|---|
| `ar_crc32`, `ar_adler32` | x0 = somme (0, 1 au début, puis le dernier résultat), x1 = données, x2 = octets | somme |
| `ar_deflate_bound` | x0 = octets, x1 = enveloppe | taille de sortie toujours suffisante |
| `ar_deflate` | x0 = source, x1 = octets, x2 = sortie, x3 = capacité, x4 = enveloppe, x5 = niveau | octets écrits |
| `ar_inflate` | x0 = source, x1 = octets, x2 = sortie, x3 = capacité, x4 = enveloppe | octets écrits, sommes vérifiées |
| `ar_inflate_size` | x0 = source, x1 = octets, x2 = enveloppe | taille décompressée, tout vérifié |
| `ar_deflate_new` | x0 = source, x1 = octets, x2 = enveloppe, x3 = niveau | **tampon** du kit |
| `ar_inflate_new` | x0 = source, x1 = octets, x2 = enveloppe, x3 = taille prévue (0 : inconnue) | **tampon** du kit |
| `ar_data`, `ar_bytes`, `ar_free` | x0 = tampon ou écrivain | pointeur, octets (un 0 suit les données), 0 |

**Lire une archive** : une seule interface pour la carte et la mémoire, le format est reconnu aux
premiers octets (ZIP avec ou sans données devant, ZIP64, descripteurs de données, tar ustar, GNU,
pax, tar.gz, gz à plusieurs membres ; noms CP437 convertis en UTF-8). Docx, xlsx, pptx, odt, ods,
odp : `ar_open_mem`.

| Fonction | Arguments | Résultat |
|---|---|---|
| `ar_open`, `ar_open_mem` | x0 = chemin ; x0 = tampon, x1 = octets (reste à l'appelant jusqu'à `ar_close`) | archive |
| `ar_close` | x0 = archive | 0 |
| `ar_format`, `ar_count` | x0 = archive | `AR_FMT_ZIP` 1, `AR_FMT_TAR` 2, `AR_FMT_TGZ` 3, `AR_FMT_GZIP` 4 ; entrées |
| `ar_entry` | x0 = archive, x1 = indice, x2 = `long info[8]` | 0 ; `ARE_NAME`, `ARE_SIZE`, `ARE_PACKED`, `ARE_TIME` (secondes UTC), `ARE_DIR`, `ARE_METHOD`, `ARE_CRC`, `ARE_FLAGS` (décalages en octets) |
| `ar_name`, `ar_entry_size` | x0 = archive, x1 = indice | nom (UTF-8, `/`, valable jusqu'à `ar_close`) ; taille |
| `ar_find` | x0 = archive, x1 = nom | indice : nom exact d'abord, puis sans casse ASCII ; `E_NOENT` |
| `ar_read` | x0 = archive, x1 = indice, x2 = tampon, x3 = capacité | octets ; CRC vérifié |
| `ar_load` | x0 = archive, x1 = indice | tampon du kit (un 0 suit : un XML s'utilise tel quel) |
| `ar_extract` | x0 = archive, x1 = indice, x2 = chemin | octets écrits ; dossiers créés, date posée, rien ne reste en cas d'erreur |
| `ar_extract_all` | x0 = archive, x1 = dossier | fichiers écrits ; les noms qui remontent par `..` sont sautés |
| `ar_test` | x0 = archive, x1 = indice | 0 : l'entrée se lit, sa somme est bonne |
| `ar_probe` | x0 = chemin | `AR_FMT_*` sans tout lire |

```c
long a = ar_open_mem ((long) docx, n);
long i = ar_find (a, (long) "word/document.xml");
long b = ar_load (a, i);                     /* ar_data (b), ar_bytes (b) */
ar_free (b); ar_close (a);
```

**Créer une archive** (`AR_FMT_ZIP`, `AR_FMT_TAR`, `AR_FMT_TGZ`, `AR_FMT_GZIP` : une seule entrée) :

| Fonction | Arguments | Résultat |
|---|---|---|
| `ar_create`, `ar_create_mem` | x0 = chemin, x1 = format ; x0 = format | écrivain |
| `ar_add_mem` | x0 = écrivain, x1 = nom, x2 = données, x3 = octets, x4 = niveau | 0 |
| `ar_add_file` | x0 = écrivain, x1 = nom (0 : celui du fichier), x2 = chemin, x3 = niveau | 0 ; un dossier : lui et tout ce qu'il contient |
| `ar_add_dir` | x0 = écrivain, x1 = nom | 0 |
| `ar_set_time` | x0 = écrivain, x1 = secondes UTC (0 : l'heure courante) | 0 ; pour les entrées suivantes |
| `ar_finish` | x0 = écrivain | octets de l'archive ; le fichier est fermé. En mémoire : `ar_data`, `ar_bytes` |
| `ar_free` | x0 = écrivain | 0 (non terminé : abandonné) |
| `ar_formats` | x0 = tableau de `ARFI_SIZE` octets par format (0 : compter), x1 = places | nombre de formats |
| `ar_format_name`, `ar_strerror` | x0 = format ; x0 = erreur | chaîne |

ZIP écrit : ni extra ni descripteur de données ; au niveau 0, le `mimetype` d'ODF ajouté en premier
est à l'octet 30, non compressé. Une entrée de mémoire que deflate ne réduit pas est stockée. Pas
de ZIP64 à l'écriture (`AR_E_FBIG` à 4 Gio) ; tar écrit : ustar, préfixe, noms longs GNU.

**Limites assumées.** ZipCrypto et AES ne sont pas pris en charge : une entrée chiffrée se liste,
sa lecture rend `AR_E_CRYPT`. Seules les méthodes stocké et deflate se lisent (`AR_E_METHOD` pour
Deflate64, bzip2, LZMA). Un tar.gz est décompressé en mémoire à l'ouverture : sa taille est bornée
par la RAM (un .gz d'un seul fichier est décodé à la volée). Pas de modification d'une archive
existante. Une date ZIP est locale et sans fuseau : elle est lue comme UTC.

**Compression** : chaînes de hachage sur 3 octets, positions absolues sur 32 bits, recherche
paresseuse à partir du niveau 4, gourmande en dessous, réglages de zlib (taille à 0,1 % de zlib aux
niveaux 1, 6, 9). Blocs d'au plus 32767 jetons ; Huffman dynamique, codes fixes ou stockage, le
moins cher : des données incompressibles grossissent de 5 octets par bloc. Longueurs de codes par
Moffat-Katajainen, limitées par correction de Kraft. CRC-32 par les instructions `crc32x`/`crc32b`
d'ARMv8 (Cortex-A72 et A76 les ont). Inflate : table de 10 bits (distances : 9), code canonique
au-delà. États de 0,7 et 0,35 Mo de `VM_MAP`, seules les pages touchées comptent.

Débits sous QEMU (émulation, sur un PC ; le Pi n'a pas été mesuré), 16 Mo :

| Données | Niveau | Taille | deflate | inflate |
|---|---|---|---|---|
| texte XML répétitif | 1 | 23,3 % | 38 Mo/s | 115 Mo/s |
| | 6 | 16,4 % | 6,9 Mo/s | 148 Mo/s |
| | 9 | 15,7 % | 2,0 Mo/s | 134 Mo/s |
| mélange | 6 | 37,9 % | 15 Mo/s | 135 Mo/s |
| hasard | 6 | 100,0 % | 8,1 Mo/s | 248 Mo/s |
| tout | 0 | 100,0 % | 185-226 Mo/s | 214-240 Mo/s |

CRC-32 : 350 Mo/s, Adler-32 : 320 Mo/s. Essai : `tools/gcc/test/artest.py` (`build`, `prep`, `run`,
`check`, `arc`) : vecteurs, interopérabilité avec zipfile, tarfile, gzip, zlib dans les deux sens,
fichiers tronqués ou abîmés. Outil : `arc` ([OUTILS.md](OUTILS.md)).

### AudioKit : formats audio, lecteur, API par paquets, encodage (version 53, `audiokit.inc`)

Écrits en assembleur d'après les normes (WAV ; FLAC : RFC 9639 ; MP3 : ISO/IEC 11172-3 et
13818-3 couche III ; Ogg : RFC 3533 ; Vorbis I ; Opus : RFC 6716, RFC 7845), dans le process de
l'application ([SON.md](SON.md), étape 9). Fonctions AAPCS64 (x0-x7 arguments, x0 résultat,
x19-x28 et d8-d15 préservés), seulement des entiers, des pointeurs et des **poignées opaques**
(adresses de blocs du kit) : appelables depuis le C. Erreurs : valeurs négatives (`AKE_NOMEM`
-12, `AKE_INVAL` -22, `AKE_IO` -5, `AKE_CORRUPT` -74, `AKE_FORMAT` -95, ou celle d'AppKit).

Formats pris en charge :

| Format | Décodage | Encodage |
|---|---|---|
| WAV | PCM 8 (non signé), 16, 24, 32 bits, flottant 32 bits, `WAVE_FORMAT_EXTENSIBLE` ; LIST/INFO | PCM 16 bits |
| FLAC | natif (RFC 9639) : 4 à 24 bits, 1 à 8 canaux, toutes les sous-trames, SEEKTABLE ; au bit près | — |
| MP3 | MPEG-1, 2, 2.5 couche III (réservoir, blocs courts/mixtes, M/S, intensité), Xing/Info, ID3v1/v2 | CBR 32-320 kbit/s (MPEG-1), 8-160 (2 et 2.5), M/S, réservoir ; blocs longs seulement |
| Ogg Vorbis | Vorbis I (plancher 1 ; le plancher 0, abandonné en 2000, est refusé) | — |
| Ogg Opus | CELT, SILK, hybride (RFC 6716), familles 0 et 1 (RFC 7845) ; pas de dissimulation de pertes | CELT seul, CBR 16-256 kbit/s, trames de 20 ms, toute fréquence rééchantillonnée à 48 kHz |

Fréquence et canaux de la source ; Opus rend toujours du 48 kHz. Écarts mesurés sur le banc du
PC contre les décodeurs C d'Onyx (minimp3, stb_vorbis, dr_flac, libopus) : FLAC et SILK au bit
près, MP3 / Vorbis / CELT à 1 LSB (s16) ; détails dans [SON.md](SON.md), étape 9.

**Décodage d'un fichier ou d'un tampon** (le format est reconnu aux premiers octets ; étiquettes
ID3v1/v2, commentaires Vorbis, LIST/INFO) :

| Fonction | Arguments | Résultat |
|---|---|---|
| `ak_dec_open` | x0 = chemin | décodeur (lecture par AppKit, tampon de 256 Ko) |
| `ak_dec_open_mem` | x0 = tampon, x1 = octets | décodeur (le tampon reste à l'appelant jusqu'à `ak_dec_close`) |
| `ak_dec_info` | x0 = décodeur, x1 = tampon de `AKI_SIZE` (64) octets | 0 ; `AKI_FORMAT` (`AKF_*`), `AKI_RATE`, `AKI_CHANNELS`, `AKI_FRAMES` (-1 : inconnu), `AKI_DURATION` (ms), `AKI_BITRATE`, `AKI_POSITION`, `AKI_BITS` |
| `ak_dec_read` | x0 = décodeur, x1 = tampon, x2 = trames | trames rendues, **s16 entrelacé** aux canaux de la source ; 0 : fin |
| `ak_dec_seek` | x0 = décodeur, x1 = ms | 0 ; position exacte (FLAC, WAV, Ogg : bissection des pages puis calibrage sur la granule) ; MP3 : à la trame près en débit constant, d'après la table Xing sinon (approchée) |
| `ak_dec_tell` | x0 = décodeur | position (trames) |
| `ak_dec_tag` | x0 = décodeur, x1 = `AKT_TITLE` / `AKT_ARTIST` / `AKT_ALBUM`, x2 = tampon, x3 = taille | octets (UTF-8, 0 final écrit) ; 0 : absente |
| `ak_dec_close` | x0 = décodeur | 0 |

**Lecteur** : `ak_play(chemin, sortie)` ouvre le fichier et un flux de sound à son format, et
lance **un thread** du process qui décode et écrit par blocs de 1024 trames ; les commandes sont
lues par ce thread à chaque bloc (~23 ms).

| Fonction | Arguments | Résultat |
|---|---|---|
| `ak_play` | x0 = chemin, x1 = sortie (0 : celle du process) | lecteur ; la lecture commence |
| `ak_play_pause` | x0 = lecteur, x1 = 1 pause / 0 reprise | 0 |
| `ak_play_seek` | x0 = lecteur, x1 = ms | 0 |
| `ak_play_pos` | x0 = lecteur | position **entendue** (ms : décodée moins l'avance du flux) |
| `ak_play_state` | x0 = lecteur | `AKP_PLAYING` 1, `AKP_PAUSED` 2, `AKP_ENDED` 3 (tout est joué), `AKP_ERROR` 4 |
| `ak_play_volume` | x0 = lecteur, x1 = 0..256 | 0 |
| `ak_play_decoder` | x0 = lecteur | son décodeur, pour `ak_dec_info` / `ak_dec_tag` (pas `ak_dec_read`) |
| `ak_play_stop` | x0 = lecteur | 0 ; attend la fin du thread, ferme flux et fichier |

**API par paquets** (pour **MediaKit** : son démultiplexeur MKV/WebM/MP4 sépare les pistes et
donne les paquets audio ; aucun accès fichier dans AudioKit). En C :

```c
typedef struct ak_codec *ak_codec;          /* poignée opaque (< 0 : erreur) */
enum { AKF_WAV = 1, AKF_FLAC = 2, AKF_MP3 = 3, AKF_VORBIS = 4, AKF_OPUS = 5 };
enum { AKC_S16 = 0, AKC_F32 = 1 };
ak_codec ak_codec_open(long format, long rate, long channels,
                       const void *extradata, long extrasize);
long ak_codec_decode(ak_codec c, const void *packet, long size,
                     void *out, long max_frames, long flags);
long ak_codec_reset(ak_codec c);             /* après une recherche : oublie l'historique */
long ak_codec_info(ak_codec c, long info[8]); /* comme ak_dec_info (AKI_*) */
long ak_codec_close(ak_codec c);
```

* `ak_codec_open` : `extradata` = le `CodecPrivate` de Matroska / l'`esds`-`dOps`-`dfLa` de MP4 :
  **Vorbis** : les trois en-têtes (identification, commentaires, livres de codes) en laçage Xiph
  (`02`, tailles des deux premiers en octets 255 + reste, puis les trois paquets) ; **Opus** :
  `OpusHead` (19 octets ou plus ; famille 0, ou 1 jusqu'à 8 canaux) ; **FLAC** : `fLaC` suivi des
  blocs de métadonnées, ou un bloc STREAMINFO avec son en-tête de 4 octets, ou les 34 octets de
  STREAMINFO seuls ; **MP3** : aucune (0, 0). `rate`, `channels` : ceux du conteneur (repris de
  l'en-tête du codec quand il les donne).
* `ak_codec_decode` : un paquet (une trame MP3 avec son en-tête, une trame FLAC, un paquet Vorbis
  ou Opus) → `out` en **entrelacé** (s16, ou flottant -1..1 avec `AKC_F32`), au plus `max_frames`
  trames (prévoir 1152 en MP3, 4096 en Vorbis, 5760 en Opus, la taille de bloc maximale en FLAC ;
  au-delà, le reste du paquet est perdu). Rend le nombre de trames (0 : paquet sans son, par
  exemple le premier paquet Vorbis ; < 0 : erreur — le paquet est ignoré, le décodage continue).
  Opus rend toujours du **48 kHz** ; l'avance (`pre-skip` d'`OpusHead`, `CodecDelay` de
  Matroska) et le rognage de fin sont **à l'appelant**. MP3 : le réservoir de bits passe d'un
  paquet à l'autre (après une recherche, `ak_codec_reset` puis une ou deux trames imparfaites).
* `ak_codec_info` : fréquence et canaux effectifs (après le premier paquet en MP3 ; FLAC : ceux de
  la trame).
* Opus : `OpusHead` de famille 1 (jusqu'à 8 canaux, plusieurs flux, table de correspondance des
  canaux) accepté ; les paquets sont alors ceux du flux multiple (délimités comme dans Ogg) ;
  le gain de sortie d'`OpusHead` est appliqué.
* Une poignée n'est pas partagée entre threads sans verrou ; plusieurs poignées sont indépendantes.

**Encodage** (fichier, ou fonctions d'écriture pour un autre conteneur) :

| Fonction | Arguments | Résultat |
|---|---|---|
| `ak_enc_open` | x0 = chemin, x1 = `AKF_WAV` / `AKF_MP3` / `AKF_OPUS`, x2 = fréquence, x3 = canaux (1, 2), x4 = débit (bits/s, 0 : 128 000 en MP3, 96 000 en Opus) | encodeur ; le fichier est créé (retiré si l'ouverture échoue) |
| `ak_enc_write` | x0 = encodeur, x1 = PCM s16 entrelacé, x2 = trames | 0 / < 0 |
| `ak_enc_close` | x0 = encodeur | 0 / < 0 ; fin du flux (WAV : tailles ; MP3 : dernière trame et réservoir ; Opus : dernière page Ogg, granule finale, EOS), fichier fermé |
| `ak_enc_size` | x0 = encodeur | octets écrits |
| `ak_enc_open_io` | x0 = écriture `(ctx, octets, n) -> n`, x1 = positionnement `(ctx, position)` ou 0, x2 = ctx, x3 = format, x4 = fréquence, x5 = canaux, x6 = débit | encodeur (sans fichier : un autre conteneur, un tampon) |

Banc d'essai sur le PC : `tools/audiotest` (le même code, `ak_codecs.inc`, dans une image nue pour
QEMU `virt` avec le semihosting d'ARM ; références : décodeurs C d'Onyx compilés sur le PC) ; sous
ARMPI : `codectest` (`src/bin/codectest`).

### AudioKit : synthèse FM et MIDI (version 81, `ak_fm.inc`, `ak_synth.inc`, `ak_midi.inc`)

**FM.** Un synthé (`ak_fm_new`) est un bloc à soi, de 1 à 64 voix ; même calcul que `fmsynth.h`
d'Onyx (son code de référence, comparaison au bit près : `tools/audiotest/fmref.cpp`, `synthtest`) :
ondes simples, ou deux opérateurs à la manière de l'OPL2. Fonctions protégées par un verrou : un
thread peut rendre pendant qu'un autre joue.

| Fonction | Arguments | Résultat |
|---|---|---|
| `ak_fm_new`, `ak_fm_free` | x0 = voix (0 : 16) ; x0 = synthé | synthé / 0 |
| `ak_fm_instrument` | x0 = synthé, x1 = voix, x2 = instrument `AKFI_*` (24 octets) | 0 |
| `ak_fm_from_fms` | x0 = `AKFI_SIZE` octets, x1 = 26 octets d'un instrument FM Song | 0 |
| `ak_fm_start` | x0 = synthé, x1 = voix, x2 = milli-Hz, x3 = `AKW_*` (`AKW_FM` : l'instrument), x4 = volume 0..255 | 0 |
| `ak_fm_stop` | x1 = voix (-1 : toutes) | le relâchement s'entend |
| `ak_fm_silence`, `ak_fm_active` | x0 = synthé | tout se tait ; 1 si une voix sonne |
| `ak_fm_pan`, `ak_fm_pitch`, `ak_fm_gain` | x1 = voix, x2 = 0..128..255 ; milli-Hz ; 0..255 | une note qui sonne |
| `ak_fm_render` | x1 = sortie s16 stéréo, x2 = trames | trames **écrites** (hors ligne) |
| `ak_fm_live` | x1 = sortie (0 : celle du process, -1 : arrêt) | un thread du kit rend les voix dans un flux (~46 ms d'avance) |
| `ak_note_mhz`, `ak_note_octave_mhz` | x0 = touche MIDI ; x0 = note 0..11, x1 = octave | milli-Hz (do4 = touche 60) |

**MIDI.** `ak_synth_new` : 16 canaux General MIDI (note, CC 0, 1, 6, 7, 10, 11, 38, 64, 91, 93, 100,
101, 120, 121, 123, programme, molette avec RPN 0), 64 voix. Deux moteurs :

- **SoundFont 2** (écrit d'après la spécification 2.04, MeltySynth d'Onyx comme référence d'essai) :
  table d'ondes à interpolation linéaire, enveloppes de volume et de modulation, deux LFO, filtre
  passe-bas résonant, panoramique, classes exclusives, réverbération par synthé (huit peignes,
  quatre passe-tout de chaque côté), chorus (ligne à retard modulée, CC 93), modulateurs de la
  SoundFont à l'attaque (vélocité, touche, contrôleurs vers l'atténuation, la coupure, enveloppe →
  coupure). Calculs lents par bloc de 64 trames.
- **banque FM** (`ak_gmbank.inc`, instruments de FM Tracker, 32 voix) : quand aucune SoundFont n'est
  sur la carte, ou avec `AKS_FM`.

| Fonction | Arguments | Résultat |
|---|---|---|
| `ak_sf_load`, `ak_sf_open_mem`, `ak_sf_free` | x0 = chemin ; x0 = tampon, x1 = octets ; x0 = SoundFont | poignée (fichier lu en mémoire), < 0 |
| `ak_sf_default` | - | `SD0:/res/soundfonts/GeneralUser-GS.sf2` (ou `default.sf2`), chargée une fois par process ; 0 : aucune |
| `ak_sf_name`, `ak_sf_presets`, `ak_sf_preset` | x0 = SoundFont, (x1 = numéro, x2 = tampon `AKSP_SIZE`) | nom ; nombre ; banque, programme, nom |
| `ak_synth_new`, `ak_synth_free` | x0 = SoundFont (0 : celle du système), x1 = `AKS_FM`, `AKS_NOFX` | synthé (lecture seule sur la SoundFont : plusieurs synthés la partagent) |
| `ak_synth_midi` | x1 = canal, x2 = commande (0x80, 0x90, 0xB0, 0xC0, 0xE0), x3, x4 = données | 0 |
| `ak_synth_render`, `ak_synth_render_f32` | x1 = s16 stéréo, x2 = trames ; x1, x2 = gauche, droite (flottants), x3 = trames | trames **écrites** (44,1 kHz) |
| `ak_synth_reset`, `ak_synth_voices` | x0 = synthé | tout coupé ; voix qui sonnent |

**Fichiers.** `ak_dec_open` reconnaît aussi les fichiers **MIDI** (`AKF_MIDI`, formats 0, 1, 2 ; sur
`ak_synth`, durée exacte, recherche : les contrôleurs sont rejoués) et **FM Song** (`AKF_FMS`, FM
Tracker ; titre et auteur dans `ak_dec_tag`), en s16 stéréo 44,1 kHz : `ak_play`, `play`, Musique, les
encodeurs (`ak_enc_*`) et `codectest` les prennent comme les autres formats.

## Le fichier d'un kit

Un ELF64 assemblé par FASMARM, lié **à l'adresse 0** :

* deux segments `PT_LOAD` : lecture + exécution (code, données en lecture seule, table
  d'exportation), puis lecture + écriture (variables, sur une page à part) ;
* le **point d'entrée est la table d'exportation** :

  | Décalage | Contenu |
  |---|---|
  | +0 | `'AKIT'` (0x54494B41) |
  | +4 | version = nombre de fonctions |
  | +8 | décalage de la fonction 0 depuis la table, puis 1, 2... (32 bits) |

* **aucune adresse absolue** : tout passe par `adr` (relatif à l'instruction), le kit
  fonctionne donc à n'importe quelle adresse sans relocation.

La table **ne fait que grandir** : une fonction n'est jamais retirée, déplacée ni renommée ; une
nouvelle s'ajoute à la fin et la version augmente. Un programme demande la version minimale
dont il a besoin.

## Chargement (AppKit lit, le noyau inscrit et mappe : `src/kernel/kits.asm`)

Le noyau ne lit aucun fichier ([FICHIERS.md](FICHIERS.md), étape 5, section 5.3) : il garde la
table des kits et les mappe.

* Appel système **`SYS_LIB_OPEN` (`0x64`) `lib_open(nom, version_min)`** (= *kit_find*) → adresse de la table dans le
  process, si le kit est **déjà en mémoire** ; sinon `E_NOENT` (aussi : format refusé, version
  trop ancienne : `E_INVAL`). Exception : `appkit`, pris dans les modules d'initramfs
  (`appkit.so`) au premier appel — c'est le seul kit qui ne passe pas par vfs.
* `kits_open` (`src/inc/kit.inc`) : sur `E_NOENT`, il ouvre AppKit et appelle **`f_kit_load(nom)`**
  (rang 31, AppKit version 32 ; `appkit.abi` vérifie le rang), puis rappelle `lib_open`.
* `f_kit_load` lit `SD0:/lib/<nom>.so` par vfs (le fichier doit porter `ATTR_LIB`, donné par vfs
  sous `SD0:/lib/`) dans un **tampon partagé temporaire**, rendu juste après, et appelle
  **`SYS_KIT_REGISTER` (`0x46`) `kit_register(nom, image, taille)`**. Un kit est partagé par tous les process : seul un
  programme de `SD0:/sys` (droits) peut l'inscrire ; un programme sans droits demande à init2
  (op `INIT_OP_KIT_LOAD`, données : le nom ; noté au journal), qui le lit et l'inscrit.
* `kit_register` recopie l'image, segments recopiés dans des pages physiques ; le kit reçoit une
  **place fixe**, la même dans tous les process : `0x50_0000_0000 + n × 64 Mo` (16 kits au
  plus). Un nom déjà inscrit (deux chargements en même temps) : rien n'est rechargé, l'existant
  est mappé. Un kit chargé **reste en mémoire**.
* Dans chaque process : le code est **mappé partagé** (mêmes pages physiques pour tous, lecture
  + exécution) ; les variables sont **recopiées dans des pages privées**. Un kit déjà ouvert
  par le process rend tout de suite son adresse.
* Les pages partagées portent un bit logiciel du descripteur (`PTE_SHARED`) : la fin d'un
  process ne les libère pas.

## Écrire un kit

La liste des fonctions d'un kit est écrite **une seule fois**, dans `src/kits/<kit>/<kit>.abi`,
et sert aux deux côtés (macros de `src/inc/kit.inc`) :

```asm
kit_begin uikit, 'uikit'
kit_fn ui_window
kit_fn ui_event
...
kit_end
```

* assemblé avec le kit, ce fichier produit la table d'exportation ;
* assemblé avec un programme, il produit pour chaque fonction un **relais** de trois
  instructions (`adr x16, table` ; `ldr x16, [x16, n × 8]` ; `br x16`) ; `kit_data` produit la
  table d'import, remplie par `kits_open`.

Une fonction de kit suit la convention d'appel habituelle (x0-x7 arguments, x19-x28
préservés) ; le relais n'utilise que x16 et x17, réservés à cet usage par l'AAPCS64.

* Un kit peut **importer un autre kit** comme un programme : `kit_use '../../kits/appkit/appkit.abi'`
  dans son segment de code (relais), `kit_data` dans son segment de variables (table d'import,
  une copie par process comme toutes ses variables), `kits_open` avant le premier appel. C'est le
  cas d'UIKit, qui importe AppKit pour lire ses polices et le thème (`uk_kits` : `kits_open` au
  premier besoin, dans `ui_init` ou au rechargement du thème). `kit_data` n'écrit que des
  décalages depuis `kits_list` (pas d'adresse absolue) : la même macro sert aux programmes et
  aux kits. Le chargement reste paresseux (`kits_open` au premier besoin) : aucun ordre de
  chargement à respecter, le kit importé est mappé dans le process à ce moment.
  Un kit qui n'a besoin que d'une petite fonction peut toujours en garder une copie privée
  (`include '../../inc/lib.inc'`).
* Les variables propres à un programme qu'il manipule directement restent dans le programme :
  UIKit reçoit l'adresse de celles de l'interface (`ui_data`) à `ui_init`.
* Un programme ne peut pas définir une fonction du même nom qu'une fonction d'un kit qu'il
  utilise (le relais porte ce nom).

## Utiliser un kit dans un programme

```asm
include '../../inc/armpi.inc'
app_header
include '../../inc/kit.inc'
include '../../kits/uikit/uikit.inc'       ; theme, macro ui_data
segment readable executable
kit_use '../../kits/appkit/appkit.abi'       ; relais des fonctions d'AppKit
kit_use '../../kits/uikit/uikit.abi'       ; et de UIKit
start:
	stp	x0,x1,[sp,-16]!             ; (arguments du programme)
	bl	kits_open                   ; ouvre les kits (message et fin si absent)
	ldp	x0,x1,[sp],16
	adr	x0,ui_win                   ; variables de l'interface
	bl	ui_init
	...
	bl	ui_window                   ; appel ordinaire
	...
segment readable writeable
kit_data                            ; tables d'import des kits
ui_data                             ; variables de l'interface
```

Importer AppKit active aussi son client de fichiers : `appkit.abi` définit `FICHIERS_APPKIT = 1`
(hors construction d'AppKit lui-même), et la macro `sys` envoie alors les appels de fichiers à
`fichier_sys` ([ABI.md](ABI.md), [FICHIERS.md](FICHIERS.md) section 3).

Tout s'assemble avec FASMARM sous Windows comme sous ARMPI : aucun outil externe, la même
`.abi` construit le kit et ses importateurs.

## UIKit : les widgets

Depuis la version 60, UIKit a de vrais widgets, sur le modèle de l'API C à poignées d'Onyx
(`uikit/flat.h`) : une fonction de création rend une **poignée opaque** (adresse d'un bloc du
kit, 0 : échec), que le programme redonne aux fonctions génériques `ui_w_*` et à celles de son
type. Les widgets appartiennent à la fenêtre courante au moment de leur création (celle du
programme, ou d'un dialogue), sont dessinés aux jetons du thème (`TJ_*`) et aux tailles de
l'échelle (DESIGN-ELEGANT.md 2 et 11). Programme de démonstration et d'essai : `src/apps/uidemo`.

```asm
	mov x0,16 ; mov x1,40 ; mov x2,440 ; mov x3,300 ; mov x4,UL_HEADER
	bl ui_list                       ; -> x0 = poignee
	...
.loop:	mov x0,1000 ; bl ui_event         ; evenement dans ui_ev
	mov x0,0 ; bl ui_w_event         ; -> x0 = widget, x1 = UN_*, x2 = 1 si pris
```

`ui_w_event` gère le survol, la capture de la souris (glisser), le focus clavier (Tab,
Maj+Tab, dans l'ordre de création), le double-clic (450 ms, 5 px) et la molette (widget
survolé, sinon celui du focus). Codes : `UN_CHANGE` (valeur, sélection, texte changés),
`UN_ACTIVATE` (double-clic, Entrée, clic d'un bouton), `UN_SORT` (liste triée par un clic sur un
en-tête). `ui_w_on(widget, fonction, donnée)` y attache une fonction appelée depuis
`ui_w_event` (x0 = widget, x1 = code ; `ui_w_user` rend la donnée). Les widgets se dessinent
eux-mêmes quand ils changent ; `ui_w_draw(0)` les dessine tous (après le fond du programme).

| Fonctions (version) | Rôle |
|---|---|
| `ui_clip(x, y, l, h)` (60) | découpe de tous les dessins (`l <= 0` : aucune) ; les widgets s'en servent pour eux |
| `ui_text_fit` (60) | `ui_text` avec une largeur max : texte coupé par « … » |
| `ui_w_event`, `ui_w_draw`, `ui_w_move`, `ui_w_show`, `ui_w_enable`, `ui_w_focus`, `ui_w_get_focus`, `ui_w_on`, `ui_w_user`, `ui_w_get_value`, `ui_w_set_value`, `ui_w_set_range(w, min, max, page, pas)`, `ui_w_destroy` (60) | génériques |
| `ui_scrollbar(x, y, l, h, USB_HORIZONTAL?)` (60) | barre de défilement sans flèches (DESIGN 11.5) : MIN..MAX le contenu, PAGE la partie visible, valeur = position ; clic dans le rail : une page ; molette : 3 pas |
| `ui_list(x, y, l, h, UL_HEADER / UL_NOSORT)`, `ui_list_column(liste, titre, largeur, ULC_RIGHT?)`, `ui_list_add(liste, texte, donnée)`, `ui_list_clear`, `ui_list_count`, `ui_list_get(liste, n) -> texte, donnée`, `ui_list_set`, `ui_list_remove`, `ui_list_sort(liste, colonne, décroissant)` (60) | liste à une ou plusieurs colonnes (texte d'un élément : colonnes séparées par des tabulations) ; en-têtes, tri par clic (numérique si les deux champs commencent par un chiffre, « 1 024 » compris ; sinon ASCII sans casse), colonnes redimensionnables (glisser le bord d'un en-tête), sélection (valeur = élément choisi, -1 : aucun), double-clic et Entrée : `UN_ACTIVATE`, flèches, Début, Fin, Page, première lettre, molette, barre de défilement superposée (un widget enfant) |

| `ui_text_prefix(chaîne, octets, px, police)`, `ui_text_hit(chaîne, px, police, x) -> octets, x` (70) | largeur d'un préfixe (exactement le stylo de `ui_text`, pour placer un curseur), point → position dans un texte |
| `ui_w_set_text`, `ui_w_get_text` (70) | texte d'un widget (champ ; plus tard étiquette, bouton...) ; le texte rendu reste au kit |
| `ui_textbox(x, y, l, h, UTF_PASSWORD / UTF_READONLY / UTF_MONO)`, `ui_textbox_hint`, `ui_textbox_select(champ, début, fin)`, `ui_textbox_selection -> début, fin` (70) | champ d'une ligne (DESIGN 11.2) : curseur, clic, glisser, double-clic (un mot), Maj + flèches, Ctrl + flèches (mot), Début, Fin, Ctrl+A, C, X, V, Retour arrière, Suppr ; Entrée : `UN_ACTIVATE` ; mot de passe (« • », ni copie ni coupe) ; longueur max = `ui_w_set_range(champ, 0, max...)` (défaut 4096) |
| `ui_clip_set(texte, octets)`, `ui_clip_get(tampon, taille)` (70) | presse-papiers du système, tenu par **Elegant** (`WS_OP_CLIP_SET/GET`, DESIGN-ELEGANT.md 16) |

| `ui_text_wrap(texte, x, y, largeur, couleur, police, px, mesurer?) -> hauteur` (84) | texte coupé en lignes aux espaces (« 
 » : nouvelle ligne ; interligne 1,5) ; bit 0 : mesure seulement |
| `ui_pushbutton(x, y, l, h, texte, UB_DEFAULT / UB_GHOST)` (84) | bouton (DESIGN 11.1 : standard, principal, fantôme ; survol, appui, grisé) ; clic, Espace, Entrée : `UN_ACTIVATE` ; largeur 0 : selon le texte (≥ 80), hauteur 0 : celle d'un contrôle |
| `ui_label(x, y, l, h, texte, ULB_*)` (84) | étiquette : gras, atténuée, coupée en lignes, à droite, centrée, titre de section |
| `ui_message(titre, texte, UM_OK / UM_OKCANCEL / UM_YESNO / UM_YESNOCANCEL)` (84) | boîte de message modale → 1 OK / Oui, 0 Annuler / Non (Non de `UM_YESNOCANCEL` : 2) ; Échap : 0 |
| `ui_ask(titre, question, tampon, taille)` (84) | saisie d'une ligne (texte de départ dans le tampon) → 1 OK, 0 Annuler |
| `ui_file_open(dossier, tampon, taille)`, `ui_file_save(dossier, nom, tampon, taille)`, `ui_folder_choose(dossier, tampon, taille)` (84) | ouvrir / enregistrer / choisir un dossier : bouton Parent, chemin modifiable (Entrée), liste Nom / Taille (dossiers d'abord, triés), double-clic ; Enregistrer : champ du nom (rempli par un clic sur un fichier) et « Remplacer ? » si le fichier existe (un dialogue dans le dialogue) |
| `ui_dialog_begin(l, h, titre) -> dialogue`, `ui_dialog_button(dlg, n, texte, réponse, UB_*, bord droit)`, `ui_dialog_run(dlg, fonction) -> réponse`, `ui_dialog_close(dlg, réponse)`, `ui_dialog_cancel(dlg, réponse d'Échap)`, `ui_dialog_end(dlg)` (84) | dialogue modal du programme : une **vraie fenêtre** de plus (rôle `WF_MODAL`, centrée à l'écran), qui devient la fenêtre courante : les widgets créés ensuite sont les siens. Elegant bloque les autres fenêtres du programme : un clic dessus les passe devant, le dialogue par-dessus, et s'arrête là (ni déplacement, ni fermeture, ni clavier) ; pas de bouton réduire. Échap, case de fermeture : la réponse d'annulation ; Entrée non prise : le bouton par défaut ; `ui_dialog_end` détruit ses widgets, ferme la fenêtre et rend la fenêtre d'avant. Les dialogues s'emboîtent |

| `ui_file_filter(motifs)` (174) | « *.kson;*.sq » (sans casse ; 0 : tout), ou plusieurs types « Nom|*.a;*.b|Nom|* » (comme `setFilters` d'Onyx) : les fichiers que montrent les dialogues suivants, jusqu'au prochain appel ; les dossiers restent. Liste des types en bas du dialogue (un seul filtre : lui, ou tous les fichiers) ; `ui_file_save` ajoute à un nom sans extension celle du type choisi |
| `ui_checkbox(x, y, l, h, libellé, cochée)`, `ui_radio(..., libellé, groupe)`, `ui_switch(..., libellé, allumé)` (89) | case (18 × 18, « ✓ »), radio (disque, point blanc ; ceux d'un même groupe de la fenêtre s'excluent), interrupteur (rail 34 × 20) — DESIGN 11.6 ; valeur 0 / 1 ; clic ou Espace : `UN_CHANGE` ; largeur / hauteur 0 : automatiques |
| `ui_dropdown(x, y, l, h, "un\|deux\|trois")`, `ui_combobox(...)` (89) | liste déroulante (valeur : élément choisi ; clic, Espace, Entrée, Alt+bas : le menu sous le widget ; haut / bas : l'élément voisin) ; liste modifiable (un champ enfant + le bouton du menu ; `ui_w_get_text`, `ui_w_set_text` : le champ) ; éléments : la chaîne de création, ou `ui_list_add` / `ui_list_clear` / `ui_list_get` comme une liste. Place de la fenêtre à l'écran : `WS_OP_WIN_POS` d'Elegant |

| `ui_textarea(x, y, l, h, UTF_READONLY / UTF_MONO / UTA_LINENUM)`, `ui_textarea_caret -> ligne, colonne`, `ui_textarea_goto(w, décalage)` (92) | texte multiligne d'après l'éditeur (tampon plat UTF-8, 1 Mio par défaut) : clic, glisser, double-clic, flèches (haut / bas gardent la colonne en pixels), Début / Fin, Ctrl+Début / Fin, Page, Maj, Ctrl + flèches, Ctrl+A / C / X / V (plusieurs lignes), Entrée, **Tab** (le widget le garde ; Ctrl+Tab passe au suivant), molette, barre de défilement, défilement horizontal au curseur, numéros de ligne. Le texte : `ui_w_set_text` / `ui_w_get_text` |
| `ui_slider(x, y, l, h, min, max, valeur)`, `ui_progress(..., max, valeur)`, `ui_numeric(..., min, max, valeur)`, `ui_knob(..., min, max, valeur)`, `ui_vumeter(..., max)`, `ui_vumeter_level(vu, niveau)`, `ui_lcd(..., texte)` (99) | glissière (DESIGN 11.6, verticale si plus haute que large, multiple du pas), barre de progression, nombre +/- (boutons, chiffres tapés, Retour arrière), molette (Onyx `knob` : repère sur 270°, glisser vers le haut / la droite), vu-mètre (segments SUCCES / ALERTE / DANGER, crête gardée), afficheur à sept segments (Onyx `lcd` : chiffres, « - », « : », « . ») ; plage par `ui_w_set_range(w, min, max, 0, pas)` ; flèches, Page (10 pas), Début, Fin, molette : `UN_CHANGE` |
| options de `ui_textarea` (106) | `UTA_LINENUM` (numéros de ligne dans une marge), `UTA_VSCROLL` / `UTA_HSCROLL` (barres de défilement intégrées : verticale, horizontale d'après la ligne la plus longue), `UTA_CODE` (éditeur de code, d'après `codeedit` d'Onyx : couleurs de l'assembleur — commentaires, chaînes, nombres, registres AArch64, directives de FASMARM ou mots-clés du programme, étiquettes — et retrait gardé par Entrée), `UTA_TABS` (Tab insère une vraie tabulation) ; chacune activable par le programme, `ui_w_style(w, drapeaux)` les change après coup |
| `ui_textarea_highlight(w, ligne)`, `ui_textarea_keywords(w, fonction)`, `ui_textarea_font(w, px)`, `ui_textarea_edit(w, UTE_UNDO / REDO / CUT / COPY / PASTE / ALL)`, `ui_textarea_find(w, mot)`, `ui_w_style`, `ui_w_text_len` (106) | ligne éclairée (erreur, pas à pas), mots-clés du programme (`x0` mot, `x1` longueur → 1), taille de la police, opérations des menus Édition, recherche sans casse (après la sélection, puis depuis le début), longueur du texte. **Annuler / rétablir** (Ctrl+Z / Ctrl+Y) : un journal de 128 opérations par texte, la frappe d'un mot et les effacements successifs fondus en une opération |
| `ui_frame_request() -> 1` (175) | demande un `WEV_FRAME` quand Elegant aura pris le contenu (0 : indisponible) ; `ui_update` ne l'attend plus (ABI.md, « present asynchrone ») |
| `ui_window_use(fenêtre) -> celle d'avant`, `ui_window_close(fenêtre)`, `ui_window_count()`, `ui_w_window(widget)` (182) | **plusieurs fenêtres par programme** (voir ci-dessous) |
| `ui_desk(espace, nombre) -> courant \| nombre << 8 \| compteur << 16` (183) | les **espaces de travail** : -1 / 0 gardent (`WS_OP_DESK`, le dock en fixe le nombre) |
| `ui_win_details(première, tampon) -> nombre` (184) | toutes les fenêtres, 7 entrées `WE_*` : place, taille, rôle, état, espace (`WES_DESK`) |
| `ui_tip_at(x, y, texte)` (185) | une infobulle tout de suite, à un point de la fenêtre (0 : la retire) |
| `ui_dialog_timer(dialogue, fonction, ms)` (186) | la fonction (x0 = dialogue) est appelée par `ui_dialog_run` avant chaque attente : compte à rebours, elle peut finir par `ui_dialog_close` |
| `ui_maximize()` (173) | comme le bouton agrandir du cadre (fenêtre `WF_RESIZABLE`) : agrandie, ou rendue à sa taille d'avant ; `WEV_RESIZE` suit |
| `ui_resize(l, h)`, `ui_min_size(l, h)`, `ui_on_resize(fonction)` (109) | fenêtre **redimensionnable** : créée avec le rôle `WF_RESIZABLE` (`ui_window_ex`) ; Elegant envoie `WEV_RESIZE` (bord droit / bas glissé, bouton agrandir, qui rend aussi la taille d'avant) ; `ui_w_event` y répond seul (`ui_resize` : nouveau tampon, cadre redessiné, fond `FACE`) puis appelle la fonction de `ui_on_resize` (x0 largeur, x1 hauteur : le programme replace ses widgets et redessine) ; taille minimale par `ui_min_size` |
| `ui_metric(UMT_*)`, `ui_anchor(w, UA_*)`, `ui_design_size(l, h)` (212-214) | densité tactile et contrôles adaptatifs du mode poche : voir « Mode poche » plus bas |
| `ui_splitter(x, y, l, h, min, max)` (110) | séparateur de volets (Onyx `splitter`) : poignée verticale ou horizontale que l'on glisse ; valeur = sa position, bornée ; `UN_CHANGE` à chaque pas : le programme replace ses volets (`ui_w_move`) et les redessine |
| `ui_panel(x, y, l, h, UPN_SURFACE?)`, `ui_groupbox(x, y, l, h, titre)`, `ui_w_set_parent(w, panneau)` (121) | panneau (Onyx `panel`), cadre de groupe titré (`groupbox`) : un conteneur ; un widget mis dedans est montré, caché et dessiné avec lui (il garde ses coordonnées de fenêtre) |
| `ui_segmented(x, y, l, h, "a\|b\|c")` (121) | contrôle segmenté (Onyx `segmented`, DESIGN 11.4 : rail `CREUX`, segment actif `SURFACE` posé sur une ombre `LIGNE`, texte gras) ; valeur = segment choisi ; clic, gauche / droite : `UN_CHANGE` |
| `ui_tabs(x, y, l, h, "a\|b\|c")`, `ui_tabs_page(onglets, n)` (121) | onglets à pages (Onyx `tabhost`) : l'en-tête segmenté centré, un panneau par page sous lui (seul celui de la page choisie est montré) ; on y met ses widgets par `ui_w_set_parent` |
| `ui_tabstrip(x, y, l, h, UTS_CLOSE / UTS_PLUS)`, `ui_tabstrip_add(barre, titre, donnée)`, `ui_tabstrip_closing(barre)` (121) | barre d'onglets de documents (Onyx `tabstrip` : navigateur, terminal) : un clic choisit (`UN_CHANGE`), la croix **demande** la fermeture (`UN_CLOSE`, l'onglet : `ui_tabstrip_closing` ; le programme décide et retire par `ui_list_remove`), « + » : `UN_NEW` ; molette : l'onglet voisin |
| `ui_toolbar(x, y, l, h)`, `ui_toolbar_add(barre, glyphe, libellé, UTB_TOGGLE / UTB_SEP)` (121) | barre d'outils (DESIGN 11.7) et ses boutons fantômes : un glyphe UTF-8 (par ex. ← → ↻ ⌂ ✂ ⚙ ▶ de DejaVu Sans), un libellé, ou les deux ; à bascule (valeur 0 / 1, allumé `ACCENT_CLAIR`) ; clic : `UN_ACTIVATE` ; sans focus clavier |
| `ui_image_load(chemin)`, `ui_image_decode(données, octets)`, `ui_image_free`, `ui_image_size -> l, h, pixels`, `ui_image_draw(image, x, y, l, h)` (128) | images **BMP** (24 / 32 bits, palettes 8 / 4 / 1 bit, de bas en haut ou de haut en bas) et **PNG** (gris, RVB, palette, gris + alpha, RVBA ; 1 à 16 bits ; `tRNS` ; filtres 0-4 ; inflate RFC 1950 / 1951 écrit dans le kit ; l'entrelacement Adam7 est refusé) ; pixels `0xAARRGGBB` ; dessin à l'échelle (plus proche voisin), mélangé par l'alpha, dans la découpe |
| `ui_imagebox(x, y, l, h, image, UIM_STRETCH / UIM_NOSCALE)`, `ui_imagebox_set` (128) | boîte d'image (Onyx `imagebox`) : ajustée en gardant les proportions et centrée, étirée, ou en taille réelle. Visionneuse : `src/apps/viewer` |
| `ui_cursor(UC_*)`, `ui_w_cursor(w, UC_*)` (140) | **forme du curseur** sur la fenêtre (`WS_OP_CURSOR` d'Elegant : flèche, texte, main, redimensionner ↔ / ↕, sablier, croix ; glisser en cours) ; `ui_w_event` la change d'après le widget survolé (champs et textes : barre de texte, séparateur : flèches) ; Elegant montre aussi ↔ sur le bord d'une fenêtre redimensionnable |
| `ui_w_tip(w, texte)`, `ui_w_idle()` (140) | **infobulles** (Onyx `padhint`) : après 600 ms de survol, une petite fenêtre sous le pointeur (texte `SURFACE` sur `TEXTE1`) ; le programme appelle `ui_w_idle` quand `ui_event` n'a rien rendu (attente conseillée : 200 ms) ; elle s'en va au premier mouvement, clic ou touche |
| `ui_drag_start(données, octets, UDT_TEXT / UDT_FILES)`, `ui_drop_get(tampon, taille) -> octets, genre` (140) | **glisser-déposer** entre fenêtres et programmes (`WS_OP_DRAG`, `WS_OP_DROP_GET`, `WEV_DROP` d'Elegant ; 464 octets au plus) : un glisser commence pendant un appui ; au relâché, la fenêtre dessous reçoit `WEV_DROP`. Un texte déposé sur un champ ou un texte multiligne y est inséré au point (`UN_CHANGE`) ; sur un autre widget : `UN_DROP` (le programme lit les données). Liste `UL_DRAG` : on glisse le texte d'un élément |
| `ui_tr(texte)`, `ui_tr_load(chemin)`, `ui_lang()` (140) | **localisation** (`TR()` d'Onyx) : langue de `lang = xx` dans `SD0:/etc/system.ini` (par défaut `fr`, la langue des sources) ; dictionnaire du système `SD0:/res/lang/<xx>.txt` (« source = traduction » ; `en.txt` traduit les textes de UIKit), et ceux du programme ; les dialogues de UIKit passent leurs textes par `ui_tr` |
| `ui_menu_state(id, UMS_DISABLED / UMS_CHECKED)`, `ui_menu_key(évènement) -> id` (140) | **menus** : entrées grisées ou cochées (marques « d » / « c » après l'id dans le texte des menus, dessinées par la barre de menus : texte `TEXTE3`, « ✓ ») ; `ui_menu_key` reconnaît le raccourci d'une entrée active (« Ctrl+S », « Ctrl+Maj+Z », « Alt+X », « F3 » ; lettres à leur place US) : le programme le traite comme un choix de menu |
| `ui_font_italic()` (140), `ui_font_get(UFN_*)` (166) | polices : celles du programme (`UFN_SANS`, `UFN_BOLD`, `UFN_MONO`) et les autres DejaVu de `SD0:/fonts`, lues au premier besoin dans le kit : italique (Oblique), gras italique, mono gras, Serif, Serif gras, Serif italique, Serif gras italique ; absente : la régulière |
| `ui_calendar(x, y, l, h, date)`, `ui_calendar_today(cal, date)` (152) | calendrier d'un mois (Onyx `calendar`) : date = année << 16 \| mois << 8 \| jour ; « ‹ » « › », Page : le mois ; clic d'un jour : `UN_CHANGE` (double : `UN_ACTIVATE`) ; flèches : jour, semaine ; aujourd'hui entouré (le programme la donne : UIKit n'a pas l'heure) ; mois et jours par `ui_tr` |
| `ui_datepicker(x, y, l, h, date)`, `ui_datepicker_today` (152) | sélecteur de date (Onyx `DatePicker`) : « AAAA-MM-JJ », un clic ouvre un calendrier **sous le widget** (fenêtre sans cadre qui prend la souris ; un appui dehors ou Échap annule) |
| `ui_palette(x, y, l, h, couleur)`, `ui_colorpicker(x, y, l, h, couleur)`, `ui_color_dialog(titre, couleur) -> couleur / -1` (152) | palette 8 × 5 (Onyx `colorpick`), pastille qui ouvre la palette dessous, dialogue de couleur (palette, glissières rouge / vert / bleu, aperçu, « #RRGGBB ») |
| `ui_tree(x, y, l, h)`, `ui_tree_add(arbre, parent, libellé, donnée) -> id`, `ui_tree_expand(arbre, id, 1 / 0)`, `ui_tree_get(arbre, id) -> libellé, donnée`, `ui_tree_clear` (152) | arborescence (Onyx `treeview`) : ids stables, triangle ▸ / ▾ (clic ou double-clic : déplier), sélection (valeur = id), double-clic sur une feuille, Entrée : `UN_ACTIVATE`, flèches (droite déplie, gauche replie ou remonte), molette, barre de défilement |
| `ui_list_virtual(liste, lignes, fonction)`, `ui_list_sorted -> colonne, décroissant`, options `UL_STRIPES`, `UL_GRID`, `UL_SIDEBAR`, code `UN_CONTEXT` (164) | **grille de données** (Onyx `datagrid`) : liste virtuelle (le programme garde ses lignes ; sa fonction rend le texte d'une ligne ; un clic d'en-tête rend `UN_SORT`, le programme trie), lignes alternées, traits entre colonnes, bouton droit : la ligne choisie et `UN_CONTEXT` ; **panneau latéral** (Onyx `sidepanel`, présentation bureau) : éléments « #titre » = sections (non choisissables) |
| `ui_layout(panneau, ULY_VSTACK / ULY_HSTACK / ULY_GRID, écart, colonnes)` (164) | mise en page (Onyx `layout`) : place le contenu d'un panneau en colonne, en ligne ou en grille de cases égales |
| `ui_canvas(x, y, l, h)`, `ui_canvas_pixels`, `ui_canvas_begin`, `ui_canvas_end` (164) | canevas (Onyx `canvas`) : des pixels à soi ; entre `begin` et `end`, tous les dessins de UIKit y vont ; la souris y rend `UN_CHANGE` |
| `ui_vp_disc(cx, cy, r, couleur)`, `ui_vp_line(x0, y0, x1, y1, épaisseur, couleur)`, `ui_vp_poly(points, n, couleur)` (164) | formes anticrénelées (Onyx `vpaint`), coordonnées en 1/16 de pixel : disque, trait épais à bouts ronds, polygone (pair-impair, 4 × 4 échantillons) |
| `ui_pad_hints(texte, x, y, h, mesurer?)` (164) | indications de manette (Onyx `padhint`, PocketTerm) : « A	choisir
B	retour
+	déplacer » → pastilles et rôles |
| `ui_objnav(rectangles, n, courant, direction)` (164) | navigation entre objets à la croix (Onyx `objnav`) : l'objet voisin le plus proche dans une direction (les modes déplacer / redimensionner restent au programme) |
| `ui_popup_local(x, y, libellés, largeur)` (165) | menu contextuel au point d'un clic dans la fenêtre ; libellés à la suite, une chaîne vide à la fin ; « - » : un trait (il compte dans les numéros) ; premier octet 0x01 : entrée grisée |
| `ui_view(x, y, l, h, UVW_FOCUS?)`, `ui_view_on(vue, dessin, souris, touche, molette)`, `ui_view_dirty`, `ui_view_flush` (176) | **vue dessinée à la main** : des pixels à soi, redessinés par le programme quand elle est marquée ; survol, capture, double-clic, focus ; infos souris : clics \| boutons << 8 \| mods << 16. **Ses enfants** (`ui_w_set_parent`) sont **coupés par elle**, au dessin comme à la souris : un formulaire défile en les déplaçant (`ui_w_move`) puis `ui_view_dirty` |
| `ui_list_cell(liste, fonction)`, option `UL_HSCROLL` (180) | cellule dessinée par le programme (x0 liste, x1 ligne, x2 colonne, x3, x4 coin dans la fenêtre, x5, x6 taille, x7 1 : fond d'une ligne choisie → 1 dessinée, 0 : le texte) ; barre horizontale quand les colonnes dépassent (cachée sinon ; gauche, droite au clavier). `ui_list_sort` sur une liste virtuelle : la flèche seule ; une colonne ne se retire pas : on refait le widget |
| `ui_date_popup(widget, date, aujourd'hui) -> jour / -1` (180) | le calendrier dans une fenêtre modale sous un widget quelconque (un champ de date à soi : `ui_datepicker` n'est ni vide ni saisi au clavier) |
| options `UTF_RIGHT`, `UTF_ERROR`, `UTA_FORMTAB` (180) | champ aligné à droite ; contour `DANGER` par `ui_w_style` (valeur refusée) ; texte multiligne d'un formulaire : Tab passe au suivant |
| `ui_dialog_at(x, y)` (181) | le prochain `ui_dialog_begin` : sans cadre, à ce point de l'écran, devant tout ; `ui_screen` pour la taille : le plein écran d'un diaporama (Slides) ; `ui.surf`, `ui.w`, `ui.h` sont alors ceux de cette fenêtre, `ui_update_all` la montre, `ui_dialog_end` rend l'ancienne ; les évènements de l'autre fenêtre (`EV_WIN`) sont à ignorer |
| `EV_MODS` de `WEV_MOUSE` (180) | Elegant donne les modificateurs (la dernière touche reçue, Maj et Ctrl seuls compris) avec la souris : Maj + clic, Ctrl + clic ; en C : `UI_MOD_CTRL`, `UI_MOD_SHIFT`, `UI_MOD_ALT` |
| `ui_surface(x, y, l, h, WS_SF_XRGB8888 / WS_SF_RGB565)`, `ui_surface_pixels -> pixels, pas, l, h`, `ui_surface_damage(s, x, y, l, h)`, `ui_surface_damage_list(s, rectangles, n)`, `ui_surface_move(s, x, y, l, h)`, `ui_surface_free` (172) | **surface attachée** (DESIGN-ELEGANT.md 17) : des pixels à soi dans un tampon partagé qu'Elegant compose directement dans une zone du contenu de la fenêtre courante (vidéo, émulateurs) ; on écrit, puis on signale les rectangles changés (pas de `ui_update`) ; `ui_surface_move` déplace ou redimensionne la zone (tampon neuf s'il est trop petit) |

Le rastériseur avance une **tabulation** jusqu'au taquet suivant (tous les quatre espaces, depuis le
début de la chaîne) et ne dessine pas le retour chariot (largeur et dessin : `ttf_text_pos`,
`ttf_text_width`, `ttf_draw_text`).

Mémoire : un petit tas propre au kit (classes de puissances de deux, listes libres), pris par
`sbrk` dans le process ; `ui_w_destroy` rend le bloc du widget, ses textes et ses éléments.

### Plusieurs fenêtres par programme (version 182, `uikit_window.inc`)

La poignée d'une fenêtre est celle que rend `ui_window` / `ui_window_ex` : son id chez Elegant, le même
que `ui_win`, `EV_WIN` et `ui_w_window`. Chaque fenêtre a son bloc d'état (surface, en-tête du présent,
cadres, titre, focus, capture, survol, taille minimale, `ui_on_resize`, curseur, découpe, menus). Les
fonctions du kit travaillent sur la **fenêtre courante**, dont l'état est dans les variables habituelles
(`ui_win`, `ui_surf`, `ui_w`, `ui_h`) ; les blocs des autres attendent.

```
	mov	x0,L ; mov x1,H ; adr x2,titre ; bl ui_window	; une de plus : elle devient la courante
	mov	x0,fenetre ; bl ui_window_use			; -> celle d'avant : dessins et widgets y vont
.loop:	mov	x0,1000 ; bl ui_event				; toutes les fenêtres du programme
	cmp	x0,WEV_CLOSE ; b.eq .ferme			; EV_WIN dit laquelle
	mov	x0,0 ; bl ui_w_event				; va à la fenêtre de l'événement
	...
.ferme:	ldr	w0,[ui_ev + EV_WIN] ; bl ui_window_close	; ses widgets détruits, fermée
	bl	ui_window_count ; cbnz x0,.loop			; le programme finit avec la dernière
```

* `ui_w_event` rend courante la fenêtre de l'événement, le temps de le traiter et des fonctions qu'il
  appelle (`ui_w_on`, `ui_on_resize`), puis rend celle d'avant. Un programme qui dessine à la main après
  un événement appelle `ui_window_use` lui-même. `ui_menu_key` et `ui_w_idle` font de même.
* Redessiner un widget (`ui_w_set_text`, `ui_w_set_value`, `ui_w_draw(widget)`...) ou lui donner le focus
  marche quelle que soit la fenêtre courante ; `ui_w_draw(0)` dessine ceux de la fenêtre courante.
* `ui_menu`, `ui_menu_state`, `ui_min_size`, `ui_on_resize`, `ui_title`, `ui_resize`, `ui_cursor`,
  `ui_surface` valent pour la fenêtre courante. **Un menu par application**, comme sur Mac : la barre montre
  le titre de la première fenêtre du programme et le menu de la première qui en a un, quelle que soit la
  fenêtre active. `WEV_MENU` arrive à la fenêtre active, avec son `EV_WIN` ; Quitter ferme la première (`WEV_CLOSE`).
  Pendant un dialogue, les menus ne font rien. Les raccourcis (`ui_menu_key`) ne valent que dans la fenêtre du menu.
* Fermer la fenêtre courante rend courante la dernière ouverte des autres. `ui_close` reste comme avant
  (les widgets ne sont pas touchés) ; `ui_window_close` les détruit, et prend n'importe quelle fenêtre.
* Un programme à une fenêtre n'échange rien : ni copie, ni allocation, même comportement qu'avant.
* Un dialogue (`ui_dialog_*`) bloque **toutes** les autres fenêtres du programme, comme `ShowDialog`
  de WinForms, pas seulement celle qui l'a ouvert.
* Démonstration : `src/bin/multiwin`.

### Mode poche : densité tactile et contrôles adaptatifs (version 214, `uikit_window.inc`)

Le mode poche d'Elegant (docs/MODE-POCHE.md) met chaque fenêtre normale en plein écran sous la barre,
y compris les fenêtres **de taille fixe** (sans `WF_RESIZABLE`) : elles reçoivent `WEV_RESIZE` (800 x 564).
Les dialogues `WF_MODAL` gardent leur taille. UIKit y répond sans que l'application change :

* **Densité tactile** : `density = touch` dans `SD0:/etc/theme.txt` (aussi `densite = tactile`) choisit la
  ligne tactile de `uk_mt_tab` : contrôles, lignes de liste et menus de 36 px, barres de défilement de 12,
  en-têtes de 28, texte inchangé (14 / 13). Relue avec le thème (`korin.theme`) : les listes et les menus
  suivent tout de suite, les contrôles à leur création (hauteur 0 = celle de l'échelle). `ui_metric(UMT_*)`
  rend chaque métrique (`UMT_CTL`, `UMT_ROW`, `UMT_MENU`... ; `UMT_TOUCH` : 1 en tactile) : les apps C++
  en tirent leur `ctl_h()` au lieu d'une table recopiée. **Qui écrit theme.txt garde la ligne `density`.**
* **Taille et rectangles de conception** : la taille de création de chaque fenêtre (ou `ui_design_size(l, h)`),
  et pour chaque widget sa place à sa création, puis à chaque `ui_w_move` tant que la fenêtre a cette taille
  (`UW_DES`). Tout replacement part de là, jamais de la place d'avant.
* À `WEV_RESIZE` (`ui_w_event`), avant la fonction de `ui_on_resize`, `uk_adapt` replace :
  * les widgets **ancrés** (`ui_anchor(w, UA_*)`), comme les ancres de WinForms, dans toute fenêtre :
    `UA_LEFT | UA_RIGHT` étire, `UA_RIGHT` seul garde la distance au bord droit, ni l'un ni l'autre garde
    la taille au même centre proportionnel ; de même `UA_TOP`, `UA_BOTTOM` ; `UA_FILL` = les quatre ;
    `UA_FIXED` : ne bouge jamais ;
  * dans une fenêtre **fixe sans `ui_on_resize`**, les autres widgets : étirement proportionnel en x et en y.
    Les contrôles à hauteur fixe (champ, bouton, étiquette, cases, bascule, listes déroulantes, curseur,
    progression, nombre, molette, segments, bandes d'onglets, barre d'outils, date ; hauteur <= 64) gardent
    leur hauteur au même centre proportionnel ; en densité tactile, si la hauteur grandit d'au moins 9/8, ils
    prennent 36. Ce qui fait <= 24 px, et les petits carrés (boutons d'icône), garde sa taille. Les enfants
    d'un widget qui place les siens (liste, arbre, texte, liste modifiable) le suivent par lui.
* Ce qu'une app **dessine à la main** n'est pas replacé : elle prend `ui_on_resize` (ou traite `WEV_RESIZE`
  et appelle `ui_resize`) et redessine à `ui_w`, `ui_h`. Exemples : Calculatrice (touches et affichage à la
  taille de la fenêtre, texte qui grandit), Horloge (cadran centré, page Alarmes par ancres), Magnétophone
  (vumètre étiré, format ancré à droite). PiBuild et Musique (widgets seuls) n'ont rien changé.

### Fenêtres de sélection

| Sélection | Forme | Pourquoi |
|---|---|---|
| `ui_message`, `ui_ask`, `ui_file_*`, `ui_folder_choose`, `ui_color_dialog`, `ui_dialog_*` | fenêtre modale, cadre et titre | un vrai dialogue |
| `ui_datepicker` | fenêtre modale « Choisir une date » sous le champ : calendrier, Annuler, OK ; un clic choisit le jour, double-clic ou Entrée valident | c'était une fenêtre sans cadre, fermée au premier clic |
| `ui_colorpicker` | ouvre `ui_color_dialog` (palette, rouge / vert / bleu, aperçu, OK / Annuler) ; double-clic dans la palette : OK | la palette seule ne permettait aucune autre couleur |
| `ui_dropdown`, `ui_combobox`, `ui_popup`, menus | restent des menus : fenêtre sans cadre `WF_GRAB` collée au widget, fermée par un clic dehors | ils ne sont pas dessinés dans la fenêtre du programme ; un titre et un clic de plus pour choisir dans une liste seraient une régression |

Le sélecteur de fichiers (`ui_file_open`, `ui_file_save`, `ui_folder_choose`) est à hauteur de `FileDialog`
d'Onyx : fenêtre redimensionnable ; Retour, Avant, Parent et le chemin, qu'on peut taper ; panneau avec
Accueil, les accès rapides de `SD0:/etc/places.ini` (ceux du navigateur) et les volumes (`VFS_VOLUMES`,
relus à l'ouverture) ; liste Nom, Taille, Date avec icônes, dossiers d'abord, triée par un clic sur un
en-tête ; champ du nom, aussi pour Ouvrir (un chemin complet y est accepté) ; types de fichiers ou nombre
d'éléments. Un dossier de départ qui est un fichier, ou qui n'existe plus : le dossier le plus proche, et
son nom est proposé. Clavier : Retour arrière (parent), Alt + gauche / droite (Retour, Avant).
Au-delà de 400 éléments, pas de date : elle demande un appel à `vfs` par fichier.

Nouveaux styles de `ui_list` : `UL_USERSORT` (un clic sur un en-tête rend `UN_SORT` sans trier, le programme
trie) et `UL_ICONS` (icône en colonne 0 d'après la donnée de l'élément & 3 : 0 fichier, 1 dossier, 2 volume).

### Pour un en-tête C `uikit.h`

Les fonctions suivent l'AAPCS64 (x0-x7 arguments, x0 / x1 résultats, x19-x28 et d8-d15
préservés) et ne prennent que des entiers, des pointeurs et des poignées : un programme C (la
chaîne d'Onyx, le futur portage de Jet) peut les appeler par les relais de `kit_use`. Il
faudrait :

* l'en-tête : `typedef struct uk_widget *ui_widget;` (poignée opaque), les prototypes de la
  table dans l'ordre de `uikit.abi` (ex. `ui_widget ui_list (long x, long y, long w, long h,
  long flags);`, `long ui_w_event (void *ev, long *code, long *taken)` — deux résultats en x0 /
  x1 : une petite structure `struct ui_result { void *w; long code; }` rendue en registres, ou
  une fonction d'enveloppe), les constantes de `uikit.inc` (`UN_*`, `UL_*`, `UTF_*`, `UB_*`,
  `UC_*`, `UMS_*`, `ULY_*`, `UFN_*`...) et celles de `src/inc/ws_ext.inc` ;
* les variables de l'interface (`ui_data`) en structure C : `struct ui_vars { long win; void
  *surf; long w, h; unsigned char ev[32]; unsigned char font[320], font_b[320], font_m[320];
  };`, passée à `ui_init` ;
* la table d'import : un objet C généré depuis `uikit.abi` (les relais `adr x16 / ldr / br`
  et `kit_data`), ou `kits_open` appelé depuis le démarrage C ;
* les fonctions de rappel (`ui_w_on`, `ui_on_resize`, `ui_dialog_run`, liste virtuelle, mots-clés)
  sont appelées par le kit avec l'AAPCS : un pointeur de fonction C convient ;
* les textes sont en UTF-8 terminés par NUL ; les chaînes rendues (`ui_w_get_text`, `ui_tr`)
  appartiennent au kit.

### Applications qui gagneraient à passer aux widgets

* `files` : sa liste à colonnes triables et redimensionnables → `ui_list` (ou virtuelle), le
  panneau → `UL_SIDEBAR`, la barre de chemin → `ui_textbox`, les dialogues « Renommer » /
  « Nouveau dossier » → `ui_ask`, le glisser-déposer de fichiers → `UDT_FILES`.
* `control` et ses applets : cases, radios, sélecteur « ‹ texte › » → `ui_checkbox`, `ui_radio`,
  `ui_dropdown` / `ui_segmented` ; défilement des volets → barres et `ui_panel`.
* `dock` (réglages), `mixer` (glissières, vu-mètres → `ui_slider`, `ui_vumeter`), `recorder`,
  `terminal` (onglets → `ui_tabstrip`), `builder` (champs, texte multiligne de sortie,
  dialogue Ouvrir), `browser` (barre d'outils, champ d'adresse, onglets), `paint` (palette →
  `ui_palette`, `ui_color_dialog`, canevas), `wifimenu` (liste, champ mot de passe), `clock`
  (calendrier).
