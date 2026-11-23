//! Nouvelle session (`pages/new_session.rs` de ClaudeCodeUI, la page par défaut) : le dossier de travail (saisi,
//! choisi par le sélecteur de dossier de la fenêtre, ou parmi les récents : ceux de l'app de Korin
//! `organize.json` puis ceux des transcriptions `recent_roots`), les dossiers en plus (`--add-dir`, vérifiés par
//! `validate_extra_dir`), l'isolation dans un worktree au nom généré (modifiable), le modèle et celui de repli,
//! l'agent, l'effort, le mode d'autorisation, les options de lancement (restreint, sans échec), la reprise depuis une
//! pull request ; « Démarrer » (Ctrl ⏎) lance `sessions.start` et ouvre la session. Un mode qui modifie sans
//! demander, hors d'un worktree isolé ou avec un dossier en plus, demande un second clic. Sans `claude` dans le
//! PATH : l'écran « Claude Code est introuvable » et la marche à suivre.

use ccui_core::new_session as ns;
use ccui_shared::api::{ClaudeFound, PastSession, StartArgs};
use ccui_shared::{i18n, session_rules, t, text};
use egui::{Align, CornerRadius, Frame, Key, Layout, Margin, Modifiers, RichText, Stroke, TextEdit, Ui, Vec2};

use crate::app::Cx;
use crate::backend::Task;
use crate::pages::form;
use crate::theme::{self, Tokens};
use crate::widgets::{self, icon};

/// Les alias de la CLI ; "" garde le défaut de la CLI ou des réglages.
const MODELS: [&str; 3] = ["opus", "sonnet", "haiku"];
/// Les niveaux de `claude --effort` ; "" garde le défaut.
const EFFORTS: [&str; 6] = ["", "low", "medium", "high", "xhigh", "max"];
const PICK_CWD: &str = "new-session-cwd";
const PICK_DIR: &str = "new-session-add-dir";

/// `Path.TrimEndingDirectorySeparator` : un séparateur final retiré, une racine gardée.
pub fn trim_sep(p: &str) -> &str {
    let t = p.strip_suffix(['/', '\\']).unwrap_or(p);
    if t.is_empty() || t.ends_with(':') { p } else { t }
}

/// `Path.GetFileName` d'une fin sans séparateur.
pub fn file_name(p: &str) -> &str {
    p.rsplit(['/', '\\']).next().unwrap_or(p)
}

/// Le dossier personnel : Claude pourrait toucher à tous les fichiers cachés, sans worktree pour l'isoler.
pub fn is_home(cwd: &str, home: &str) -> bool {
    !home.is_empty() && trim_sep(cwd).to_lowercase() == trim_sep(home).to_lowercase()
}

/// La normalisation de `SetCwd` : espaces puis guillemets retirés (un « C:\x » collé).
pub fn clean_cwd(v: &str) -> String {
    v.trim().trim_matches('"').to_string()
}

/// auto / acceptEdits modifient le dossier sans demander : hors d'un worktree isolé, ou avec un dossier en plus
/// (jamais isolé), le second Démarrer confirme.
pub fn needs_confirm(mode: &str, add_dirs: usize, isolated: bool, cwd: &str, confirmed: bool) -> bool {
    session_rules::risky(mode) && (add_dirs > 0 || !isolated && text::root_of(cwd) == cwd) && !confirmed
}

/// Le premier nom de la session : le worktree, sinon le dossier (la CLI la renomme après le premier message).
pub fn session_name(wt: Option<&str>, cwd: &str) -> String {
    wt.map_or_else(|| file_name(trim_sep(cwd)).to_string(), str::to_string)
}

/// Les dossiers récents : ceux choisis dans l'app d'abord, puis ceux des transcriptions, sans doublon (casse
/// ignorée), 8 au plus.
pub fn merge_recents(app: &[String], roots: &[String]) -> Vec<String> {
    let mut out: Vec<String> = Vec::new();
    for d in app.iter().chain(roots) {
        let d = trim_sep(d).to_string();
        if !d.is_empty() && !out.iter().any(|x| x.to_lowercase() == d.to_lowercase()) {
            out.push(d);
        }
    }
    out.truncate(8);
    out
}

