# Un IDE en Rust et egui pour Korin (et ailleurs)

Étude du 21/11/2026, demandée par l'utilisateur : un nouvel IDE écrit en **Rust avec egui**, dans le style des apps
Craft, **portable** (Korin d'abord, puis Windows, Linux, macOS), qui prend en charge **Rust, C (gcc), l'assembleur,
FreeBASIC et C#**. Rien n'est fait.

## 1. Pourquoi c'est faisable maintenant

- **egui tourne sur Korin** : `egui-korin` / `eframe-korin` (docs/EGUI.md) font marcher les 17 apps Craft, avec la
  barre de menus de Korin (`muda` a une plateforme Korin, `egui_korin::menu`), le presse-papiers, les raccourcis.
  Le même code se construit pour les autres systèmes avec l'eframe de crates.io : c'est ainsi que les Craft sont faites.
- **Le style Craft est réutilisable** : `craft-libs` (storytold, **MIT ou Apache-2.0**) donne ce qu'il faut à une app
  de bureau, déjà patché pour Korin (`tools/craft/korin_libs.py`) :

  | Caisse | Pour l'IDE |
  |---|---|
  | `craft-ui` (`docking`, boutons, icônes, combos, polices) | panneaux ancrables (arbre, éditeur, sortie, problèmes), les contrôles du style |
  | `craft-ui-shell` (`TitleBar`) | la bande du titre (déjà réglée pour Elegant : 112 px à droite, titre à gauche) |
  | `craft-window` | cadre, liste des fenêtres, placement |
  | `craft-menu` | menus en arbre, raccourcis (`craft-keymap`) |
  | `craft-keymap` | raccourcis, profils, import (VS Code, Visual Studio…) |
  | `craft-settings` | réglages `config.toml`, enregistrement sûr |
  | `craft-dialogs` | dialogues de fichiers sans bloquer |
  | `craft-jsonrpc` | enveloppes JSON-RPC : la base d'un client LSP, DAP ou ACP |

- **Les compilateurs sont dans Korin** : gcc 14.2 (`/devtools/gcc`), `fbc` (FreeBASIC 1.10.1, backend gcc),
  `/bin/fasmarm` (Pi), Roslyn et `csbuild` pour les `.csproj` (`/devtools/dotnet`) ; **pibuild** (`pibuild.mk`)
  sait déjà construire un dossier d'app pour chacun (`.asm`, `.bas`, `.c`, `.cpp`, `.cs`, `.csproj`). Korin a des
  tubes et des pseudo-terminaux (vterm) : un terminal intégré est possible.

## 2. Ce qui manque, langage par langage

| Langage | Construire dans Korin | Construire ailleurs | Intelligence (complétion, erreurs, aller à la définition) |
|---|---|---|---|
| **C / C++** | gcc, pibuild, make | gcc, clang, make, CMake | clangd n'est pas dans Korin (LLVM) ; ailleurs, clangd par LSP. Dans Korin : erreurs de gcc analysées, index tree-sitter |
| **Assembleur** | FASMARM (Pi) ; sur le PC, l'assembleur de gcc (`as`) | idem, NASM… | `asm-lsp` (Rust) possible partout ; les macros de Korin (`sys SYS_xxx`, `src/inc`) à lui apprendre |
| **FreeBASIC** | `fbc` | `fbc` | aucun LSP connu : tree-sitter (une grammaire existe) et l'analyse des messages de `fbc` |
| **C#** | Roslyn, `csbuild` (`.csproj`) | `dotnet build` | un serveur Roslyn (`csharp-ls`, Roslyn LSP) : tourne sur .NET, donc **peut-être dans Korin** (à essayer : mémoire, temps de démarrage) |
| **Rust** | **en cours** : une autre session porte rustc dans Korin, LLVM est fait (21/11) ; d'ici là, la chaîne `x86_64-unknown-korin` du PC (WSL) | cargo | rust-analyzer ailleurs ; dans Korin, rust-analyzer une fois rustc et cargo là (Rust pur) |

Donc, dans Korin, l'IDE construit et lance C, asm, FreeBASIC et C# tout de suite, et **Rust dès que le port de rustc
(en cours dans une autre session, LLVM déjà fait le 21/11) sera là** : cargo dans `/devtools`, comme gcc et dotnet.
L'IDE n'a donc pas à prévoir de construction déportée pour Rust ; elle peut rester une option (ssh) pour une grosse
caisse ou un Pi trop lent.

## 3. L'architecture proposée

1. **L'éditeur** : le cœur, et le plus gros morceau. `egui::TextEdit` ne suffit pas (pas de corde, lent sur un grand
   fichier, pas de multicurseur). Un widget à soi : texte dans une corde (`ropey`), rendu des seules lignes visibles,
   coloration par **tree-sitter** (C, Rust, C#, asm, FreeBASIC ont des grammaires ; tree-sitter est en C pur et déjà
   compilé pour Korin, module Python de Vibe, docs/PYTHON.md), repli, numéros, minicarte, recherche et remplacement (regex), multicurseur,
   annuler par groupes. Compter 4 000 à 6 000 lignes.
