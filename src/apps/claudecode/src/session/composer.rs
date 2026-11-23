//! Le composeur de la session (`session/composer.rs` de ClaudeCodeUI, `Composer.razor`) : la ligne d'état au-dessus,
//! les messages en file (envoyer maintenant, annuler), la zone de texte et ses pièces jointes, les outils (mode
//! d'autorisation, joindre, modèle, conseiller, style, effort, rapide, réflexion, ultracode), l'envoi (Ctrl ⏎ ; en
//! cours de tour : en file, ou Ctrl ⇧ ⏎ maintenant ; Interrompre, Échap), et la ligne des chiffres dessous (tour,
//! session, outils, publications, historique, contexte, issue du tour ou erreur).
//!
//! Le brouillon est tenu par le moteur (`SessionHandle.draft`, avec sa révision) : chaque changement du texte part par
//! `set_draft_text(texte, rév)`, les pièces jointes par `set_draft_images` ; un brouillon poussé par le moteur (le
//! rewind remet un message) est repris (`draft_changed`). Comme dans l'original, Entrée va à la ligne et Ctrl ⏎
//! envoie ; ↑ ↓ (curseur sur la première ou la dernière ligne) parcourent l'historique des demandes (celles de la
//! session, puis celles des 10 dernières sessions du dossier) ; Tab prend la suggestion de demande. « / » ouvre les
//! commandes, « @ » les mentions (popovers.rs) ; certaines commandes s'ouvrent ici plutôt que d'être envoyées
//! (`/model`, `/config`, `/mcp`, `/rewind`, `/diff`…).
//!
//! Pièces jointes : pas de glisser-déposer ni de collage d'image sous Korin (egui ne donne que le texte collé) ;
//! « Joindre » ouvre un champ de chemin (images PNG, JPEG, GIF, WebP, PDF, texte) ; sur l'hôte de développement, un
//! fichier déposé sur la fenêtre est joint aussi. Les images sont montrées en étiquettes (nom, type), sans vignette.

use std::future::Future;
use std::sync::Arc;
use std::time::{Duration, Instant};

use ccui_core::live::LiveSession;
use ccui_shared::api::AnswerArgs;
use ccui_shared::model::*;
use ccui_shared::prompt_history::PromptHistory;
use ccui_shared::session::SessionMeta;
use ccui_shared::{file_index, fmt, i18n, images, invisible, json, session_rules, t};
use egui::text::{CCursor, CCursorRange};
use egui::{Align, Color32, CornerRadius, Frame, Id, Key, Layout, Margin, Modifiers, Rect, Response, RichText, Sense, Stroke, Ui};
use serde_json::Value;

use crate::app::{Cx, Route};
use crate::backend::{Backend, Task};
use crate::session::menus::{self, ConfigMenu, ModeMenu, ModelMenu};
use crate::session::popovers::{self, Cursor, MentionScan};
use crate::session::{effort, status_line};
use crate::theme::{self, Tokens};
use crate::widgets::{self, icon, tr};

// ------------------------------------------------------------------------------------------- règles pures

/// `SlashQuery` : pendant qu'un nom est tapé après « / » (pas encore d'espace). Une commande rappelée de l'historique
/// n'ouvre pas la liste (elle prendrait les flèches).
pub fn slash_query(text: &str, dismissed: bool, history_index: i32) -> Option<String> {
    (!dismissed && history_index < 0 && text.starts_with('/') && !text.chars().any(char::is_whitespace)).then(|| text.to_string())
}

/// `MentionQuery` : un chemin tapé après un « @ » qui commence le dernier mot ; jamais sur un message rappelé intact.
pub fn mention_query(text: &str, dismissed: bool, menus: bool, slash: bool, recalled: Option<&str>) -> Option<String> {
    if dismissed || menus || slash || recalled == Some(text) {
        return None;
    }
    file_index::query(text)
}

/// Ce que fait une commande tapée au lieu d'être envoyée (la CLI ouvrirait une boîte de terminal).
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Native {
    ModelMenu,
    Config,
    /// Aller à cette adresse relative (Extensions, son onglet, la session).
    Go(String),
    /// Le panneau du contexte de l'inspecteur (mémoire, dossiers en plus).
    Context,
    Rewind,
    Diff,
}

/// `Native(t)` sur le texte nettoyé (égalité exacte).
pub fn native(t: &str, session_id: &str, has_models: bool, crashed: bool, has_cwd: bool) -> Option<Native> {
    let ext = |tab: &str| Some(Native::Go(format!("extensions?tab={tab}&session={session_id}")));
    match t {
        "/model" | "/output-style" if has_models => Some(Native::ModelMenu),
        "/config" | "/settings" if !crashed => Some(Native::Config),
        "/mcp" | "/hooks" | "/permissions" => ext(&t[1..]),
        "/allowed-tools" => ext("permissions"),
        "/plugin" | "/plugins" => ext("plugins"),
        "/memory" | "/add-dir" => Some(Native::Context),
        "/rewind" | "/checkpoint" | "/undo" => Some(Native::Rewind),
        "/diff" if has_cwd => Some(Native::Diff),
        _ => None,
    }
}

/// Le texte d'attente de la zone (et la suggestion, en fantôme, quand elle est montrée).
pub fn placeholder(status: SessionStatus, ultra_turn: bool, suggestion: Option<&str>) -> String {
    match status {
        SessionStatus::Crashed => t!("Composer.PhCrashed"),
        SessionStatus::Waiting => t!("Composer.PhWaiting"),
        SessionStatus::Exited => t!("Composer.PhHistory"),
        SessionStatus::Running if ultra_turn => t!("Composer.PhWorkflow"),
        SessionStatus::Running => t!("Composer.PhRunning"),
        _ => match suggestion {
            Some(s) => t!("Composer.Suggestion", s),
            None => t!("Composer.PhIdle"),
        },
    }
}

/// `FastReason`.
pub fn fast_reason(reason: Option<&str>) -> Option<String> {
    reason.map(|r| match r {
        "sdk_opt_in_required" => t!("Composer.FastSdkOptIn"),
        "extra_usage_disabled" => t!("Composer.FastExtraUsage"),
        other => other.to_string(),
    })
}

fn latest_rewindable(meta: &SessionMeta, items: &[&Item]) -> Option<UserItem> {
    items.iter().rev().filter_map(|i| i.as_user()).find(|u| session_rules::can_rewind(meta, items, u.uuid.as_deref())).cloned()
}

/// Le curseur est sur la première ligne (↑ : rappel) ou la dernière (↓).
fn caret_edge(text: &str, caret: usize, up: bool) -> bool {
    if up { !text.chars().take(caret).any(|c| c == '\n') } else { !text.chars().skip(caret).any(|c| c == '\n') }
}

