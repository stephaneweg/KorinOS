# KorinStudio : l'IDE de Korin

KorinStudio (`src/apps/korinstudio`, `/apps/korinstudio.app`, catégorie **Développement**) est l'environnement de
développement de Korin, écrit en **Rust avec egui**, à la manière de **VS Code**, et portable : Korin PC (l'eframe de
Korin, [EGUI.md](EGUI.md)), Linux, Windows, macOS (l'eframe de crates.io). Il remplacera à terme les IDE de Korin
(Studio Avalonia, QBStudio…) : l'étude et le plan sont dans [etude/IDE-RUST.md](etude/IDE-RUST.md).

Son **copilote** pilote un agent de code (**Claude Code**, **Claude par l'API** avec claude-acp, **Mistral Vibe**,
**MAI**) qui travaille **dans l'IDE** : il crée des projets, ouvre et modifie les fichiers sous les yeux de
l'utilisateur, construit, lance et lit les erreurs, par le **serveur MCP** de KorinStudio.

## L'interface

Celle de VS Code, aux couleurs « Dark Modern » / « Light Modern » (le thème du système par défaut) :

| Zone | Contenu |
|---|---|
| barre du haut | les menus (Fichier, Édition, Aller, Affichage, Exécuter, Terminal, Copilote, Aide ; **sous Korin dans la barre de menus du système**), le centre de commandes (Ctrl+P), la mise en page ; sous Korin, 112 px libres à droite pour les boutons du cadre d'Elegant (dessinés sur le contenu) |
| barre d'activité | Explorateur, Recherche, Gestion de versions (Git), Exécuter et construire, Copilote |
| barre latérale | l'arbre du projet (créer, renommer, supprimer, état Git), la recherche dans les fichiers (casse, mot, regex), Git (modifications, valider), construire / lancer / nettoyer, arguments, tâches |
| éditeur | onglets, fil d'Ariane, numéros de ligne, ligne courante, coloration (Rust, C, C++, C#, assembleur GNU et FASM, FreeBASIC, Python, TOML, JSON, Markdown, Shell, Makefile, XML, INI), soulignés des erreurs, rechercher / remplacer (Ctrl+F, Ctrl+H), commenter (Ctrl+/), indentation automatique |
| panneau du bas (sa hauteur, comme la largeur des panneaux, se règle à la poignée et reste dans les réglages) | Problèmes (cliquables), Sortie (construction, exécution), Terminal (commandes, entrée du programme), Journal de l'agent |
| copilote (à droite) | la conversation, les outils de l'agent (titre, état, sortie, diff), le plan, les demandes d'accord, le mode et le modèle |
| barre d'état | branche Git, erreurs et avertissements, tâche en cours, activité du copilote, ligne et colonne, indentation, langage, port MCP |

Raccourcis de VS Code : Ctrl+P (fichier), Ctrl+Maj+P (commandes), Ctrl+G (ligne), Ctrl+Maj+B (construire), F5
(exécuter), Maj+F5 (arrêter), F8 (problème suivant), Ctrl+B, Ctrl+J, Ctrl+Alt+I (copilote), Ctrl+I (demander),
Ctrl+Maj+F, Ctrl+Maj+K, Maj+Alt+Bas, Ctrl+= et Ctrl+- (taille du code), Alt+Z (retour à la ligne).

## Les projets

Le genre d'un dossier se devine (`workspace.rs`) ; construire et lancer suivent le système :

| Genre | Reconnu par | Construire | Lancer |
|---|---|---|---|
| Rust | `Cargo.toml` | `cargo build` | `cargo run` |
| App Korin | `app.txt` | `pibuild app <dossier> <nom>` (Korin) | `/apps/<nom>.app/main` |
| C# | `*.csproj` | `pibuild` (Korin), `dotnet build` | l'app, `dotnet run` |
| Makefile (C, C++, asm) | `Makefile` | `make` | `make run` |
| FreeBASIC | `*.bas` | `fbc -O 2 main.bas … -x build/<nom>` | `build/<nom>` |
| Python | `*.py` | `python3 -m py_compile` | `python3 main.py` |

Les erreurs de gcc, g++, as, rustc, fbc, csc/csbuild, fasm et les traces de Python deviennent des **problèmes**
(`tasks.rs`) : liste cliquable, soulignés ondulés et points dans la gouttière.

**Nouveau projet** (Ctrl+Maj+N) : Rust, C, C++, **App Korin (C)** (UIKit), C#, FreeBASIC, Assembleur x86-64, Python,
dans `HOME:/projets` (Korin) ou `~/Projets`.

## Le copilote et le serveur MCP

| Agent | Programme | Protocole | MCP de l'IDE | Accords |
|---|---|---|---|---|
| Claude Code | `claude` (paquet `claude-code`, couche Linux) | `claude -p --input-format stream-json --output-format stream-json` | `--mcp-config` (HTTP) | l'outil MCP `approve` (`--permission-prompt-tool`) : la carte d'accord du copilote |
| Claude (API) | `claude-acp` (Python, SDK Anthropic, [CLAUDE-ACP.md](CLAUDE-ACP.md)) | ACP | `session/new`, HTTP | `session/request_permission` |
| Mistral Vibe | `vibe-acp` | ACP | `session/new`, stdio : `korinstudio --mcp-bridge <url> <jeton>` | idem |
| MAI | `mai-acp` ([MAI-ACP.md](MAI-ACP.md)) | ACP | `session/new`, HTTP | idem |

**Le serveur MCP** (`mcp.rs`) : « Streamable HTTP » sur `127.0.0.1:7314` (Forge 7311, TerraCube 7312, Jet 7313 ; un
port libre si 7314 est pris), jeton aléatoire à chaque lancement (`Authorization: Bearer`), origine vérifiée. Ses
outils :

| Outil | Ce qu'il fait dans l'IDE |
|---|---|
| `get_workspace` | projet, genre, éditeurs ouverts, fichier actif, curseur, sélection, fichiers non enregistrés |
| `list_project_templates`, `create_project` | crée un projet d'un modèle et **l'ouvre** (fichier principal dans un onglet) |
| `open_project`, `open_file` | ouvre un dossier, montre un fichier à la ligne voulue |
| `read_file` | le texte tel que l'IDE le voit (l'onglet non enregistré compris), lignes numérotées |
| `write_file`, `replace_in_file` | écrit sur le disque **et** dans l'onglet, montré à l'utilisateur (titre de l'onglet en bleu) |
| `list_files`, `search` | l'arbre, la recherche (`chemin:ligne:colonne: texte`) |
| `build`, `run`, `run_command` | construit / lance / exécute dans le panneau du bas ; la réponse attend la fin (ou le délai) |
| `get_diagnostics` | les problèmes de la dernière construction |
| `approve` | l'accord demandé par Claude Code, posé à l'utilisateur dans le copilote |

**Questions et plans** : les questions à choix de Claude Code (`AskUserQuestion`, qui passent par l'outil `approve`
comme les accords) ont leur carte : les options (choix simple ou multiple, avec leur description), une réponse libre
(« Autre »), Répondre ; la réponse revient à Claude Code dans `updatedInput.answers` (question -> libellé(s)), comme le SDK
d'agents l'attend (essayé le 22/11 avec le vrai Claude Code). Le plan proposé en mode Planifier (`ExitPlanMode`) se lit en
Markdown, avec Approuver le plan / Continuer à planifier.

Un fichier ouvert que l'agent change par ses propres outils (Edit de Claude Code, `write_file` de claude-acp) est relu
par l'éditeur (vérification chaque seconde) ; s'il était modifié, une barre propose de recharger.

Les agents reçoivent une consigne sur l'IDE (`app.rs`, `agent_hint`) : le système, le projet ouvert, le dossier des
projets, et de préférer les outils de l'IDE. Chaque message peut joindre le fichier actif, la position et la sélection.

## Construire

Sur l'hôte (Linux, Windows, macOS) : `cargo run --release` dans `src/apps/korinstudio` (Linux : `libxkbcommon-x11`).
Pour Korin PC, après la chaîne Rust du PC (`ARCH=x86_64 bash tools/rust/build.sh`) et `libuikit.a`
(`tools/pc/apps.sh`) :

```sh
bash tools/korinstudio/build.sh     # -> build/korinstudio/x86_64/apps/korinstudio.app (main, app.txt, icon.rgba)
```

L'eframe de crates.io y est remplacé par celui de Korin (`[patch]` donné à cargo), comme pour les apps Craft ; le
`Cargo.lock` du dépôt reste celui de l'hôte.

## Fichiers

| Fichier | Rôle |
|---|---|
| `main.rs` | l'entrée (et le relais `--mcp-bridge`) |
| `app.rs` | l'état, les commandes, les tâches, la recherche, Git, les outils MCP |
| `ui.rs` | l'affichage (barres, panneaux, onglets, palette, dialogues) |
| `editor.rs` | les documents et l'éditeur de code |
| `highlight.rs` | la coloration (lexeurs écrits à la main, sans dépendance) |
| `copilot.rs` | le panneau du copilote (conversation, outils, accords, Markdown) |
| `agent.rs` | les agents : client ACP, Claude Code en stream-json |
| `mcp.rs` | le serveur MCP de l'IDE, le relais stdio |
| `tasks.rs` | les process (construire, lancer, terminal), les diagnostics |
| `workspace.rs` | genres de projets, commandes, modèles, arbre, réglages (`~/.config/korinstudio/settings.json`) |
| `theme.rs`, `icons.rs` | les palettes de VS Code, les polices (Inter, DejaVu Sans Mono), les icônes dessinées |

## Limites et suite

- L'éditeur est le `TextEdit` d'egui (coloré, numéroté) : pas de multicurseur ni de repli ; un éditeur à soi (corde,
  rendu des lignes visibles) est l'étape suivante pour les très grands fichiers.
- Pas encore de LSP (complétion, aller à la définition), de débogueur, de terminal à pty, d'éditeur d'interfaces
  (étapes 3 à 6 de [etude/IDE-RUST.md](etude/IDE-RUST.md)).
- Le terminal lance une commande à la fois (pas de shell interactif) ; l'entrée va au programme en cours.
