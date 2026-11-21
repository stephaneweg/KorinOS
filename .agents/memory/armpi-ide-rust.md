---
name: armpi-ide-rust
description: "21/11 : KorinStudio, étude d'un IDE en Rust/egui style Craft (Rust, gcc, asm, FreeBASIC, C#), portable ; rustc porté par une autre session (LLVM fait)"
metadata:
  type: project
---

Le 21/11/2026, l'utilisateur a demandé d'étudier un nouvel IDE en **Rust avec egui**, dans le style des apps Craft, à
porter ailleurs ensuite, pour Rust, gcc, asm, FreeBASIC et C#. Étude : docs/etude/IDE-RUST.md (rien de fait ; à
décider : nom, dépôt, sort du Studio Avalonia).

**Rust dans Korin** : une autre session porte rustc, **LLVM est déjà fait** (dit par l'utilisateur le 21/11) ; il
dira quand c'est fini. Ne pas prévoir de construction déportée de Rust dans l'IDE.

**Cible : le PC.** **À terme le seul IDE de Korin** (remplace le Studio Avalonia, QBStudio…), **BASIC aussi** (dit le
21/11). Le BASIC d'Onyx n'est que sur le Pi (asm AArch64) : FreeBASIC `-lang qb` ou un port, à décider. Pas de FASM
x86 dans Korin (à porter).

Même jour : docs/etude/CRAFT-ANYTHING-IA.md (claude-acp et Vibe en ACP dans Craft Anything ; Jet à la place de
Chromium par un serveur CDP, § 4 bis).

Voir [[armpi-craft]], [[armpi-ide-avalonia]], [[armpi-rust]].

**Dans ce dépôt** (décidé le 21/11 : il a tout pour Korin). **Nom : KorinStudio** (choisi le 21/11, après DevCraft écarté : Telerik DevCraft existe) ; `src/apps/korinstudio`, catégorie
**Développement**, pas dans la Suite ArtCraft.
