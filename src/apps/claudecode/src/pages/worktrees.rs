//! Les worktrees (`pages/worktrees.rs` de ClaudeCodeUI) : la découverte des dépôts, le parcours de chacun (classés
//! en « peuvent partir », « à vérifier », « actifs », avec la raison de git), la taille de chaque dossier, les filtres,
//! le tri par taille, les replis (branches orphelines, HEAD détachées, squashées, mêmes fichiers ignorés), et à
//! droite le **nettoyage** pas à pas : le plan (les commandes git exactes, jamais `--force` ni `-D`), puis son
//! exécution par le moteur (`worktrees.run`, chaque étape arrive en `Push::WorktreeRunStep` dans
//! `cx.app.wt_steps`, la fin en `wt_done`). Pousser une branche jamais poussée ; copier un chemin ; ouvrir une
//! session dans un worktree. Les dépôts où `.claude/worktrees/` n'est pas ignoré sont signalés.

use std::collections::{HashMap, HashSet};
use std::sync::Arc;
use std::sync::atomic::{AtomicUsize, Ordering};

use ccui_shared::model::Time;
use ccui_shared::worktree_rules::{self as rules, WorktreeInfo, WtState, display_name};
use ccui_shared::{fmt, i18n, t};
use egui::{Align, Frame, Layout, Margin, RichText, ScrollArea, Stroke, Ui};

use super::parts::{self, one_line, table_head, table_row};
use crate::app::{Cx, Route};
use crate::backend::Task;
use crate::theme::{self, Tokens};
use crate::widgets::{self, icon};

/// Les titres sont des clés i18n.
const GROUPS: [(&str, &str, &[WtState]); 3] = [
    ("Wt.Group.Go", "Wt.Group.GoHelp", &[WtState::Safe, WtState::Orphan]),
    ("Wt.State.Check", "Wt.Group.CheckHelp", &[WtState::Check]),
    ("Wt.Filter.Active", "Wt.Group.ActiveHelp", &[WtState::Active]),
];

type Sizes = HashMap<String, i64>;

fn file_name(p: &str) -> &str {
    p.rsplit(['/', '\\']).next().unwrap_or(p)
}

fn focus_key() -> egui::Id {
    egui::Id::NULL.with("wt-focus-path")
}

/// La ligne à choisir en arrivant (le `?path=` de ClaudeCodeUI : la vue d'ensemble y mène).
pub fn focus_path(ctx: &egui::Context, path: &str) {
    ctx.data_mut(|d| d.insert_temp(focus_key(), path.to_string()));
}

fn take_focus_path(ctx: &egui::Context) -> Option<String> {
    ctx.data_mut(|d| d.remove_temp::<String>(focus_key()))
}

/// Le même fichier ignoré, une seule décision.
pub fn ignored_only(w: &WorktreeInfo) -> bool {
    !w.branch_only && w.state == WtState::Check && w.kept.is_some() && w.dirty == 0 && w.at_risk() == 0 && rules::cleanable(w, true)
}

/// Repliés par dépôt et par genre : branches orphelines, dossiers sûrs détachés, dossiers sûrs squashés, branches
/// squashées dont le dossier est parti, le même fichier ignoré.
pub fn folds(w: &WorktreeInfo) -> bool {
    w.branch_only && w.state == WtState::Orphan
        || !w.branch_only && w.state == WtState::Safe && (w.branch.is_none() || w.squash_merged && !w.merged)
        || w.branch_only && w.state == WtState::Check && w.squash_merged && !w.merged
        || ignored_only(w)
}

pub fn fold_kind(w: &WorktreeInfo) -> char {
    if w.branch_only {
        if w.state == WtState::Check { 'q' } else { 'b' }
    } else if ignored_only(w) {
        'i'
    } else if w.branch.is_none() {
        'd'
    } else {
        's'
    }
}

pub fn fold_key(w: &WorktreeInfo) -> String {
    let k = fold_kind(w);
    let mut s = format!("fold:{k}:{}", w.repo);
    if k == 'i' {
        s.push(':');
        s.push_str(w.kept.as_deref().unwrap_or(""));
    }
    s
}

/// Les branches, puis les dossiers détachés ; les lignes à vérifier : les commits que rien d'autre ne garde, puis les
/// fichiers non commités…
pub fn risk(w: &WorktreeInfo) -> i32 {
    if w.state != WtState::Check {
        return if w.branch_only { 2 } else if w.branch.is_none() { 1 } else { 0 };
    }
    if w.at_risk() > 0 {
        0
    } else if w.dirty > 0 {
        1
    } else if w.unreadable.is_some() {
        2
    } else if w.kept.is_some() {
        3
    } else if w.recent_min.is_some() {
        4
    } else if w.branch_only {
        6
    } else if w.locked {
        7
    } else {
        5
    }
}

/// Les plus risqués d'abord, puis un ordre fixe : le dernier parcours et le nouveau listent les mêmes lignes au même
/// endroit.
pub fn order(mut g: Vec<WorktreeInfo>, by_size: bool, sizes: &Sizes) -> Vec<WorktreeInfo> {
    if by_size {
        let sz = |w: &WorktreeInfo| sizes.get(&w.path).copied().unwrap_or(-1);
        g.sort_by(|a, b| sz(b).cmp(&sz(a)).then_with(|| a.path.cmp(&b.path)));
    } else {
        g.sort_by(|a, b| {
            risk(a)
                .cmp(&risk(b))
                .then(b.at_risk().cmp(&a.at_risk()))
                .then(b.dirty.cmp(&a.dirty))
                .then(b.last_activity.cmp(&a.last_activity))
                .then_with(|| file_name(&a.repo).to_lowercase().cmp(&file_name(&b.repo).to_lowercase()))
                .then_with(|| display_name(a).cmp(display_name(b)))
        });
    }
    g
}

pub enum Part {
    Row(WorktreeInfo),
    Fold(Vec<WorktreeInfo>),
}

/// Une seule suite ordonnée : un repli (≥ 2 membres d'un genre dans un dépôt) prend la place de son premier membre.
pub fn split(g: Vec<WorktreeInfo>) -> Vec<Part> {
    let mut groups: Vec<(String, Vec<WorktreeInfo>)> = Vec::new();
    for w in g.iter().filter(|w| folds(w)) {
        let k = fold_key(w);
        match groups.iter_mut().find(|(x, _)| *x == k) {
            Some((_, v)) => v.push(w.clone()),
            None => groups.push((k, vec![w.clone()])),
        }
    }
    groups.retain(|(_, v)| v.len() >= 2);
    let folded: HashSet<&str> = groups.iter().flat_map(|(_, v)| v.iter().map(|w| w.path.as_str())).collect();
    let mut out = Vec::new();
    for w in &g {
        if let Some(i) = groups.iter().position(|(_, v)| v[0].path == w.path) {
            out.push(Part::Fold(groups[i].1.clone()));
        } else if !folded.contains(w.path.as_str()) {
            out.push(Part::Row(w.clone()));
        }
    }
    out
}