/// Lance une commande de la session vivante `id` sur le runtime ; l'erreur (« session introuvable » comprise) revient
/// dans le résultat.
pub(crate) fn act<F, Fut>(be: &Backend, id: &str, f: F) -> Task<Result<(), String>>
where
    F: FnOnce(Arc<LiveSession>) -> Fut + Send + 'static,
    Fut: Future<Output = Result<(), String>> + Send + 'static,
{
    let s = be.session(id);
    be.task(async move { f(s?).await })
}

// ------------------------------------------------------------------------------------------- l'état

/// Le menu ouvert au-dessus du composeur (un seul à la fois).
#[derive(Default)]
enum Menu {
    #[default]
    None,
    Mode(ModeMenu),
    Model(ModelMenu),
    Config(ConfigMenu),
}

impl Menu {
    fn is_open(&self) -> bool {
        !matches!(self, Menu::None)
    }
}

/// Un envoi en cours : (uuid ou erreur), « maintenant », et ce qui était envoyé (remis en cas d'échec).
struct Sending {
    task: Task<Result<String, String>>,
    now: bool,
    text: String,
    images: Vec<UserImage>,
}

#[derive(Default)]
pub struct ComposerState {
    /// Le brouillon a été repris du moteur (la première fois).
    init: bool,
    text: String,
    error: Option<String>,
    history: PromptHistory,
    recalled: Option<String>,
    past: Option<Vec<String>>,
    past_task: Option<Task<Vec<String>>>,
    /// Un rappel qui attend les demandes passées du dossier (son pas).
    recall_wait: Option<i32>,
    slash_dismissed: bool,
    mention_dismissed: bool,
    slash_cur: Cursor,
    mention_cur: Cursor,
    mention: Option<MentionScan>,
    /// Une liste (/ ou @) a pris les flèches à l'image précédente.
    pop_open: bool,
    menu: Menu,
    /// Les boutons qui ouvrent les menus (un clic dessus n'est pas un clic « à côté »).
    triggers: Vec<Rect>,
    settings_asked: bool,
    settings: Option<Task<Result<Value, String>>>,
    ultra_available: Option<bool>,
    applied_effort: Option<String>,
    effort_cap: Option<String>,
    /// Les actions en cours (true : relire les réglages ensuite).
    jobs: Vec<(Task<Result<(), String>>, bool)>,
    sending: Option<Sending>,
    /// « Envoyer maintenant » : le message attendu dans la file, jusqu'à quand.
    send_now: Option<(String, Instant)>,
    attach_open: bool,
    attach_path: String,
    reading: Option<Task<Vec<Result<UserImage, String>>>>,
    /// Le texte a été changé par le composeur : le curseur à la fin, le focus dans la zone.
    caret_end: bool,
}

impl ComposerState {
    fn read_settings(&mut self, be: &Backend, id: &str) {
        let s = be.session(id);
        self.settings = Some(be.task(async move { s?.request("get_settings", Value::Object(Default::default()), None).await }));
    }

    fn run(&mut self, task: Task<Result<(), String>>, reread: bool) {
        self.error = None;
        self.jobs.push((task, reread));
    }

    /// Le texte change (frappe exceptée) : le curseur à la fin, et le moteur le garde.
    fn set_text(&mut self, cx: &mut Cx, id: &str, v: String) {
        self.text = v;
        self.caret_end = true;
        self.write_text(cx, id);
    }

    /// Le texte au moteur (`session_set_draft_text`), sauf pendant un parcours de l'historique : le brouillon tapé
    /// y reste.
    fn write_text(&mut self, cx: &mut Cx, id: &str) {
        if self.history.index() >= 0 {
            return;
        }
        let Some(h) = cx.app.session_mut(id) else { return };
        h.draft.text = self.text.clone();
        if let Ok(s) = cx.be.session(id) {
            s.set_draft_text(self.text.clone(), h.draft.rev);
        }
    }

    /// Les pièces jointes au moteur (`session_set_draft_images`), seulement quand elles changent.
    fn set_images(&mut self, cx: &mut Cx, id: &str, v: Vec<UserImage>) {
        let Some(h) = cx.app.session_mut(id) else { return };
        h.draft.images = v.clone();
        if let Ok(s) = cx.be.session(id) {
            s.set_draft_images(v, h.draft.rev);
        }
    }

