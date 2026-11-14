---
name: armpi-x86-pc
description: "Portage de Korin sur le PC de l'utilisateur (HP ZBook Fury 17 G8) — branche claude/x86-64-pc, micronoyau, code commun en C"
metadata:
  node_type: memory
  type: project
  originSessionId: f37cb635-d720-4a7d-a203-55460bb5fb2c
  modified: 2026-11-11T20:25:06.683Z
---

Commencé le 11/11/2026 sur la branche `claude/x86-64-pc` (créée depuis claude/agents-korin), étude dans
docs/etude/ETUDE-X86.md (§0 = état). Cible unique : le PC de l'utilisateur, HP ZBook Fury 17 G8 (i7-11800H,
Intel UHD Gen12 + NVIDIA T1200, HDA ALC285, AX201, I219-V sans câble, NVMe Windows, lecteur SD Realtek 10EC:5261,
clavier PS/2 HPQ8002, pavé ELAN0750 en I2C-HID).

Décisions de l'utilisateur (11/11) :
- Korin reste un **micronoyau** sur PC.
- Dans cette branche, le code qui peut servir au Pi et au PC est **réécrit en C** (src/kernel/core + korin/arch.h) ;
  l'asm (FASM x86, FASMARM ARM) seulement pour ce qui dépend du processeur ou de la carte.
- (remplace « kits en asm x86 ») **Le plus de code commun Pi/PC** : kits, services, pilotes et apps asm réécrits
  en C ou C++, un à un, pour les deux systèmes ; seul ce qui dépend de la machine reste à part.
- Objectifs : son, USB, Wi-Fi (dès le début : pas de câble Ethernet), 3D (GPU Intel, Mesa iris).
- Démarrage sur carte SD d'abord, SSD plus tard.

État : jalon 1 (chargeur UEFI FASM, noyau C avec console GOP, ACPI, APIC, clavier, moniteur) commité ee1da300 ;
jalon 2 (process en mode utilisateur, ordonnanceur préemptif, syscall/sysret, initrd ustar, ELF) commité f57829d6 ;
jalon 3 SMP (tous les cœurs actifs de la MADT, jusqu'à 256 : l'utilisateur veut tirer parti de ses 16 cœurs
logiques mais que ça marche avec moins ou plus) marche sous QEMU 1/4/16 cœurs ; **rien encore essayé sur le ZBook**.
Test : `qemu_pc.py test --smp N`. Jalon 4 (11/11) : premier bloc des appels du Pi en C (mémoire, process, threads,
futex, services SVC_*, DEV_MAP/IRQ, horloge, aléa, journal), ktest.c 37/37 sous QEMU 1/4/16 cœurs. L'utilisateur veut
la **parité du noyau d'abord, les services ensuite** ; reste listé dans l'étude (signaux, fork/exec, IPC 0x70, images,
kits, démarrage, ws). Jalon 5 (11/11) : signaux, contrôle des tâches, SETITIMER, fork (copie sur
écriture), exec (image en mémoire seulement) ; ptest 22/22 (`urun ptest` : sans droits, fork interdit aux process à
droits comme sur le Pi). Jalon 6 (11/11) : IPC 0x70-0x7D (poignées, canaux, mémoire partagée), itest
29/29 (commité bc4d828f). Jalon 7 : kits (LIB_OPEN depuis l'initrd « nom.so ») et images préchargées, mtest
18/18 (commité 0144d948). Jalon 8 : démarrage (MOD_*, BOOT_FILE, DEMARRAGE_PRET, options), SVC_MAP_CALLER,
PROC_STATS, SVF_MEMORY ; btest 10/10 (commité 7f501d0d). Jalon 9 : canal graphique WS_* ; wtest 12/12. **Parité des appels
système atteinte** (sauf FW_CALL). Suite : services, pilotes, kits, apps en C/C++ communs, puis le ZBook.
Kits (11/11) : **en C**, **vérifiés d'abord sur le Pi** (le .so en C remplace l'asm sur la carte), puis x86 ;
infra src/kits/common + tools/kits/build_c.sh + abi2table.py ; GpuKit pas porté (12/11 : asm propre au Pi, version PC à part, même .abi).
**Tous les kits communs en C le 12/11** (BusKit, NetKit, GpioKit, DocumentKit, ArchiveKit, AppKit, UIKit, AudioKit, PrinterKit, EmuKit), commités ; noms publics français renommés en anglais (AppKit, UIKit). /bin en C sur le runtime korinrt (src/lib/korinrt, docs/BIN-C.md, tools/bin/compare.sh) : 103 commandes, lots par agents ; restent les commandes de son ; gpu*/dmareach restent en asm (Pi seul). Pilotes du PC (12/11, docs/etude/PC-PILOTES.md) : ACPI_TABLE, IRQ_MSI, pcid, usbd en C (xHCI commun) sous QEMU ; manque : un usbd pour deux xHCI, hwmanager en C, vfs/init sur PC. main fusionné le 12/11 (cf745f03). Avant : BusKit, NetKit, GpioKit, DocumentKit, ArchiveKit faits (ces deux par agents en parallèle ; essais
par comparaison asm/C/PC : src/kits/<kit>/test, tools/kits/run_pi.sh ; imports : abi2import.py) (Pi QEMU par telnet/basic, PC kbtest/nktest/gktest) ;
kit.conf : keep (relais qui gardent x1-x15, x18 sur AArch64), cflags_aarch64. Pas de
relocations dans les chargeurs : à ajouter quand un kit range des pointeurs. Telnet QEMU Pi : remote.txt
« password = essai » + ARMPI_REMOTE_PW, attendre ~80 s.
QEMU : sendkey AZERTY : « m » = semicolon, « w » = z, « a » = q. Piège : éditer du C contenant des « \ » par un script Python écrit avec Write dans le scratchpad,
pas par heredoc (les échappements sont mangés).
Piège : les scripts Python de Windows écrivent en CRLF (build.sh casse) : newline='\n' ou sed 's/\r$//'.

