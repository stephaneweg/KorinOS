---
name: armpi-cargo
description: "cargo 0.98.0 dans Korin PC (21/11/2026, cloud) : construction, patchs, essais QEMU, ce qui attend rustc ; plan du port de Rust dans Korin"
metadata:
  type: project
---

20/11/2026 : l'utilisateur veut **Rust complètement porté dans Korin, pour Korin PC** (rustc et cargo dans l'OS).
Ordre convenu : 1) cargo croisé, 2) cible intégrée au compilateur (rustc_target, x.py), 3) LLVM pour Korin,
4) librustc_driver.so (dylib Rust, jamais essayée), 5) macros procédurales (dlopen), 6) paquet /devtools/rust.
L'utilisateur a précisé que Korin PC a ext4 (ext4fs) : ne pas raisonner sur FAT32 ; mais ext4fs n'a que des dates
à la seconde (pas les champs *_extra), pas d'écriture dans un dossier htree, pas de journal.

Étape 1 faite le 21/11 sur claude/epic-dirac-9trnk0 (docs/CARGO.md) : `bash tools/cargo/build.sh` -> build/pc/cargo/cargo,
`sh tools/pc/sysroot_cargo.sh` -> /devtools/rust/bin/cargo (pas dans le PATH). QEMU seulement : HTTP, HTTPS (vrai
crates.io, KORIN_TEST_CA pour le proxy du cloud), git en HTTP, new, vendor marchent ; build/fetch/tree/add/édition 2024
demandent rustc. Patchs : tools/cargo/patches (curl-sys, socket2, gix-sec, gix-prompt, terminal_size) + tools/craft/patches.
libc 0.2.191 des projets : tools/rust/libc-projects.sh (sortie de craft/build.sh).

Pièges : version de caisse à tiret (curl-sys 0.4.88+curl-8.20.0) ; `cargo vendor` ignore le remplacement de source
sans --respect-source-config ; testimage.py --pc veut `. ~/korin-cloud/env.sh` (KORIN_FASM) ; src/sys/linuxhost
n'avait pas de posix.conf (sysroot.sh cassé dans un clone neuf, corrigé) ; tools/pc/sysroot.sh veut aussi la
libposix du Pi (NO_TESTS=1 sh tools/posix/build.sh).

Lié : [[armpi-rust]], [[armpi-craft]].
