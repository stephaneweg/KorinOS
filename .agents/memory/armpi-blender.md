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

**Décisions de l'utilisateur (23/11)** : Blender **5.2 LTS** ; **Vulkan seul** (GL de bureau peut-être un jour) ;
**lavapipe** construit pour essayer l'interface sous QEMU.
**Jalon 0 fait (23/11, main 8a082cfe)** : tools/blender (env.sh, korin.cmake, versions.py qui lit versions.cmake de
Blender, fetch.sh) ; sources de 5.2.4 + 26 archives de classe A dans WSL ~/korin-blender/src (somme de Blender : .md5sum,
pas de .sha256). Suite : jalon 1 (dépendances A, ordre dans l'étude § 4).

**Jalon 1 fait (24/11)** : `tools/blender/deps.sh` (tout en statique dans ~/korin-blender/x86_64/deps, libs de Jet
recopiées, retouches dans tools/blender/patches) et `python.sh` (CPython 3.13 par tools/python/build.sh avec PYVER,
PYPATCH, STATIC_LIBPY) ; rien encore lancé dans Korin ; numpy reporté au jalon 4 (meson croisé). Suite : jalon 2
(`blender -b`, WITH_HEADLESS). Pièges : jamais -I vers le sysroot de Jet (copies des en-têtes de libposix) ;
build/pc/posix peut retarder sur src/lib/posix.

**How to apply:** JOBS=4 dans WSL (9 Go). Lancer les scripts par `wsl -- sh tools/blender/…` (pas `wsl bash -c '…$X…'` :
les $ sont développés avant ; Git Bash : MSYS_NO_PATHCONV=1 pour les chemins /home). Voir [[armpi-godot]], [[armpi-cpython]], [[armpi-devtools]], [[armpi-cao]].
