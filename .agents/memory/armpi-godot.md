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
**Publié le 15/11** (main 660741f) : paquets godot et godot-templates 4.7.2 du PC, archives dans la Release (gh release upload) ; publication partielle par scratchpad pub_godot.py (fonctions de mkrepo, abi publiée gardée).
**15/11 retours du ZBook** : fermeture à l'ouverture d'un projet (argv[0] = « godot » → /sd0/godot) et Forward+ (vulkan) corrigés ; lenteur = intelgpu arrêté (GGTT pleine de restes après redémarrage à chaud depuis Windows), corrigé ; publiés pc 2026.11.6, godot 4.7.2-1 (main c82601b). Le disque du ZBook est monté ici en D: par SMB (\192.168.0.9\SD0) : journal.txt lisible.
**15/11 (suite)** : l'éditeur relancé tombait sur un défaut d'iris (SETPARAM VM = 1, le booléen use_global_vm) ; contourné dans korin_i915.c (espaces du process seulement) ; pc 2026.11.9 (main db6ada5). Repro QEMU : intelgpu start simu + tools/godot/test/spawn.
**15/11 VALIDÉ sur le ZBook** : Racing Kit de Kenney ouvert et importé dans l'éditeur (pc 2026.11.9, godot 4.7.2-1), GPU Intel.
**15/11 greffon d'export** : tools/godot/addons/korin_export (EditorExportPlugin sur le préréglage Linux : <id>.app avec main, app.txt, icon.rgba, zip, install_dir) ; essayé sous QEMU ; publié godot/godot-templates 4.7.2-2 (main 93dc460) ; section pck des modèles gardée (-u pck_section_dummy_call).
**16/11 retour du ZBook** : un gros jeu construit et joué sur Korin PC (export OK), mais plus lent qu'attendu (Compatibility sur l'Intel). L'utilisateur a choisi de **régler l'Intel d'abord** (avant NVIDIA/Vulkan) : intelgpu demande une fréquence fixe au PCU (rps.c, freq=max par défaut), Mesa iris avec cache de shaders (base de données, zlib du sysroot de Jet, korin_glkit_id), mesure des échanges d'images au journal (/etc/armgl-stats) ; docs/etude/GPU-INTEL.md § 13 étape 5. Suite : selon la mesure, échange sans glFinish et copie par le copieur ; puis anv (Vulkan Intel), puis NVIDIA (GSP + NVK + zink).
**16/11 publié** : pc 2026.11.22 (main 1c0bed92), publication partielle : seuls intelgpu (x2) et glkit.so remplacés ; intelgpu-test (paquet d'essai, main 7895d33b) retiré de l'index. /etc/armgl-stats à créer à la main pour la mesure.
**16/11 échange sans glFinish** (branche 9bbaa948, publié en pc 2026.11.23, main cdb61caf) : trois images par fenêtre GL du PC, un thread de présentation (fence_finish puis attache), repli /etc/armgl-sync ; pas essayé sur le ZBook (QEMU = softpipe = ancien chemin).
**16/11 Vulkan, étape 1** (branche 9a9ec0e7) : anv construit à part (configure-anv.sh, build-anv.sh, build-vk), vktest statique ; vktest --dry RÉUSSI sous QEMU (moniteur, vktest dans l'initrd : scratchpad vkinitrd.py) ; paquet d'essai build/pc/vktest-1.0.0.opk pas publié ; vrai GPU à essayer. Étapes suivantes : kit + chargeur, WSI Korin, Godot (docs/etude/VULKAN.md).
**16/11 Vulkan étape 1 VALIDÉE sur le ZBook** : vktest RÉUSSI sur le vrai GPU (paquet vktest 1.0.0 publié, main 29944e77). Suite : étape 2 (anv en kit + chargeur).
**16/11 Vulkan étapes 2 à 4 publiées** (branche b2c23abc..51414a23, main 20d484bb) : VkKit (/lib/vkkit.so dans pc 2026.11.29), WSI de Korin (surface headless + VkKorinSurfaceInfo), vktest 1.0.1 (--win), godot/godot-templates 4.7.2-2.2 avec Forward+/Mobile (repli Compatibility sans VkKit). Pas encore essayé sur le ZBook. Script de publication partielle : scratchpad pubvulkan.py.
