---
name: armpi-craft
description: "apps Craft de storytold (Rust pur, egui) pour Korin : wordcraft-cli marche sous QEMU PC (20/11/2026), tools/craft/build.sh, pièges"
metadata:
  type: project
---

20/11/2026 : l'utilisateur veut les 16 apps Craft (github.com/storytold) sur Korin, en commençant par WordCraft.
**Le PC seulement, pas le Pi** (décision de l'utilisateur, 20/11/2026). `wordcraft-cli` 0.18.1 **marche sous QEMU
PC** (md→docx→pdf/png, info, text) ; aarch64 compile aussi, sans essai ni suite. Détail et niveaux des apps : docs/CRAFT.md.

- `bash tools/craft/build.sh [app paquet]` (WSL, x86_64 par défaut) ; travail dans ~/korin-craft.
- Pièges : libc 0.2.191 pour les projets **en plus** de la 0.2.185 de la std (deux entrées dans le [patch], sinon
  la std perd son module korin) ; rediriger **toutes** les caisses de craft-libs vers la copie patchée ;
  cap-primitives/rustix évités dans craft-font-store ; ring : `korin` dans LINUX_ABI sinon pas d'asm au lien.
- Essai PC : QEMU de WSL avec `. ~/korin-cloud/env.sh` (OVMF avec shell UEFI ; pas de QEMU côté Windows ici),
  `chmod +x` sur Korin avant de lancer.
- Hôte egui fait le 20/11 : `src/lib/egui-korin` (relais C sur UIKit + hdrshot-egui-backend, dessin CPU dans ui.surf),
  démo `/apps/eguidemo.app` (`bash tools/craft/egui.sh`) OK sous QEMU PC (souris, clavier AZERTY, presse-papiers) ;
  essais : `tools/craft/pctest.py prep|run` (key:/type: ajoutés à tools/python/pctest.py), docs/EGUI.md.
- Suite : WordCraft avec fenêtre = un `eframe` pour Korin sur egui-korin + couper winit/wayland/zbus/rfd/arboard/wgpu.

Lié : [[armpi-rust]], [[armpi-python-rust-ext]].