/// « 5 worktrees + 21 branches » : les lignes de branche seule ne sont pas des worktrees.
pub fn items(ws: &[WorktreeInfo]) -> String {
    let b = ws.iter().filter(|w| w.branch_only).count() as i64;
    let n = ws.len() as i64 - b;
    if b == 0 {
        i18n::plural(n, "Wt.Worktrees")
    } else if n == 0 {
        i18n::plural(b, "Wt.Branches")
    } else {
        format!("{} + {}", i18n::plural(n, "Wt.Worktrees"), i18n::plural(b, "Wt.Branches"))
    }
}

/// Les branches seules n'ont pas de taille ; « … » tant que tous les dossiers ne sont pas mesurés.
pub fn size_of(ws: &[WorktreeInfo], sizes: &Sizes) -> String {
    let known: Vec<&WorktreeInfo> = ws.iter().filter(|w| w.state != WtState::Orphan && !w.branch_only).collect();
    if known.is_empty() {
        return "—".into();
    }
    if known.iter().all(|w| sizes.contains_key(&w.path)) {
        fmt::size(known.iter().map(|w| sizes[&w.path]).sum())
    } else {
        "…".into()
    }
}

pub fn what(w: &WorktreeInfo, kept_ok: bool) -> String {
    if let (true, Some(k)) = (kept_ok, &w.kept) {
        return t!("Wt.What.WithKept", k);
    }
    if w.branch_only {
        return t!("Wt.What.BranchOnly");
    }
    if w.state == WtState::Orphan {
        return t!("Wt.What.RefOnly");
    }
    let key = if w.branch.is_none() {
        "Wt.What.Detached"
    } else if !w.merged {
        "Wt.What.FolderOnly"
    } else {
        "Wt.What.FolderAndBranch"
    };
    format!("{}{}", if w.locked { t!("Wt.What.Unlock") } else { String::new() }, t!(key))
}

#[derive(Clone, Debug, PartialEq)]
pub struct LogLine {
    pub repo: String,
    pub cmd: String,
    pub result: Option<String>,
}

/// Le journal des commandes : (texte, commentaire) par ligne ; « # dépôt » et les résultats sont des commentaires.
pub fn cmd_log(lines: &[LogLine], home: &str) -> Vec<(String, bool)> {
    let mut repos: Vec<&str> = Vec::new();
    for l in lines {
        if !repos.contains(&l.repo.as_str()) {
            repos.push(&l.repo);
        }
    }
    let mut out = Vec::new();
    for r in repos {
        out.push((format!("# {}", fmt::home(r, home)), true));
        for l in lines.iter().filter(|l| l.repo == r) {
            out.push((l.cmd.clone(), false));
            if let Some(res) = &l.result {
                out.push((format!("→ {res}"), true));
            }
        }
    }
    out
}

/// La ligne de résultat d'une étape : « ok », la sortie de prune / branch -d (« Deleted branch x (was 1a2b3c4) » : le
/// sha pour la retrouver), le refus du moteur, ou le code de sortie.
pub fn step_result(display: &str, exit: i32, output: &str) -> String {
    let first: Vec<&str> = output.split('\n').map(str::trim).filter(|l| !l.is_empty()).collect();
    match exit {
        0 => {
            let verb = display.split(' ').nth(2).unwrap_or("");
            if !first.is_empty() && (verb == "prune" || verb == "-d") { first.join(" · ") } else { "ok".into() }
        }
        -1 => output.to_string(),
        _ => t!("Wt.ExitOutput", exit, first.join(" · ")),
    }
}

/// Les actions d'une ligne (la première branche qui convient).
#[derive(Clone, Debug, PartialEq)]
pub enum Act {
    Unplan,
    /// prune (un dossier déjà parti) ou supprimer.
    Add(bool),
    Go(String),
    Push,
    UnplanKept,
    /// Copier le chemin, + « Supprimer quand même » quand il part avec ses fichiers ignorés.
    Kept(bool),
    CopyActive,
    /// Nouvelle session, avec Copier le chemin quand il a des changements.
    Plain(bool),
    None,
}

pub fn act_of(w: &WorktreeInfo, planned: bool, kept_ok: bool, session: Option<String>) -> Act {
    if rules::cleanable(w, false) {
        return if planned { Act::Unplan } else { Act::Add(w.state == WtState::Orphan && !w.branch_only) };
    }
    if w.state == WtState::Active
        && let Some(s) = session
    {
        return Act::Go(s);
    }
    if w.at_risk() > 0 && w.dirty == 0 && w.branch.is_some() && !w.branch_only {
        return Act::Push;
    }
    if kept_ok && planned {
        return Act::UnplanKept;
    }
    if w.kept.is_some() && w.dirty == 0 && w.at_risk() == 0 {
        return Act::Kept(rules::cleanable(w, true));
    }
    if w.state == WtState::Active && !w.branch_only {
        return Act::CopyActive;
    }
    if !w.branch_only {
        return Act::Plain(w.dirty > 0);
    }
    Act::None
}

#[derive(Clone, Debug, PartialEq)]
enum Entry {
    Group(&'static str, &'static str),
    Row(String, bool),
    Fold(String),
}

#[derive(Clone, Default)]
struct Lay {
    entries: Vec<Entry>,
    by_path: HashMap<String, WorktreeInfo>,
    folds: HashMap<String, Vec<WorktreeInfo>>,
}

fn layout(rows: &[WorktreeInfo], filter: Option<WtState>, open: &HashSet<String>, by_size: bool, sizes: &Sizes) -> Lay {
    let mut l = Lay { by_path: rows.iter().map(|w| (w.path.clone(), w.clone())).collect(), ..Lay::default() };
    for (title, help, states) in GROUPS {
        let g: Vec<WorktreeInfo> = rows.iter().filter(|w| states.contains(&w.state) && filter.is_none_or(|f| w.state == f)).cloned().collect();
        if g.is_empty() {
            continue;
        }
        l.entries.push(Entry::Group(title, help));
        for part in split(order(g, by_size, sizes)) {
            match part {
                Part::Row(w) => l.entries.push(Entry::Row(w.path, false)),
                Part::Fold(f) => {
                    let key = fold_key(&f[0]);
                    l.entries.push(Entry::Fold(key.clone()));
                    if open.contains(&key) || filter.is_some() {
                        for w in &f {
                            l.entries.push(Entry::Row(w.path.clone(), true));
                        }
                    }
                    l.folds.insert(key, f);
                }
            }
        }
    }
    l
}

/// Ce que rend un parcours : le nombre de dépôts, les lignes, ceux qui n'ignorent pas `.claude/worktrees/`, les erreurs.
type ScanOut = Result<(usize, Vec<WorktreeInfo>, Vec<String>, Vec<String>), String>;

/// Ce qu'un clic demande, appliqué après la passe.
enum Do {
    Plan(Vec<String>, bool),
    Unplan(Vec<String>),
    Go(String),
    Push(WorktreeInfo),
    Copy(String),
    NewSession(String),
    Toggle(String),
    Select(String),
}

