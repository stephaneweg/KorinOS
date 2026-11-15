---
name: armpi-godot
description: "Godot 4.7.2 sur Korin PC (platform/korin dans tools/godot) : éditeur, démo et export de jeux essayés sous QEMU le 15/11 ; ZBook et Pi pas encore"
metadata:
  type: project
  modified: 2026-11-15
---

Commencé le 14/11/2026 à la demande de l'utilisateur (« lance le portage de godot »), branche claude/x86-64-pc.
Rapport : docs/GODOT.md. Sources hors du dépôt (~/korin-godot/src, tools/godot/fetch.sh, 4.7.2-stable ed1daf0b).

- Plateforme `korin` dans tools/godot/platform/korin, copiée dans l'arbre par tools/godot/build.sh ; une seule
  retouche de Godot (tools/godot/korin.patch : `_S` de manifold = macro de ctype.h de newlib).
- Rendu Compatibility seul (GL ES 3.0 par armgl/GlKit) ; une fenêtre UIKit, dialogues de l'éditeur dedans ; son
  AudioKit ; Jolt avec JPH_PLATFORM_BSD ; marges de titre pour les boutons du chrome (FRAME_CAP_W).
- libposix : sys/timex.h et sys/xattr.h ajoutés (ENOTSUP).
- 15/11 sous QEMU (KVM, softpipe) : éditeur OK, démo 2D 9-14 images/s, export PCK + template_release OK.
  Le binaire exporté copié sur FAT demande `chmod +x`.

Demande de l'utilisateur (14/11) : **exporter depuis Godot sous Windows vers Korin PC** : préréglage Linux x86_64 +
modèle personnalisé template_release.x86_64 (build/pc/godot/templates) ; ensuite un greffon qui fait une app Korin.

**How to apply:** essais par `wsl -u root` + tools/godot/pctest.py (KVM : l'utilisateur de WSL n'est pas dans le
groupe kvm) ; WSL n'a que 7 Go : JOBS=6 à 8, une seule grosse compilation à la fois ; le shell de Korin ne rend
pas la main sur `&` (les captures QMP continuent). Voir [[armpi-idee-godot]], [[armpi-x86-pc]], [[armpi-mesa]].
