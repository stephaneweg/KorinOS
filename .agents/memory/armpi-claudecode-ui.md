---
name: armpi-claudecode-ui
description: "23/11 : l'app Claude Code (src/apps/claudecode) reçoit l'interface et le moteur de ClaudeCodeUI (MIT), sans Tauri, en egui ; branche claude/claudecode-ui, Linux dev + QEMU PC"
metadata:
  type: project
---

Demande de l'utilisateur (23/11/2026) : « porter ClaudeCodeUI sur Korin, sans Tauri » : l'app Claude Code de Korin
garde son nom, son paquet (claude-code), egui-korin et son thème (theme.rs : pas les 5 thèmes de ClaudeCodeUI), et
reçoit TOUTES les fonctions et la disposition de ClaudeCodeUI ; on garde ses extras (dossiers, épingles, rangement).
Étude et inventaire : docs/etude/CLAUDECODE-UI.md. Branche claude/claudecode-ui (pas poussée).

- Moteur repris tel quel : `src/apps/claudecode/crates/{shared,core}` (ccui-shared, ccui-core, MIT, crates/README.md,
  LICENSE). Adaptations Korin : données ~/.config/claudecode (CLAUDECODE_DATA), CLI = CLAUDECODE_CLI | /bin/claude |
  PATH, $HOME, pas de ps ni lsof sous Korin. tokio::process pour Korin : tools/craft/patches/tokio-1.54.1.patch
  (uid/gid 16 bits) — le paquet rust (vendor) n'est pas republié avec ce patch.
- Coquille : backend.rs (runtime tokio, commandes Tauri → appels directs `be.task/fire/blocking`, UiSink → canal),
  state.rs (miroir : patchs, flux, brouillon), host.rs (liens, sélecteur de dossier, titre), app.rs (rail | page |
  inspecteur, raccourcis, barre de menus de Korin, bande de titre sous Korin). UI portée par 5 agents (fil/en-tête,
  composeur, inspecteur, pages, extensions) puis fusionnée.
- Deux verrous : Cargo.lock (hôte) et Cargo.korin.lock (Korin) ; tools/claudecode/build.sh les échange et prépare
  korin-vendor dans ~/korin-craft/korin-vendor.
- Hôte de développement Linux : `bash tools/claudecode/dev.sh` (eframe), `--shot f.png --route … --click …` : image
  sans fenêtre par le rendu CPU de Korin. Les vraies sessions de ~/.claude.
- Pièges : sous Korin, aucun appel à UIKit avant la fenêtre (system_theme dans App::new = saut à 0, vu sous QEMU) ;
  glyphes : beaucoup manquent (▦ ⎇ ⧉ ➤ ↶ ✎ ⑂ ◐), widgets::icon n'a que des glyphes vérifiés ; le shell de telnet
  attend les apps même avec & : `spawn:` ajouté à tools/python/pctest.py.
- Essayé sous QEMU PC (KVM, tools/craft/pctest.py) : l'app s'ouvre, rail, menus, une transcription relue et montrée.
  Pas essayé : une vraie session claude sous Korin (pas de claude ni de compte dans l'image d'essai), le ZBook, le Pi.
  claude-code 2.1.330.10 dans versions.ini, pas publié.