#[derive(Default)]
pub struct Worktrees {
    frame: u64,
    rows: Option<Vec<WorktreeInfo>>,
    repos_n: usize,
    unignored: Vec<String>,
    /// Montrés avec les lignes du dernier parcours : les avertissements ne poussent pas le tableau à l'arrivée.
    last_unignored: Vec<String>,
    errors: Vec<String>,
    /// Gardées d'une visite à l'autre : l'arrivée montre les dernières tailles, puis remesure.
    sizes: Sizes,
    plan: HashSet<String>,
    /// Les lignes choisies avec « Supprimer quand même » : leurs fichiers ignorés partent aussi.
    kept_ok: HashSet<String>,
    log: Vec<LogLine>,
    filter: Option<WtState>,
    selected: Option<String>,
    push_result: Option<String>,
    push: Option<WorktreeInfo>,
    running: bool,
    by_size: bool,
    open: HashSet<String>,
    from_cache: bool,
    cached_at: Option<Time>,
    progress: Arc<(AtomicUsize, AtomicUsize)>,
    /// La ligne où l'on arrive (depuis la vue d'ensemble) : choisie, son repli ouvert, amenée à l'écran.
    focus: Option<String>,
    reveal: bool,
    cur_run: Option<u64>,
    t_scan: Option<Task<ScanOut>>,
    t_sizes: Vec<(String, Task<i64>)>,
    t_push: Option<Task<Result<i32, String>>>,
    t_run: Option<(u64, String, Task<Result<(), String>>)>,
    /// « Copié » sur le bouton qui vient de copier (le texte copié, l'heure).
    copied: Option<(String, f64)>,
}

impl Worktrees {
    fn scan(&mut self, cx: &Cx, refresh: bool) {
        if self.t_scan.is_some() {
            return;
        }
        // le dernier parcours tout de suite (un lien de la vue d'ensemble arrive sur sa ligne), puis le nouveau
        if self.rows.is_none() {
            let c = cx.be.core.worktrees.cached();
            if c.cached_at.is_some() {
                self.cached_at = c.cached_at;
                self.rows = Some(c.rows);
                self.from_cache = true;
                self.unignored = self.last_unignored.clone();
                self.seed_focus();
            }
        }
        let core = cx.be.core.clone();
        let home = cx.app.boot.home.clone();
        let progress = self.progress.clone();
        progress.0.store(0, Ordering::Relaxed);
        progress.1.store(0, Ordering::Relaxed);
        self.t_scan = Some(cx.be.task(async move {
            let repos = core.worktrees.discover_repos(refresh).await?;
            progress.1.store(repos.len(), Ordering::Relaxed);
            let (mut found, mut bad, mut errs) = (Vec::new(), Vec::new(), Vec::new());
            for r in &repos {
                match core.worktrees.scan(r, None).await {
                    Ok(list) => {
                        if !list.is_empty() && !super::overview::scratch(r) && !core.worktrees.ignores_worktrees(r).await {
                            bad.push(r.clone());
                        }
                        found.extend(list);
                    }
                    Err(e) => errs.push(t!("Wt.RepoError", fmt::home(r, &home), e)),
                }
                progress.0.fetch_add(1, Ordering::Relaxed); // compté une fois lu : le dernier ne paraît jamais fini trop tôt
            }
            Ok((repos.len(), found, bad, errs))
        }));
    }

    /// Le repli de la ligne d'arrivée s'ouvre une fois ; ensuite c'est à l'utilisateur de le fermer.
    fn seed_focus(&mut self) {
        let Some(f) = &self.focus else { return };
        let fw = self.rows.as_ref().and_then(|r| r.iter().find(|w| &w.path == f).cloned());
        if let Some(fw) = fw.filter(folds) {
            self.open.insert(fold_key(&fw));
        }
    }

    fn measure(&mut self, cx: &Cx, path: String) {
        let p = path.clone();
        let task = cx.be.blocking(move || ccui_core::worktrees::WorktreeService::size(&p));
        self.t_sizes.push((path, task));
    }

    fn poll(&mut self, cx: &mut Cx) {
        if let Some(r) = self.t_scan.as_ref().and_then(Task::take) {
            self.t_scan = None;
            match r {
                Err(e) => {
                    // git absent : aucun dépôt ne se résout
                    self.rows = Some(Vec::new());
                    self.unignored.clear();
                    self.errors = vec![e];
                }
                Ok((n, found, bad, errs)) => {
                    self.repos_n = n;
                    self.last_unignored = bad.clone();
                    let ko = self.kept_ok.clone();
                    self.plan.retain(|path| found.iter().any(|w| w.path == *path && rules::cleanable(w, ko.contains(path))));
                    let pl = self.plan.clone();
                    self.kept_ok.retain(|p| pl.contains(p));
                    // par-dessus les anciennes tailles : les totaux ne se vident jamais
                    for w in found.iter().filter(|w| w.state != WtState::Orphan && !w.branch_only) {
                        self.measure(cx, w.path.clone());
                    }
                    self.rows = Some(found);
                    self.unignored = bad;
                    self.errors = errs;
                    self.from_cache = false;
                    self.seed_focus();
                }
            }
        }
        let sizes = &mut self.sizes;
        self.t_sizes.retain(|(p, task)| match task.take() {
            Some(z) => {
                sizes.insert(p.clone(), z);
                false
            }
            None => true,
        });
        if let Some(r) = self.t_push.as_ref().and_then(Task::take) {
            self.t_push = None;
            self.running = false;
            match r {
                Ok(0) => {
                    self.push_result = Some("ok".into());
                    self.scan(cx, false);
                }
                Ok(n) => self.push_result = Some(format!("exit {n}")),
                Err(e) => self.push_result = Some(e),
            }
        }
        // les étapes du nettoyage arrivent en pushes marqués de son numéro ; WorktreeRunDone le termine
        if let Some(id) = self.cur_run {
            let mine: Vec<LogLine> = cx
                .app
                .wt_steps
                .iter()
                .filter(|s| s.run_id == id)
                .map(|s| LogLine { repo: s.repo.clone(), cmd: s.display.clone(), result: Some(step_result(&s.display, s.exit, &s.output)) })
                .collect();
            if !mine.is_empty() {
                self.log = mine;
            }
            let failed = match &self.t_run {
                Some((rid, repo, task)) if *rid == id => match task.take() {
                    Some(Err(e)) => Some((repo.clone(), e)),
                    _ => None,
                },
                _ => None,
            };
            if let Some((repo, e)) = failed {
                self.log.push(LogLine { repo, cmd: "git".into(), result: Some(e) });
                self.finish(cx, id);
            } else if cx.app.wt_done == Some(id) {
                self.finish(cx, id);
            }
        }
    }

    fn finish(&mut self, cx: &mut Cx, id: u64) {
        self.cur_run = None;
        self.t_run = None;
        self.running = false;
        self.plan.clear();
        self.kept_ok.clear();
        cx.app.wt_steps.retain(|s| s.run_id != id);
        self.scan(cx, false);
    }

    fn run(&mut self, cx: &Cx, chosen: &[WorktreeInfo]) {
        if self.running || chosen.is_empty() {
            return;
        }
        let paths: Vec<String> = chosen.iter().map(|w| w.path.clone()).collect();
        let kept: Vec<String> = self.kept_ok.iter().cloned().collect();
        let repo = chosen[0].repo.clone();
        let nanos = std::time::SystemTime::now().duration_since(std::time::UNIX_EPOCH).map(|d| d.as_nanos() as u64).unwrap_or(1);
        let id = nanos ^ u64::from(std::process::id()).rotate_left(32);
        self.running = true;
        self.log.clear();
        self.cur_run = Some(id);
        let core = cx.be.core.clone();
        self.t_run = Some((id, repo, cx.be.task(async move { core.worktrees.run(id, paths, kept).await })));
    }

