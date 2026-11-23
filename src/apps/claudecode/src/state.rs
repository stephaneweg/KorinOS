//! Le miroir de l'état du moteur pour l'interface : `ui/src/state.rs` de ClaudeCodeUI (AppStore, SessionHandle,
//! UiState) sans les signaux de Leptos. Le moteur garde tout ; l'interface tient une copie par session regardée
//! (`watch`), tenue à jour par les pushes (`apply_push`) selon les règles de `ccui_shared::session` : un patch par
//! version (`patch_step`), le texte en cours par incréments (`stream_next`), le brouillon par révision
//! (`adopt_draft`). Un trou (patch manqué) relit l'instantané.

use std::collections::HashMap;

use ccui_shared::api::{self as api, BootInfo, Flags, Prefs, WorktreeRunStep};
use ccui_shared::memory::MemoryEdit;
use ccui_shared::model::{Item, ToolItem, UserItem};
use ccui_shared::push::Push;
use ccui_shared::session::{SessionDraft, SessionHeavy, SessionMeta, SessionPatch, SessionSnapshot, SessionStream, SessionsSummary};

use crate::backend::{Backend, Task};

/// L'état de la fenêtre (`UiState` : l'inspecteur ouvert, la palette, le diff, l'outil choisi…).
pub struct UiState {
    pub inspector_open: bool,
    pub palette_open: bool,
    /// L'inspecteur montre les changements non validés (`/diff`).
    pub diff_open: bool,
    /// Un ToolItem (son id) ou une ligne d'agent.
    pub selected_tool_id: Option<String>,
    /// La phase choisie d'un Workflow.
    pub selected_phase: Option<String>,
    /// `/rewind` tapé : le fil ouvre la boîte sur ce message.
    pub rewind: Option<UserItem>,
}

impl Default for UiState {
    fn default() -> Self {
        UiState { inspector_open: true, palette_open: false, diff_open: false, selected_tool_id: None, selected_phase: None, rewind: None }
    }
}

// ------------------------------------------------------------------------------------------- règles pures

#[derive(Debug, PartialEq, Eq)]
pub(crate) enum PatchStep {
    Ignore,
    Gap,
    Apply,
}

pub(crate) fn patch_step(held: u64, len_now: usize, p: &SessionPatch) -> PatchStep {
    if p.version <= held {
        return PatchStep::Ignore;
    }
    if p.base_version > held {
        return PatchStep::Gap;
    }
    let mut n = len_now.min(p.len);
    for (i, _) in &p.items {
        if *i > n {
            return PatchStep::Gap;
        }
        if *i == n {
            n += 1;
        }
    }
    if n != p.len {
        return PatchStep::Gap;
    }
    PatchStep::Apply
}

pub(crate) fn stream_next(held: &str, s: &SessionStream) -> Option<String> {
    if s.offset == 0 {
        return Some(s.append.clone());
    }
    if s.offset <= held.len() && held.is_char_boundary(s.offset) {
        let mut t = String::with_capacity(s.offset + s.append.len());
        t.push_str(&held[..s.offset]);
        t.push_str(&s.append);
        return Some(t);
    }
    None
}

pub(crate) fn adopt_draft(held: &SessionDraft, incoming: &SessionDraft) -> bool {
    incoming.rev != held.rev || (*held == SessionDraft::default() && incoming != held)
}

// ------------------------------------------------------------------------------------------- une session

/// La copie d'une session (méta, lourd, brouillon, éléments, texte en cours), et l'état de l'interface qui doit
/// lui survivre (édition de la mémoire).
pub struct SessionHandle {
    pub id: String,
    pub loaded: bool,
    /// None : pas encore su ; Some(false) : pas de telle session.
    pub found: Option<bool>,
    pub version: u64,
    pub meta: SessionMeta,
    pub heavy: SessionHeavy,
    pub draft: SessionDraft,
    pub items: Vec<Item>,
    pub stream: String,
    pub memory: MemoryEdit,
    /// Le brouillon a été changé par le moteur (`cut_back` du rewind) : le composeur le reprend.
    pub draft_changed: bool,
    buffer: Vec<SessionPatch>,
    stream_buffer: Vec<SessionStream>,
    pub watchers: u32,
    fetch: Option<Task<Option<SessionSnapshot>>>,
}