    fn walk(&mut self, step: i32) {
        let current = self.text.clone();
        if let Some(t) = self.history.r#move(step, &current) {
            self.recalled = Some(t.clone());
            self.text = t;
            self.caret_end = true;
        }
    }

    /// ↑ (pas 1) / ↓ (pas -1) : le premier ↑ charge l'historique (cette session, puis les demandes passées du dossier,
    /// lues une fois).
    fn recall(&mut self, cx: &mut Cx, id: &str, step: i32) {
        if self.history.index() != -1 || step <= 0 {
            self.walk(step);
            return;
        }
        match self.past.clone() {
            Some(p) => {
                self.load_history(cx, id, p);
                self.walk(step);
            }
            None => {
                if self.past_task.is_none() {
                    let cwd = cx.app.session(id).map(|h| h.meta.cwd.clone()).unwrap_or_default();
                    let sid = id.to_string();
                    self.past_task = Some(cx.be.blocking(move || ccui_core::transcript_store::prompts(&cwd, Some(&sid), 10)));
                }
                self.recall_wait = Some(step);
            }
        }
    }

    fn load_history(&mut self, cx: &Cx, id: &str, past: Vec<String>) {
        // (un message d'images seules n'a pas de texte à rappeler)
        let mut entries: Vec<String> = cx
            .app
            .session(id)
            .map(|h| h.items.iter().filter_map(|i| i.as_user()).map(|u| u.text.clone()).filter(|t| !t.is_empty()).collect())
            .unwrap_or_default();
        entries.reverse();
        entries.extend(past);
        self.history.load(entries);
    }

    fn apply_native(&mut self, cx: &mut Cx, id: &str, n: Native) {
        match n {
            Native::ModelMenu => {
                if let Some(h) = cx.app.session(id) {
                    self.menu = Menu::Model(ModelMenu::new(&h.meta, &h.heavy));
                }
            }
            Native::Config => self.menu = Menu::Config(ConfigMenu::default()),
            // (la page des extensions n'a pas d'adresse par onglet ici : on y va, sur son premier onglet)
            Native::Go(_) => cx.navigate(Route::Extensions),
            Native::Context => {
                cx.ui.selected_tool_id = None;
                cx.ui.diff_open = false;
                cx.ui.inspector_open = true;
            }
            // le sélecteur de message du terminal : le dernier qui peut revenir en arrière (les boutons du fil
            // atteignent les autres)
            Native::Rewind => {
                let last = cx.app.session(id).and_then(|h| latest_rewindable(&h.meta, &h.item_refs()));
                match last {
                    Some(u) => cx.ui.rewind = Some(u),
                    None => self.error = Some(t!("Rewind.Unavailable")),
                }
            }
            Native::Diff => {
                cx.ui.selected_tool_id = None;
                cx.ui.diff_open = true;
                cx.ui.inspector_open = true;
            }
        }
    }

    /// Envoyer (`now` : le « envoyer maintenant » de la CLI, le message interrompt le tour au lieu de l'attendre).
    fn send(&mut self, cx: &mut Cx, id: &str, now: bool) {
        let Some(h) = cx.app.session(id) else { return };
        let text = self.text.trim().to_string();
        let imgs = h.draft.images.clone();
        let crashed = h.meta.status == SessionStatus::Crashed;
        if (text.is_empty() && imgs.is_empty()) || self.reading.is_some() || crashed || self.sending.is_some() {
            return;
        }
        // les caractères invisibles sont ôtés et montrés d'abord : l'envoi suivant envoie ce qui a été relu
        let (clean, hidden) = invisible::strip(&text);
        if hidden > 0 {
            self.history.reset();
            self.set_text(cx, id, clean);
            self.error = Some(t!("Composer.Invisible", hidden));
            return;
        }
        let has_models = !menus::models(h.heavy.initialize_info.as_ref()).is_empty();
        let has_cwd = !h.meta.cwd.is_empty();
        let pending: Vec<String> = h.meta.pending.iter().map(|p| p.request_id.clone()).collect();
        self.recalled = None;
        self.history.reset();
        self.set_text(cx, id, String::new());
        if !imgs.is_empty() {
            self.set_images(cx, id, Vec::new());
        }
        self.menu = Menu::None;
        if imgs.is_empty()
            && let Some(n) = native(&text, id, has_models, crashed, has_cwd)
        {
            self.apply_native(cx, id, n);
            return;
        }
        self.error = None;
        let s = cx.be.session(id);
        let (t2, i2) = (text.clone(), imgs.clone());
        let task = cx.be.task(async move {
            let s = s?;
            // « ou dis à Claude ce que tu veux à la place » : les outils en attente sont refusés, puis le message suit
            for request_id in pending {
                let args = AnswerArgs { request_id, decision: Decision::Deny, mode: None, message: None, input: None, destination: None };
                s.answer(args).await?;
            }
            s.send(t2, i2).await
        });
        self.sending = Some(Sending { task, now, text, images: imgs });
    }

    /// Les fichiers à joindre (chemins), lus hors de l'interface ; le type vient des octets, chacun lu entier jusqu'à
    /// `MAX_BYTES`.
    fn attach(&mut self, cx: &Cx, id: &str, paths: Vec<std::path::PathBuf>) {
        if paths.is_empty() || self.reading.is_some() {
            return;
        }
        self.error = None;
        let already = cx.app.session(id).map_or(0, |h| h.draft.images.len());
        self.reading = Some(cx.be.blocking(move || {
            let mut out = Vec::new();
            let mut n = already;
            for p in paths {
                let name = p.file_name().map(|n| n.to_string_lossy().to_string()).unwrap_or_else(|| p.display().to_string());
                if n >= images::MAX_COUNT {
                    out.push(Err(t!("Composer.ImageTooMany", images::MAX_COUNT)));
                    break;
                }
                match std::fs::metadata(&p).map(|m| m.len() as i64) {
                    Err(e) => {
                        out.push(Err(t!("Composer.ActionFailed", format!("{} : {e}", p.display()))));
                        continue;
                    }
                    Ok(s) if s > images::MAX_BYTES as i64 => {
                        out.push(Err(t!("Composer.ImageTooBig", name, fmt::size(s), fmt::size(images::MAX_BYTES as i64))));
                        continue;
                    }
                    Ok(_) => {}
                }
                match std::fs::read(&p) {
                    Ok(bytes) => match images::from(&bytes, Some(&name)) {
                        Some(img) => {
                            n += 1;
                            out.push(Ok(img));
                        }
                        None => out.push(Err(t!("Composer.ImageType", name))),
                    },
                    Err(e) => out.push(Err(t!("Composer.ActionFailed", format!("{} : {e}", p.display())))),
                }
            }
            out
        }));
    }

    /// Les résultats arrivés : réglages, actions, envoi, « envoyer maintenant », pièces jointes, demandes passées.
    fn poll(&mut self, cx: &mut Cx, id: &str) {
        if let Some(task) = &self.settings
            && let Some(r) = task.take()
        {
            self.settings = None;
            match r {
                Ok(settings) => {
                    if let Some(a) = json::prop(&settings, "applied") {
                        self.ultra_available = match json::prop(a, "ultracodeAvailable") {
                            Some(Value::Bool(b)) => Some(*b),
                            _ => None,
                        };
                        self.applied_effort = json::str(a, "effort").map(str::to_string);
                        self.effort_cap = json::prop(&settings, "effective").and_then(|eff| session_rules::effort_cap(eff, json::str(a, "model")));
                    }
                }
                Err(e) => eprintln!("[{id}] get_settings : {e}"),
            }
        }
        let mut reread = false;
        let mut failed = None;
        self.jobs.retain(|(task, again)| match task.take() {
            Some(r) => {
                if let Err(e) = r {
                    failed = Some(t!("Composer.ActionFailed", e));
                }
                reread |= *again;
                false
            }
            None => true,
        });
        if failed.is_some() {
            self.error = failed;
        }
        if reread {
            self.read_settings(cx.be, id);
        }
        if let Some(s) = &self.sending
            && let Some(r) = s.task.take()
        {
            let s = self.sending.take().unwrap();
            match r {
                Ok(uuid) => {
                    if s.now {
                        // la puce de la file arrive avec le patch suivant : l'envoyer maintenant une fois là (jamais à
                        // l'aveugle : sur un message qui n'est pas en file, cela interromprait le tour)
                        self.send_now = Some((uuid, Instant::now() + Duration::from_millis(600)));
                    }
                }
                Err(e) => {
                    self.error = Some(t!("Composer.ActionFailed", e));
                    // ne jamais perdre une demande à un envoi raté
                    if self.text.is_empty() {
                        self.set_text(cx, id, s.text);
                    }
                    if !s.images.is_empty() && cx.app.session(id).is_some_and(|h| h.draft.images.is_empty()) {
                        self.set_images(cx, id, s.images);
                    }
                }
            }
        }
        if let Some((uuid, until)) = self.send_now.clone()
            && let Some(h) = cx.app.session(id)
        {
            let queued = h.meta.queued.iter().any(|q| q.uuid == uuid);
            let live = matches!(h.meta.status, SessionStatus::Running | SessionStatus::Waiting);
            if queued {
                self.send_now = None;
                let task = act(cx.be, id, move |s| async move { s.send_now(&uuid).await });
                self.run(task, false);
            } else if !live || Instant::now() > until {
                self.send_now = None;
            }
        }
        if let Some(task) = &self.reading
            && let Some(results) = task.take()
        {
            self.reading = None;
            let mut now = cx.app.session(id).map(|h| h.draft.images.clone()).unwrap_or_default();
            let n = now.len();
            for r in results {
                match r {
                    Ok(img) => now.push(img),
                    Err(e) => self.error = Some(e),
                }
            }
            if now.len() != n {
                self.set_images(cx, id, now);
                self.attach_path.clear();
                self.attach_open = false;
            }
        }
        if let Some(task) = &self.past_task
            && let Some(p) = task.take()
        {
            self.past_task = None;
            self.past = Some(p.clone());
            if let Some(step) = self.recall_wait.take()
                && self.history.index() == -1
            {
                self.load_history(cx, id, p);
                self.walk(step);
            }
        }
    }
}

