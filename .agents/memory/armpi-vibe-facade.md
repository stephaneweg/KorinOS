---
name: armpi-vibe-facade
description: "Façade de Vibe (étapes 7-9, 12/11/2026) : libacp et agent en C, l'app UIKit en C (pas un mode de PocketQI) pour la future version x86-64 ; Python et Vibe copiés à la main sur E:"
metadata:
  node_type: memory
  type: project
  originSessionId: 2a5bf7a3-d346-4716-9939-f127a0c8091c
  modified: 2026-11-12T19:59:49.568Z
---

Étapes 7 à 9 du portage de Vibe, faites sous QEMU le 12/11 (dans main et poussé le 12/11, 51ce3a33) : `src/lib/acp` (libacp, C), `tools/agent` (client terminal `agent`), `src/apps/vibe` (vibe.app).

Forme de l'app voulue par l'utilisateur : comme Claude Code / la CLI de Vibe — un terminal sobre (chasse fixe, fond
sombre, 3 couleurs : demande, agent, système ; pas de bulles), séances à gauche repliables, fichiers/infos/commandes à
droite, barre d'outils dans la bande du titre sans couvrir les boutons du cadre, invite sombre même sous Givre,
Entrée = Envoyer (rien si vide), Maj+Entrée = à la ligne, Échap efface le texte (interrompt seulement champ vide).

**L'app de l'étape 9 est en C** (demande de l'utilisateur, 12/11) : il veut la reprendre dans la future version x86-64 de
Korin ([[armpi-x86-pc]]). Donc une app à part sur libacp, pas le mode « code » de PocketQI (C++).

Carte du Pi (E:) le 12/11 : système 2026.11.28 par pkg ; copiés à la main (pas de paquet) vfs.mod, sys/svc/vfs, ramfs,
bin/sh, bash, git (+git-core), etc/paths.txt (arbre POSIX), python3, /python, vibe-acp, vibe-app-server ; originaux dans
le scratchpad de la session (sd_backup_vibe). L'utilisateur proposait aussi des paquets système, python, vibe : c'est
l'étape 10, à faire ([[armpi-paquets]]).

**How to apply:** toute nouvelle app destinée aussi au PC x86-64 : en C sur les ui_*. Avant une mise à jour système de E:
par pkg, savoir que ces fichiers ont été posés à la main.