impl SessionHandle {
    fn new(id: &str) -> SessionHandle {
        SessionHandle {
            id: id.to_string(),
            loaded: false,
            found: None,
            version: 0,
            meta: SessionMeta::default(),
            heavy: SessionHeavy::default(),
            draft: SessionDraft::default(),
            items: Vec::new(),
            stream: String::new(),
            memory: MemoryEdit::default(),
            draft_changed: false,
            buffer: Vec::new(),
            stream_buffer: Vec::new(),
            watchers: 0,
            fetch: None,
        }
    }

    pub fn apply_snapshot(&mut self, snap: SessionSnapshot) {
        if self.watchers == 0 {
            return;
        }
        let SessionSnapshot { version, meta, heavy, draft, items, streaming_text } = snap;
        self.items = items;
        self.meta = meta;
        self.heavy = heavy;
        if adopt_draft(&self.draft, &draft) {
            self.draft = draft;
            self.draft_changed = true;
        }
        self.stream = streaming_text;
        self.version = version;
        self.found = Some(true);
        self.loaded = true;
        for p in std::mem::take(&mut self.buffer) {
            self.apply_patch(p);
        }
        for s in std::mem::take(&mut self.stream_buffer) {
            self.apply_stream(s);
        }
    }

    /// Un patch ; true : un trou, l'instantané est à relire.
    fn apply_patch(&mut self, patch: SessionPatch) -> bool {
        if !self.loaded {
            self.buffer.push(patch);
            return false;
        }
        match patch_step(self.version, self.items.len(), &patch) {
            PatchStep::Ignore => false,
            PatchStep::Gap => {
                self.buffer.push(patch);
                self.loaded = false;
                true
            }
            PatchStep::Apply => {
                let SessionPatch { version, meta, heavy, draft, items, len, .. } = patch;
                self.items.truncate(len.min(self.items.len()));
                for (i, item) in items {
                    if i < self.items.len() {
                        self.items[i] = item;
                    } else {
                        self.items.push(item);
                    }
                }
                self.meta = meta;
                if let Some(h) = heavy {
                    self.heavy = h;
                }
                if let Some(d) = draft
                    && self.draft.rev != d.rev
                {
                    self.draft = d;
                    self.draft_changed = true;
                }
                self.version = version;
                false
            }
        }
    }

    fn apply_stream(&mut self, stream: SessionStream) -> bool {
        if !self.loaded {
            self.stream_buffer.push(stream);
            return false;
        }
        match stream_next(&self.stream, &stream) {
            Some(t) => {
                self.stream = t;
                false
            }
            None => {
                self.loaded = false;
                true
            }
        }
    }

    /// Le ToolItem `id` (le dernier de ce nom).
    pub fn tool_by_id(&self, id: &str) -> Option<&ToolItem> {
        self.items.iter().rev().find_map(|i| i.as_tool().filter(|t| t.id == id))
    }

    pub fn tool_at(&self, i: usize) -> Option<&ToolItem> {
        self.items.get(i).and_then(|i| i.as_tool())
    }

    /// Les éléments en références, pour les aides génériques de ccui-shared (`thread_blocks`, `todo_list`…).
    pub fn item_refs(&self) -> Vec<&Item> {
        self.items.iter().collect()
    }

    pub fn busy(&self) -> bool {
        use ccui_shared::model::SessionStatus;
        matches!(self.meta.status, SessionStatus::Running | SessionStatus::Waiting)
    }
}

// ------------------------------------------------------------------------------------------- l'app

/// Ce que le moteur pousse pour toute l'app, et les sessions regardées.
pub struct AppStore {
    pub boot: BootInfo,
    pub summary: SessionsSummary,
    pub transcripts_generation: u64,
    pub worktree_count: Option<i32>,
    pub bg_needs: i32,
    pub prefs: Prefs,
    pub flags: Flags,
    /// Le numéro du dernier `Push::Bookmarks` et sa session.
    pub bookmarks: Option<(u64, String)>,
    pub wt_steps: Vec<WorktreeRunStep>,
    pub wt_done: Option<u64>,
    /// Une navigation demandée par le moteur (clic sur une notification).
    pub navigate: Option<String>,
    pub sessions: HashMap<String, SessionHandle>,
    /// Les sessions à relire (un trou dans les patchs).
    gaps: Vec<String>,
}

