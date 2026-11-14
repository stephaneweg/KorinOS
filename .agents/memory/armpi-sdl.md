---
name: armpi-sdl
description: "SDL 3 + sdl2-compat, SuperTux et SuperTuxKart en paquets split ; blocage GPU avec profondeur résolu le 5/11 (format du Z dans armgl)"
metadata:
  node_type: memory
  type: project
  originSessionId: d722d9fe-b9e6-44d7-9479-63b5546feccf
  modified: 2026-11-03T01:53:58.619Z
---

Rapport : docs/SDL.md ; scripts tools/sdl ; travail WSL ~/armpi-sdl. Dans main (40f516c, 3/11/2026). Installés par FTP avec CRC : SD0:/apps/supertux.app et supertuxkart.app, par tools/sdl/deploy.py, qui reprend après une coupure. Le 3/11, l'utilisateur a joué SuperTux sur le Pi : fluide.

**À faire quand le Pi est rallumé (il l'a éteint le 3/11, il chauffait) :** renvoyer SuperTux par deploy.py (`main` et `supertux2` changés) et, dans SD0:/home/supertux/config, remplacer `(key 44)` par `(key 4)` pour le saut. L'utilisateur veut le plein écran et les touches A et B : essayé sous QEMU seulement. Attendre son feu vert avant de contacter le Pi.

Choix : SDL 3.4.18 avec sdl2-compat 2.32.74, sources non modifiées : crochets PRIVATE de SDL, symboles SDL 3 renommés SDL3R_* dans libSDL3r.a, dlopen/dlsym remplacés par une table à la compilation. Pilotes dans tools/sdl/armpi (Elegant/UIKit, armgl sans EGL, AudioKit, WEV_PAD). armgl en références faibles : Mesa seulement avec -Wl,-u,armgl_init. tools/sdl/supertux.patch : seules retouches du jeu, touches A et B.

**Blocage GPU résolu le 5/11/2026 :** armgl déclarait le Z des fenêtres en PIPE_FORMAT_Z24_UNORM_S8_UINT, absent de la table du V3D ; tout job qui écrivait ou relisait ce Z ne finissait pas. Corrigé en S8_UINT_Z24_UNORM (glkit.so). Avant, SuperTuxKart : menus OK, course figée (plusieurs jobs par image), jeu qui tourne derrière. Prouvé sur le Pi par sdl2test --split (0 remise à zéro). **How to apply:** pour un blocage GPU, sdl2test a --keep, --split, --split-store, --fbo, --fbosize, et les scripts ztest*.sh (build/sdl/sdroot/home) ; gpuinfo compte les remises à zéro, `journal` puis grep v3d. Le service v3d se relève seul, éteint le GPU après plus de 3 blocages de suite (redémarrer). Je ne peux pas me connecter au Pi (mot de passe) : l'utilisateur copie par FTP et colle les sorties.

**Clavier-manette (règle de l'utilisateur, 5/11/2026) :** un jeu SDL lit la manette, jamais les touches mappées ; le mapping clavier -> manette est l'affaire du système (HIDManager), pour tous les jeux à venir. Le pilote SDL lit la manette 0 et avale les touches mappées (docs/SDL.md « Le clavier-manette »). **How to apply:** ne pas remapper des touches dans un jeu porté (le patch A/B de SuperTux est devenu inutile) ; vérifier avec sdl2test --pad.

Pièges d'essai : `sdroot` n'est plus à la racine, la carte construite est dans build/sdroot, le modèle dans skel/sd0. Sous QEMU, un appui bref tombe dans une seule image et SuperTux ne le voit pas : `hmp sendkey a 300` dans le script ; plus de 400 ms, la touche se répète. L'image d'essai est en AZERTY : la touche A donne `q`. RAM0: reçoit par FTP sans toucher la carte (la commande `journal` écrit SD0:/journal.txt) ; `run` de qemu_drive.py attend les pipes, passer par tools/sdl/bg.py ; armpi_remote ne rend la sortie qu'à @FIN : rediriger vers RAM0: puis tail ; pas de FTP passif sous QEMU. Dans un bloc Bash de cet outil, `\\n` devient un vrai retour à la ligne : écrire ces fichiers avec Write. Voir [[armpi-mesa]], [[armpi-pas-de-pont-onyx]].
