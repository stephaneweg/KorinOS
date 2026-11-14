---
name: armpi-onyx-manques
description: "Ce qu'Onyx avait et Korin pas (5/11/2026) : décisions de l'utilisateur app par app, ce qui est fait dans main, ce qui reste"
metadata:
  node_type: memory
  type: project
  originSessionId: 0268d609-ebc4-4e47-a5a3-79fdd81ea29e
  modified: 2026-11-05T20:55:00.331Z
---

Comparaison faite le 5/11/2026 (C:\Projects\Onyx\user\Apps, BinUtils). Décisions de l'utilisateur :
- **Pas à porter** : ledger (pas sa cible), 3dforge (prototype), rtfview (letters lit/écrit RTF, docx, odt), langconf et keyconf (dans Région et langue), wpaconf (le Wi-Fi de Korin est mieux), démos, outils llm/groq/tone/rec (pas pour l'instant).
- **Fait, dans main (340cc75f), QEMU seulement** : imageview (visionneuse par défaut ; `viewer` asm gardé sans `opens`, sa suppression est à décider), screenshot (app ; `ui_screen_grab`, `ui_minimize`, `WS_OP_MINIMIZE`), clockd (src/sys/clockd, alarmes dans l'Horloge, SD0:/etc/alarms.txt, sujet `korin.clock.alarms`), 19 commandes asm (df date uname diff hexdump nl ed zip unzip sysstat volume wget nslookup netstat whois ftp mount mkfs eject ; `SYS_PROC_STATS` 0x69, `NET_OP_SOCKETS`, `FS_SYNC`), Fichiers MAXE 4096.
- **Quake retiré** le 5/11 (ne répondait pas à ses attentes) ; voir [[armpi-quake]].
- **Jeux** (session à part, dans main) : plein écran, manette, pause commune (`gamepause.inc`, `ga::Pause`) pour snake, arkanoid, invaders, cosmozoom, pinball, critters, pipes, same, tetris ; nouveau jeu asm Bubbles.
- **Études de design** dans docs/DESIGN-ETUDES-ONYX.md : Wi-Fi (retouches), Fichiers inspiré de fileviewer, mode repos `powerd` au lieu du verrou, mode poche dans Elegant au lieu de PocketUI. Cinq décisions attendent l'utilisateur ; rien n'est mis en œuvre.

**Why:** l'utilisateur a choisi app par app ; ne pas reproposer ce qu'il a écarté.

**How to apply:** reste à essayer sur le Pi, publier les paquets (Quake encore dans le dépôt en ligne), mettre les nouvelles commandes dans la liste du selfhost au prochain jalon. Voir [[armpi-paquets]], [[armpi-selfhost-jalons]].
