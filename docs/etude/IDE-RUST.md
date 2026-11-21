# KorinStudio : un IDE en Rust et egui pour Korin (et ailleurs)

Étude du 21/11/2026, demandée par l'utilisateur : un nouvel IDE écrit en **Rust avec egui**, dans le style des apps
Craft, **portable** (Korin d'abord, puis Windows, Linux, macOS), qui prend en charge **Rust, C (gcc), l'assembleur,
FreeBASIC, le BASIC et C#**. Rien n'est fait.

**Précisions de l'utilisateur (21/11)** : la cible est **le PC** (Korin x86-64) ; **à terme ce sera le seul IDE** de
Korin (il remplace le Studio Avalonia, QBStudio…), et il prend aussi **le BASIC**.

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
| **Assembleur** | sur le PC : l'assembleur de gcc (`as`, syntaxe GNU) ; **pas de FASM x86 dans Korin** (le noyau s'assemble par le FASM de Windows) : FASM est en assembleur et a une version Linux, son port est petit (ses appels au système) ; FASMARM pour le Pi | idem, NASM… | `asm-lsp` (Rust) possible partout ; les macros de Korin (`sys SYS_xxx`, `src/inc`) à lui apprendre |
| **FreeBASIC** | `fbc` (paquet `freebasic`) | `fbc` | aucun LSP connu : tree-sitter (une grammaire existe) et l'analyse des messages de `fbc` |
| **BASIC** (à la QBasic) | le BASIC d'Onyx (`/bin/basic`, QBStudio, `.form`) **n'est pas sur le PC** (il est en assembleur AArch64) ; sur le PC, `fbc -lang qb` compile le dialecte QBasic | `fbc -lang qb` | celui de FreeBASIC ; les `.form` de QBStudio à reprendre (§ 4) |
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

## 4. Le seul IDE de Korin, à terme

Korin a aujourd'hui le **Studio Avalonia** (`avstudio`, C# : `.csproj`, éditeur d'interface Avalonia, AvaloniaEdit,
compilation dans l'OS), **QBStudio** (le BASIC d'Onyx, fenêtres dessinées en `.form` ; Pi seulement), **WebStudio**,
et l'**Éditeur**. Pour que le nouvel IDE les remplace, il lui faut, en plus du code :

- **les projets de chacun** : `.csproj` (lus par `tools/dotnet/project`, construits par `csbuild`), dossiers d'app de
  Korin (`app.txt`, pibuild), `Cargo.toml`, `Makefile`, sites web (WebStudio : un dossier servi, F5 dans Jet) ;
- **les éditeurs d'interface** : le plus gros morceau après l'éditeur de code. Deux voies :
  - un **éditeur de fenêtres à soi**, en egui, pour un format de Korin (les `.form` de QBStudio, ou un format nouveau
    qui produit du C sur UIKit, du C#, du BASIC) : l'aperçu se dessine dans l'IDE ;
  - pour **Avalonia** (`.axaml`), l'aperçu ne peut être fait que par Avalonia : un **processus d'aperçu** (le
    concepteur du Studio actuel, en C#, sans son habillage) qui peint dans une surface partagée que l'IDE affiche, et
    lui renvoie les clics (sélection, glisser) ; l'IDE garde l'arbre, les propriétés et le texte.
- **le BASIC** : sur le PC, par FreeBASIC (`-lang qb` pour les programmes à la QBasic). Le BASIC d'Onyx (classes,
  fenêtres, kits par `#import`) n'existe que sur le Pi : soit le porter au PC (VM en assembleur AArch64, donc une
  réécriture, en C ou en Rust), soit dire que le BASIC du PC est FreeBASIC. **À décider.**

Le passage se fait par étapes : l'IDE d'abord pour le code (tous les langages), puis les éditeurs d'interface ; le
Studio et QBStudio restent tant que l'IDE ne fait pas tout ce qu'ils font.

## 5. Les étapes et leur taille

| Étape | Contenu | Taille |
|---|---|---|
| 0 | Le squelette : app egui, `craft-libs` (bande du titre, panneaux, menus, réglages), arbre du projet, ouvrir et enregistrer | 2–3 jours |
| 1 | L'éditeur (corde, rendu, tree-sitter, recherche, multicurseur, annuler) | 1–2 semaines |
| 2 | Construire et lancer (pibuild, make, cargo, csbuild), sortie, erreurs cliquables, pour les cinq langages | 3–4 jours |
| 3 | Intelligence interne (symboles tree-sitter, aller à la définition dans le projet) ; client LSP ailleurs (clangd, rust-analyzer, Roslyn) | 1 semaine |
| 4 | Panneau d'agent ACP (claude-acp, Vibe) | 2–3 jours |
| 5 | Terminal intégré (pty de Korin), Git (`git` complet dans Korin), débogage DAP ailleurs | 1–2 semaines |
| 6 | Éditeur de fenêtres (UIKit, BASIC, C#), aperçu d'Avalonia par un processus à part, projets web | 3–4 semaines |
| 7 | FASM x86 dans Korin (port), et le BASIC d'Onyx sur le PC si on le veut | 1 semaine ; le BASIC : à chiffrer |

Le travail se fait **hors de l'OS** comme les Craft (cargo, puis `tools/craft/build.sh` pour Korin), et tourne d'abord
sur Linux/Windows pour aller vite, puis sous QEMU.

## 6. Ce qu'il faut décider

- ~~Le nom~~ : décidé, **KorinStudio** (21/11) : `src/apps/korinstudio` → `/apps/korinstudio.app`, catégorie
  **Développement** (pas dans la Suite ArtCraft) ; un paquet à lui dans `packages-pc.ini`.
- ~~Où vit le code~~ : décidé, **ce dépôt** (il a tout ce qu'il faut pour Korin ; 21/11) : `src/apps/<nom>`, construit par
  une recette comme `tools/craft`.
- **Rust dans Korin** : le port de rustc est en cours (autre session, LLVM fait) ; l'utilisateur dira quand il est fini.
- **Le BASIC du PC** : FreeBASIC seul (`-lang qb` pour le style QBasic), ou aussi un port du BASIC d'Onyx.
- ~~Le Studio Avalonia~~ : décidé, le nouvel IDE remplacera à terme tous les IDE de Korin (21/11).