/// Un texte des tables qui contient du HTML (`<code>`, `<kbd>`, `<b>`) en Markdown pour `md::show`.
pub fn html_to_md(s: &str) -> String {
    s.replace("<code>", "`")
        .replace("</code>", "`")
        .replace("<kbd>", "`")
        .replace("</kbd>", "`")
        .replace("<b>", "**")
        .replace("</b>", "**")
        .replace("<strong>", "**")
        .replace("</strong>", "**")
        .replace("<br>", "\n")
        .replace("&nbsp;", "\u{a0}")
}

fn opt(s: String) -> Option<String> {
    Some(s).filter(|s| !s.is_empty())
}

/// Ce que donne « Démarrer ».
enum Outcome {
    NoDir,
    /// À confirmer (la branche relue).
    Confirm(Option<String>),
    Started(String),
}

/// Le sondage d'un dossier : son numéro, il existe, sa branche, les agents connus.
type Probe = (u64, bool, Option<String>, Vec<String>);

pub struct NewSession {
    mounted: bool,
    boot: Option<Task<(ClaudeFound, Vec<String>, String)>>,
    found: ClaudeFound,
    cwd: String,
    cwd_edit: String,
    mode: String,
    model: String,
    effort: String,
    fallback: String,
    from_pr: String,
    agent: String,
    agent_names: Vec<String>,
    branch: Option<String>,
    probed: bool,
    probe_n: u64,
    probe: Option<Task<Probe>>,
    wt_name: Option<String>,
    generated: String,
    draft: String,
    editing: bool,
    exists: bool,
    worktree: bool,
    busy: bool,
    confirm_risk: bool,
    restricted: bool,
    safe_mode: bool,
    pr_missing: bool,
    pr_task: Option<Task<Option<PastSession>>>,
    roots: Vec<String>,
    add_dirs: Vec<String>,
    dir_draft: String,
    dir_error: Option<String>,
    start: Option<Task<Outcome>>,
}

impl Default for NewSession {
    fn default() -> Self {
        let seed = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_nanos() as u64).unwrap_or(0);
        NewSession {
            mounted: false,
            boot: None,
            found: ClaudeFound { found: true, npm_shim_only: false },
            cwd: String::new(),
            cwd_edit: String::new(),
            mode: String::new(),
            model: String::new(),
            effort: String::new(),
            fallback: String::new(),
            from_pr: String::new(),
            agent: String::new(),
            agent_names: Vec::new(),
            branch: None,
            probed: false,
            probe_n: 0,
            probe: None,
            wt_name: None,
            generated: text::random_name(seed % 8000),
            draft: String::new(),
            editing: false,
            exists: true,
            worktree: true,
            busy: false,
            confirm_risk: false,
            restricted: false,
            safe_mode: false,
            pr_missing: false,
            pr_task: None,
            roots: Vec::new(),
            add_dirs: Vec::new(),
            dir_draft: String::new(),
            dir_error: None,
            start: None,
        }
    }
}

impl NewSession {
    fn isolated(&self) -> bool {
        self.worktree && self.branch.is_some()
    }

    /// `SetCwd` : le dossier, puis son sondage (existe, branche, agents) ; un sondage plus récent l'emporte.
    fn set_cwd(&mut self, cx: &Cx, value: &str) {
        let value = clean_cwd(value);
        self.cwd = value.clone();
        self.cwd_edit = value.clone();
        self.confirm_risk = false;
        self.probe_n += 1;
        let n = self.probe_n;
        let core = cx.be.core.clone();
        self.probe = Some(cx.be.task(async move {
            let ok = ns::local_dir(&value);
            if !ok {
                return (n, false, None, Vec::new());
            }
            let b = ccui_core::git::branch(&value).await;
            let names = ns::agent_names(&value, &core.sessions);
            (n, true, b, names)
        }));
    }

