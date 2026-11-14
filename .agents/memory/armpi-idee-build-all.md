---
name: armpi-idee-build-all
description: "tools/python/build-all.sh écrit le 13/11 (98ba39c1, dans main) mais PAS ESSAYÉ de bout en bout ; reste à scripter la publication partielle"
metadata:
  node_type: memory
  type: project
  originSessionId: bceb4a7a-460a-472e-b8c1-788ccf40c9c1
  modified: 2026-11-13T00:37:33.562Z
---

Fait le 13/11 sans essai (l'utilisateur l'a demandé ainsi) : syntaxe et --list vérifiés seulement. Idée d'origine : un **script maître** `tools/python/build-all.sh` qui enchaîne tout ce qu'il
faut pour reconstruire Python et Vibe pour le Pi depuis un clone neuf (autre PC Windows avec WSL, ou session cloud) :
vérifier d'abord tous les prérequis (python3.14, uv, curl, patch, /opt/toolchains inscriptible) avec un message clair,
puis dans l'ordre de docs/PYTHON-SUITE.md §2.2 : fetch.sh toolchain, fetch.sh (SANS_WEBKIT=1), posix, openssl,
python, rust, site-packages.sh, gcc/build.sh + kits.sh, agent/build.sh, office/build.sh vibe, tools/vibe/home.py ;
sauter ce qui est déjà fait (sauf --force), s'arrêter à la première erreur en la nommant. build.ps1 n'aurait plus
qu'à assembler la carte.

**Why:** aujourd'hui chaque morceau a son script (téléchargement vérifié, patchs, construction), mais rien ne les
enchaîne : l'ordre est seulement écrit dans la doc. Aussi à faire alors : scripter la publication partielle
(montage de build/sdroot_pub, docs/PAQUETS.md), faite à la main le 13/11.
**How to apply:** quand l'utilisateur le demande ; l'essai complet se fait sur une machine vierge (son autre PC).
Voir [[armpi-python-vibe]], [[armpi-vibe-competences]].