    fn do_push(&mut self, cx: &Cx, w: &WorktreeInfo) {
        if self.running {
            return;
        }
        self.running = true;
        self.push_result = Some("…".into());
        let core = cx.be.core.clone();
        let (path, branch) = (w.path.clone(), w.branch.clone());
        self.t_push = Some(cx.be.task(async move { core.worktrees.push(&path, branch.as_deref()).await }));
    }

    fn session_of(cx: &Cx, w: &WorktreeInfo) -> Option<String> {
        let p = w.path.to_lowercase();
        cx.app.summary.sessions.iter().find(|x| x.has_process && x.cwd.replace('\\', "/").to_lowercase().starts_with(&p)).map(|x| x.id.clone())
    }

    pub fn ui(&mut self, ui: &mut Ui, cx: &mut Cx) {
        let nr = ui.ctx().cumulative_frame_nr();
        let arrived = self.frame == 0 || nr > self.frame + 1;
        self.frame = nr;
        if let Some(f) = take_focus_path(ui.ctx()) {
            self.focus = Some(f.clone());
            self.selected = Some(f);
            self.reveal = true;
            self.seed_focus();
        }
        if arrived {
            self.scan(cx, false);
        }
        self.poll(cx);
        let t = cx.t;
        if self.t_scan.is_some() || !self.t_sizes.is_empty() || self.running {
            ui.ctx().request_repaint_after(std::time::Duration::from_millis(200));
        }

        if cx.ui.inspector_open {
            egui::Panel::right("wt-plan")
                .resizable(true)
                .default_size(400.0)
                .size_range(320.0..=620.0)
                .frame(Frame::new().fill(t.chrome).stroke(Stroke::new(1.0, t.separator)))
                .show(ui, |ui| self.inspector(ui, cx));
        }
        egui::CentralPanel::default().frame(Frame::new().fill(t.canvas)).show(ui, |ui| {
            let mut refresh = false;
            widgets::pane_header(
                ui,
                &t,
                |ui| {
                    ui.label(RichText::new(t!("Nav.Worktrees")).font(theme::semibold(theme::BODY)));
                    if let Some(r) = self.rows.as_ref().filter(|_| !self.from_cache && self.t_scan.is_none()) {
                        let mut repos: Vec<&str> = r.iter().map(|w| w.repo.as_str()).collect();
                        repos.sort_unstable();
                        repos.dedup();
                        let txt = format!("{} · {} · {}", i18n::plural(repos.len() as i64, "Wt.Repos"), items(r), size_of(r, &self.sizes));
                        widgets::mono_dim(ui, &t, &txt).on_hover_text(t!("Wt.ReposScanned", self.repos_n));
                    }
                },
                |ui| {
                    let scanning = self.t_scan.is_some();
                    refresh = ui.add_enabled(!scanning, egui::Button::new(format!("{} {}", icon("refresh"), t!("Wt.Refresh")))).clicked();
                    if scanning && self.rows.is_some() {
                        let (d, n) = (self.progress.0.load(Ordering::Relaxed), self.progress.1.load(Ordering::Relaxed));
                        let txt = if n > 0 { t!("Wt.ScanProgress", d, n) } else { t!("Wt.Scanning") };
                        let r = widgets::mono_dim(ui, &t, &txt);
                        if let (true, Some(at)) = (self.from_cache, self.cached_at) {
                            let mins = (chrono::Utc::now() - at).num_minutes().max(1);
                            r.on_hover_text(t!("Wt.CachedAge", fmt::dur(mins * 60_000)));
                        }
                    }
                },
            );
            if refresh {
                self.scan(cx, true);
            }
            ScrollArea::vertical().auto_shrink([false, false]).show(ui, |ui| {
                Frame::new().inner_margin(Margin::symmetric(28, 24)).show(ui, |ui| {
                    ui.set_width(ui.available_width());
                    ui.spacing_mut().item_spacing.y = 8.0;
                    if self.rows.is_some() {
                        self.table_part(ui, cx);
                    } else {
                        ui.label(RichText::new(t!("Wt.Scanning")).font(theme::semibold(theme::TITLE)).color(t.text));
                        ui.label(RichText::new(t!("Wt.ScanningText")).color(t.text_dim));
                    }
                });
            });
        });
    }