pub(crate) fn apply_pref(prefs: &mut Prefs, flags: &mut Flags, name: &str, value: Option<String>) -> bool {
    let on = value.is_some();
    match name {
        api::PREF_THEME => prefs.theme = value,
        api::PREF_CODE_SIZE => prefs.code_size = value,
        api::PREF_RAIL_WIDTH => prefs.rail_width = value,
        api::PREF_INSPECTOR_WIDTH => prefs.inspector_width = value,
        api::FLAG_THINKING => flags.thinking = on,
        api::FLAG_TIMESTAMPS => flags.timestamps = on,
        api::FLAG_NOTIFY => flags.notify = on,
        _ => return false,
    }
    true
}

impl AppStore {
    /// L'état de départ (`provide_stores`) : préférences, résumé des sessions, agents d'arrière-plan, worktrees.
    pub fn new(be: &Backend) -> AppStore {
        let boot = ccui_core::app_flag::boot_info(&be.version, None);
        let summary = be.core.sessions.summary();
        let bg_needs = ccui_core::background_agents::needs_you_count();
        // (la veille des agents d'arrière-plan, une fois par minute, comme `bg_watch` au montage du rail)
        if std::env::var_os("CLAUDECODE_NO_BG").is_none() {
            let _g = be.rt.enter();
            ccui_core::background_agents::watch(be.core.sink.clone());
        }
        AppStore {
            prefs: boot.prefs.clone(),
            flags: boot.flags.clone(),
            boot,
            summary,
            transcripts_generation: ccui_core::transcript_store::generation(),
            worktree_count: None,
            bg_needs,
            bookmarks: None,
            wt_steps: Vec::new(),
            wt_done: None,
            navigate: None,
            sessions: HashMap::new(),
            gaps: Vec::new(),
        }
    }

    pub fn session(&self, id: &str) -> Option<&SessionHandle> {
        self.sessions.get(id)
    }

    pub fn session_mut(&mut self, id: &str) -> Option<&mut SessionHandle> {
        self.sessions.get_mut(id)
    }

    /// La page d'une session la regarde : `session_open` (vivante, sinon sa transcription ouverte en historique).
    pub fn load_session(&mut self, be: &Backend, id: &str) {
        self.load(be, id, false);
    }

    /// Seulement une session vivante (Extensions) : `session_watch`.
    pub fn load_live(&mut self, be: &Backend, id: &str) {
        self.load(be, id, true);
    }

    fn load(&mut self, be: &Backend, id: &str, live_only: bool) {
        let h = self.sessions.entry(id.to_string()).or_insert_with(|| SessionHandle::new(id));
        h.watchers += 1;
        if h.watchers > 1 {
            return;
        }
        h.loaded = false;
        let core = be.core.clone();
        let id = id.to_string();
        h.fetch = Some(be.task(async move {
            if live_only {
                core.sessions.get(&id).map(|s| s.watch())
            } else {
                core.sessions.open_id(&id).await.map(|s| s.watch())
            }
        }));
    }

    /// La page s'en va : à zéro, `session_unwatch`, la copie vidée (brouillon et mémoire gardés).
    pub fn release(&mut self, be: &Backend, id: &str) {
        let Some(h) = self.sessions.get_mut(id) else { return };
        h.watchers = h.watchers.saturating_sub(1);
        if h.watchers > 0 {
            return;
        }
        h.loaded = false;
        h.items.clear();
        h.stream.clear();
        h.version = 0;
        h.buffer.clear();
        h.stream_buffer.clear();
        h.fetch = None;
        if let Some(s) = be.core.sessions.get(id) {
            s.unwatch();
        }
    }

    /// À chaque image : les instantanés arrivés, les pushes, les trous à relire.
    pub fn poll(&mut self, be: &Backend) {
        for h in self.sessions.values_mut() {
            if let Some(t) = &h.fetch
                && let Some(r) = t.take()
            {
                h.fetch = None;
                match r {
                    Some(snap) => h.apply_snapshot(snap),
                    None => h.found = Some(false),
                }
            }
        }
        for p in be.drain() {
            self.apply_push(p);
        }
        for id in std::mem::take(&mut self.gaps) {
            if let Some(h) = self.sessions.get_mut(&id)
                && h.fetch.is_none()
                && h.watchers > 0
            {
                let core = be.core.clone();
                let i = id.clone();
                h.fetch = Some(be.task(async move { core.sessions.get(&i).map(|s| s.snapshot()) }));
            }
        }
    }

