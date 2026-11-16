# Vulkan sur Korin PC

Étude et état du chantier commencé le 16/11/2026 à la demande de l'utilisateur, après le port de Godot
([GODOT.md](../GODOT.md)) : avoir Vulkan sur le PC, d'abord sur le GPU Intel du ZBook (anv, le pilote Vulkan
d'Intel de Mesa), puis sur la NVIDIA (NVK), en réutilisant le même travail. Le GPU Intel et son service :
[GPU-INTEL.md](GPU-INTEL.md).

## 0. En bref

| Étape | Quoi | État |
|---|---|---|
| 1 | anv construit pour Korin, `vktest` sans affichage : instance, GPU, périphérique, un lot vérifié | **validé sur le ZBook (16/11)** : `vktest` RÉUSSI sur le vrai GPU (paquet vktest 1.0.0) ; à blanc sous QEMU aussi |
| 2 | anv en kit partagé (comme GlKit), un chargeur minimal (`vkGetInstanceProcAddr`) | à faire |
| 3 | la WSI de Korin : une surface d'Elegant (trois images, barrière, attache, comme GlKit) | à faire |
| 4 | Godot : la plateforme `korin` sur Vulkan (Forward+, Mobile) | à faire |
| 5 | NVIDIA : service du GPU (GSP), faux libdrm de nouveau, NVK ; GL par zink | plus tard |

Ce que Vulkan apporte sur l'Intel UHD du ZBook : les rendus Forward+ et Mobile de Godot (fonctions), pas de la
vitesse (Forward+ y est en général plus lent que Compatibility). Les étapes 2 à 4 servent telles quelles à NVK.

## 1. anv sur le service intelgpu

anv parle au noyau i915 comme iris : BO créés par `GEM_CREATE_EXT`, adresses choisies par le pilote (softpin),
contextes avec moteurs, espaces d'adresses, lots par `EXECBUFFER2` avec barrières chronologiques en extension,
syncobj. Le faux libdrm du PC (`tools/mesa/libdrm-pc/korin_i915.c`) sert déjà tout cela pour iris. Ce qu'anv
demande en plus :

| Besoin | Réponse |
|---|---|
| Trouver le GPU par `drmGetDevices2`, un nœud de rendu, puis `open("/dev/dri/renderD128")` | `drmGetDevice2` annonce un nœud de rendu ; `korin-pc.patch` remplace `open` par `korin_anv_open` (le service, ou à blanc par `KORIN_VK_DRY`) |
| `HAS_WAIT_TIMEOUT`, `HAS_EXECBUF2`, `HAS_EXEC_ASYNC`, `HAS_EXEC_CAPTURE` | rendus à 1 (la capture d'un BO au journal d'un blocage : sans effet) |
| La vue processeur d'un BO par `mmap` du descripteur | `korin_i915_bo_map`, comme iris ; pas de placement (`VK_EXT_map_memory_placed` : refusé) |
| `munmap` de ces vues | jamais : elles appartiennent au faux libdrm (`anv_device_unmap_bo`) |
| Les « sync files » des dma-buf (contrôle au lancement) | tenus pour présents : ils ne servent qu'au partage avec un autre programme |
| Le descripteur fermé par anv à la fin d'une instance | `open_fd` en refait un |
| Le périphérique logique rouvre le GPU (`open` dans `anv_CreateDevice`) | `korin_anv_open` aussi |
| Un build-id lisible (UUID du pilote, cache de shaders) | `korin_vk_id` : le sha1 des archives d'anv, écrit par `build-anv.sh` |
| `drmGetCap` : `DRM_CAP_SYNCOBJ`, `DRM_CAP_SYNCOBJ_TIMELINE` | 1 (sans eux, pas de sémaphore chronologique) |
| La table d'états : un `memfd` de 1 Go | 256 Mo sous `__armpi__` (`SHM_MAX` du noyau : 1008 Mo ; pages prises à l'usage) |
| Les futex (`anv_allocator.c`) | ceux du noyau (`SYS_FUTEX_WAIT`, `SYS_FUTEX_WAKE`), pour anv seulement |
| `drm_format_mod` de `vk_image` (`DETECT_OS_LINUX`) | aussi pour `DETECT_OS_ARMPI` |
| L'affichage KMS (`VK_KHR_display`, `wsi_common_display.c`) | écarté : propriété `korin` du fichier croisé |
| `drmDevicesEqual` (WSI) | dans le faux libdrm |
| `regcomp` et compagnie (`INTEL_DEBUG=attach`) | remplaçants « pas de correspondance » |

Piège de l'édition de liens statique : les tables d'entrées de Vulkan citent leurs fonctions par des références
faibles, qui ne tirent pas un membre d'archive. anv et le runtime de Vulkan sont donc pris en entier
(`--whole-archive`, comme `link_whole` pour `libvulkan_intel.so`) ; sans cela, une entrée reste à 0 (faute à
l'adresse 0 dans `wsi_device_init`).

Essai à blanc (16/11, QEMU, moniteur du noyau, `vktest` mis dans l'initrd) : `vktest --dry -d` réussit ; le GPU
annoncé est « Intel(R) UHD Graphics (TGL GT1) », Vulkan 1.4, 183 extensions, géométrie, pavage, rendu dynamique,
synchronization2, sémaphores chronologiques. Sous QEMU sans KVM, la découverte prend 30 s et le périphérique 5 s
(les shaders internes d'anv compilés par un processeur émulé). `vktest -d` : les messages du pilote (par
`VK_EXT_debug_utils`), le journal du faux libdrm, et une faute dit son adresse et le haut de la pile.

Les barrières chronologiques gardent la simplification d'iris : un syncobj garde le numéro du dernier lot qui le
signale, la valeur d'un point n'est pas suivie. Avec une seule file qui exécute dans l'ordre, attendre le dernier
lot suffit ; à revoir si Godot attend des points en avance (attente avant soumission).

Construction (dans WSL), à part du Mesa de GlKit (`out-x86_64/build-vk`, même source, même fichier croisé, même
zlib) :

```
sh tools/mesa/configure-iris.sh     # (une fois : fichier croisé, zlib, faux libdrm)
sh tools/mesa/configure-anv.sh
sh tools/mesa/build-anv.sh          # -> build/pc/mesa/sdroot/bin/vktest
```

`vktest` est relié en statique à anv (pas de chargeur : `vk_icdGetInstanceProcAddr` directement). Sur le ZBook,
le service intelgpu en marche : `vktest` (ou `vktest -v` pour toutes les extensions) ; il dit chaque étape
(`[ok]`, `[ÉCHEC]`) et finit par `RÉUSSI` si le GPU a rempli et copié le tampon. `vktest --dry` : à blanc, sans
service (sous QEMU), les lots sont comptés et ne partent pas.