    /// Les réponses arrivées : départ (claude, récents), sondage, sélecteur de dossier, PR, démarrage.
    fn poll(&mut self, cx: &mut Cx) {
        if !self.mounted {
            self.mounted = true;
            self.boot = Some(cx.be.blocking(|| (ns::claude_found(), ns::recent_roots(), ns::default_cwd())));
        }
        if let Some(r) = self.boot.as_ref().and_then(Task::take) {
            self.boot = None;
            let (found, roots, default) = r;
            self.found = found;
            self.roots = roots;
            let first = merge_recents(&cx.org.recent_dirs, &self.roots).into_iter().next().unwrap_or(default);
            self.set_cwd(cx, &first);
        }
        if let Some((n, ok, b, names)) = self.probe.as_ref().and_then(Task::take) {
            self.probe = None;
            if n == self.probe_n {
                self.exists = ok;
                self.branch = b;
                self.agent_names = names;
                self.probed = true;
            }
        }
        if let Some(d) = crate::host::picked(PICK_CWD) {
            let d = ccui_core::folder::trim_picked(&d);
            self.set_cwd(cx, &d);
        }
        if let Some(d) = crate::host::picked(PICK_DIR) {
            self.dir_draft = ccui_core::folder::trim_picked(&d);
            self.add_dir();
        }
        if let Some(r) = self.pr_task.as_ref().and_then(Task::take) {
            self.pr_task = None;
            match r {
                Some(p) => cx.open_session(&p.id),
                None => self.pr_missing = true,
            }
        }
        if let Some(o) = self.start.as_ref().and_then(Task::take) {
            self.start = None;
            match o {
                Outcome::NoDir => {
                    self.exists = false;
                    self.busy = false;
                }
                Outcome::Confirm(b) => {
                    self.branch = b;
                    self.confirm_risk = true;
                    self.busy = false;
                }
                Outcome::Started(id) => {
                    // (busy reste : la page s'en va ; le prochain passage repart d'un nom neuf)
                    cx.org.note_dir(&self.cwd);
                    cx.org.save();
                    cx.open_session(&id);
                    let keep = (self.cwd.clone(), self.roots.clone(), self.found.clone());
                    *self = NewSession::default();
                    self.mounted = true;
                    self.roots = keep.1;
                    self.found = keep.2;
                    self.set_cwd(cx, &keep.0);
                }
            }
        }
    }

    /// Ajoute le dossier saisi (`ExtraDirs.Add`) ; false : la raison est montrée.
    fn add_dir(&mut self) -> bool {
        let raw = self.dir_draft.clone();
        if raw.trim().is_empty() {
            return true;
        }
        match ns::validate_extra_dir(&raw, &self.cwd, &self.add_dirs) {
            Ok(d) => {
                self.add_dirs.push(d);
                self.dir_draft.clear();
                self.dir_error = None;
                self.confirm_risk = false;
                true
            }
            Err(m) => {
                self.dir_error = Some(m);
                false
            }
        }
    }

    fn commit_name(&mut self) {
        // (un nom qui ne donne rien garde le nom généré)
        if self.draft != self.wt_label() {
            self.wt_name = text::worktree_name(&self.draft);
        }
        self.editing = false;
    }

    fn wt_label(&self) -> String {
        self.wt_name.clone().unwrap_or_else(|| self.generated.clone())
    }

    fn resume_pr(&mut self, cx: &Cx) {
        if self.busy || !self.exists || self.from_pr.trim().is_empty() || self.pr_task.is_some() {
            return;
        }
        let (pr, c) = (self.from_pr.clone(), self.cwd.clone());
        self.pr_task = Some(cx.be.blocking(move || ccui_core::transcript_store::linked_to(&pr, &c)));
    }