    fn table_part(&mut self, ui: &mut Ui, cx: &mut Cx) {
        let t = cx.t;
        let rows = self.rows.clone().unwrap_or_default();
        let safe: Vec<WorktreeInfo> = rows.iter().filter(|w| matches!(w.state, WtState::Safe | WtState::Orphan)).cloned().collect();
        let count = |s: WtState| rows.iter().filter(|w| w.state == s).count();
        let check = count(WtState::Check) as i64;
        let mixed = safe.iter().any(|w| w.branch_only);

        // le verdict, et son action à côté
        let h2 = if safe.is_empty() {
            t!("Wt.NothingToClean")
        } else if mixed {
            t!("Wt.SafeMixed", items(&safe))
        } else {
            i18n::plural(safe.len() as i64, "Wt.Safe")
        };
        let mut add_safe = false;
        ui.horizontal(|ui| {
            ui.vertical(|ui| {
                ui.label(RichText::new(h2).font(theme::semibold(theme::TITLE)).color(t.text));
                if safe.is_empty() {
                    ui.label(RichText::new(i18n::plural(check, "Wt.CheckOnly")).color(t.text_dim));
                } else {
                    let size = size_of(&safe, &self.sizes);
                    ui.horizontal_wrapped(|ui| {
                        ui.spacing_mut().item_spacing.x = 4.0;
                        if size != "…" {
                            ui.label(RichText::new(t!("Wt.Recoverable", size)).color(t.text_dim));
                        }
                        if check > 0 {
                            if ui.link(i18n::plural(check, "Wt.Check")).on_hover_text(t!("Wt.ShowCheck")).clicked() {
                                self.filter = Some(WtState::Check);
                            }
                        } else {
                            ui.label(RichText::new(i18n::plural(check, "Wt.Check")).color(t.text_dim));
                        }
                        ui.label(RichText::new(t!("Wt.NothingActive")).color(t.text_dim));
                    });
                }
            });
            if !safe.is_empty() {
                // gardé en place, invisible, quand un plan est ouvert : le tableau ne saute pas
                let held = !self.plan.is_empty() || !self.log.is_empty();
                let label = if mixed { t!("Wt.AddSafe", items(&safe)) } else { i18n::plural(safe.len() as i64, "Wt.AddSafe") };
                ui.with_layout(Layout::right_to_left(Align::Max), |ui| {
                    if held {
                        ui.add_visible(false, egui::Button::new(label));
                    } else if ui.button(label).clicked() {
                        add_safe = true;
                    }
                });
            }
        });
        if add_safe {
            self.plan.extend(safe.iter().map(|w| w.path.clone()));
            cx.ui.inspector_open = true;
        }

        for r in self.unignored.clone() {
            ui.horizontal(|ui| {
                ui.label(RichText::new(icon("warn")).color(t.warning));
                let b = format!("<b>{}</b>", fmt::enc(file_name(&r)));
                parts::label(ui, &t, &t!("Wt.Unignored", b), theme::LABEL, t.text_dim).on_hover_text(&r);
            });
        }
        for e in &self.errors {
            widgets::error_text(ui, &t, e);
        }

        // les filtres
        ui.horizontal_wrapped(|ui| {
            ui.spacing_mut().item_spacing.x = 6.0;
            let chips: [(Option<WtState>, Option<egui::Color32>, String); 5] = [
                (None, None, t!("Filter.All")),
                (Some(WtState::Safe), Some(t.success), t!("Wt.Filter.Safe")),
                (Some(WtState::Orphan), Some(t.text_faint), t!("Wt.Filter.Orphan")),
                (Some(WtState::Check), Some(t.warning), t!("Wt.State.Check")),
                (Some(WtState::Active), Some(t.accent_ink), t!("Wt.Filter.Active")),
            ];
            for (state, dot, label) in chips {
                let on = self.filter == state;
                let n = match state {
                    None => rows.len(),
                    Some(s) => count(s),
                };
                let r = Frame::new()
                    .fill(if on { t.hover } else { egui::Color32::TRANSPARENT })
                    .stroke(Stroke::new(1.0, if on { t.text_faint } else { t.card_border }))
                    .corner_radius(12)
                    .inner_margin(Margin::symmetric(9, 2))
                    .show(ui, |ui| {
                        ui.horizontal(|ui| {
                            ui.spacing_mut().item_spacing.x = 5.0;
                            if let Some(c) = dot {
                                widgets::dot(ui, c, true);
                            }
                            ui.label(RichText::new(label).font(theme::mono(theme::CAPTION)).color(if on { t.text } else { t.text_dim }));
                            ui.label(RichText::new(n.to_string()).font(theme::mono(theme::CAPTION)).color(t.text).strong());
                        });
                    })
                    .response;
                if ui.interact(r.rect, ui.scope_id().with(("chip", label_id(state))), egui::Sense::click()).clicked() {
                    self.filter = state;
                }
            }
        });

        // le tableau
        let w = parts::widths(ui.available_width(), &[0.0, 0.0, 84.0, 300.0], &[0.9, 1.1, 0.0, 0.0]);
        let right = [false, false, true, true];
        let size_title = format!("{}{}", t!("Wt.Col.Size"), if self.by_size { " ↓" } else { "" });
        let titles = [t!("Wt.Col.Worktree"), t!("Wt.Col.Why"), size_title, String::new()];
        let top = ui.cursor().top();
        let left = ui.cursor().left();
        table_head(ui, &t, &w, &right, &titles);
        // le tri par taille : un clic sur l'en-tête de sa colonne
        let r = egui::Rect::from_min_size(egui::pos2(left + w[0] + w[1], top), egui::vec2(w[2], 30.0));
        if ui.interact(r, ui.scope_id().with("sort-size"), egui::Sense::click()).on_hover_text(t!("Wt.SortSize")).clicked() {
            self.by_size = !self.by_size;
        }
        let lay = layout(&rows, self.filter, &self.open, self.by_size, &self.sizes);
        let mut todo: Vec<Do> = Vec::new();
        for e in &lay.entries {
            match e {
                Entry::Group(title, help) => {
                    ui.add_space(6.0);
                    ui.horizontal(|ui| {
                        ui.add_space(10.0);
                        widgets::cap(ui, &t, &t!(title));
                        one_line(ui, RichText::new(t!(help)).font(egui::FontId::proportional(theme::CAPTION)).color(t.text_faint)).on_hover_text(t!(help));
                    });
                    let r = ui.max_rect();
                    ui.painter().hline(r.x_range(), ui.cursor().top(), Stroke::new(1.0, t.separator));
                }
                Entry::Row(path, child) => {
                    if let Some(wi) = lay.by_path.get(path) {
                        self.row_view(ui, cx, &w, wi, *child, &mut todo);
                    }
                }
                Entry::Fold(key) => {
                    if let Some(f) = lay.folds.get(key) {
                        self.fold_view(ui, &t, &w, key, f, &mut todo);
                    }
                }
            }
        }
        for d in todo {
            match d {
                Do::Plan(paths, kept) => {
                    self.log.clear();
                    if kept {
                        self.kept_ok.extend(paths.iter().cloned());
                    }
                    self.plan.extend(paths);
                    cx.ui.inspector_open = true; // le plan qu'elle a rejoint est en vue
                }
                Do::Unplan(paths) => {
                    for p in &paths {
                        self.plan.remove(p);
                        self.kept_ok.remove(p);
                    }
                }
                Do::Go(id) => cx.open_session(&id),
                Do::Push(wi) => {
                    self.selected = Some(wi.path.clone());
                    self.push = Some(wi);
                    self.push_result = None;
                    cx.ui.inspector_open = true;
                }
                Do::Copy(p) => {
                    ui.ctx().copy_text(p.clone());
                    self.copied = Some((p, ui.ctx().input(|i| i.time)));
                }
                Do::NewSession(p) => {
                    cx.ui.new_session_cwd = Some(p);
                    cx.navigate(Route::NewSession);
                }
                Do::Toggle(k) => {
                    if self.filter.is_none() && !self.open.remove(&k) {
                        self.open.insert(k);
                    }
                }
                Do::Select(p) => self.selected = Some(p),
            }
        }
    }

