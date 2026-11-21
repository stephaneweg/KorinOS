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

Étape 2 faite le 21/11 (docs/RUST.md « La cible dans le compilateur ») : tools/rust/rustc.patch (Os::Korin, base/korin.rs,
les deux cibles, doc de plateforme, STAGE0_MISSING_TARGETS), tools/rust/rustc-build.sh (~/korin-rustc, LLVM rust-dev
de la CI, link-shared) ; korintest 52/0 et rdl 14/14 sous QEMU PC avec la cible intégrée ; linker par défaut gcc.
Pièges : x.py --target en une liste à virgules ; library/Cargo.lock à refaire (--frozen) ; doublons de libc korin avec le
tronc unix = dead_code refusé par -D warnings (allow ajouté à libc.patch).

Étape 3 faite le 21/11 (docs/RUST.md « LLVM pour Korin ») : tools/rust/llvm-build.sh (korin.cmake de la CAO, tblgen de la CI),
llvm.patch (statvfs f_flag sous __armpi__), libposix : sysexits.h, _POSIX_ARG_MAX, AT_PAGESZ. 117 libs, outils liés sans
symbole manquant ; opt/llc dans Korin = sortie identique à Linux. À retenir pour librustc_driver.so : libstdc++.a ne va
pas dans une .so (TLS) -> la prendre chez l'hôte (dlhost-cxx.ld) ; bootstrap donne CMAKE_SYSTEM_NAME=Generic à « korin »
(à patcher en Linux dans llvm.rs).

Étapes 4-5 faites le 21/11 (docs/RUST.md « rustc dans Korin ») : rustc + librustc_driver.so (LLVM statique, sans
libstdc++ ; rustc l'exporte par dlhost-cxx.ld) construits par KORIN_HOSTS=x86_64-unknown-korin rustc-build.sh ;
/devtools/rust ; cargo build/run, proc-macro, build.rs marchent sous QEMU. Corrigé en route : noyau PC (chemins d'exec
80 -> 256), libposix RLIMIT_NOFILE=64 (bfd). Pièges : cargo ne recompile pas une caisse vendue patchée (effacer
stage2-rustc/.../libX) ; changer bootstrap recompile tout le stage 1 ; mtest 17/2 existait avant.
Publiés le 21/11 (pub.py --api --manual-upload depuis le cloud, --only les fichiers changés) : pc 2026.11.71, bash 1.0.6,
gcc 1.0.13, help 1.0.7, rust-data-1 et rust 1.97.1-2 (abi >= 626 : la 1.97.1 demandait 627, que le système publié n'a pas).
Reste : ZBook, Pi, reconstruction depuis zéro. Piège : un paquet --new prend l'abi des kits du source : vérifier [repo] abi.

Lié : [[armpi-rust]], [[armpi-craft]].