2. **Les langages par LSP** (client JSON-RPC sur `craft-jsonrpc`) quand un serveur existe, sinon un « petit LSP » interne :
   symboles par tree-sitter, erreurs lues dans la sortie du compilateur (gcc, fbc, fasmarm, csc ont des formats
   simples). Chaque langage = un module (`lang/c.rs`, `lang/fb.rs`…) : grammaire, construction, analyse des erreurs.
3. **Les projets** : un dossier ; le genre se devine (`Cargo.toml`, `*.csproj`, `Makefile`, dossier d'app de Korin avec
   `app.txt` → pibuild). Construire, lancer, nettoyer sont des commandes de la chaîne du projet, avec sortie en direct
   et erreurs cliquables.
4. **Le débogage** par DAP plus tard : gdb parle DAP depuis gdb 14, mais il n'y a ni gdb ni ptrace dans Korin
   aujourd'hui ; ailleurs, gdb/lldb/netcoredbg.
5. **L'IA** : un panneau d'agent en **ACP**, avec claude-acp et Vibe de Korin (le même chemin que l'étude
   [CRAFT-ANYTHING-IA.md](CRAFT-ANYTHING-IA.md) ; le client ACP de Craft Anything, MIT/Apache, peut servir de base).
6. **Korin en plus** : ouvrir une app de `src/apps` ou de `HOME:/`, l'installer (`/apps`), la lancer ; les jetons du
   thème (`ui_tokens()`), l'aide des kits (`/apps/help.app/api/`) dans la complétion et au survol.

## 4. Et les IDE qui existent déjà dans Korin

Korin a le **Studio Avalonia** (`avstudio`, C# : `.csproj`, éditeur d'interface Avalonia, AvaloniaEdit, compilation dans
l'OS), **QBStudio** (BASIC), **WebStudio**, et l'**Éditeur**. Le nouvel IDE ne remplace pas l'éditeur d'interface
d'Avalonia ; il serait l'IDE **de code**, multi-langage et portable. À décider : soit le Studio reste pour C# et les
interfaces, soit le nouvel IDE le remplace à terme en ouvrant les `.axaml` dans le Studio.

## 5. Les étapes et leur taille

| Étape | Contenu | Taille |
|---|---|---|
| 0 | Le squelette : app egui, `craft-libs` (bande du titre, panneaux, menus, réglages), arbre du projet, ouvrir et enregistrer | 2–3 jours |
| 1 | L'éditeur (corde, rendu, tree-sitter, recherche, multicurseur, annuler) | 1–2 semaines |
| 2 | Construire et lancer (pibuild, make, cargo, csbuild), sortie, erreurs cliquables, pour les cinq langages | 3–4 jours |
| 3 | Intelligence interne (symboles tree-sitter, aller à la définition dans le projet) ; client LSP ailleurs (clangd, rust-analyzer, Roslyn) | 1 semaine |
| 4 | Panneau d'agent ACP (claude-acp, Vibe) | 2–3 jours |
| 5 | Terminal intégré (pty de Korin), Git (`git` complet dans Korin), débogage DAP ailleurs | 1–2 semaines |

Le travail se fait **hors de l'OS** comme les Craft (cargo, puis `tools/craft/build.sh` pour Korin), et tourne d'abord
sur Linux/Windows pour aller vite, puis sous QEMU.

## 6. Ce qu'il faut décider

- **Le nom** (en anglais, règle du dépôt).
- **Où vit le code** : un dossier de ce dépôt (`src/apps/<nom>`, construit par une recette comme `tools/craft`) ou un
  dépôt à part (plus simple à porter ailleurs, à publier).
- **Rust dans Korin** : le port de rustc est en cours (autre session, LLVM fait) ; l'utilisateur dira quand il est fini.
- **Le Studio Avalonia** : il reste, ou le nouvel IDE le remplace à terme.
