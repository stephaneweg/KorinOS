# ClaudeCodeUI dans l'app Claude Code de Korin (sans Tauri)

Demande de l'utilisateur (23/11/2026) : « porter ClaudeCodeUI sur Korin, mais sans Tauri » : partir de l'app Claude Code
de Korin (`src/apps/claudecode`, egui sur `egui-korin`) et lui donner **toutes les fonctions et la disposition** de
ClaudeCodeUI, **sauf le thème** (l'app garde l'apparence de Korin, `theme.rs` : « Studio » sombre et clair qui suit
le thème du système), et garder ce que l'app de Korin avait en plus (dossiers, épingles, rangement).

## 1. La source

ClaudeCodeUI (« Claude Code UI », Atypical Consulting, **MIT**, v0.6.1, dépôt archivé le 21/11/2026) est un frontal
de bureau de la CLI officielle `claude` (protocole stream-json de l'Agent SDK). Un espace de travail Cargo :

| Caisse | Taille | Rôle | Ici |
|---|---|---|---|
| `crates/shared` (`ccui-shared`) | ≈ 7 500 lignes | types échangés, aides pures, i18n fr/en (1 178 clés), Markdown, diffs, règles | **repris tel quel** : `src/apps/claudecode/crates/shared` |
| `crates/core` (`ccui-core`) | ≈ 11 400 lignes | le moteur : process `claude`, sessions, transcriptions, worktrees, permissions, MCP, plugins, hooks, mémoire, quotas | **repris**, adapté à Korin : `src/apps/claudecode/crates/core` |
| `ui` (`ccui-ui`) | ≈ 18 800 lignes | l'interface en Leptos (wasm), `app.css` | **réécrite en egui** : `src/apps/claudecode/src` |
| `desktop/src-tauri` | ≈ 1 250 lignes | la coquille Tauri : `commands.rs` (114 commandes), `sink.rs` (évènements poussés), mise à jour, notifications | **remplacée** par `backend.rs` (appels directs) et `host.rs` |

Pourquoi `crates/` dans l'app plutôt que `src/lib/ccui` : seule cette app s'en sert (AGENTS.md : ce qui sert à
plusieurs programmes va dans un kit ; ce n'est pas le cas), et l'espace de travail Cargo de l'app les construit avec
elle, avec un seul verrou. Licence et mention d'origine : `crates/LICENSE`, `crates/README.md`.

## 2. Comment c'est branché

```
egui (app.rs, layout/, pages/, session/)
   │  lit le miroir (state.rs : AppStore, SessionHandle)     ▲ Push (patchs, résumé, préférences…)
   │  appelle le moteur : backend.task / fire / block        │ canal (ChannelSink : UiSink)
   ▼                                                         │
Backend (backend.rs) : runtime tokio à part, Arc<Core>  ─────┘
   ▼
ccui-core : SessionManager, LiveSession (process `claude -p --input-format stream-json …`), transcript_store, …
```

- **Les commandes Tauri deviennent des appels directs** : l'interface appelle `core` sur le runtime (`be.task(async
  move { … })` rend un `Task<T>` relevé aux images suivantes ; `be.fire(what, fut)` pour ce qui ne rend rien, les
  erreurs vont dans un bandeau ; `be.blocking(f)` pour les lectures de fichiers ; `be.block(fut)` pour l'instantané).
- **Le sink devient un canal** : `ChannelSink` (le `UiSink` du moteur) envoie chaque `Push` dans un `mpsc` et réveille
  egui (`request_repaint`) ; la boucle les relève à chaque image (`AppStore::poll`) et les applique au miroir selon
  les règles de ClaudeCodeUI (`docs/ARCHITECTURE.md` §2 de la source : patch par version, flux de texte par
  incréments, brouillon par révision, trou → instantané relu).
- **Les notifications natives** deviennent des bandeaux dans la fenêtre ; **les liens et le sélecteur de dossier**
  passent par `host.rs`, que la boucle de la fenêtre sert (Korin : `egui_korin::Host`, Jet et le sélecteur d'UIKit ;
  Linux : `xdg-open`, `zenity`).
- **Pas de mise à jour automatique** (les paquets de Korin s'en chargent), pas de web view, pas d'IPC.

## 3. Ce qui change dans le moteur pour Korin

- Données de l'app dans `~/.config/claudecode` (préférences, signets, coûts, limites, `organize.json`) ; dossier
  personnel par `$HOME` (sinon `/home`).
- La CLI : `$CLAUDECODE_CLI`, sinon `/bin/claude` (paquet claude-code : linuxhost + binaire officiel), sinon le PATH.
- tokio : la caisse de Korin (`tools/craft/patches/tokio-1.54.1.patch`) a gagné `tokio::process` (uid/gid de
  newlib en 16 bits) ; mio par `poll()` et un tube (`--cfg` de `korin-vendor.sh`). Les process : fork + `setsid`
  (la std de Korin passe toujours par fork) ; tuer = `kill(-pgid)` (bash de /bin/claude, linuxhost, claude).
- `ps` et `lsof` n'existent pas sous Korin : « ouverte ailleurs » et l'occupation des worktrees restent vides.
- Deux verrous Cargo : `Cargo.lock` (l'hôte de développement, `cargo test`) et `Cargo.korin.lock` (libc, tokio, mio,
  socket2 patchés) ; `tools/claudecode/build.sh` les échange.

## 4. La disposition (celle de ClaudeCodeUI)

```
┌ rail (redim.) ─┬ page ──────────────────────────────┬ inspecteur (redim., Ctrl I) ┐
│ ■ Claude Code   │ en-tête : nom · branche · dépôt ·  │ demande d'autorisation       │
│ + Nouvelle  AltN│          mode           [actions]  │ (diff, Autoriser, Refuser,   │
│ 🔍 Rechercher ^K│ fil : messages, outils (registre), │  Toujours autoriser + portée)│
│ ⊞ Vue d'ensemble│  réflexion, compaction, erreurs,   │ tâches d'arrière-plan        │
│ ⌥ Worktrees  n  │  texte en cours                    │ outil choisi (sortie, entrée,│
│ ◎ Arrière-plan  │ liste des tâches (todo)            │  JSON) / agent / workflow    │
│ 🔌 Extensions   │ composeur : texte, @, /, pièces    │ diff (/diff) / contexte,     │
│ ⚙ Réglages      │  jointes, modèle, effort, mode,    │  mémoire, état, MCP          │
│ ACTIVES (dépôt) │  Interrompre (Échap), file         │                              │
│ rangement Korin │ ligne d'état : tour, coût, outils  │                              │
│ quotas 5 h / 7 j│                                    │                              │
│ état de l'app   │                                    │                              │
└─────────────────┴────────────────────────────────────┴──────────────────────────────┘
          palette Ctrl K par-dessus ; Ctrl ⇧ A : la décision suivante ; Ctrl ⇧ O : vue d'ensemble
```

Le rail garde, à la place des 8 « Récentes » de ClaudeCodeUI, **le rangement de l'app de Korin** : filtre, épinglées,
dossiers (glisser, clic droit : renommer, épingler, ranger, copier l'identifiant, supprimer), puis toutes les
conversations par date. La barre de menus de Korin (Fichier, Session, Affichage) donne les mêmes commandes.

## 5. Inventaire : écrans et fonctions de ClaudeCodeUI → egui

| ClaudeCodeUI (Leptos) | Fonction | egui (ici) |
|---|---|---|
| `layout/main_layout.rs` | grille, séparateurs (largeurs gardées), raccourcis | `app.rs` |
| `layout/rail.rs`, `rail_session_row.rs`, `mascot.rs` | rail, sessions actives par dépôt, récentes, état | `layout/rail.rs` (+ rangement de Korin) |
| `layout/quota_meter.rs` | quotas 5 h / 7 j, modèles, crédits, détail de la semaine | `layout/quota.rs` |
| `layout/command_palette.rs` | palette Ctrl K : sessions, récentes, recherche dans les transcriptions, actions | `layout/palette.rs` |
| `pages/new_session.rs` | dossier, dossiers récents, worktree isolé (nom), mode, agent, dossiers en plus | `pages/new_session.rs` |
| `pages/overview.rs` | toutes les sessions (état, mode, dernière action, outils, durée, coût), file des décisions (J K, Entrée) | `pages/overview.rs` |
| `pages/background.rs` | agents d'arrière-plan (`claude logs/stop/respawn/rm`) | `pages/background.rs` |
| `pages/worktrees.rs` | découverte, classement, taille, push, nettoyage sûr pas à pas | `pages/worktrees.rs` |
| `pages/extensions/*` | MCP (état, activer, reconnecter, authentifier), skills, agents, plugins et places de marché, hooks (éditeur), règles d'autorisation | `pages/extensions.rs` |
| `pages/appearance.rs` | thèmes (non repris), taille du code, langue, réflexion, horodatage, notifications | `pages/settings.rs` |
| `session/session_header.rs`, `session_title.rs` | nom (renommer), branche, dépôt, mode, PR, export, fork, Remote Control, arrêter | `session/header.rs` |
| `session/thread.rs`, `tool_row.rs`, `ledger.rs`, `file_card.rs`, `workflow_card.rs`, `hook_activity.rs`, `api_error_card.rs`, `crash_banner.rs`, `streaming_tail.rs`, `todo_panel.rs` | le fil, le registre des outils, cartes, signets, copier, rewind | `session/thread.rs` (et fichiers voisins) |
| `session/composer.rs`, `mention_popover.rs`, `slash_popover.rs`, `model_menu.rs`, `mode_menu.rs`, `config_menu.rs`, `common/effort_slider.rs`, `rewind_dialog.rs`, `status_line.rs` | le composeur, @, /, modèle, effort, mode, /config, pièces jointes, file d'attente (envoyer maintenant, annuler), suggestions, historique | `session/composer.rs` (et voisins) |
| `session/inspector.rs`, `permission_card.rs`, `question_form.rs`, `elicit_form.rs`, `diff_panel.rs`, `tool_detail.rs`, `tasks_panel.rs`, `task_actions.rs`, `agent_panel.rs`, `context_panel.rs`, `status_panel.rs`, `memory_panel.rs`, `workflow_panel.rs`, `extra_dirs.rs`, `sign_in.rs` | l'inspecteur et ses panneaux, les autorisations avec diff et portée, questions, formulaires MCP | `session/inspector.rs` (et voisins) |
| `common/markdown.rs` (pulldown-cmark + highlight.js) | Markdown | `md.rs` de l'app de Korin (sélectionnable, Copier) |
| `common/duck.rs` (BlazorKawaii) | la mascotte | une ligne d'état (pas de canard) |
| 5 thèmes sombres, `app.css` | apparence | **non repris** : `theme.rs` de Korin |

Ce que l'app de Korin avait et qui reste : dossiers, épingles, filtre, renommer et supprimer une conversation passée
(rail) ; barre de menus de Korin ; sélecteur de dossier d'UIKit ; thème du système. Ce qui n'est pas repris pour
l'instant (voir § 7) : l'onglet Terminal et les sessions de Claude Code sur le web de l'ancienne app.

## 6. Essayer

- `TEST=1 bash tools/claudecode/build.sh` : les essais (ccui-shared, ccui-core, l'app), puis l'app pour Korin PC
  (`build/pc/apps/sdroot/apps/claudecode.app`).
- `bash tools/claudecode/dev.sh` : l'hôte de développement Linux (fenêtre eframe) sur le `claude` installé ;
  `--shot f.png --route overview …` : une image sans fenêtre, faite par le rendu CPU de Korin (le même
  qu'egui-korin) : c'est ainsi que la disposition est vérifiée.

## 7. État (23/11/2026)

Fait (branche `claude/claudecode-ui`) :

- étapes 1 et 2 : cet inventaire ; ccui-shared et ccui-core dans le dépôt, construits pour Linux et Korin PC, leurs
  essais verts (98 + 95 ; l'app : 102) ;
- étape 3 : la coquille (backend.rs, state.rs, host.rs, app.rs), le rail avec le rangement de Korin, les quotas,
  l'hôte de développement (`dev.sh`, `--shot`) ;
- étapes 4 à 6, faites par cinq agents puis fusionnées : le fil (registre d'outils, Markdown avec tableaux et blocs
  de code, réflexion, compaction, sous-agents, workflows, cartes, signets, copier, fork, rewind, texte en cours,
  reprise après plantage), l'en-tête (renommer, PR, export, Remote Control, terminer, titre de la fenêtre), le
  composeur (brouillon tenu par le moteur, envoi, envoyer maintenant, file, interrompre, historique, `/` et `@`,
  modèle, conseiller, style, effort, mode, `/config`, Rapide, Réflexion, Ultracode, pièces jointes par chemin,
  ligne d'état), l'inspecteur (autorisation avec diff et portée, plan, questions, formulaires MCP, tâches, outil :
  sortie, entrée, JSON brut, agent, workflow, diff non validé, contexte, mémoire, dossiers en plus, état,
  connexion), la vue d'ensemble et la file des décisions, la palette Ctrl K (recherche dans les transcriptions),
  l'arrière-plan, les worktrees (nettoyage pas à pas), Nouvelle session, Extensions (MCP, skills, agents, plugins
  et places de marché, hooks, règles d'autorisation), Réglages (langue, thème de Korin, taille du code, drapeaux,
  à propos) ;
- sous Korin : une bande de titre (les boutons d'Elegant), pas d'appel à UIKit avant la fenêtre, pas de `ps` ni de
  `lsof` ; la langue par défaut est le français.

Vérifié : captures de l'hôte de développement (sombre et clair) sur de vraies sessions (quelques sessions haiku
très courtes : envoi, autorisation d'un Edit par Entrée, question) ; **sous QEMU PC (KVM)** : l'app s'ouvre (barre de
menus de Korin, rail, palette, une transcription relue et affichée par ccui-core sur le runtime tokio de Korin).

Pas vérifié : une vraie session `claude` sous Korin (pas de CLI ni de compte dans l'image d'essai), le ZBook, le Pi ;
à l'écran : le tableau et la file de la vue d'ensemble avec des sessions vivantes, le plan de nettoyage des
worktrees, les cartes d'agent et de workflow, la boîte du rewind.

Manque ou simplifié :

- pièces jointes : pas de collage d'image ni de glisser-déposer sous Korin (un champ de chemin ; sur Linux, le dépôt
  de fichiers) ; pas de vignettes (pas de décodeur d'images) ;
- la taille du code est enregistrée mais pas encore appliquée au fil (`theme::CODE` fixe) ;
- pas de navigation au clavier dans les tableaux des Extensions et des Worktrees ;
- `/mcp`, `/hooks`, `/plugins` ouvrent Extensions sans choisir l'onglet ;
- notifications natives : un bandeau dans la fenêtre ; pas de canard (mascotte) ; pas de mise à jour automatique ;
- de l'ancienne app de Korin : l'onglet Terminal et les sessions de Claude Code sur le web ne sont pas repris
  (term.rs reste dans le dépôt) ;
- le patch de tokio (`tokio::process`) n'est pas encore dans le paquet `rust` publié (son vendor).