    pub fn apply_push(&mut self, push: Push) {
        match push {
            Push::SessionPatch(p) => {
                let id = p.id.clone();
                if let Some(h) = self.sessions.get_mut(&id)
                    && h.apply_patch(p)
                {
                    self.gaps.push(id);
                }
            }
            Push::SessionStream(s) => {
                let id = s.id.clone();
                if let Some(h) = self.sessions.get_mut(&id)
                    && h.apply_stream(s)
                {
                    self.gaps.push(id);
                }
            }
            Push::Sessions(s) => self.summary = s,
            Push::SessionRemoved { id } => {
                // (la page la résout de nouveau : sa transcription s'ouvre en historique)
                if let Some(mut h) = self.sessions.remove(&id) {
                    h.watchers = 0;
                }
            }
            Push::TranscriptsInvalidated { generation } => self.transcripts_generation = generation,
            Push::WorktreeCount { count } => self.worktree_count = count,
            Push::WorktreeRunStep(step) => self.wt_steps.push(step),
            Push::WorktreeRunDone { run_id } => self.wt_done = Some(run_id),
            Push::BgAgents { needs_you } => self.bg_needs = needs_you,
            Push::Bookmarks { session_id } => {
                let n = self.bookmarks.as_ref().map_or(1, |(n, _)| n + 1);
                self.bookmarks = Some((n, session_id));
            }
            Push::PrefChanged { name, value } => {
                apply_pref(&mut self.prefs, &mut self.flags, &name, value);
                self.boot.prefs = self.prefs.clone();
                self.boot.flags = self.flags.clone();
            }
            Push::Navigate { path } => self.navigate = Some(path),
            Push::Updating => {}
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn patch(base: u64, version: u64, items: Vec<usize>, len: usize) -> SessionPatch {
        SessionPatch {
            id: "s".into(),
            base_version: base,
            version,
            meta: SessionMeta::default(),
            heavy: None,
            draft: None,
            items: items.into_iter().map(|i| (i, Item::Reset)).collect(),
            len,
        }
    }

    #[test]
    fn patch_rule() {
        assert_eq!(patch_step(5, 3, &patch(4, 5, vec![], 3)), PatchStep::Ignore);
        assert_eq!(patch_step(5, 3, &patch(6, 7, vec![], 3)), PatchStep::Gap);
        assert_eq!(patch_step(5, 3, &patch(4, 6, vec![2], 3)), PatchStep::Apply);
        assert_eq!(patch_step(5, 3, &patch(5, 6, vec![3], 4)), PatchStep::Apply);
        assert_eq!(patch_step(5, 3, &patch(5, 6, vec![4], 5)), PatchStep::Gap);
        assert_eq!(patch_step(5, 10, &patch(5, 6, vec![1], 2)), PatchStep::Apply);
    }

    #[test]
    fn stream_rule() {
        let s = |offset: usize, append: &str| SessionStream { id: "s".into(), offset, append: append.into() };
        assert_eq!(stream_next("abc", &s(0, "x")).as_deref(), Some("x"));
        assert_eq!(stream_next("abc", &s(3, "d")).as_deref(), Some("abcd"));
        assert_eq!(stream_next("abc", &s(1, "z")).as_deref(), Some("az"));
        assert_eq!(stream_next("abc", &s(4, "z")), None);
    }

    #[test]
    fn patches_apply_in_place() {
        let mut h = SessionHandle::new("s");
        h.watchers = 1;
        h.apply_snapshot(SessionSnapshot {
            version: 1,
            meta: Default::default(),
            heavy: Default::default(),
            draft: Default::default(),
            items: vec![Item::Reset],
            streaming_text: String::new(),
        });
        assert!(h.loaded);
        assert!(!h.apply_patch(patch(1, 2, vec![1], 2)));
        assert_eq!((h.items.len(), h.version), (2, 2));
        assert!(h.apply_patch(patch(4, 5, vec![], 2)));
        assert!(!h.loaded);
    }
}