    fn start(&mut self, cx: &Cx) {
        if self.busy || self.start.is_some() {
            return;
        }
        if self.cwd_edit != self.cwd {
            let v = self.cwd_edit.clone();
            self.set_cwd(cx, &v);
        }
        // (un dossier saisi n'est pas perdu, ni un nom de worktree)
        if !self.add_dir() {
            return;
        }
        if self.editing {
            self.commit_name();
        }
        self.busy = true;
        let core = cx.be.core.clone();
        let (c, worktree, mode, dirs, confirmed) = (self.cwd.clone(), self.worktree, self.mode.clone(), self.add_dirs.clone(), self.confirm_risk);
        let (wt_name, generated) = (self.wt_name.clone(), self.generated.clone());
        let (model, fallback, effort) = (self.model.clone(), self.fallback.clone(), self.effort.clone());
        let (restricted, safe_mode, agent) = (self.restricted, self.safe_mode, self.agent.trim().to_string());
        self.start = Some(cx.be.task(async move {
            if !ns::local_dir(&c) {
                return Outcome::NoDir;
            }
            let b = ccui_core::git::branch(&c).await;
            let iso = worktree && b.is_some();
            if needs_confirm(&mode, dirs.len(), iso, &c, confirmed) {
                return Outcome::Confirm(b);
            }
            let wt = if iso {
                Some(match wt_name {
                    Some(n) => n,
                    None => ns::free_worktree_name(&c, &generated, &core.sessions).await,
                })
            } else {
                None
            };
            let args = StartArgs {
                name: session_name(wt.as_deref(), &c),
                cwd: c,
                mode,
                worktree: wt,
                model: opt(model.clone()),
                effort: opt(effort),
                add_dirs: dirs,
                fallback: opt(fallback).filter(|f| *f != model),
                restricted,
                safe_mode,
                agent: opt(agent),
            };
            Outcome::Started(core.sessions.start(args).await.id().to_string())
        }));
    }

    pub fn ui(&mut self, ui: &mut Ui, cx: &mut Cx) {
        self.poll(cx);
        let t = cx.t;
        form::page(ui, &t, &t!("Rail.NewSession"), "", 680.0, |_| {}, |ui| {
            ui.add_space(24.0);
            if self.found.found { self.card(ui, cx, &t) } else { self.not_found(ui, cx, &t) }
        });
    }

    fn hello(ui: &mut Ui, t: &Tokens, title: &str, text: &str) {
        ui.label(RichText::new(title).font(theme::semibold(theme::TITLE)).color(t.text));
        ui.add_space(2.0);
        crate::md::show(ui, t, text);
        ui.add_space(14.0);
    }

    /// « Claude Code est introuvable » : la marche à suivre, et « Vérifier à nouveau ».
    fn not_found(&mut self, ui: &mut Ui, cx: &mut Cx, t: &Tokens) {
        let text = t!(if self.found.npm_shim_only { "New.NpmShimText" } else { "New.NotFoundText" });
        Self::hello(ui, t, &t!("New.NotFound"), &html_to_md(&text));
        form::field_label(ui, t, &t!("New.Step1"));
        crate::md::show(ui, t, &format!("{} `curl -fsSL https://claude.ai/install.sh | bash`", t!("New.Unix")));
        crate::md::show(ui, t, &format!("{} `irm https://claude.ai/install.ps1 | iex`", t!("New.Windows")));
        ui.horizontal(|ui| {
            ui.label(t!("New.Guide"));
            if ui.link("docs.claude.com").clicked() {
                crate::backend::open_external(&t!("New.GuideUrl"));
            }
        });
        form::field_label(ui, t, &t!("New.Step2"));
        crate::md::show(ui, t, &html_to_md(&t!("New.Step2Text")));
        form::field_label(ui, t, &t!("New.Step3"));
        crate::md::show(ui, t, &html_to_md(&t!("New.Step3Text")));
        ui.add_space(12.0);
        if widgets::primary(ui, t, &t!("New.CheckAgain")).clicked() {
            self.boot = Some(cx.be.blocking(|| (ns::claude_found(), ns::recent_roots(), ns::default_cwd())));
        }
    }

