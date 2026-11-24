---
name: armpi-blender
description: "Chantier Blender 5.2 sur Korin PC (docs/etude/BLENDER.md) : jalons 0-2 faits, interface OK sur le ZBook le 24/11 (0.6 + pc 2026.11.101) ; reste lavapipe sous QEMU et jalon 4"
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

**Jalon 2 FAIT (24/11, QEMU PC + KVM)** : `blender -b` 5.2.4 dans Korin : SCENE OK (créer, enregistrer, relire un
.blend), RENDU OK (Cycles CPU 4 fils, 0,7 s). Outils de construction pour l'hôte (hosttools.sh + émulateur CMake),
patches/blender.patch (mkpatch.sh, source sous git), libposix : off64_t, timespec_get, alloca (machine/stdlib.h).
Essai : `. ~/korin-cloud/env.sh` (fasm Linux, OVMF avec shell UEFI : celui d'Ubuntu n'en a pas), `wsl -u root`,
`--qemu "-accel kvm"`, `chmod +x` du binaire. WSL passé à 12 Go (.wslconfig, accord de l'utilisateur).
L'utilisateur m'a laissé continuer seul jusqu'au jalon 5 la nuit du 24/11.

**24/11 (nuit) : interface construite** (CONFIG=gui : GHOST de Korin compilé du premier coup, lié avec UIKit, VkKit
à trampolines, shaderc ; 162 Mo). Sous QEMU sans GPU : GHOST démarre, Vulkan « No Vulkan device », puis plantage dans
GPU_context_create (DebugDraw::reset) au lieu d'un arrêt propre : à corriger. **Paquet d'essai publié à la demande
de l'utilisateur : blender 5.2.4-0.1** (PC, Release, 113 Mo, main 0ea5dbbd ; tools/blender/stage.sh + pub.py --new
--tree build/pc/blender-pkg) pour l'essayer sur le ZBook (vrai GPU Intel). lavapipe : LLVM 21 pour Korin en cours
(tools/mesa/build-llvm-korin.sh), présentation par copie écrite (tools/mesa/lvp/korin_lvp_wsi.c).

**24/11 (matin)** : paquets d'essai 5.2.4-0.2 à 0.5 publiés (external_memory coupé, assertions coupées, tables RNA
refaites par des outils de l'hôte de la MÊME configuration — hosttools.sh CONFIG=gui, sinon arguments nommés au
mauvais rang —, icône, SKILL.md). lavapipe pour QEMU : LLVM 21 pour Korin (build-llvm-korin.sh, PIC), lié en
statique (build-lvp.sh, link-lvp.sh ; kit impossible : 71 Mo > 64 Mo des tampons d'AppKit), pile 8 Mo des fils de
Mesa (korin-lvp.patch) ; vktest --win OK. Blender sur lavapipe : fenêtre + Vulkan, puis attente sans dessin
(interblocage, CPU oisifs) : à chercher. ZBook : 5.2.4-0.1 avait trouvé le GPU puis l'assertion ; 0.4+ pas encore
essayé. Essais : pctest watch:/fresh:/rips:, `. ~/korin-cloud/env.sh`, `wsl -u root`, `--qemu "-accel kvm"`.

**24/11 (midi), ZBook** : 0.5 affiche l'interface puis plante au premier mouvement : BO > 64 Mo refusé (BUF_SLOT du
noyau), image sans mémoire, faute dans une barrière d'anv. Corrigé dans korin_i915.c (BO > 64 Mo = tampons du noyau
consécutifs ; vrais points des syncobj chronologiques) publié dans **pc 2026.11.101** (--only lib/vkkit.so), et
blender 0.6 (faux message « using OpenGL instead » retiré). **Essayé par l'utilisateur : ne plante plus.** Journal utile : KORIN_DRM_DEBUG=1 ;
pile du plantage : addr2line sur install-gui/blender et out-x86_64/vkkit/vkkit.elf (kit chargé vers 0x501c000000).

**24/11 (après-midi), jalon 4 première vague** : OpenSubdiv, Manifold, GMP, FFTW3, Embree (deps.sh DEPS_B, pièges
dans l'étude § jalon 4) + Freestyle, Mantaflow, remaillage, QuadriFlow, SLIM, Cineon ; libmv coupé (Ceres) ;
test/features.py OK sous QEMU. Publié : blender 0.7 + pc 2026.11.104 (vkkit.so seul ; correction de la fermeture,
faute dans le fil de soumission d'anv quand l'anneau plein rendait le verrou). Piège récurrent : macros _L _N _S… de
ctype.h de newlib dans le code tiers.

**How to apply:** JOBS=6 (12 Go) dans WSL (9 Go). Lancer les scripts par `wsl -- sh tools/blender/…` (pas `wsl bash -c '…$X…'` :
les $ sont développés avant ; Git Bash : MSYS_NO_PATHCONV=1 pour les chemins /home). Voir [[armpi-godot]], [[armpi-cpython]], [[armpi-devtools]], [[armpi-cao]].
