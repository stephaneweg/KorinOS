---
name: armpi-rust
description: "cible Rust aarch64-unknown-korin (compilation croisée depuis WSL), branche claude/rust-target-06eed0, état et pièges"
metadata:
  node_type: memory
  type: project
  originSessionId: a2401313-6b2a-4f7a-b066-3d3672b725c9
  modified: 2026-11-12T01:37:34.679Z
---

Étape 4 de docs/PYTHON.md faite le 12/11/2026 sur la branche claude/rust-target-06eed0 (commit 6d06865c, pas dans main,
non poussée) ; QEMU seulement, pas essayé sur le Pi. Détail : docs/RUST.md.

- Compilation croisée : rustc tourne dans WSL, pas sur Korin (l'utilisateur a demandé la différence).
- Rust 1.97.1 + RUSTC_BOOTSTRAP=1 et -Zbuild-std ; rust-src patché par tools/rust/libc.patch et std.patch ;
  `bash tools/rust/build.sh` refait tout dans ~/.cache/korin-rust ; `cargo korin build`.
- Pièges : build-std ne recompile pas la std quand rust-src change (effacer target/) ; libc de la std vient du registre,
  d'où le [patch.crates-io] dans library/Cargo.toml ; --gc-sections de rustc retirait le mot nul de .eh_frame (dlmod.S "aR") ;
  le dérouleur de libgcc est GLOBAL HIDDEN → relais __posix_unw_* + libgcc_mod.a pour les modules.
- Reste : current_exe/getcwd en SD0:/ (pas absolus pour Rust), RamFS readdir pendant les effacements, essais unitaires de la std (x.py).

Lié : [[armpi-dlopen]], [[armpi-cpython]], [[armpi-agent-ia-korin]].
