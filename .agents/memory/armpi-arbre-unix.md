---
name: armpi-arbre-unix
description: Décision du 15/11 — arbre unique à la Unix : / = volume système (plus de SD0: ni de SYS:), /home/<user>, /media/<x> pour l'amovible, /net/<x> pour le réseau, montage possible dans n'importe quel dossier ; HOME: alias par utilisateur ; /devtools/{gcc,dotnet,python} ; /sbin
metadata:
  type: project
---

Le 15/11/2026, l'utilisateur a choisi la manière Unix au lieu du renommage SD0: → SYS:.

L'arbre :
- la racine `/` est le volume système ;
- les utilisateurs sont dans `/home/<nom>` ;
- les médias amovibles se montent dans `/media/<x>` ;
- le réseau se monte dans `/net/<x>`, ou dans n'importe quel dossier (par exemple `/home/user1` monté depuis un partage SMB).

Ce chantier va avec l'étape 1 du multi-utilisateur :
- `HOME:` reste un alias par utilisateur ;
- `/gcc`, `/dotnet` et `/python` vont dans `/devtools/…` (« pour ne pas polluer la racine ») ;
- les commandes d'administration vont dans `/sbin`.

L'utilisateur a demandé de « tout faire jusqu'à ce que le chantier soit fini », et d'adapter aussi les compétences de Vibe et de Claude (`tools/vibe/home`, `tools/claude/home`, `.agents/skills`).

**Why:**
- les ports POSIX et Linux voient directement des chemins Unix ;
- le volume courant n'a plus de sens caché ;
- la racine reste propre.

**How to apply:**
- Remplace le renommage SD0: → SYS: de [[armpi-volumes-sys-home]] : plus de nom de volume pour le système.
- Les points de montage remplacent les chemins virtuels (`paths.txt`).
- Les noms `VOL:` restent acceptés en entrée pendant la transition (binaires précompilés).
- Voir [[armpi-multi-utilisateur]] et [[armpi-idee-mount-dossier]] : cette idée est maintenant nécessaire.

**État au 15/11 (soir), commit 8e30a8a :**
- Fait : vfs (points de montage, VFS_MOUNTS 53, HOME:, nom interne root/fs.root), libposix, tout le dépôt renommé (tools/unixtree.py), /devtools (tools/devtools_move.sh), réglages personnels HOME:/.config/korin (armpi/userconf.h, conf_path, uk_conf_path), compétences et docs.
- Vérifié sous QEMU sur le PC : disque mkkorindisk --no-windows --layout zbook --sysroot ; esp sans cmdline monitor (build/pc/esp-full) ; qemu_nvme.py dans WSL.
- Pi : construit, pas essayé (demande de l'utilisateur).
- **Reste : les gros ports à reconstruire.** Leurs arbres WSL et leurs sorties build/ ont disparu (GCC PC, Python, .NET, Avalonia, WebKit/Jet, SDL, git, bash PC, Mesa, Godot) ; les binaires publiés gardent les anciens chemins (SD0:, /gcc, /dotnet, /python).
- L'asm remplacé par le C n'a reçu que le renommage mécanique : les essais de comparaison asm/C (tools/*/compare.sh) diffèrent désormais.
- Pièges :
  - str_cat de korinrt/AppKit copie et rend la fin (ce n'est pas strcat) ;
  - un heredoc de bash mange les « \0 » d'un script Python : écrire ces scripts avec Write ;
  - build/pc/sysroot était à root : rm par `wsl -u root`.