    fn card(&mut self, ui: &mut Ui, cx: &mut Cx, t: &Tokens) {
        if ui.input_mut(|i| i.consume_key(Modifiers::COMMAND, Key::Enter)) && self.exists {
            self.start(cx);
        }
        Self::hello(ui, t, &t!("New.Hello"), &t!("New.HelloText"));
        let (home, sep) = (cx.app.boot.home.clone(), cx.app.boot.sep.clone());

        // ---- le dossier de travail
        form::field_label(ui, t, &t!("New.Folder"));
        box_row(ui, t, |ui| {
            ui.label(RichText::new(icon("folder")).color(t.text_dim));
            let browse = ui
                .with_layout(Layout::right_to_left(Align::Center), |ui| {
                    let b = ui.add(egui::Button::new(RichText::new(t!("Pick.Folder")).color(t.text_dim)).frame(false));
                    let r = ui.add(TextEdit::singleline(&mut self.cwd_edit).font(theme::mono(theme::CODE)).frame(Frame::NONE).desired_width(ui.available_width()));
                    if r.lost_focus() && self.cwd_edit != self.cwd {
                        let v = self.cwd_edit.clone();
                        self.set_cwd(cx, &v);
                    }
                    b.clicked()
                })
                .inner;
            if browse {
                crate::host::pick_folder(&ccui_core::folder::pick_start(Some(&self.cwd)), PICK_CWD);
            }
        });
        if !self.exists {
            form::fmsg(ui, t, &t!("New.NoFolder"), false);
        } else if is_home(&self.cwd, &home) {
            form::fmsg(ui, t, &t!("New.HomeFolder"), true);
        }
        let recents = merge_recents(&cx.org.recent_dirs, &self.roots);
        if !recents.is_empty() {
            let mut pick = None;
            ui.horizontal_wrapped(|ui| {
                ui.spacing_mut().item_spacing = Vec2::new(6.0, 6.0);
                for d in &recents {
                    let on = trim_sep(d).eq_ignore_ascii_case(trim_sep(&self.cwd));
                    if chip(ui, t, &text::chip(d, &home, &sep), on).on_hover_text(d).clicked() {
                        pick = Some(d.clone());
                    }
                }
            });
            if let Some(d) = pick {
                self.set_cwd(cx, &d);
            }
        }

        // ---- les dossiers en plus
        form::field_label(ui, t, &t!("Dirs.Label"));
        if !self.add_dirs.is_empty() {
            let mut remove = None;
            ui.horizontal_wrapped(|ui| {
                for d in &self.add_dirs {
                    let label = format!("{} {}  {}", icon("folder"), text::chip(d, &home, &sep), icon("x"));
                    if chip(ui, t, &label, false).on_hover_text(t!("Dirs.Remove", d)).clicked() {
                        remove = Some(d.clone());
                    }
                }
            });
            if let Some(d) = remove {
                self.add_dirs.retain(|x| *x != d);
                self.confirm_risk = false;
            }
        }
        box_row(ui, t, |ui| {
            ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                let add = ui.add_enabled(!self.dir_draft.trim().is_empty(), egui::Button::new(t!("Dirs.Add")).frame(false)).clicked();
                if ui.add(egui::Button::new(RichText::new(icon("folder")).color(t.text_dim)).frame(false)).on_hover_text(t!("Pick.Folder")).clicked() {
                    let start = if self.dir_draft.trim().is_empty() { self.cwd.clone() } else { self.dir_draft.trim().to_string() };
                    crate::host::pick_folder(&ccui_core::folder::pick_start(Some(&start)), PICK_DIR);
                }
                let r = ui.add(TextEdit::singleline(&mut self.dir_draft).hint_text(t!("Dirs.Placeholder")).font(theme::mono(theme::CODE)).frame(Frame::NONE).desired_width(ui.available_width()));
                if r.changed() {
                    self.dir_error = None;
                }
                if add || (r.lost_focus() && ui.input(|i| i.key_pressed(Key::Enter))) {
                    self.add_dir();
                }
            });
        });
        if let Some(e) = &self.dir_error {
            form::fmsg(ui, t, e, false);
        }

        // ---- l'isolation
        form::field_label(ui, t, &t!("New.Isolation"));
        box_row(ui, t, |ui| {
            let iso = self.isolated();
            if form::switch(ui, t, iso, &t!("New.Worktree"), self.branch.is_some()).clicked() {
                self.worktree = !self.worktree;
                self.confirm_risk = false;
            }
            ui.add_space(8.0);
            if self.branch.is_none() {
                if self.probed {
                    widgets::mono_dim(ui, t, &t!("New.NotGit"));
                }
            } else if self.worktree {
                if self.editing {
                    widgets::mono_dim(ui, t, &format!("{} worktree-", icon("branch")));
                    let r = ui.add(TextEdit::singleline(&mut self.draft).font(theme::mono(theme::CODE)).desired_width(220.0));
                    let (enter, esc) = ui.input(|i| (i.key_pressed(Key::Enter) && !i.modifiers.command, i.key_pressed(Key::Escape)));
                    if esc {
                        self.editing = false;
                    } else if (r.lost_focus() && enter) || ui.button(t!("Action.Ok")).clicked() {
                        self.commit_name();
                    }
                } else {
                    let label = t!("New.WorktreeFrom", format!("worktree-{}", self.wt_label()), self.branch.clone().unwrap_or_default());
                    ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                        if widgets::ghost(ui, t, &t!("New.Edit")).clicked() {
                            self.draft = self.wt_label();
                            self.editing = true;
                        }
                        ui.add(egui::Label::new(RichText::new(format!("{} {label}", icon("branch"))).font(theme::mono(theme::CAPTION)).color(t.text_dim)).truncate()).on_hover_text(&label);
                    });
                }
            }
        });

        // ---- modèle, repli, agent, effort
        ui.add_space(4.0);
        ui.columns(2, |cols| {
            form::field_label(&mut cols[0], t, &t!("Model"));
            let name = |m: &str, none: &str| if m.is_empty() { none.to_string() } else { m.to_string() };
            egui::ComboBox::from_id_salt("ns-model").width(cols[0].available_width()).selected_text(name(&self.model, &t!("New.DefaultModel"))).show_ui(&mut cols[0], |ui| {
                ui.selectable_value(&mut self.model, String::new(), t!("New.DefaultModel"));
                for m in MODELS {
                    ui.selectable_value(&mut self.model, m.to_string(), m);
                }
            });
            form::field_label(&mut cols[1], t, &t!("New.Fallback"));
            if self.fallback == self.model {
                self.fallback.clear();
            }
            egui::ComboBox::from_id_salt("ns-fallback")
                .width(cols[1].available_width())
                .selected_text(name(&self.fallback, &t!("New.NoFallback")))
                .show_ui(&mut cols[1], |ui| {
                    ui.selectable_value(&mut self.fallback, String::new(), t!("New.NoFallback"));
                    for m in MODELS.into_iter().filter(|m| *m != self.model) {
                        ui.selectable_value(&mut self.fallback, m.to_string(), m);
                    }
                })
                .response
                .on_hover_text(t!("New.FallbackHint"));
        });
        form::field_label(ui, t, &t!("New.Agent"));
        box_row(ui, t, |ui| {
            ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                if !self.agent_names.is_empty() {
                    ui.menu_button(icon("chev-down"), |ui| {
                        for a in &self.agent_names {
                            if ui.button(a).clicked() {
                                self.agent = a.clone();
                                ui.close();
                            }
                        }
                    });
                }
                ui.add(TextEdit::singleline(&mut self.agent).hint_text(t!("New.AgentHint")).frame(Frame::NONE).desired_width(ui.available_width()));
            });
        });
        form::field_label(ui, t, &t!("New.Effort"));
        let names: Vec<String> = EFFORTS.iter().map(|e| i18n::name("Effort.", Some(if e.is_empty() { "default" } else { e }))).collect();
        if let Some(i) = form::seg(ui, t, &names, EFFORTS.iter().position(|e| *e == self.effort)) {
            self.effort = EFFORTS[i].to_string();
        }

        // ---- le mode d'autorisation
        form::field_label(ui, t, &t!("New.Permissions"));
        let modes: Vec<&str> = std::iter::once("").chain(session_rules::MODES).collect();
        ui.columns(modes.len(), |cols| {
            for (col, m) in cols.iter_mut().zip(&modes) {
                if mode_card(col, t, m, self.mode == *m).clicked() {
                    self.mode = m.to_string();
                    self.confirm_risk = false;
                }
            }
        });

        // ---- options de lancement
        form::field_label(ui, t, &t!("New.Launch"));
        box_row(ui, t, |ui| {
            if form::switch(ui, t, self.restricted, &t!("New.Restricted"), true).on_hover_text(t!("New.RestrictedHint")).clicked() {
                self.restricted = !self.restricted;
            }
            ui.add_space(16.0);
            if form::switch(ui, t, self.safe_mode, &t!("New.SafeMode"), true).on_hover_text(t!("New.SafeModeHint")).clicked() {
                self.safe_mode = !self.safe_mode;
            }
        });

        // ---- depuis une pull request
        form::field_label(ui, t, &t!("New.FromPr"));
        box_row(ui, t, |ui| {
            ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                let can = !self.busy && !self.from_pr.trim().is_empty() && self.exists;
                let go = ui.add_enabled(can, egui::Button::new(t!("New.FromPrGo")).frame(false)).clicked();
                let r = ui.add(TextEdit::singleline(&mut self.from_pr).hint_text(t!("New.FromPrHint")).frame(Frame::NONE).desired_width(ui.available_width()));
                if r.changed() {
                    self.pr_missing = false;
                }
                if go || (r.lost_focus() && ui.input(|i| i.key_pressed(Key::Enter) && !i.modifiers.command)) {
                    self.resume_pr(cx);
                }
            });
        });
        if self.pr_missing {
            form::fmsg(ui, t, &t!("New.FromPrNone", self.from_pr.trim().to_string()), false);
        }

        // ---- démarrer
        ui.add_space(16.0);
        ui.horizontal(|ui| {
            ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                let label = format!("{}   {}", if self.confirm_risk { t!("New.StartAnyway") } else { t!("New.Start") }, i18n::kbd("Ctrl ⏎"));
                if ui.add_enabled_ui(!self.busy && self.exists, |ui| widgets::primary(ui, t, &label)).inner.clicked() {
                    self.start(cx);
                }
                if self.busy {
                    ui.spinner();
                }
                if self.confirm_risk {
                    let key = if self.add_dirs.is_empty() { "New.ConfirmRisky" } else { "Dirs.ConfirmRisky" };
                    ui.add(egui::Label::new(RichText::new(t!(key, i18n::name("Mode.Name.", Some(&self.mode)))).color(t.warning)).wrap());
                }
            });
        });
        ui.add_space(14.0);
        ui.horizontal_wrapped(|ui| {
            for (k, h) in [("Ctrl K", "New.HintSwitch"), ("Alt N", "New.HintNew"), ("Ctrl I", "New.HintInspector")] {
                widgets::kbd(ui, t, &i18n::kbd(k));
                widgets::dim(ui, t, &t!(h));
                ui.add_space(10.0);
            }
        });
    }
}