// ------------------------------------------------------------------------------------------- petites pièces

/// Un bouton d'outil (`.pick` / `.chip` de app.css) : cadre fin ; `on` : à l'accent ; `enabled` faux : atténué ;
/// `risk` : un mode risqué.
fn pick(ui: &mut Ui, t: &Tokens, text: &str, on: bool, enabled: bool, risk: bool) -> Response {
    let color = if !enabled {
        t.text_faint
    } else if on {
        t.accent_ink
    } else if risk {
        t.warning
    } else {
        t.text_dim
    };
    let stroke = if on { t.accent } else { t.card_border };
    let b = egui::Button::new(RichText::new(text).font(theme::medium(theme::LABEL)).color(color))
        .fill(if on { t.accent_soft } else { Color32::TRANSPARENT })
        .stroke(Stroke::new(1.0, stroke))
        .corner_radius(CornerRadius::same(theme::R))
        .min_size(egui::vec2(0.0, 24.0));
    ui.add_enabled(enabled, b)
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum Kind {
    Primary,
    Plain,
    Danger,
}

/// Un bouton d'envoi : son libellé et sa touche dans un cadre (`.btn.go` et `<kbd>`).
fn go(ui: &mut Ui, t: &Tokens, label: &str, key: Option<&str>, kind: Kind, enabled: bool) -> Response {
    let (fill, fg, stroke) = match kind {
        Kind::Primary if enabled => (t.accent, t.on_accent, Stroke::NONE),
        Kind::Danger => (Color32::TRANSPARENT, t.danger, Stroke::new(1.0, t.danger.gamma_multiply(0.6))),
        _ => (Color32::TRANSPARENT, if enabled { t.text } else { t.text_faint }, Stroke::new(1.0, t.card_border)),
    };
    let inner = Frame::new().fill(fill).stroke(stroke).corner_radius(CornerRadius::same(theme::R)).inner_margin(Margin::symmetric(12, 5)).show(ui, |ui| {
        // (dans une rangée de droite à gauche, la touche vient d'abord)
        let rtl = ui.layout().prefer_right_to_left();
        ui.horizontal(|ui| {
            if rtl && let Some(k) = key {
                widgets::kbd(ui, t, k);
            }
            ui.label(RichText::new(label).font(theme::medium(theme::SMALL)).color(fg));
            if !rtl && let Some(k) = key {
                widgets::kbd(ui, t, k);
            }
        });
    });
    let sense = if enabled { Sense::click() } else { Sense::hover() };
    let r = ui.interact(inner.response.rect, inner.response.id.with("go"), sense);
    if enabled && r.hovered() {
        ui.painter().rect_filled(inner.response.rect, CornerRadius::same(theme::R), t.hover.gamma_multiply(if kind == Kind::Primary { 0.25 } else { 0.6 }));
        ui.ctx().set_cursor_icon(egui::CursorIcon::PointingHand);
    }
    r
}

/// Une valeur de la ligne des chiffres : « tour 12,8 s ».
fn fact(ui: &mut Ui, t: &Tokens, label: &str, value: &str) {
    ui.label(RichText::new(label).font(theme::mono(theme::CAPTION)).color(t.text_faint));
    if !value.is_empty() {
        ui.label(RichText::new(value).font(theme::mono(theme::CAPTION)).color(t.text_dim));
    }
    ui.add_space(8.0);
}

/// Les touches qu'un menu ouvert prend (clé d'egui, nom de la touche dans ClaudeCodeUI).
const LIST_KEYS: [(Key, &str); 8] = [
    (Key::ArrowUp, "ArrowUp"),
    (Key::ArrowDown, "ArrowDown"),
    (Key::Home, "Home"),
    (Key::End, "End"),
    (Key::Enter, "Enter"),
    (Key::Space, " "),
    (Key::Escape, "Escape"),
    (Key::Tab, "Tab"),
];

/// L'identifiant (données temporaires d'egui) qui dit qu'une liste ou un menu du composeur est ouvert : la page n'a
/// pas à interrompre le tour sur cet Échap.
pub const POPOVER_OPEN: &str = "ccui-composer-popover";

// ------------------------------------------------------------------------------------------- le composeur

pub fn show(ui: &mut Ui, cx: &mut Cx, st: &mut ComposerState, id: &str) {
    let t = cx.t;
    let ctx = ui.ctx().clone();
    let te_id = Id::new(("ccui-composer", id));

    // ------------------------------------------------------------------ le brouillon du moteur
    {
        let Some(h) = cx.app.session_mut(id) else { return };
        if !st.init {
            st.init = true;
            st.text = h.draft.text.clone();
            h.draft_changed = false;
        } else if h.draft_changed {
            // un brouillon poussé (le rewind remet une demande) : repris, le parcours de l'historique oublié
            h.draft_changed = false;
            st.text = h.draft.text.clone();
            st.history.reset();
            st.caret_end = true;
        }
    }
    st.poll(cx, id);

    // ------------------------------------------------------------------ ce qui se lit de la session
    let Some(h) = cx.app.session(id) else { return };
    let meta = h.meta.clone();
    let status = meta.status;
    let crashed = status == SessionStatus::Crashed;
    let running = status == SessionStatus::Running;
    let ultra_turn = running && h.items.iter().rev().find_map(|i| i.as_user()).is_some_and(|u| u.ultracode);
    let info = h.heavy.initialize_info.clone();
    let model_list = menus::models(info.as_ref());
    let model = menus::current_model(meta.model.as_deref(), info.as_ref());
    let imgs = h.draft.images.clone();
    let has_images = !imgs.is_empty();
    if meta.has_process && info.is_some() && !st.settings_asked {
        // ultracodeAvailable et l'effort appliqué viennent de get_settings : lus une fois la CLI prête
        st.settings_asked = true;
        st.read_settings(cx.be, id);
    }
    let effort_now = st.applied_effort.clone().or_else(|| meta.effort.clone());
    let model_label = match &model {
        Some(m) => json::str(m, "displayName").map(str::to_string).or(meta.model.clone()).unwrap_or_default(),
        None => meta.model.clone().unwrap_or_else(|| t!("Model")),
    };
    let suggestion = meta.suggestion.clone();
    let suggestion_shown = suggestion.is_some() && status == SessionStatus::Idle && st.text.is_empty() && !has_images;
    let fast_on = meta.fast_mode_state == "on";
    let fast_ok = fast_on && model.as_ref().is_some_and(|m| matches!(json::prop(m, "supportsFastMode"), Some(Value::Bool(true))));
    let effort_levels: Option<Vec<String>> = model.as_ref().and_then(|m| {
        if !matches!(json::prop(m, "supportsEffort"), Some(Value::Bool(true))) {
            return None;
        }
        let Some(Value::Array(a)) = json::prop(m, "supportedEffortLevels") else { return None };
        let levels: Vec<&str> = a.iter().map(|x| x.as_str().unwrap_or("")).collect();
        Some(session_rules::capped(&levels, st.effort_cap.as_deref()).into_iter().map(str::to_string).collect())
    });

    // ------------------------------------------------------------------ le clavier
    let focused = ctx.memory(|m| m.has_focus(te_id));
    let mut menu_keys: Vec<&'static str> = Vec::new();
    let mut pop_key: Option<&'static str> = None;
    match &st.menu {
        Menu::Config(_) => {
            if ctx.input_mut(|i| i.consume_key(Modifiers::NONE, Key::Escape)) {
                st.menu = Menu::None;
                st.caret_end = true;
            }
        }
        Menu::Mode(_) | Menu::Model(_) => {
            for (k, name) in LIST_KEYS {
                while ctx.input_mut(|i| i.consume_key(Modifiers::NONE, k)) {
                    menu_keys.push(name);
                }
            }
        }
        Menu::None if focused => {
            if ctx.input_mut(|i| i.consume_key(Modifiers::COMMAND | Modifiers::SHIFT, Key::Enter)) {
                st.send(cx, id, running);
            } else if ctx.input_mut(|i| i.consume_key(Modifiers::COMMAND, Key::Enter)) {
                st.send(cx, id, false);
            }
            if suggestion_shown
                && ctx.input_mut(|i| i.consume_key(Modifiers::NONE, Key::Tab))
                && let Some(s) = suggestion.clone()
            {
                st.set_text(cx, id, s);
            }
            if ctx.input(|i| i.key_pressed(Key::Escape)) {
                st.slash_dismissed = slash_query(&st.text, st.slash_dismissed, st.history.index()).is_some();
                st.mention_dismissed = file_index::query(&st.text).is_some();
                if st.slash_dismissed || st.mention_dismissed {
                    st.caret_end = true; // (le focus reste dans la zone)
                }
            }
            if st.pop_open {
                for (k, name) in [(Key::ArrowDown, "ArrowDown"), (Key::ArrowUp, "ArrowUp"), (Key::Tab, "Tab")] {
                    if ctx.input_mut(|i| i.consume_key(Modifiers::NONE, k)) {
                        pop_key = Some(name);
                    }
                }
            } else {
                let caret = egui::TextEdit::load_state(&ctx, te_id)
                    .and_then(|s| s.cursor.char_range())
                    .map_or(st.text.chars().count(), |r| r.primary.index.into());
                if caret_edge(&st.text, caret, true) && ctx.input_mut(|i| i.consume_key(Modifiers::NONE, Key::ArrowUp)) {
                    st.recall(cx, id, 1);
                } else if caret_edge(&st.text, caret, false) && ctx.input_mut(|i| i.consume_key(Modifiers::NONE, Key::ArrowDown)) {
                    st.recall(cx, id, -1);
                }
            }
        }
        Menu::None => {}
    }
    // un fichier déposé sur la fenêtre (l'hôte de développement ; pas sous Korin)
    let dropped: Vec<std::path::PathBuf> = ctx.input(|i| i.raw.dropped_files.iter().map(|f| f.path().to_path_buf()).filter(|p| !p.as_os_str().is_empty()).collect());
    if !dropped.is_empty() && !crashed {
        st.attach(cx, id, dropped);
    }

    // ------------------------------------------------------------------ la ligne d'état
    status_line::show(ui, cx, id);

    // ------------------------------------------------------------------ la file d'attente
    for q in &meta.queued {
        ui.horizontal(|ui| {
            widgets::pill(ui, &t, &t!("Composer.Queued"), widgets::Tone::Accent).on_hover_text(t!("Composer.QueueHint"));
            ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                // la puce part avec la trame command_lifecycle de la CLI ; le focus revient à la zone
                let cancel = t!("Composer.CancelQueued", q.text);
                if widgets::ghost(ui, &t, icon("x")).on_hover_text(&cancel).clicked() {
                    let u = q.uuid.clone();
                    st.run(act(cx.be, id, move |s| async move { s.cancel(&u).await }), false);
                    st.caret_end = true;
                }
                if widgets::ghost(ui, &t, icon("arrow-right")).on_hover_text(t!("Composer.SendNowHint")).clicked() {
                    let u = q.uuid.clone();
                    st.run(act(cx.be, id, move |s| async move { s.send_now(&u).await }), false);
                }
                ui.with_layout(Layout::left_to_right(Align::Center), |ui| {
                    ui.add(egui::Label::new(RichText::new(&q.text).color(t.text_dim)).truncate()).on_hover_text(&q.text);
                });
            });
        });
    }
    if !meta.queued.is_empty() {
        ui.add_space(4.0);
    }

    // ------------------------------------------------------------------ la boîte
    let mode_name = i18n::name("Mode.Name.", Some(&meta.mode));
    let mut triggers = Vec::new();
    let boxed = Frame::new()
        .fill(t.field)
        .stroke(Stroke::new(1.0, if focused { t.accent.gamma_multiply(0.7) } else { t.card_border }))
        .corner_radius(CornerRadius::same(theme::R_LG))
        .inner_margin(Margin { left: 12, right: 12, top: 10, bottom: 8 })
        .show(ui, |ui| {
            ui.set_width(ui.available_width());
            // les pièces jointes
            if has_images {
                let mut remove = None;
                ui.horizontal_wrapped(|ui| {
                    for (i, img) in imgs.iter().enumerate() {
                        let n = i + 1;
                        let shown = if img.document() {
                            format!("{} {}", icon("paperclip"), img.name.clone().unwrap_or_else(|| t!("Composer.Document", n)))
                        } else {
                            let kind = img.media_type.trim_start_matches("image/").to_uppercase();
                            format!("{} · {kind}", img.name.clone().unwrap_or_else(|| t!("Composer.ImageAlt", n)))
                        };
                        Frame::new()
                            .fill(t.raised)
                            .stroke(Stroke::new(1.0, t.card_border))
                            .corner_radius(CornerRadius::same(theme::R))
                            .inner_margin(Margin::symmetric(6, 2))
                            .show(ui, |ui| {
                                ui.horizontal(|ui| {
                                    ui.label(RichText::new(shown).font(theme::medium(theme::CAPTION)).color(t.text_dim));
                                    if widgets::ghost(ui, &t, icon("x")).on_hover_text(t!("Composer.RemoveImage", n)).clicked() {
                                        remove = Some(i);
                                    }
                                });
                            });
                    }
                });
                if let Some(i) = remove {
                    let mut now = imgs.clone();
                    now.remove(i);
                    st.set_images(cx, id, now);
                    st.caret_end = true;
                }
                ui.add_space(4.0);
            }
            // le texte
            if st.caret_end {
                st.caret_end = false;
                let mut s = egui::TextEdit::load_state(&ctx, te_id).unwrap_or_default();
                s.cursor.set_char_range(Some(CCursorRange::one(CCursor::new(st.text.chars().count()))));
                egui::TextEdit::store_state(&ctx, te_id, s);
                if !crashed {
                    ctx.memory_mut(|m| m.request_focus(te_id));
                }
            }
            let ph = placeholder(status, ultra_turn, if suggestion_shown { suggestion.as_deref() } else { None });
            let out = egui::ScrollArea::vertical().id_salt(("composer-scroll", id)).max_height(220.0).show(ui, |ui| {
                ui.add_enabled(
                    !crashed,
                    egui::TextEdit::multiline(&mut st.text)
                        .id(te_id)
                        .frame(Frame::NONE)
                        .hint_text(RichText::new(ph).color(t.text_faint))
                        .font(egui::FontId::proportional(theme::BODY))
                        .text_color(t.text)
                        .desired_rows(2)
                        .desired_width(f32::INFINITY),
                )
            });
            if out.inner.changed() {
                st.history.reset();
                st.write_text(cx, id);
            }
            ui.add_space(6.0);
            // les outils
            ui.horizontal_wrapped(|ui| {
                ui.spacing_mut().item_spacing.x = 6.0;
                // le mode d'autorisation
                let mode_open = matches!(st.menu, Menu::Mode(_));
                let mode_r = if crashed {
                    widgets::tag(ui, &t, &mode_name)
                } else {
                    pick(ui, &t, &format!("{mode_name} {}", icon("chev-down")), mode_open, true, session_rules::risky(&meta.mode))
                };
                let mode_r = mode_r.on_hover_text(t!("Composer.PermissionMode"));
                triggers.push(mode_r.rect);
                if !crashed && mode_r.clicked() {
                    st.menu = if mode_open { Menu::None } else { Menu::Mode(ModeMenu::new(&meta)) };
                }
                // joindre
                if pick(ui, &t, icon("paperclip"), st.attach_open, !crashed, false).on_hover_text(t!("Composer.Attach")).clicked() {
                    st.attach_open = !st.attach_open;
                }
                // le modèle
                if !model_list.is_empty() && !crashed {
                    let model_open = matches!(st.menu, Menu::Model(_));
                    let win = meta.context_window.map(|w| format!("  {}", fmt::tokens(w))).unwrap_or_default();
                    let r = pick(ui, &t, &format!("{model_label}{win} {}", icon("chev-down")), model_open, true, false).on_hover_text(t!("Model"));
                    triggers.push(r.rect);
                    if r.clicked() {
                        if model_open {
                            st.menu = Menu::None;
                            if meta.has_process {
                                st.read_settings(cx.be, id);
                            }
                        } else if let Some(h) = cx.app.session(id) {
                            st.menu = Menu::Model(ModelMenu::new(&h.meta, &h.heavy));
                        }
                    }
                } else {
                    pick(ui, &t, &meta.model.clone().unwrap_or_else(|| t!("Model")), false, false, false);
                }
                // le conseiller, le style
                if let Some(adv) = &meta.advisor {
                    let label = model_list
                        .iter()
                        .find(|m| json::str(m, "resolvedModel") == Some(adv.as_str()) && json::str(m, "value") != Some("default"))
                        .map(|m| json::str(m, "displayName").unwrap_or(adv).to_string())
                        .unwrap_or_else(|| adv.clone());
                    widgets::tag(ui, &t, &t!("Composer.Advisor", label)).on_hover_text(t!("Model.AdvisorHint"));
                }
                if let Some(os) = meta.output_style.as_ref().filter(|s| *s != "default") {
                    widgets::tag(ui, &t, &t!("Composer.Style", os)).on_hover_text(t!("Model.StyleHint"));
                }
                // l'effort
                if !crashed
                    && let Some(levels) = &effort_levels
                    && let Some(level) =
                        effort::show(ui, &t, levels, effort_now.as_deref().unwrap_or_default(), &t!("Composer.Effort"), |l| i18n::name("Effort.", Some(l)))
                {
                    st.run(act(cx.be, id, move |s| async move { s.set_effort(&level).await }), true);
                }
                // rapide
                if fast_ok {
                    if pick(ui, &t, &t!("Composer.Fast"), fast_on, true, false).on_hover_text(t!("Composer.FastMode")).clicked() {
                        let on = !fast_on;
                        st.run(act(cx.be, id, move |s| async move { s.set_fast(on).await }), false);
                    }
                } else {
                    let why = match fast_reason(meta.fast_mode_reason.as_deref()) {
                        Some(r) => t!("Composer.FastUnavailableWhy", r),
                        None => t!("Composer.FastUnavailable"),
                    };
                    pick(ui, &t, &t!("Composer.Fast"), false, false, false).on_disabled_hover_text(why);
                }
                // la réflexion (le basculement du terminal ; en cours de conversation, la CLI prévient : latence)
                let hint = t!(if meta.context_tokens > 0 { "Composer.ThinkingMid" } else { "Composer.ThinkingHint" });
                if pick(ui, &t, &t!("Composer.Thinking"), meta.thinking, !crashed, false).on_hover_text(hint).clicked() {
                    let v = !meta.thinking;
                    st.run(act(cx.be, id, move |s| async move { s.set_thinking(v).await }), false);
                }
                // ultracode, à tout effort (son propre bouton)
                let ultra = format!("{} Ultracode", icon("agents"));
                if crashed || st.ultra_available == Some(false) {
                    pick(ui, &t, &ultra, false, false, false).on_disabled_hover_text(t!("Composer.UltraUnavailable"));
                } else if pick(ui, &t, &ultra, meta.ultracode, true, false).on_hover_text(t!("Composer.UltraHint")).clicked() {
                    match cx.be.session(id) {
                        Ok(s) => s.set_ultracode(!meta.ultracode),
                        Err(e) => st.error = Some(t!("Composer.ActionFailed", e)),
                    }
                }
            });
            ui.add_space(4.0);
            // le champ de chemin des pièces jointes, l'envoi
            ui.horizontal(|ui| {
                if st.attach_open && !crashed {
                    let r = ui.add(
                        egui::TextEdit::singleline(&mut st.attach_path)
                            .id(Id::new(("ccui-attach", id)))
                            .hint_text(tr("Chemin du fichier (image, PDF, texte)…", "File path (image, PDF, text)…"))
                            .desired_width((ui.available_width() - 320.0).max(160.0)),
                    );
                    let enter = r.lost_focus() && ui.input(|i| i.key_pressed(Key::Enter));
                    let add = ui.add_enabled(st.reading.is_none() && !st.attach_path.trim().is_empty(), egui::Button::new(tr("Joindre", "Attach")));
                    if (enter || add.clicked()) && !st.attach_path.trim().is_empty() {
                        let p = st.attach_path.trim();
                        let p = match p.strip_prefix("~/") {
                            Some(rest) => format!("{}/{rest}", std::env::var("HOME").unwrap_or_default()),
                            None => p.to_string(),
                        };
                        st.attach(cx, id, vec![p.into()]);
                    }
                    if st.reading.is_some() {
                        ui.spinner();
                    }
                }
                ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                    let blank = st.text.trim().is_empty();
                    let has_content = !blank || has_images;
                    let pending_n = meta.pending.len();
                    if running {
                        let stop = format!("{} {}", icon("stop"), t!(if ultra_turn { "Composer.StopWorkflow" } else { "Composer.Interrupt" }));
                        if go(ui, &t, &stop, Some(&t!("Key.Esc")), Kind::Plain, true).clicked() {
                            st.run(act(cx.be, id, move |s| async move { s.interrupt().await }), false);
                        }
                        if !blank {
                            if go(ui, &t, &t!("Composer.SendNow"), Some(&i18n::kbd("Ctrl ⇧ ⏎")), Kind::Plain, true).on_hover_text(t!("Composer.SendNowHint")).clicked() {
                                st.send(cx, id, true);
                            }
                            if go(ui, &t, &t!("Composer.Queue"), Some(&i18n::kbd("Ctrl ⏎")), Kind::Primary, true).on_hover_text(t!("Composer.QueueHint")).clicked() {
                                st.send(cx, id, false);
                            }
                        }
                        if ultra_turn {
                            widgets::pill(ui, &t, "ultracode", widgets::Tone::Accent);
                        }
                    } else if crashed {
                        go(ui, &t, &t!("Composer.Send"), None, Kind::Plain, false);
                    } else if pending_n > 0 {
                        // envoyer répond d'abord aux demandes en attente par un refus : le bouton le dit
                        if go(ui, &t, &t!("Composer.DenyAndSend", pending_n), Some(&i18n::kbd("Ctrl ⏎")), Kind::Danger, has_content)
                            .on_hover_text(t!("Composer.DenyAndSendHint"))
                            .clicked()
                        {
                            st.send(cx, id, false);
                        }
                    } else if go(ui, &t, &t!("Composer.Send"), Some(&i18n::kbd("Ctrl ⏎")), if has_content { Kind::Primary } else { Kind::Plain }, has_content)
                        .clicked()
                    {
                        st.send(cx, id, false);
                    }
                    if st.sending.is_some() {
                        ui.spinner();
                    }
                });
            });
        });
    let box_rect = boxed.response.rect;
    st.triggers = triggers;

    // ------------------------------------------------------------------ la ligne des chiffres
    ui.add_space(4.0);
    ui.horizontal(|ui| {
        ui.spacing_mut().item_spacing.x = 4.0;
        let turn = meta.turn_started_at.map(|s| (chrono::Utc::now() - s).num_milliseconds()).or(meta.last_turn_ms);
        if let Some(d) = turn {
            fact(ui, &t, &t!("Composer.Turn"), &fmt::dur(d));
        }
        fact(ui, &t, &t!("Composer.Session"), &fmt::cost(meta.cost_usd, 4));
        fact(ui, &t, &t!("Kv.Tools"), &meta.tool_count.to_string());
        // ce que la session a publié (/artifacts de la CLI)
        if !meta.artifacts.is_empty() {
            ui.menu_button(RichText::new(i18n::plural(meta.artifacts.len() as i64, "Artifacts.Count")).font(theme::mono(theme::CAPTION)).color(t.accent_ink), |ui| {
                for a in &meta.artifacts {
                    if ui.link(&a.title).on_hover_text(&a.url).clicked() {
                        crate::backend::open_external(&a.url);
                    }
                }
            });
            ui.add_space(8.0);
        }
        if st.history.index() >= 0 {
            fact(ui, &t, &t!("Composer.History", st.history.index() + 1, st.history.count()), "");
        }
        if meta.context_tokens > 0 {
            let w = meta.context_window.map(|w| format!(" / {}", fmt::tokens(w))).unwrap_or_default();
            fact(ui, &t, &t!("Composer.Context"), &format!("{}{w}", fmt::tokens(meta.context_tokens)));
        }
        ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
            if let Some(e) = &st.error {
                ui.add(egui::Label::new(RichText::new(e).font(theme::medium(theme::CAPTION)).color(t.danger)).truncate()).on_hover_text(e);
            } else if meta.ultracode && !running {
                ui.label(RichText::new(t!("Composer.UltraThisTurn")).font(theme::mono(theme::CAPTION)).color(t.text_dim));
                widgets::pill(ui, &t, "ultracode", widgets::Tone::Accent);
            } else if !running {
                let word = match meta.last_result_subtype.as_deref() {
                    None | Some("success") => String::new(),
                    Some(session_rules::INTERRUPTED) => t!("Composer.Interrupted"),
                    Some(_) => t!("Composer.TurnError"),
                };
                if !word.is_empty() {
                    ui.label(RichText::new(word).font(theme::mono(theme::CAPTION)).color(t.text_faint));
                }
            }
        });
    });
    if meta.turn_started_at.is_some() {
        ctx.request_repaint_after(Duration::from_secs(1));
    }
    if st.send_now.is_some() {
        ctx.request_repaint_after(Duration::from_millis(25));
    }

    // ------------------------------------------------------------------ menus et listes, au-dessus de la boîte
    let Some(h) = cx.app.session(id) else { return };
    let mut close = false;
    let mut menu_rect = None;
    let was_model = matches!(st.menu, Menu::Model(_));
    match &mut st.menu {
        Menu::Mode(m) => {
            let out = m.show(&ctx, cx.be, &t, box_rect, id, &h.meta, &menu_keys);
            menu_rect = Some(out.rect);
            close = out.close;
        }
        Menu::Model(m) => {
            let out = m.show(&ctx, cx.be, &t, box_rect, id, &h.meta, &h.heavy, &menu_keys);
            menu_rect = Some(out.rect);
            close = out.close;
        }
        Menu::Config(m) => {
            let refs = h.item_refs();
            let out = m.show(&ctx, cx.be, &t, box_rect, id, &refs);
            menu_rect = Some(out.rect);
            close = out.close;
        }
        Menu::None => {}
    }
    // un clic à côté ferme le menu (le « bouclier » de l'original)
    if let Some(r) = menu_rect
        && ctx.input(|i| i.pointer.any_pressed())
        && let Some(p) = ctx.input(|i| i.pointer.interact_pos())
        && !r.contains(p)
        && !st.triggers.iter().any(|t| t.contains(p))
    {
        close = true;
    }
    if close {
        st.menu = Menu::None;
        st.caret_end = true;
        if was_model && meta.has_process {
            // un autre modèle : son effort appliqué et son plafond
            st.read_settings(cx.be, id);
        }
    }
    let menus_open = matches!(st.menu, Menu::Mode(_) | Menu::Model(_));
    if !st.text.starts_with('/') {
        st.slash_dismissed = false;
    }
    if file_index::query(&st.text).is_none() {
        st.mention_dismissed = false;
    }
    let sq = slash_query(&st.text, st.slash_dismissed, st.history.index());
    let mq = mention_query(&st.text, st.mention_dismissed, menus_open, sq.is_some(), st.recalled.as_deref());
    let mut pop_open = false;
    let mut picked_slash = None;
    let mut picked_mention = None;
    if !st.menu.is_open() {
        if let Some(q) = &sq {
            let out = popovers::slash(&ctx, &t, box_rect, &h.heavy, status == SessionStatus::Starting, q, &mut st.slash_cur, pop_key);
            pop_open = out.open;
            picked_slash = out.picked;
        } else if let Some(q) = &mq {
            // le dossier est parcouru une fois par ouverture de la liste
            let scan = st.mention.get_or_insert_with(|| MentionScan::start(cx.be, &meta.cwd, &h.heavy));
            let out = popovers::mention(&ctx, &t, box_rect, scan, q, &mut st.mention_cur, pop_key);
            pop_open = out.open;
            picked_mention = out.picked;
        }
    }
    if mq.is_none() || st.menu.is_open() {
        st.mention = None;
    }
    st.pop_open = pop_open;
    if let Some(name) = picked_slash {
        st.history.reset();
        st.set_text(cx, id, format!("/{name} "));
    }
    if let Some(path) = picked_mention {
        st.history.reset();
        let v = file_index::insert(&st.text, &path);
        st.set_text(cx, id, v);
    }
    ctx.data_mut(|d| d.insert_temp(Id::new(POPOVER_OPEN), pop_open || st.menu.is_open()));
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn queries() {
        assert_eq!(slash_query("/co", false, -1).as_deref(), Some("/co"));
        assert_eq!(slash_query("/co ", false, -1), None);
        assert_eq!(slash_query("/co", false, 0), None, "a recalled command does not open the popover");
        assert_eq!(slash_query("/co", true, -1), None);
        assert_eq!(mention_query("look @src/Fo", false, false, false, None).as_deref(), Some("src/Fo"));
        assert_eq!(mention_query("fix @src/Foo.cs", false, false, false, Some("fix @src/Foo.cs")), None);
        assert_eq!(mention_query("fix @src", false, true, false, None), None);
    }

    #[test]
    fn natives() {
        assert_eq!(native("/model", "s", true, false, true), Some(Native::ModelMenu));
        assert_eq!(native("/model", "s", false, false, true), None);
        assert_eq!(native("/config", "s", true, true, true), None);
        assert_eq!(native("/hooks", "s", true, false, true), Some(Native::Go("extensions?tab=hooks&session=s".into())));
        assert_eq!(native("/allowed-tools", "s", true, false, true), Some(Native::Go("extensions?tab=permissions&session=s".into())));
        assert_eq!(native("/plugins", "s", true, false, true), Some(Native::Go("extensions?tab=plugins&session=s".into())));
        assert_eq!(native("/undo", "s", true, false, true), Some(Native::Rewind));
        assert_eq!(native("/diff", "s", true, false, false), None);
        assert_eq!(native("/diff now", "s", true, false, true), None);
    }

    #[test]
    fn placeholders_and_reasons() {
        assert_eq!(placeholder(SessionStatus::Running, true, None), t!("Composer.PhWorkflow"));
        assert_eq!(placeholder(SessionStatus::Idle, false, Some("go on")), t!("Composer.Suggestion", "go on"));
        assert_eq!(placeholder(SessionStatus::Idle, false, None), t!("Composer.PhIdle"));
        assert_eq!(fast_reason(Some("sdk_opt_in_required")), Some(t!("Composer.FastSdkOptIn")));
        assert_eq!(fast_reason(Some("other")).as_deref(), Some("other"));
        assert_eq!(fast_reason(None), None);
    }

    #[test]
    fn caret_lines() {
        assert!(caret_edge("abc", 1, true));
        assert!(caret_edge("abc", 1, false));
        assert!(!caret_edge("a\nb", 3, true));
        assert!(caret_edge("a\nb", 3, false));
        assert!(caret_edge("a\nb", 1, true));
        assert!(!caret_edge("a\nb", 1, false));
    }
}
