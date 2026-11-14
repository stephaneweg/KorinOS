---
name: armpi-emulateurs
description: "Émulateurs NES SNES N64 GBC GBA NDS (31/10/2026) : décisions de l'utilisateur, EmuKit, manettes par HIDManager/Elegant, ce qui reste"
metadata:
  node_type: memory
  type: project
  originSessionId: d70a73ab-2638-45ff-9d2e-d44c6dec3af2
  modified: 2026-11-01T23:02:30.270Z
---

Branche `claude/emulators-d32ef0`, doc : docs/EMULATEURS.md. NES, GBC, SNES et GBA en asm (identiques au C++ d'Onyx, comparés image par image sous qemu-aarch64 ; SNES et GBA dans main depuis le 31/10 ; l'utilisateur garde ces versions asm, 10 % de gain valent la peine en jeu vidéo) ; N64 en asm aussi, recompilateur et rendu logiciel compris (dans main depuis le 01/11 ; GpuKit jamais lancé, QEMU seulement) ; NDS : cœur C++ dans `src/apps/ndsemu` sur EmuKit ; `gamelib` en asm. La référence C++ du SNES et du GBA se lit chez Onyx ; celle du N64 est dans `tools/emu/n64ref` (elle a le recompilateur, qu'Onyx n'a pas).

Décisions de l'utilisateur :
- **SNES et GBA doivent passer en asm** (GBA décidé le 31/10 : « on sait pas le faire en asm ? ») ; le C++ sert de référence.
- Manettes : **pas de page d'état ni d'InputKit**. Le mapping (`SD0:/etc/gamepad.ini`) est dans HIDManager, appliqué aux boutons de manette et aux touches ; `EV_PAD` dans l'anneau, Elegant le transmet à la fenêtre qui a le clavier (`WEV_PAD`).
- Chaque émulateur a trois vues : fenêtrée, maximisée, plein écran (F9 ; F10 et F11 sont à Elegant).
- `C:\Temp\Roms` : ROM de l'utilisateur pour les essais, sur copies, jamais au dépôt ni sur la carte du Pi ; ses `.sav` à ne pas toucher.

**Why:** but de l'utilisateur : tout l'asm possible, sans pont, et les manettes routées par le focus comme le clavier.

**How to apply:** pour réécrire un cœur en asm, méthode qui marche : harnais Linux sous qemu-aarch64 + référence C++ sur le PC + trace par instruction (`tools/emu`). Pièges : un worktree sans build complet n'a pas de noyau cohérent avec Elegant (lancer build.ps1 dans le worktree) ; registres x13/x14 perdus par render_line ; zones de la machine qui se chevauchent ; `ret xN` et `bfi ...,0,8` mal assemblés par FASMARM (`br xN`, `bfxil`) ; un label `local` de macro casse la portée des `.label`. Macros d'émission du JIT : poser les arguments dans l'ordre qui n'écrase pas `w0`. Référence N64 : `-O1 -ffp-contract=off` (en `-O2`, GCC échange les opérandes et le résultat sur deux NaN change). `romgen.py snes-stress` / `gba-stress` / `n64-stress` + `cover_*.sh` : ROM au hasard et leur couverture. Reste : applet Manette, vitesse réelle sur le Pi. Voir [[armpi-ports-sans-pont]], [[armpi-feuille-de-route]].

Saccades N64 (01/11, dans main, commit 62a6c46, non essayé sur le Pi) : la ROM d'Ocarina de l'utilisateur est **PAL** (`C:\Temp\ARMPI\SD1\Roms`, 50 Hz) ; toujours vérifier la région avant de raisonner en 60 Hz. Rejouer la scène du rêve : manette scriptée, Start et A à tour de rôle toutes les 20 images de 300 à 3000, le rêve est aux images 3500-4800. Sous QEMU, les temps de la liste graphique sont faux (flottants émulés) : profiler la référence en natif x86.

**13/11 (PC)** : NES/GB/GBA/SNES en C++ depuis Onyx (8dc73af0), n64emu C++ à deux générateurs AArch64/x86-64 (9fc11fac ; le Pi garde n64emu.asm, son rendu Mesa est plus avancé), gcemu JIT x86-64 d'Onyx (78da3e84). Sous QEMU raspi4b, GB (+20/+39 %) et SNES (+53/+71 %) plus lents en C++ : la carte du Pi garde leur asm (3e8c85ca) jusqu'à mesure sur le vrai Pi ; NES et GBA plus rapides en C++. n3dsemu x86-64 pas fait (asm AArch64 dans le C++, n3ds_kern.S), ndsemu sans JIT x86-64.
**Décisions de l'utilisateur (13/11)** : gbemu et snesemu restent en **asm sur le Pi pour de bon** (le C++ seulement sur le PC, jugé assez rapide) : ne pas retirer leur asm lors du « retrait de l'asm remplacé ». Une fois Mesa porté sur le PC, **n64emu, gcemu et n3dsemu (3DS) utilisent Mesa** sur le PC aussi (3DS ajouté le 13/11).
