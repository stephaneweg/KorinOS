//! La page d'une session (`ui/src/session/*` et `pages/session_page.rs` de ClaudeCodeUI) : l'en-tête, le fil, la
//! liste des tâches, le composeur au centre ; l'inspecteur à droite (la demande d'autorisation en attente, les tâches
//! d'arrière-plan, l'outil ou l'agent choisi, le diff, le contexte).
//!
//! Chaque session ouverte a son `SessionView` (gardé par app.rs) : l'état d'interface qui doit survivre d'une image à
//! l'autre (le brouillon tapé, les menus ouverts, l'onglet de l'inspecteur…). Chaque partie a le sien, défini dans son
//! fichier.

pub mod agents;
pub mod composer;
pub mod context;
pub mod forms;
pub mod header;
pub mod inspector;
pub mod page;
pub mod permission;
pub mod tasks;
pub mod thread;
pub mod tool_detail;

#[derive(Default)]
pub struct SessionView {
    pub header: header::HeaderState,
    pub thread: thread::ThreadState,
    pub composer: composer::ComposerState,
    pub inspector: inspector::InspectorState,
}
