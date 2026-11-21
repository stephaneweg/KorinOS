---
name: armpi-ide-rust
description: "21/11 : KorinStudio, étude d'un IDE en Rust/egui style Craft (Rust, gcc, asm, FreeBASIC, C#), portable ; rustc porté par une autre session (LLVM fait)"
metadata:
  type: project
---

Le 21/11/2026, l'utilisateur a demandé d'étudier un nouvel IDE en **Rust avec egui**, dans le style des apps Craft, à
porter ailleurs ensuite, pour Rust, gcc, asm, FreeBASIC et C#. Étude : docs/etude/IDE-RUST.md (rien de fait ; à
décidé : KorinStudio, ce dépôt, il remplacera les autres IDE).

**Rust dans Korin** : une autre session porte rustc, **LLVM est déjà fait** (dit par l'utilisateur le 21/11) ; il
dira quand c'est fini. Ne pas prévoir de construction déportée de Rust dans l'IDE.

**Cible : le PC.** **À terme le seul IDE de Korin** (remplace le Studio Avalonia, QBStudio…), **BASIC aussi** (dit le
21/11). Le BASIC d'Onyx n'est que sur le Pi (asm AArch64) : **décidé : FreeBASIC seul** (`-lang qb`), pas de port. Pas de FASM
x86 dans Korin (à porter).

Même jour : docs/etude/CRAFT-ANYTHING-IA.md (claude-acp et Vibe en ACP dans Craft Anything ; Jet à la place de
Chromium par un serveur CDP, § 4 bis).

Voir [[armpi-craft]], [[armpi-ide-avalonia]], [[armpi-rust]].

**Dans ce dépôt** (décidé le 21/11 : il a tout pour Korin). **Nom : KorinStudio** (choisi le 21/11, après DevCraft écarté : Telerik DevCraft existe) ; `src/apps/korinstudio`, catégorie
**Développement**, pas dans la Suite ArtCraft.

21/11 (soir) : **KorinStudio v0.1 fait** (demande : « l'air pro comme VS Code », un copilote Vibe/Claude Code/claude-acp
qui crée des projets et travaille dans l'IDE) : `src/apps/korinstudio` (Rust, egui 0.37 ; eframe sur l'hôte,
egui-korin sous Korin car l'eframe de Korin suit la fourche d'egui de storytold), `tools/korinstudio/build.sh`,
docs/KORINSTUDIO.md. Serveur MCP 127.0.0.1:7314 (15 outils, `approve` pour Claude Code, `--mcp-bridge` stdio pour
Vibe), `~/.config/korinstudio/mcp.json`. Essayé : Linux (Xvfb) avec le vrai Claude Code : create_project, write_file,
build, run d'un projet Rust en 16 s ; Korin sous QEMU : s'ouvre, menus dans la barre, thème clair. Pas encore publié
(paquet à ajouter). Suite : LSP, éditeur à soi, pty.
