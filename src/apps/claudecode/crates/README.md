# ccui-shared et ccui-core : le moteur de ClaudeCodeUI

> **Origine** : ces deux caisses viennent de **ClaudeCodeUI** (« Claude Code UI »), d'Atypical Consulting,
> version 0.6.1 (dépôt archivé ; commit `0f2f974`, `crates/shared` et `crates/core`), sous **licence MIT**
> (© 2026 Atypical Consulting, texte complet dans [LICENSE](LICENSE)). Les fichiers gardent leurs commentaires
> d'origine (en anglais) ; nos changements sont marqués « Korin » et listés plus bas.

L'app Claude Code de Korin (`src/apps/claudecode`) est un frontal egui de la CLI officielle `claude`. Son moteur est
celui de ClaudeCodeUI, repris tel quel plutôt que réécrit :

| Caisse | Ce qu'elle a | Dépendances |
|---|---|---|
| `ccui-shared` (`shared/`) | les types échangés entre le moteur et l'interface (`model`, `session`, `push`, `api`), et les aides pures : blocs du fil, liste des tâches, questions, formulaires MCP, erreurs de l'API, règles d'autorisation, consommation, Markdown, diffs, i18n fr/en (`i18n/*.json`), règles des worktrees… | serde, chrono, rust_decimal, regex, pulldown-cmark… (ni tokio, ni fichiers, ni process) |
| `ccui-core` (`core/`) | le moteur : process `claude` en stream-json, analyse des évènements, état d'une session et ses commandes, registre des sessions, transcriptions (`~/.claude/projects`), worktrees, git, plugins, MCP, hooks, mémoire, workflows, agents d'arrière-plan, préférences, signets, quotas | tokio, futures, parking_lot, dirs, dunce, uuid, base64, libc |

Dans ClaudeCodeUI, une coquille Tauri (`desktop/src-tauri`) tenait le moteur (`Arc<Core>`) et l'offrait à une interface
Leptos dans une web view : `commands.rs` (114 commandes) et `sink.rs` (les évènements poussés, `trait UiSink`). Ici,
la coquille est l'app egui elle-même : les commandes deviennent des appels directs (`src/backend.rs`), et le `UiSink`
envoie les `Push` dans un canal que la boucle d'egui relève à chaque image.

## Ce qui change pour Korin

- `core/src/paths.rs` : les données de l'app vont dans `~/.config/claudecode` (`$CLAUDECODE_DATA` pour un autre
  dossier), là où l'app de Korin range déjà `organize.json` (l'original : `dirs::data_local_dir()/ClaudeCodeUI`) ;
  sous Korin, le dossier personnel est `$HOME`, sinon `/home`, sans `getpwuid`.
- `core/src/process.rs` (`program`), `core/src/cli.rs` : la commande `claude` est `$CLAUDECODE_CLI` (essais), sinon
  `/bin/claude` sous Korin (le paquet claude-code : l'hôte Linux et le binaire officiel), sinon `claude` du PATH.
- La caisse `tokio` de Korin (`tools/craft/patches/tokio-1.54.1.patch`) a appris `tokio::process` (`uid_t` et `gid_t`
  de newlib en 16 bits) ; `mio` passe par `poll()` et un tube de réveil (`--cfg` de `tools/rust/korin-vendor.sh`).
- `Cargo.toml` : les dépendances communes viennent du `[workspace.dependencies]` de `src/apps/claudecode/Cargo.toml`
  (sans la fonction `wasmbind` de chrono : pas de wasm ici).

Le reste (fork + `setsid` pour tuer l'arbre de la CLI, `ps` et `lsof` absents sous Korin : listes vides) passe tel
quel. Essais : `cargo test --workspace` dans `src/apps/claudecode` (ceux d'origine).