**Why:** choix explicites de l'utilisateur.
**How to apply:** ne jamais écrire sur le NVMe (Windows) ; Secure Boot et BitLocker sont à l'utilisateur ; QEMU :
`python -I tools/pc/qemu_pc.py test --keys ...` (sendkey en noms US, le noyau décode en AZERTY : « m » = semicolon).
Voir [[armpi-projet]], [[armpi-wifi-coupures]].

**Tout à la fin du plan (décision du 13/11)** : faire passer le noyau du Pi sur le noyau commun en C (src/kernel/core) avec une couche Pi (GIC, minuterie, MMU AArch64, vecteurs, cache ; ce qui ne sert qu'au Pi peut rester en asm), comparé sous QEMU puis sur le vrai Pi, vitesse de démarrage surveillée ; puis retirer src/kernel/*.asm. À ajouter à docs/etude/PC-PILOTES.md (« Reste ») quand l'agent du démarrage jusqu'au shell aura fini avec ce fichier.

**13/11 : démarrage jusqu'au shell (commits 318acd39..bc29ab6b, QEMU seulement)** : ps2kbd (service, SYS_IO_PORT 0x4F limité à 0x60/0x64, lancé par services.txt du PC), init.txt du PC (pcid -n, nvme, vfs SD0 blk.korin fatfs), init2 lance la console texte (src/svc/console) sans Elegant, SD1 = blk.korin /home, moniteur seulement avec cmdline monitor (secours après 45 s sans SD0:), tools/pc/sysroot.sh + korin.ps1 install (installed.txt). Pas d'extinction propre encore. À valider sur le ZBook.

**Ordre donné le 13/11 (après le shell sur le ZBook)** : 1) tout le nécessaire pour le bureau Elegant sur le PC, avec menubar et panel en bas (services et pilotes nécessaires : Elegant et programmes de session en C communs, souris PS/2/touchpad, un usbd pour les deux xHCI…) ; 2) construire les apps C/C++ pour x86-64 ; 3) porter les apps asm (celles d'Onyx depuis le source Onyx, voir [[armpi-apps-asm-onyx]]).
- 4) ensuite (13/11) : GCC, FreeBASIC (fbc) et fasm (FASM x86-64, binaire « fasm ») construits pour Korin x86-64, dans l'OS (comme /gcc du Pi). Voir [[armpi-fasm-pc]], [[armpi-freebasic]], [[armpi-gcc-sur-armpi]].
- 5) ensuite (13/11) : .NET (runtime dotnet core) pour Korin x86-64, puis les dll (WinForms, Avalonia, Studio…), et enfin Jet (WebKit). Voir [[armpi-but-dotnet]], [[armpi-avalonia]], [[armpi-webkit]].
  Précision de l'utilisateur (13/11) : sur le PC, **ne pas essayer Avalonia ni WebGL** avant le pilote du GPU (Intel) ; on les essaiera quand le GPU sera fait (pas de rendu processeur pour ces essais).
  **Ordre revu (13/11)** : le pilote GPU (Intel UHD Gen12 d'abord) passe **avant .NET et Jet** : ... GCC/FreeBASIC/fasm → GPU Intel → .NET → dll → Jet (Avalonia et WebGL essayés sur le GPU).
  Apps qui utilisent le GPU (émulateurs sur GlKit/Mesa ou EmuKit, QBasic/Viewport3D/Cosmo Zoom sur GpuKit) : **portées avec les autres apps, essayées après le GPU** (13/11). Elles passent par un kit : GlKit (Mesa, iris sur PC) et GpuKit (version PC à écrire après le pilote GPU Intel, même table .abi).
  **À la fin (13/11)** : si le pilote NVIDIA (T1200, Turing) aboutit, la 3D (GlKit/Mesa, GpuKit, Cosmo Zoom, émulateurs, WebGL, Avalonia GPU) passe sur le GPU NVIDIA, l'affichage restant sur l'Intel (Optimus : rendu NVIDIA, copie vers l'image Intel, comme PRIME). Piste Mesa : nouveau/NVK + zink, firmware GSP de Turing.

**13/11 (matin, utilisateur absent, autonomie)** : bureau Elegant en C commun Pi/PC (213530ba..f0e0d9c1 ; correctif noyau b0be8307 : shootdown hors svc_lock), chaîne x86_64-onyx-elf + libposix PC + 34 apps C/C++ (f5c0aff7..cd204e95), installé sur D: avec \apps et \fonts (576 fichiers, 7cc1cbb3 : korin.ps1 accepte apps/fonts, jamais \home). Bureau pas encore vu sur le vrai ZBook. Manques connus : horloge 00:00 (pas de source CMOS sur le bus), Terminal absent (asm). Agents lancés : apps asm hors Onyx en C ; GCC/FreeBASIC/fasm dans Korin PC. Les apps asm d'Onyx attendent l'accord de l'utilisateur pour cloner le dépôt Onyx.
**13/11 (06:30)** : sur D: (2260 fichiers) : bureau + 49 apps (16 apps asm hors Onyx passées en C, 238ec8c8..5fa95745, communes au Pi par tools/office/build.sh), GCC/make/fbc/fasm dans /gcc (55aaf97f..81f80aba), heure CMOS dans menubar (51f3ad53), sonde GPU Intel `SD0:/sys/svc/intelgpu probe` puis `probe submit -v` (5de45864, Mesa iris 4854da51, docs/etude/GPU-INTEL.md) à lancer par l'utilisateur sur le ZBook ; étape 2 du GPU attend son journal. L'agent GPU a installé LLVM 21/clang/libclc/SPIR-V dans WSL par apt. Apps d'Onyx : attendent l'accord pour cloner le dépôt Onyx.
**Ajouté à la fin du plan (13/11)**, après Jet et avant le GPU NVIDIA puis le noyau du Pi en C : bash, CPython (+ modules Rust), cible Rust x86_64-unknown-korin, Vibe et sa façade, SDL 3 + sdl2-compat avec SuperTux et SuperTuxKart, le générateur de KotonStudio. Vector après le GPU. printd portable (532f7c27).
**13/11 (matin) sur le vrai ZBook** : démarrage très rapide, tout marche sauf le touchpad (pas de pilote I²C). `intelgpu probe submit -v` RÉUSSI sur le vrai GPU (lot exécuté en 1 ms, état remis) ; framebuffer GOP à la GGTT 0 (64 Mo), accusés du forcewake lus à 0 (adresses à revoir). La sonde n'écrit que dans le journal (l'utilisateur ne voit rien au Terminal). Étape 2 du GPU lancée (agent).
**Décision (13/11)** : pilotes du touchpad (I²C HID), du son (Intel DSP/HDA) et du Wi-Fi (AX201) faits ensemble, placés après le GPU Intel et avant .NET.
**GPU étape 2 (13/11)** : service intelgpu (contexte doré, PPGTT, execlists, MSI, reset, MOCS, 15 contournements TGL) 0bed431a, faux libdrm branché + gltest --win 8a298f7f, simu seulement ; marche à suivre : intelgpu test → start → gltest --win → status/stop, puis journal. GlKit/GpuKit PC pas faits. Accusés forcewake à 0 = normal (lus hors prise).
**Son du PC lancé avant la fin du GPU (13/11, demande de l'utilisateur)** : pilote HDA en C (00:1f.3 8086:43c8 classe 04/01 cAVS sans DSP, codec ALC285 ; HDMI NVIDIA 01:00.1 10de:10fa), étude docs/etude/SON-PC.md. Touchpad et Wi-Fi restent après le GPU sauf avis contraire. Le journal montre aussi un Ethernet Intel I219 8086:15fa en 00:1f.6.
**Son du PC (13/11)** : service sound en C (12afdeed, Pi garde l'asm), pilote hda (9c05d4dd, cAVS sans DSP, ALC285 de HP, volume 50) ; vérifié dans le wav de QEMU ; DMIC et HDMI pas faits. Essai sur le ZBook : soundctl, sontest, casque, recorder --test 0, puis journal.
**bash sur le PC (13/11)** : 50b138d3 (tools/bash/build.sh ARCH=x86_64), correctifs du noyau PC 66149e0e (SIGTTOU masqué, exec avec les poignées de libposix : posixtest exec 15/0). Pas de commande clear sur le PC.
**Extinction du PC (13/11)** : SYS_POWER 0x7F, ACPI S5 et reset (cf6ac505), init2 du PC arrête programmes/volumes/nvme, commande shutdown, dialogue en C commun (2dc36b0f) ; vérifié sous QEMU depuis bash (fichier sur SD1: présent, fsck propre) ; installé sur D: (2439 fichiers) avec bash. Manque : _PTS, S3, couvercle, bouton.
**Retours du ZBook (13/11 après-midi)** : très réactif ; son HDA OK ; reboot et extinction OK ; N64 très fluide (même plein écran) ; gltest --win PASS (triangle juste, GPU ou repli inconnu : journal pas écrit, penser à `journal`). À corriger (3 agents lancés) : casque USB inutilisable (isochrone d'usbd C), bash « not owner » pour lancer SD0:/sys (sh passe par init2), GameCube ne voit pas les ROM (.rvz), soundmenu/wifimenu absents (asm), volume en curseur dans Réglages, volet .NET seulement avec le paquet dotnetcore, émulateurs hors catégorie Jeux (seul gamelib, comme Onyx), doublons dans gamelib, Cosmo Zoom (GlKit/GpuKit du PC).
**Décision (13/11 après-midi)** : quand les 3 agents des retours du ZBook (bureau, bash/USB/RVZ, GlKit/GpuKit) auront fini, commencer le pilote Wi-Fi AX201 (8086:43f0, 00:14.3).
**13/11 après-midi** : retours du ZBook traités (soundmenu/wifimenu C 216c6107, curseur volume et volet .NET si SD0:/dotnet/dllpreload 6e6facc7, émulateurs en catégorie cachée c7a6e491, doublons gamelib 2a035a76, bash par init2 416d175f, isochrone xHCI 24fd8e5b, RVZ/WIA zstd 426a7914, GlKit PC 54b3c894, GpuKit PC en C 7c6c46aa, intelgpu dans drivers.txt b20286fd). gltest du ZBook = vrai GPU. Ensuite : Wi-Fi AX201.
**Wi-Fi AX201 étape 1 (13/11)** : firmware iwlwifi-QuZ-a0-hr-b0-77.ucode dans firmware/pc (10ccfa78), service/commande iwl (fwinfo, probe, info, scan) lancé à la main (d5ef0bce), étude docs/etude/WIFI-PC.md (0d15d229) ; crypto WPA2 prête ; installé sur D: (2449 fichiers) ; à essayer par l'utilisateur. Étapes suivantes : join ouvert + DHCP, WPA2, service wifi comme le Pi, MSI-X, WPA3, pays.
**ZBook 13/11 fin d'après-midi** : 3D OK (Cosmo Zoom par GPU), casque USB OK ; musique de Cosmo Zoom et Arabesque « en ressorts » (agent son lancé : AudioKit C vs asm, fréquence) ; Wi-Fi : probe OK (QuZ-a0-hr-b0 juste) mais info sans « vivant », toutes les lectures prph = a5a5a5a2 (accès NIC/périphérie à corriger, agent Wi-Fi relancé).
**Son « ressorts » (13/11)** : pas de SoundFont sur le PC → banque FM d'AudioKit (basse une octave plus bas, timbre « ressort ») ; asm=C et Pi=PC vérifiés identiques. sysroot.sh copie GeneralUser-GS.sf2 (63d11d16), build.ps1 la cherche aussi dans ..\Onyx.
**Touchpad (13/11)** : acpidump (a9791675, ACPI_TABLE étendu) et i2chid (72276ad4, sondage 8 ms, réglages SD0:/etc/i2chid.txt, défauts ELAN0750 addr 0x15 hid_desc 0x01 ; BAR0 du LPSS laissée à 0 par le firmware, i2chid en choisit une : mieux, celle de Windows) ; installé sur D: (2456 fichiers). Attente des tables D:\acpi.
**13/11 18 h** : installé sur D: : Wi-Fi 3e version (370ce415 : rdtsc, ENABLE_WFPM avant CNVI, pas de lecture UMAC avant le firmware) ; touchpad = I2C0 00:15.0, ELAN0750 addr 0x2C, HID desc 0x20, GpioInt niveau bas (IRQ 1024 Windows), _PS3 du contrôleur efface la BAR0 ; i2chid lit la NVS par ACPI_TABLE « FWMM » (39b7e22c).
**13/11 18 h 30 sur le ZBook** : touchpad OK (i2chid run) ; à faire : vitesse x2 réglable dans Réglages, touchpad + souris USB ensemble, i2chid au démarrage. Wi-Fi : VIVANT, NVM lu, échec sur MCC (1.c8 : 32 octets envoyés, 28 attendus). Bugs : Terminal muet après quelques iwl/i2chid, journal.txt figé à 08:57. 3 agents (Wi-Fi, entrée, terminal/journal).
**13/11 19 h 45** : Wi-Fi AX201 : iwl scan liste les réseaux sur le ZBook (étape 1 réussie, a91c0f49 MCC 28 octets). Touchpad lancé par hwmanager ne marche pas ; souris USB : clic perdu après un moment (OU des boutons par pilote ?) ; agent touchpad relancé. Le shell de Korin écrit D:\.bash_history (cwd = racine SD0:).
**13/11 19 h 55** : touchpad au démarrage : drivers.txt > 4 Ko, INI_MAX 4096 coupait la ligne d'i2chid → INI_MAX 16 Ko partout (5b874edb, asm du Pi compris, pas réessayé sur le Pi) ; clic USB bloqué : boutons d'une autre souris muette > 3 s ignorés (13d56a6a). Installé sur D:. Wi-Fi étapes 2-4 (association, WPA2, netd, service wifi comme le Pi) lancées.
**Clé USB sur le ZBook (13/11)** : lue, fichiers accessibles (usbd C xHCI + usbstorage) : validé par l'utilisateur.
**Plan (13/11 soir)** : git et rg pour le PC ajoutés avant Vibe (après la cible Rust : rg est en Rust). binutils déjà faits avec GCC (55aaf97f).
**Wi-Fi étapes 2-4 (13/11 20 h)** : iwl serve = service wifi (même protocole que le Pi), association + WPA2 + CCMP matériel, wlan0 chez netd, reconnexion auto, drivers.txt + arrêt par init2 (20384fcd..ab3032a7) ; 54 Mb/s max, pas de WPA3 ; installé sur D: (wifi.txt de l'utilisateur intact, 8/11) ; pas encore essayé sur le ZBook.
**13/11 21 h** : touchpad + souris OK sur le ZBook ; Wi-Fi associé et IP OK quand netd lancé à la main → netd ajouté à skel/pc/etc/services.txt (e31f753a), liste dédoublonnée par SSID, 5 GHz prioritaire si > -78 dBm (9e923d51) ; installé sur D:. Démarrage auto du Wi-Fi à revérifier (journal).
**13/11 21 h 05 : Wi-Fi du ZBook VALIDÉ** : connexion automatique au démarrage, rapide ; nslookup et ping (example.com, google) OK. Touchpad + souris USB OK. Étape suivante du plan : .NET, dll, Jet (avec essais Avalonia/WebGL/Vector sur le GPU).
**Ordre revu (13/11 21 h)** : Jet AVANT .NET (puis .NET, dll). Pilotes touchpad/son/Wi-Fi validés sur le ZBook.
**Jet sur le PC (13/11)** : user agent de Safari de BUREAU (macOS), pas mobile (le PC est plus puissant que le Pi) ; le Pi garde le sien. Agent Jet lancé.
**Jet PC** : JavaScriptCore AVEC le JIT x86-64 (Baseline/DFG/FTL si possible), pas seulement l'interpréteur ; exigence de l'utilisateur (13/11).
**13/11 soir : Jet VALIDÉ sur le ZBook** : démarre instantanément, Wikipédia s'affiche instantanément, connexion à Gmail quasi instantanée (« hallucinant »). Commits c80510f9..c788245b ; sysroot_webkit.sh entre sysroot_apps.sh et sysroot_gcc.sh. Prochaine étape : .NET puis les dll.
**14/11 0 h 10 : installé sur D:** Jet 1.1.0 (vidéo VP8/VP9/AV1, onglets façon Safari, recherche, cookies SD0:/home/jet/webkit), git+rg (protocole 0 forcé : blocage du protocole 2 à chercher), CPython+Rust modules, Vibe (comptes, choix du modèle), cible Rust. Suite d'installation : build.sh, posix/build.sh, apps.sh, sysroot.sh, sysroot_apps.sh, sysroot_webkit.sh, sysroot_git.sh, sysroot_python.sh (verrou), sysroot_gcc.sh en dernier ; korin.ps1 accepte \python et \.vibe. Pas encore essayé sur le ZBook.
**14/11 4 h 20 : installé sur D:** noyau avec futex par adresse virtuelle (cause probable des blocages git p2/Vibe/YouTube), courses SMP, double libération kfree, mprotect de fichier, XSAVE/AVX (AVX-512 éteint, option avx512), HWP (pstate=balanced/off, cpufreq) ; Jet 1.2.0 (onglet vide, JIT WASM, --trace-media, plus de lignes net au journal) ; Vibe avec vibe-diag et journaux ; programmes reliés à la libposix corrigée. Piège : tools/python/build.sh vide site-packages, relancer site-packages.sh avant sysroot_python.sh. Pas encore essayé sur le ZBook.
**14/11 midi : Vibe VALIDÉ sur le ZBook** (connexion Mistral par Jet OK après le correctif de l'horloge : CMOS en heure locale de Windows, CLOCK_SET « temps sûr » de netd). YouTube joue avec le son sans blocage, mais pas fluide (une image sur deux ?) : chantier de la peinture vidéo.
**14/11 13 h 50 : .NET installé sur D:** (runtime, csbuild dans l'OS, WinForms, Avalonia sur CPU, Studio ; 23a7d111..681bcf0e) ; noyau PC : SIGFPE pour la division par zéro, si_pid des signaux (sinon GC.Collect bloqué). Le Pi a probablement le même blocage du GC (noyau asm sans si_pid) : à confirmer par tools/dotnet/stress sur le Pi. Pas encore essayé sur le ZBook. sysroot_dotnet.sh dans la suite d'installation (avant sysroot_gcc.sh).
