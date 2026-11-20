---
name: armpi-widevine
description: "Widevine dans Jet : hôte Linux, jalon 1 fait (liblinux + linuxhost, QEMU PC) ; décisions du 19/11 prises ; module mesuré ; étapes 2b à 7 faites (licence réelle, audio déchiffré, vidéo décodée par le module, dans Korin sous QEMU) ; docs/etude/WIDEVINE.md"
metadata:
  type: project
---
Demande de l'utilisateur (18/11/2026), à partir de LibreWolf (une case télécharge Widevine) et de l'envie d'avoir
Netflix et Disney+. Le 19/11, il lance le chantier par un **hôte qui exécute du code Linux** (sans attendre la
couche Linux complète de Claude Code). Autre motif : la lecture de librespot (app Spotify) n'est pas au point,
le lecteur web de Spotify a besoin de Widevine.

**Jalon 1 fait le 19/11 (fce63664, main)** : `src/lib/linux` (chargeur ELF de .so Linux x86-64, glibc de
compatibilité sur libposix, appels de Linux traduits) et `src/sys/linuxhost` (`--check`, `--self-test`,
`FICHIER.so FONCTION`). Module d'essai lxtest.so construit pour Linux par la gcc de WSL : 16/16 sous QEMU PC.
Construire : `wsl sh tools/linux/build.sh` ; essayer : dans WSL, `. ~/korin-cloud/env.sh` puis
`python3 -I tools/linux/test_pc.py` (QEMU n'est plus installé côté Windows).

Décisions de l'utilisateur (19/11) : glibc de compatibilité ; module par le canal de Google (component
updater, .crx3), archive à la main en secours ; ClearKey d'abord dans Jet. Module 4.10.3050.0 téléchargé
(build/pc/linux/widevine, SHA-512 de Mozilla vérifiée ; versions.txt de Google = 404) : interface et hôte 10
seulement, 177 imports dont 108 non servis, TLS par __tls_get_addr, **791 syscall directs obscurcis** : la
personnalité Linux du noyau est obligatoire (étape 2b). Sonde sous Linux autorisée et faite (tools/linux/cdmprobe/run.sh) : avec un Host_10 minimal le module
produit une vraie requête de licence (1702 octets) ; ses syscall directs = openat/read/close de /proc/self/maps
(×11) et /proc/net/unix, rt_sigaction en lecture (SIGILL, SIGTRAP, SIGABRT). À fabriquer dans liblinux.

