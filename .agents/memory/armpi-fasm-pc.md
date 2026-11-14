---
name: armpi-fasm-pc
description: "FASM (x86-64) à porter sur Korin PC ; l'assembleur natif s'appelle « fasm » sur les deux machines (FASMARM sur le Pi)"
metadata:
  node_type: memory
  type: project
  originSessionId: f37cb635-d720-4a7d-a203-55460bb5fb2c
  modified: 2026-11-12T15:22:00.353Z
---

Le 12/11/2026, l'utilisateur a demandé deux choses :
- porter aussi FASM, l'assembleur x86, sur Korin PC ;
- que le binaire s'appelle `fasm` dans les deux versions : `/bin/fasm` est FASMARM (cible ARM) sur le Pi et flat assembler (cible x86-64) sur le PC.

Aujourd'hui sur le Pi, c'est `/bin/fasmarm`, généré par tools/x86toa64.py, qui traduit le source x86 de fasm 1.73.30 et de FASMARM 1.44 en AArch64 et émule les appels Linux i386.

Sur PC, pas de traduction : fasm 1.73.30 a un front Linux x64 (ref/fasm17330/fasm-1.73.30.tgz, source/Linux/x64). Il suffit d'y remplacer system.inc par une couche Korin (fichiers par file_sys d'AppKit, mémoire sous 4 Go, car fasm.x64 adresse en 32 bits).

**Why:** même nom d'outil sur les deux machines, pour les scripts, selfhost et l'IDE.

**How to apply:**
- Renommer fasmarm en fasm dans tout le dépôt (environ 55 fichiers : build.ps1, selfhost, pibuild, docs, compétences), mais pas l'outil hôte tools/fasmarm/fasmarm.exe.
- Faire ce renommage quand aucun agent ne construit la carte.

Voir [[armpi-x86-pc]].