    fn row_view(&mut self, ui: &mut Ui, cx: &Cx, widths: &[f32], wi: &WorktreeInfo, child: bool, todo: &mut Vec<Do>) {
        let t = cx.t;
        let path = wi.path.clone();
        let planned = self.plan.contains(&path);
        let act = act_of(wi, planned, self.kept_ok.contains(&path), Self::session_of(cx, wi));
        let sel = self.selected.as_deref() == Some(path.as_str()) || planned;
        let landed = self.focus.as_deref() == Some(path.as_str());
        let dim = wi.state == WtState::Orphan;
        let now = ui.ctx().input(|i| i.time);
        let copied = self.copied.as_ref().is_some_and(|(p, at)| *p == path && now - at < 2.0);
        let size = (!wi.branch_only && wi.state != WtState::Orphan).then(|| self.sizes.get(&wi.path).copied()).flatten();
        let running = self.running;
        let r = table_row(ui, &t, widths, &[false, false, true, true], 60.0, sel, landed.then_some(t.accent), |c, ui| match c {
            0 => {
                if child {
                    ui.add_space(14.0);
                }
                ui.vertical(|ui| {
                    ui.spacing_mut().item_spacing.y = 2.0;
                    ui.horizontal(|ui| {
                        one_line(ui, RichText::new(display_name(wi)).font(theme::medium(theme::LABEL)).color(if dim { t.text_dim } else { t.text }))
                            .on_hover_text(if wi.branch_only { wi.branch.clone().unwrap_or_default() } else { wi.path.clone() });
                        if wi.branch_only {
                            widgets::tag(ui, &t, &t!("Wt.BranchTag"));
                        }
                    });
                    ui.horizontal(|ui| {
                        ui.spacing_mut().item_spacing.x = 4.0;
                        ui.label(RichText::new(icon("branch")).font(theme::mono(theme::CAPTION)).color(t.text_faint));
                        if wi.branch.is_none() && !wi.branch_only {
                            widgets::tag(ui, &t, &t!(if wi.state == WtState::Orphan { "Wt.BranchMissing" } else { "Wt.DetachedTag" }));
                        }
                        // le dossier d'abord : c'est lui qui distingue deux lignes sur la même branche
                        let lead = (!wi.branch_only && wi.branch.is_some()).then(|| format!("{} · ", wi.name)).unwrap_or_default();
                        one_line(ui, RichText::new(format!("{lead}{}", file_name(&wi.repo))).font(theme::mono(theme::CAPTION)).color(t.text_faint));
                    });
                });
            }
            1 => {
                let mut job = egui::text::LayoutJob::default();
                for (s, b, _) in parts::spans(&wi.why) {
                    let font_id = if b { theme::semibold(theme::LABEL) } else { egui::FontId::proportional(theme::LABEL) };
                    job.append(&s, 0.0, egui::TextFormat { font_id, color: if b { t.text } else { t.text_dim }, ..Default::default() });
                }
                job.wrap.max_rows = 2;
                ui.add(egui::Label::new(job).wrap()).on_hover_text(parts::plain(&wi.why));
            }
            2 => {
                let s = if wi.state == WtState::Orphan || wi.branch_only {
                    "—".to_string()
                } else {
                    size.map_or_else(|| "…".to_string(), fmt::size)
                };
                ui.label(RichText::new(s).font(theme::mono(theme::LABEL)).color(t.text));
            }
            _ => {
                ui.spacing_mut().item_spacing.x = 6.0;
                let copy = |ui: &mut Ui, title: String, todo: &mut Vec<Do>| {
                    let label = if copied { format!("{} {}", icon("check"), t!("Wt.CopyPath")) } else { t!("Wt.CopyPath") };
                    if widgets::ghost(ui, &t, &label).on_hover_text(title).clicked() {
                        todo.push(Do::Copy(path.clone()));
                    }
                };
                match &act {
                    Act::Unplan | Act::UnplanKept => {
                        if ui.add_enabled(!running, egui::Button::new(t!("Wt.Unplan"))).clicked() {
                            todo.push(Do::Unplan(vec![path.clone()]));
                        }
                    }
                    Act::Add(prune) => {
                        // prune : le mot de git pour un dossier déjà parti
                        let verb = t!(if *prune { "Wt.Prune" } else { "Wt.Delete" });
                        if ui.add_enabled(!running, egui::Button::new(verb)).on_hover_text(t!("Wt.AddsToPlan")).clicked() {
                            todo.push(Do::Plan(vec![path.clone()], false));
                        }
                    }
                    Act::Go(id) => {
                        if ui.button(t!("Wt.Go")).clicked() {
                            todo.push(Do::Go(id.clone()));
                        }
                    }
                    Act::Push => {
                        if ui.add_enabled(!running, egui::Button::new(t!("Wt.Push"))).clicked() {
                            todo.push(Do::Push(wi.clone()));
                        }
                    }
                    // regarder d'abord (le chemin copié), puis décider ; jamais --force : git retire lui-même les
                    // fichiers ignorés
                    Act::Kept(anyway) => {
                        if *anyway
                            && ui
                                .add_enabled(!running, egui::Button::new(t!("Wt.DeleteAnyway")))
                                .on_hover_text(t!("Wt.DeleteAnywayHelp", wi.kept.clone().unwrap_or_default()))
                                .clicked()
                        {
                            todo.push(Do::Plan(vec![path.clone()], true));
                        }
                        copy(ui, t!("Wt.CopyPathHelp"), todo);
                    }
                    Act::CopyActive => copy(ui, t!("Wt.CopyPathActive"), todo),
                    Act::Plain(dirty) => {
                        if ui.button(t!("Wt.NewSession")).on_hover_text(t!("Wt.NewSessionIn", path.clone())).clicked() {
                            todo.push(Do::NewSession(path.clone()));
                        }
                        if *dirty {
                            copy(ui, t!("Wt.CopyPathHelp"), todo);
                        }
                    }
                    Act::None => {}
                }
            }
        });
        if landed && self.reveal {
            self.reveal = false;
            r.scroll_to_me(Some(Align::Center));
        }
        if r.clicked() {
            todo.push(Do::Select(path));
        }
    }

    fn fold_view(&self, ui: &mut Ui, t: &Tokens, widths: &[f32], key: &str, f: &[WorktreeInfo], todo: &mut Vec<Do>) {
        let all: Vec<String> = f.iter().map(|w| w.path.clone()).collect();
        let planned = all.iter().all(|x| self.plan.contains(x));
        let is_open = self.open.contains(key) || self.filter.is_some();
        let kind = fold_kind(&f[0]);
        let branches = kind == 'b' || kind == 'q';
        let repo_name = file_name(&f[0].repo).to_string();
        let kept = f[0].kept.clone().unwrap_or_default();
        let n = f.len() as i64;
        let label = if kind == 'i' {
            t!("Wt.FoldIgnored", n, kept)
        } else {
            i18n::plural(
                n,
                match kind {
                    'b' => "Wt.FoldBranches",
                    's' => "Wt.FoldSquash",
                    'q' => "Wt.FoldKeptBranches",
                    _ => "Wt.FoldDetached",
                },
            )
        };
        let help = match kind {
            'b' => "Wt.FoldHelp",
            's' => "Wt.FoldSquashHelp",
            'q' => "Wt.FoldKeptBranchesHelp",
            'i' => "Wt.FoldIgnoredHelp",
            _ => "Wt.FoldDetachedHelp",
        };
        let size = if branches { "—".to_string() } else { size_of(f, &self.sizes) };
        let running = self.running;
        let r = table_row(ui, t, widths, &[false, false, true, true], 52.0, planned, None, |c, ui| match c {
            0 => {
                ui.vertical(|ui| {
                    ui.spacing_mut().item_spacing.y = 2.0;
                    let arrow = if is_open { icon("chev-down") } else { icon("chev-right") };
                    one_line(ui, RichText::new(format!("{arrow} {label} · {repo_name}")).font(theme::medium(theme::LABEL)).color(t.text));
                    one_line(ui, RichText::new(t!(help)).font(egui::FontId::proportional(theme::CAPTION)).color(t.text_faint)).on_hover_text(t!(help));
                });
            }
            1 => {}
            2 => {
                ui.label(RichText::new(&size).font(theme::mono(theme::LABEL)).color(t.text));
            }
            _ => {
                if kind == 'q' {
                    // la page ne force jamais une suppression : la commande est à l'utilisateur
                    let cmd = format!("git -C {} branch -D {}", f[0].repo, f.iter().filter_map(|w| w.branch.clone()).collect::<Vec<_>>().join(" "));
                    if widgets::ghost(ui, t, &t!("Wt.CopyCommands")).on_hover_text(t!("Wt.CopyCommandsHelp")).clicked() {
                        todo.push(Do::Copy(cmd));
                    }
                } else if planned {
                    if ui.add_enabled(!running, egui::Button::new(t!("Wt.Unplan"))).clicked() {
                        todo.push(Do::Unplan(all.clone()));
                    }
                } else if kind == 'i' {
                    if ui.add_enabled(!running, egui::Button::new(t!("Wt.DeleteAllAnyway"))).on_hover_text(t!("Wt.DeleteAllAnywayHelp", kept.clone())).clicked() {
                        todo.push(Do::Plan(all.clone(), true));
                    }
                } else if ui.add_enabled(!running, egui::Button::new(t!("Wt.DeleteAll"))).on_hover_text(t!("Wt.AddsToPlanAll")).clicked() {
                    todo.push(Do::Plan(all.clone(), false));
                }
            }
        });
        if r.clicked() {
            todo.push(Do::Toggle(key.to_string()));
        }
    }