**Décision de forme (19/11)** : le noyau ne connaît pas « Linux » ; il offre une **capture générique** : l'hôte
déclare les syscall qu'il veut capturer (plages d'adresses de l'instruction, numéros), le noyau les lui renvoie
(même thread, comme un signal) ; porte ouverte à d'autres personnalités. Étude § 3.2. Livraison dans le
thread appelant choisie par l'utilisateur (pas de thread hôte séparé).
**Étape 2b faite le 19/11** : SYS_SYSCALL_CAPTURE 0x96 (noyau PC : core/signal.c, arch/x86_64/sigframe.c ;
trame de signal SIGSYS, retour par SIG_RETURN), liblinux capture.c ; QEMU : 4 syscall directs traduits, ptest
22/0. Noyau d'essai construit à part : `PC_OUT=build/pc/linux/kbuild PC_QUICK=1 PC_SVC=pcid sh tools/pc/build.sh`,
puis `test_pc.py --esp-src build/pc/linux/kbuild/esp`. **Étape 2c faite le 19/11 (QEMU)** : le vrai module produit une requête de licence (1702 octets) DANS Korin :
glibc_thread.c, glibc_misc.c (177 imports servis), TLS (__tls_get_addr), .eh_frame déclaré à libgcc, /proc/self/maps
fabriqué (hôte nommé libc.so.6), futex ; noyau PC : **arène haute** 0x1000_0000_0000-0x7000_0000_0000 (PartitionAlloc
réserve 32 Go alignés sur 32 Go ; docs/ABI.md). Sonde Korin : `WIDEVINE_SO=… sh tools/linux/build.sh` puis
`test_pc.py --probe --esp-src build/pc/linux/kbuild/esp` (module embarqué, build/ seulement).
**Étape 3 faite le 19/11** (src/sys/cdmhost : host.cpp Host_10, cenc.cpp, cdmtest.cpp) : vraie licence du
serveur de test de Shaka (« Angel One »), 5 clés ; **la vidéo ne sort jamais en clair** (Decrypt → kNoKey) :
DecryptAndDecodeFrame rend des images I420 (H.264 en Annex B obligatoire, comme Chromium) ; l'audio sort en clair
par Decrypt. Mêmes empreintes sous Linux et DANS Korin (QEMU avec netd et usb-net) : vidéo fabf757bb7928937 (sans les
images de la fin ; toutes : 967a04bdc5ec8b46, depuis le 20/11), audio
85bba6b1d46b3472. Refaire : `sh tools/linux/cdmtest.sh MODULE` (Linux) ; `sh tools/linux/build.sh` puis
`WIDEVINE_SO=MODULE sh tools/linux/test_cdm_pc.sh` (disque d'essai effacé à la fin).
**Étape 4 faite le 20/11 (QEMU)** : EME ClearKey joue dans Jet (249 images après le début chiffré). av : CENC en
correctif tools/webkit/av-patches/0001-cenc.patch (src/extra/onyx-libs pas suivi ; glue.sh l'applique) ; WebKit :
tools/webkit/patches/0008-eme-clearkey.patch (ENABLE_ENCRYPTED_MEDIA, CDMOnyx, lecteur) ; Jet : permission
decidePolicyForMediaKeySystemPermissionRequest (sinon NotSupportedError). cdmhost (process à part, proto.h,
cdmclient) : mêmes empreintes Linux/Korin. Essais : tools/linux/test_eme_jet.sh (JET=…), ESSAI=ipctest
test_cdm_pc.sh. Pièges : build-jet.sh refuse si av.h > libWebCore.a ; WSL 9 Go = 4 travaux pour WebCore (~1 h 30 en
plus de 3 h d'une construction neuve) ; Jet EME à part dans ~/korin-webkit-x86_64/out/jet-eme.app (webcore-eme).
**Étape 5 faite le 20/11 (QEMU)** : Widevine joue dans Jet (flux Shaka, licence réelle, 5 clés, 1496 images décodées
par le module). av : tools/webkit/av-patches/0002 (décodeur externe, fabrique) ; WebKit : patches/0009-eme-widevine
(CDMWidevineOnyx : cdmhost lancé par l'instance, audio par Decrypt, vidéo par DecryptAndDecodeFrame) ; cdmhost dans
/apps/jet.app/libexec (ajouté à skel/sd0/etc/exec.txt : sinon pas exécutable). Essai : test_eme_jet.sh KS=widevine.
Pièges : le FFmpeg de /tmp (WSL) disparaît, build-media.sh avant build-jet.sh ; CString::data() est en char8_t.
**Étape 6 faite le 20/11 (QEMU)** : UA Chrome Linux sur les sites Widevine quand le module est là (engine_webkit.cpp) ;
cdmhost lancé au premier besoin (probeSupport de Shaka : 14 hôtes sinon) ; WebM chiffré pas lu par av (retiré des
configurations : les sites prennent le MP4) ; le lecteur Shaka joue le flux DASH Widevine d'Internet (KS=shaka) ;
netflix.com et open.spotify.com chargent. Reste : lecture avec un compte (l'utilisateur), ZBook, WebM chiffré,
la case des réglages (étape 7). Décision de l'utilisateur du 20/11 : Widevine d'abord, Claude Code après.
**Étape 7 faite le 20/11 (QEMU)** : Réglages > « Lire les contenus protégés (Widevine)… » (main.cpp, montrée si
engine_protected_content) ; cdmupdate (src/sys/cdmhost, jet.app/libexec) : serveur de composants de Google (Omaha 3.1,
oimompecagnajdejgnnjijobebaeigek), .crx3 vérifié (SHA-256), installé dans HOME/.jet/widevine. Le module 4.10.3112.0
importe 11 fonctions de plus (glibc_misc.c) : une mise à jour de Google peut en demander d'autres (cdmhost le dit).
Essais : test_eme_jet.sh KS=update, CLICKS=… (clics sous QEMU). **Jet ordinaire avec EME le 20/11** : 0008 et 0009
dans webkit et webkit-gl, webcore reconstruit (build-webkit.sh force -DENABLE_ENCRYPTED_MEDIA=ON : le cache CMake
gardait OFF), out/jet.app joue ; copies *-eme effacées. **Publié le 20/11** (847c988a, demande de l'utilisateur) :
jet 1.4.0, pc 2026.11.63 (noyau du PC refait avec la capture et l'arène haute : build/pc/esp était du 19/11 au matin),
python 1.0.4 et claude 1.0.7 (changements d'autres sessions, accord de l'utilisateur) ; mai 1.0.0 écarté à sa demande
(publish = 0 dans une copie de packages-pc.ini) ; MKREPO_EXTRA=--keep-unbuilt (koton-samples, wallpapers,
home-samples pas construits) ; gh absent de WSL : la Release envoyée par le gh de Windows, puis git push.
**ZBook (20/11)** : Spotify web joue bien (l'utilisateur) ; Netflix sans erreur ; TF1+ 720p (9-11 ms par image) mais
des images perdues : la lenteur vient du tracé de Jet sur un seul fil et du compositeur à vide (session « Jet plus
fluide sur PC »), pas de Widevine (cdmhost au repos : 10 appels/s). Netflix lançait 6 cdmhost : corrigé le
20/11 au soir (certificat gardé jusqu'au démarrage de l'hôte ; page ?sondes=5 : un seul cdmhost ; traces horodatées
« Widevine: [t s] » dans le journal du process web). Le démarrage lent (40-60 s) venait de la lecture
du module par fread (11 s pour 21,7 Mo sous QEMU), pas de la charge : read () en gros morceaux, 1 s (cdmhost
module.cpp, 20/11 soir). Image brouillée au changement de qualité (Netflix, YouTube) : av-patches/0003 (versions de
la description par image, reprise sur une image clé après un retrait) ; essai eme-shaka.html?switch=1. Reste : WebM
chiffré ; la lecture avance sans la vidéo pendant l'attente d'une clé (Chrome se met en pause).
**Publié le 20/11 au soir (ce77e2a7)** : jet 1.4.1 (tracé multi-cœurs de l'autre session + un seul cdmhost) et
spotify 1.1.0 (le lecteur web, session Spotify), mai toujours écarté. Piège : build/pc/sysroot ne reçoit pas les apps
refaites par les autres sessions (spotify y était encore l'app native de 10 Mo) ; avant de pousser, regarder la taille
des archives et l'index (git show), et installer l'app par tools/pc/apps.sh NOM puis copie dans l'arbre.

Pièges : `linux` est une macro prédéfinie de gcc sous Linux (pas comme nom de champ) ; le canari de pile du code
Linux (%fs:0x28) tombe dans le TCB de 64 octets de libposix, stable. Le module ne va jamais dans nos paquets.

**How to apply:** suite = étapes 2 à 7 de docs/etude/WIDEVINE.md § 5 ; la capture générique des syscall dans le
noyau sert aussi [[armpi-linux-compat]]. Voir [[armpi-webkit]],
[[armpi-musique]].