/// Une ligne encadrée comme un champ (`.input`) : icône, saisie, bouton.
fn box_row(ui: &mut Ui, t: &Tokens, add: impl FnOnce(&mut Ui)) {
    Frame::new()
        .fill(t.field)
        .stroke(Stroke::new(1.0, t.card_border))
        .corner_radius(CornerRadius::same(theme::R_LG))
        .inner_margin(Margin::symmetric(12, 8))
        .show(ui, |ui| {
            ui.set_width(ui.available_width());
            ui.set_min_height(22.0);
            ui.horizontal(add);
        });
}

/// Une puce cliquable (`.chip` : dossier récent, dossier en plus).
fn chip(ui: &mut Ui, t: &Tokens, text: &str, on: bool) -> egui::Response {
    let b = egui::Button::new(RichText::new(text).font(theme::mono(theme::CAPTION)).color(if on { t.accent_ink } else { t.text_dim }))
        .fill(if on { t.accent_soft } else { t.card })
        .stroke(Stroke::new(1.0, if on { t.accent_ink.gamma_multiply(0.5) } else { t.card_border }))
        .corner_radius(CornerRadius::same(10));
    ui.add(b)
}

/// Une carte de mode (`.mode` : son nom, en couleur d'avertissement s'il modifie sans demander, et ce qu'il fait).
fn mode_card(ui: &mut Ui, t: &Tokens, m: &str, on: bool) -> egui::Response {
    let r = Frame::new()
        .fill(if on { t.accent_soft } else { t.card })
        .stroke(Stroke::new(if on { 1.5 } else { 1.0 }, if on { t.accent_ink } else { t.card_border }))
        .corner_radius(CornerRadius::same(theme::R_LG))
        .inner_margin(Margin::symmetric(10, 8))
        .show(ui, |ui| {
            ui.set_width(ui.available_width());
            ui.set_min_height(80.0);
            ui.with_layout(Layout::top_down(Align::Min), |ui| {
            let c = if session_rules::risky(m) { t.warning } else if on { t.accent_ink } else { t.text };
            ui.label(RichText::new(i18n::name("Mode.Name.", Some(m))).font(theme::semibold(theme::LABEL)).color(c));
            ui.add(egui::Label::new(RichText::new(t!(&format!("Mode.{m}"))).font(egui::FontId::proportional(theme::CAPTION)).color(t.text_dim)).wrap());
            });
        })
        .response;
    let r = ui.interact(r.rect, r.id.with("mode"), egui::Sense::click());
    if r.hovered() {
        ui.ctx().set_cursor_icon(egui::CursorIcon::PointingHand);
    }
    r
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn home_folder() {
        assert!(is_home("/Users/me/", "/Users/me"));
        assert!(is_home("C:\\Users\\Me", "c:\\users\\me\\"));
        assert!(!is_home("/Users/me/repo", "/Users/me"));
        assert!(!is_home("/", ""));
    }

    #[test]
    fn cwd_cleaning() {
        assert_eq!(clean_cwd("  \"C:\\x\"  "), "C:\\x");
        assert_eq!(trim_sep("/"), "/");
        assert_eq!(trim_sep("C:\\"), "C:\\");
        assert_eq!(trim_sep("/a/b/"), "/a/b");
    }

    #[test]
    fn risk_confirm() {
        assert!(needs_confirm("auto", 0, false, "/r", false));
        assert!(!needs_confirm("auto", 0, false, "/r", true));
        assert!(!needs_confirm("acceptEdits", 0, true, "/r", false));
        assert!(needs_confirm("acceptEdits", 1, true, "/r", false));
        assert!(!needs_confirm("auto", 0, false, "/r/.claude/worktrees/x", false));
        assert!(!needs_confirm("plan", 2, false, "/r", false));
    }

    #[test]
    fn names() {
        assert_eq!(session_name(Some("brave-x"), "/r"), "brave-x");
        assert_eq!(session_name(None, "/a/repo/"), "repo");
        assert_eq!(session_name(None, "C:\\a\\api"), "api");
    }

    #[test]
    fn recents() {
        let app = vec!["/a/x/".to_string(), "/b".to_string()];
        let roots = vec!["/A/X".to_string(), "/c".to_string()];
        assert_eq!(merge_recents(&app, &roots), vec!["/a/x", "/b", "/c"]);
    }

    #[test]
    fn html() {
        assert_eq!(html_to_md("le CLI <code>claude</code>"), "le CLI `claude`");
    }
}