    // ------------------------------------------------------------------------------------------- le nettoyage

    fn inspector(&mut self, ui: &mut Ui, cx: &mut Cx) {
        let t = cx.t;
        let rows = self.rows.clone().unwrap_or_default();
        let chosen: Vec<WorktreeInfo> = rows.iter().filter(|w| self.plan.contains(&w.path)).cloned().collect();
        widgets::pane_header(
            ui,
            &t,
            |ui| {
                ui.label(RichText::new(t!("Wt.Cleanup")).font(theme::semibold(theme::BODY)));
                if self.rows.is_some() {
                    let txt = if chosen.is_empty() { t!("Wt.NothingPlannedShort") } else { format!("{} · {}", items(&chosen), size_of(&chosen, &self.sizes)) };
                    widgets::mono_dim(ui, &t, &txt);
                }
            },
            |_| {},
        );
        ScrollArea::vertical().auto_shrink([false, false]).show(ui, |ui| {
            Frame::new().inner_margin(Margin::symmetric(20, 16)).show(ui, |ui| {
                ui.set_width(ui.available_width());
                ui.spacing_mut().item_spacing.y = 8.0;
                self.push_section(ui, cx);
                ui.label(RichText::new(t!("Wt.WhatHappens")).font(theme::semibold(theme::SECTION)).color(t.text));
                self.plan_body(ui, cx, &chosen);
            });
        });
    }

    fn push_section(&mut self, ui: &mut Ui, cx: &mut Cx) {
        let t = cx.t;
        let Some(pw) = self.push.clone() else { return };
        let branch = pw.branch.clone().unwrap_or_default();
        ui.label(RichText::new(t!("Wt.PushTitle", display_name(&pw).to_string())).font(theme::semibold(theme::SECTION)).color(t.text));
        let unpushed = if pw.unpushed == 1 { t!("Wt.Unpushed1", fmt::enc(&branch)) } else { t!("Wt.UnpushedN", pw.unpushed, fmt::enc(&branch)) };
        parts::label(ui, &t, &unpushed, theme::LABEL, t.text_dim);
        let line = LogLine { repo: pw.path.clone(), cmd: format!("git push -u origin {branch}"), result: self.push_result.clone() };
        log_view(ui, &t, &cmd_log(&[line], &cx.app.boot.home));
        let ok = self.push_result.as_deref() == Some("ok");
        let mut close = false;
        ui.horizontal(|ui| {
            if !ok && ui.add_enabled(!self.running, egui::Button::new(RichText::new(t!("Wt.Push")).color(t.on_accent)).fill(t.accent)).clicked() {
                self.do_push(cx, &pw);
            }
            if widgets::ghost(ui, &t, &t!(if ok { "Action.Close" } else { "Action.Cancel" })).clicked() {
                close = true;
            }
        });
        if close {
            self.push = None;
            self.selected = None;
        }
        ui.add_space(8.0);
        ui.separator();
        ui.add_space(4.0);
    }

    fn plan_body(&mut self, ui: &mut Ui, cx: &mut Cx, chosen: &[WorktreeInfo]) {
        let t = cx.t;
        if chosen.is_empty() && self.log.is_empty() {
            ui.label(RichText::new(t!("Wt.NothingPlanned")).color(t.text_dim));
            return;
        }
        let n = chosen.len();
        // un bloc par dépôt, replié quand le plan est long : un plan de 120 lignes doit tenir d'un regard
        let mut groups: Vec<(String, Vec<WorktreeInfo>)> = Vec::new();
        for w in order(chosen.to_vec(), self.by_size, &self.sizes) {
            match groups.iter_mut().find(|(r, _)| *r == w.repo) {
                Some((_, g)) => g.push(w),
                None => groups.push((w.repo.clone(), vec![w])),
            }
        }
        for (repo, g) in &groups {
            let head = format!("{} · {}", items(g), size_of(g, &self.sizes));
            egui::CollapsingHeader::new(RichText::new(format!("{}   {head}", file_name(repo))).font(theme::medium(theme::LABEL)).color(t.text))
                .id_salt(("plan", repo))
                .default_open(n <= 8)
                .show(ui, |ui| {
                    for w in g {
                        let sub = format!("{}{}", if w.branch_only || w.branch.is_none() { String::new() } else { format!("{} · ", w.name) }, what(w, self.kept_ok.contains(&w.path)));
                        let size = if w.state == WtState::Orphan || w.branch_only {
                            "—".to_string()
                        } else {
                            self.sizes.get(&w.path).map_or_else(|| "…".to_string(), |z| fmt::size(*z))
                        };
                        ui.horizontal(|ui| {
                            ui.label(RichText::new(icon("check")).color(t.success));
                            ui.vertical(|ui| {
                                ui.spacing_mut().item_spacing.y = 1.0;
                                ui.horizontal(|ui| {
                                    ui.with_layout(Layout::right_to_left(Align::Min), |ui| {
                                        ui.label(RichText::new(size).font(theme::mono(theme::CAPTION)).color(t.text_dim));
                                        ui.with_layout(Layout::left_to_right(Align::Min), |ui| {
                                            one_line(ui, RichText::new(display_name(w)).font(theme::medium(theme::LABEL)).color(t.text));
                                        });
                                    });
                                });
                                ui.label(RichText::new(sub).font(theme::mono(theme::CAPTION)).color(t.text_faint));
                            });
                        });
                    }
                });
        }
        let lines: Vec<LogLine> = if self.log.is_empty() {
            let kept: Vec<String> = self.kept_ok.iter().cloned().collect();
            rules::plan(chosen, &kept).into_iter().map(|s| LogLine { repo: s.repo, cmd: s.display, result: None }).collect()
        } else {
            self.log.clone()
        };
        let log = cmd_log(&lines, &cx.app.boot.home);
        if !self.log.is_empty() || lines.len() <= 12 {
            log_view(ui, &t, &log);
        } else {
            egui::CollapsingHeader::new(t!("Wt.PlanCmds", lines.len())).id_salt("plan-cmds").show(ui, |ui| log_view(ui, &t, &log));
        }
        if lines.iter().any(|l| l.cmd.contains(" branch -d ")) {
            ui.horizontal(|ui| {
                ui.label(RichText::new(icon("info")).color(t.text_faint));
                parts::label(ui, &t, &t!("Wt.NeverForce"), theme::LABEL, t.text_dim);
            });
        }
        ui.add_space(4.0);
        ui.horizontal(|ui| {
            if self.log.is_empty() {
                let go = ui.add_enabled(!self.running && !chosen.is_empty(), egui::Button::new(RichText::new(t!("Wt.CleanN", items(chosen))).color(t.on_accent)).fill(t.accent));
                if go.clicked() {
                    self.run(cx, chosen);
                }
                if widgets::ghost(ui, &t, &t!("Action.Cancel")).clicked() {
                    self.plan.clear();
                    self.kept_ok.clear();
                }
            } else if ui.add_enabled(!self.running, egui::Button::new(t!("Action.Close"))).clicked() {
                self.log.clear();
                self.plan.clear();
                self.kept_ok.clear();
            }
        });
    }
}

