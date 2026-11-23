---
name: armpi-blender
description: "23/11/2026 : chantier Blender sur Korin PC ouvert, étude docs/etude/BLENDER.md (Vulkan seul, GHOST sur UIKit, jalon 2 = blender -b) ; rien de construit"
metadata:
  type: project
  modified: 2026-11-23
---

Demande de l'utilisateur du 23/11 : porter Blender dans Korin. Étude et état : docs/etude/BLENDER.md (§ 0).

- Cible : Korin PC seulement (le Pi n'a ni Vulkan ni GL de bureau). Blender 5.3.0 = dernière (10/11/2026), 5.2.4 LTS
  proposée ; Python 3.13 exigé (Korin a 3.14) ; GCC 14.2 comme notre chaîne.
- Korin n'a pas d'OpenGL de bureau (iris en ES) : moteur **Vulkan** de Blender sur VkKit (exigences vérifiées dans
  vk_backend.cc, anv Gen12 les remplit) ; shaderc à construire.
- GHOST : version Korin sur UIKit à écrire (modèle : tools/godot/platform/korin) ; GHOST SDL écarté (SDL de Korin
  sans Vulkan).
- OpenImageIO obligatoire ; jalons : 0 base, 1 dépendances A, 2 `blender -b` + Cycles CPU sous QEMU, 3 interface,
  4 fonctions, 5 paquet.
- Clone partiel de la 5.3.0 (build_files, intern/ghost) dans WSL ~/korin-blender/src.

**Why:** demande de l'utilisateur ; suite de Godot, Vulkan, Python, outils de construction.

**How to apply:** questions ouvertes au § 6 de l'étude (version, GL de bureau en repli, lavapipe pour QEMU) ; JOBS=4
dans WSL (9 Go). Voir [[armpi-godot]], [[armpi-cpython]], [[armpi-devtools]], [[armpi-cao]].
