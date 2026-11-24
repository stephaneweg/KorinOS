---
name: armpi-llamacpp-openjarvis
description: "llama.cpp construit dans Korin PC (paquet llamacpp, tools/llamacpp) et OpenJarvis natif (tools/openjarvis), 24/11/2026"
metadata:
  node_type: memory
  type: project
---

**24/11/2026** : à la demande de l'utilisateur, OpenJarvis installé sur Korin PC **sans linuxhost** (qu'il juge trop
lent). Ollama (Go, pas de Go sous Korin) remplacé par **llama-server** de llama.cpp, construit **dans Korin** (GCC,
cmake, ninja de /devtools/gcc) : `tools/llamacpp/build.sh`, paquet `llamacpp` (bin/llama-server, bin/llama-cli).
OpenJarvis : `tools/openjarvis/install.sh` (pip --user dans un PYTHONUSERBASE, remplaçants jiter et SQLite sans WAL,
patch de `jarvis gui`). Tout est décrit dans les deux README.

L'utilisateur a jugé Qwen3.5-2B trop faible (il bouclait dans Vibe) et ne veut plus de Qwen : ne pas le reproposer.
Il prévoit d'écrire un pilote pour le **NVIDIA T1200** du ZBook (10de:1fbc, TU117GLM, 4 Go) : ensuite, backend Vulkan
de llama.cpp (voie NVK), et peut-être Colibri (VRAM, RAM, NVMe) pour les modèles plus gros que la RAM.

Pièges trouvés (dans les README) : uname -m « armpi ? » trompe ggml (toolchain-korin.cmake) ; newlib sans wcwidth
hors _GNU_SOURCE ; ninja au-delà de -j3 sature une limite **globale** de descripteurs ; /tmp refuse les noms longs ;
SQLite WAL impossible (pas de mmap partagé) ; pip tue `rustc --version` (SIGKILL à 0,5 s) et cela a fait tomber
ext4fs deux fois (corrigé dans pc 2026.11.98) ; llama-server et llama-cli ne se terminent pas après SIGTERM (threads
de ggml bloqués : SIGKILL). L'app Vibe lit `~/.local/vibe`, pas `~/.vibe`.

`pcilist` (dans le dossier de l'utilisateur, ~/projects/pcilist) : liste PCI en lecture seule par PCI_OP_COUNT/INFO de
pcid, sans refaire l'énumération (pcid -l coupe le décodage des BAR, NVMe compris).
