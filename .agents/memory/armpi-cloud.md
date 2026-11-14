---
name: armpi-cloud
description: "Reprise par des agents du cloud (14/11) : tools/cloud/setup.sh, docs/agents/CLOUD.md, variables KORIN_* qui remplacent les chemins de la machine de l'utilisateur"
metadata:
  type: project
---

Le 14/11/2026, l'utilisateur a demandé que des agents Claude Code **dans le cloud** (Linux x86-64 neuf, sans WSL ni
Windows) puissent reprendre Korin PC et le Pi : construire et essayer sous QEMU.

* `tools/cloud/setup.sh` (étapes `root` (seule en root) `base toolchains qemu sources python pwsh verify rust webkit
  dotnet`, `all`) écrit `~/korin-cloud/env.sh` ; guide : `docs/agents/CLOUD.md`.
* Variables lues par les scripts, **sans effet quand elles sont absentes** (la machine de l'utilisateur ne change
  pas) : `KORIN_TOOLCHAINS` (défaut `/opt/toolchains`, dans tous les scripts de `tools/`), `KORIN_FASM`,
  `KORIN_FASMARM` (fasm et FASMARM assemblés sous Linux depuis `ref/` et `tools/fasmarm/source` : même sortie que
  les .exe), `KORIN_QEMU_X86`, `KORIN_OVMF_CODE`, `KORIN_OVMF_VARS` (`qemu_pc.py`), `ARMPI_QEMU`, `BUILD_PYTHON`.
* `pwsh build.ps1` marche sous Linux (Pi complet en ≈ 30 s). Chaînes précompilées : glibc ≥ 2.38. QEMU < 9.0
  (Ubuntu 24.04) n'a pas raspi4b : setup.sh construit QEMU 10.2.1.
* Vérifié : WSL 26.04 sans root et Ubuntu 24.04 neuve (chroot en espace de noms) : ktest 47/47, Pi démarré.
* À publier pour aller vite : la chaîne `x86_64-onyx-elf-14.2` en archive (`X86_TC_URL`, `X86_TC_SHA256`).

**How to apply:** un script qui ajoute un chemin propre à la machine (outil Windows, dossier de `$HOME`, `/opt`) le
rend paramétrable de la même façon et le dit dans CLOUD.md. Voir [[armpi-x86-pc]].