fn label_id(s: Option<WtState>) -> &'static str {
    match s {
        None => "all",
        Some(WtState::Safe) => "safe",
        Some(WtState::Orphan) => "orphan",
        Some(WtState::Check) => "check",
        Some(WtState::Active) => "active",
    }
}

/// Le journal des commandes (`.cmdlog`) : à chasse fixe, sélectionnable, les commentaires atténués.
fn log_view(ui: &mut Ui, t: &Tokens, lines: &[(String, bool)]) {
    Frame::new().fill(t.code_bg).stroke(Stroke::new(1.0, t.card_border)).corner_radius(theme::R).inner_margin(Margin::same(10)).show(ui, |ui| {
        ui.set_width(ui.available_width());
        ui.spacing_mut().item_spacing.y = 2.0;
        for (s, comment) in lines {
            ui.add(egui::Label::new(RichText::new(s).font(theme::mono(theme::CODE - 1.0)).color(if *comment { t.text_faint } else { t.text })).wrap().selectable(true));
        }
    });
}

#[cfg(test)]
mod tests {
    use super::*;

    fn w(path: &str, state: WtState) -> WorktreeInfo {
        WorktreeInfo {
            repo: "/r/app".into(),
            path: path.into(),
            name: file_name(path).into(),
            branch: Some(file_name(path).into()),
            head: None,
            state,
            why: "<b>merged</b> &amp; gone".into(),
            size_bytes: None,
            last_activity: None,
            locked: false,
            lock_reason: None,
            dirty: 0,
            unpushed: 0,
            merged: false,
            squash_merged: false,
            upstream_gone: false,
            branch_only: false,
            unreadable: None,
            pushed: None,
            recent_min: None,
            kept: None,
        }
    }

    #[test]
    fn folding() {
        let mut d1 = w("/r/app/.claude/worktrees/a", WtState::Safe);
        d1.branch = None;
        let mut d2 = w("/r/app/.claude/worktrees/b", WtState::Safe);
        d2.branch = None;
        let plain = w("/r/app/.claude/worktrees/c", WtState::Safe);
        let mut lone = w("/r/app/refs/heads/x", WtState::Orphan);
        lone.branch_only = true;
        assert_eq!(fold_kind(&d1), 'd');
        assert_eq!(fold_key(&lone), "fold:b:/r/app");
        let parts = split(vec![plain.clone(), d1, lone, d2]);
        let shape: Vec<String> = parts
            .iter()
            .map(|p| match p {
                Part::Row(r) => r.name.clone(),
                Part::Fold(f) => format!("fold{}", f.len()),
            })
            .collect();
        assert_eq!(shape, ["c", "fold2", "x"]);
    }

    #[test]
    fn ordering() {
        let mut risky = w("/r/z", WtState::Check);
        risky.unpushed = 3;
        let mut dirty = w("/r/y", WtState::Check);
        dirty.dirty = 2;
        let plain = w("/r/x", WtState::Check);
        let got: Vec<String> = order(vec![plain.clone(), dirty.clone(), risky.clone()], false, &Sizes::new()).into_iter().map(|w| w.path).collect();
        assert_eq!(got, ["/r/z", "/r/y", "/r/x"]);
        let mut s = Sizes::new();
        s.insert("/r/x".into(), 10);
        let got: Vec<String> = order(vec![risky, dirty, plain], true, &s).into_iter().map(|w| w.path).collect();
        assert_eq!(got, ["/r/x", "/r/y", "/r/z"]);
    }

    #[test]
    fn totals() {
        i18n::set_thread_lang(Some(i18n::Lang::En));
        let a = w("/r/a", WtState::Safe);
        let mut b = w("/r/b", WtState::Orphan);
        b.branch_only = true;
        let mut s = Sizes::new();
        assert_eq!(size_of(&[a.clone(), b.clone()], &s), "…");
        assert_eq!(size_of(&[b.clone()], &s), "—");
        s.insert("/r/a".into(), 2048);
        assert_eq!(size_of(&[a.clone(), b.clone()], &s), fmt::size(2048));
        assert_eq!(items(&[a.clone(), b.clone()]), format!("{} + {}", i18n::plural(1, "Wt.Worktrees"), i18n::plural(1, "Wt.Branches")));
        assert_eq!(items(&[a]), i18n::plural(1, "Wt.Worktrees"));
        i18n::set_thread_lang(None);
    }

    #[test]
    fn logs_and_results() {
        let lines = vec![
            LogLine { repo: "/h/r".into(), cmd: "git worktree remove a".into(), result: Some("ok".into()) },
            LogLine { repo: "/x".into(), cmd: "git branch -d <b>".into(), result: None },
            LogLine { repo: "/h/r".into(), cmd: "git worktree prune -v".into(), result: None },
        ];
        let want: Vec<(String, bool)> = [
            ("# ~/r", true),
            ("git worktree remove a", false),
            ("→ ok", true),
            ("git worktree prune -v", false),
            ("# /x", true),
            ("git branch -d <b>", false),
        ]
        .into_iter()
        .map(|(s, c)| (s.to_string(), c))
        .collect();
        assert_eq!(cmd_log(&lines, "/h"), want);
        assert_eq!(step_result("git branch -d x", 0, "Deleted branch x (was 1a2b3c4).\n"), "Deleted branch x (was 1a2b3c4).");
        assert_eq!(step_result("git worktree remove /a", 0, "done\n"), "ok");
        assert_eq!(step_result("git worktree remove /a", -1, "refused"), "refused");
    }

    #[test]
    fn row_actions() {
        let safe = w("/r/a", WtState::Safe);
        assert_eq!(act_of(&safe, false, false, None), Act::Add(false));
        assert_eq!(act_of(&safe, true, false, None), Act::Unplan);
        let active = w("/r/b", WtState::Active);
        assert_eq!(act_of(&active, false, false, Some("s".into())), Act::Go("s".into()));
        assert_eq!(act_of(&active, false, false, None), Act::CopyActive);
        let mut ahead = w("/r/c", WtState::Check);
        ahead.unpushed = 2;
        assert_eq!(act_of(&ahead, false, false, None), Act::Push);
        let mut dirty = w("/r/d", WtState::Check);
        dirty.dirty = 1;
        assert_eq!(act_of(&dirty, false, false, None), Act::Plain(true));
    }
}
