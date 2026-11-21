// L'état de KorinStudio et sa logique : projet, documents, tâches, recherche, Git, commandes, et les outils que le
// serveur MCP donne aux agents (le copilote agit dans l'IDE). L'affichage est dans ui.rs.

use crate::agent::AgentKind;
use crate::copilot::{Copilot, Launch};
use crate::editor::Doc;
use crate::highlight::Lang;
use crate::mcp::{self, McpServer, ToolCall, ToolResult};
use crate::tasks::{Diagnostic, Severity, Task, TaskKind};
use crate::theme::Palette;
use crate::workspace::{self, Cmd, ProjectKind, Settings, ThemeChoice, Tree};
use serde_json::{Value, json};
use std::path::{Path, PathBuf};
use std::sync::mpsc::{Receiver, Sender};
use std::time::Instant;

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum SideView {
    Explorer,
    Search,
    Git,
    Run,
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum PanelTab {
    Problems,
    Output,
    Terminal,
    AgentLog,
}

/// Les commandes de l'IDE (menus, palette, raccourcis).
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Command {
    NewFile,
    NewProject,
    OpenFolder,
    OpenFile,
    Save,
    SaveAs,
    SaveAll,
    CloseTab,
    CloseFolder,
    Quit,
    Undo,
    Redo,
    Find,
    Replace,
    FindInFiles,
    ToggleComment,
    DeleteLine,
    DuplicateLine,
    QuickOpen,
    CommandPalette,
    GotoLine,
    NextProblem,
    NextTab,
    PrevTab,
    ToggleSidebar,
    TogglePanel,
    ToggleCopilot,
    ShowExplorer,
    ShowSearch,
    ShowGit,
    ShowRun,
    ShowTerminal,
    ShowProblems,
    ShowOutput,
    ZoomIn,
    ZoomOut,
    ToggleWrap,
    ThemeSystem,
    ThemeDark,
    ThemeLight,
    Build,
    Run,
    Stop,
    Clean,
    NewTerminal,
    AskCopilot,
    ExplainSelection,
    FixProblems,
    NewChat,
    About,
}

pub struct CmdInfo {
    pub cmd: Command,
    pub label: &'static str,
    pub keys: &'static str,
    pub menu: &'static str,
}

macro_rules! c {
    ($c:ident, $l:expr, $k:expr, $m:expr) => {
        CmdInfo { cmd: Command::$c, label: $l, keys: $k, menu: $m }
    };
}

pub const COMMANDS: &[CmdInfo] = &[
    c!(NewFile, "Nouveau fichier", "Ctrl+N", "Fichier"),
    c!(NewProject, "Nouveau projet…", "Ctrl+Maj+N", "Fichier"),
    c!(OpenFolder, "Ouvrir un dossier…", "Ctrl+O", "Fichier"),
    c!(OpenFile, "Ouvrir un fichier…", "", "Fichier"),
    c!(Save, "Enregistrer", "Ctrl+S", "Fichier"),
    c!(SaveAs, "Enregistrer sous…", "Ctrl+Maj+S", "Fichier"),
    c!(SaveAll, "Tout enregistrer", "", "Fichier"),
    c!(CloseTab, "Fermer l'éditeur", "Ctrl+W", "Fichier"),
    c!(CloseFolder, "Fermer le dossier", "", "Fichier"),
    c!(Quit, "Quitter", "Ctrl+Q", "Fichier"),
    c!(Undo, "Annuler", "Ctrl+Z", "Édition"),
    c!(Redo, "Rétablir", "Ctrl+Y", "Édition"),
    c!(Find, "Rechercher", "Ctrl+F", "Édition"),
    c!(Replace, "Remplacer", "Ctrl+H", "Édition"),
    c!(FindInFiles, "Rechercher dans les fichiers", "Ctrl+Maj+F", "Édition"),
    c!(ToggleComment, "Commenter / décommenter", "Ctrl+/", "Édition"),
    c!(DeleteLine, "Supprimer la ligne", "Ctrl+Maj+K", "Édition"),
    c!(DuplicateLine, "Dupliquer la ligne", "Maj+Alt+Bas", "Édition"),
    c!(QuickOpen, "Aller au fichier…", "Ctrl+P", "Aller"),
    c!(CommandPalette, "Palette de commandes…", "Ctrl+Maj+P", "Aller"),
    c!(GotoLine, "Aller à la ligne…", "Ctrl+G", "Aller"),
    c!(NextProblem, "Problème suivant", "F8", "Aller"),
    c!(NextTab, "Éditeur suivant", "Ctrl+Tab", "Aller"),
    c!(PrevTab, "Éditeur précédent", "Ctrl+Maj+Tab", "Aller"),
    c!(ToggleSidebar, "Barre latérale", "Ctrl+B", "Affichage"),
    c!(TogglePanel, "Panneau du bas", "Ctrl+J", "Affichage"),
    c!(ToggleCopilot, "Copilote", "Ctrl+Alt+I", "Affichage"),
    c!(ShowExplorer, "Explorateur", "Ctrl+Maj+E", "Affichage"),
    c!(ShowSearch, "Recherche", "", "Affichage"),
    c!(ShowGit, "Gestion de versions", "Ctrl+Maj+G", "Affichage"),
    c!(ShowRun, "Exécuter et construire", "Ctrl+Maj+D", "Affichage"),
    c!(ShowProblems, "Problèmes", "Ctrl+Maj+M", "Affichage"),
    c!(ShowOutput, "Sortie", "Ctrl+Maj+U", "Affichage"),
    c!(ZoomIn, "Agrandir le code", "Ctrl+=", "Affichage"),
    c!(ZoomOut, "Réduire le code", "Ctrl+-", "Affichage"),
    c!(ToggleWrap, "Retour à la ligne", "Alt+Z", "Affichage"),
    c!(ThemeSystem, "Thème : celui du système", "", "Affichage"),
    c!(ThemeDark, "Thème : sombre", "", "Affichage"),
    c!(ThemeLight, "Thème : clair", "", "Affichage"),
    c!(Build, "Construire", "Ctrl+Maj+B", "Exécuter"),
    c!(Run, "Exécuter", "F5", "Exécuter"),
    c!(Stop, "Arrêter", "Maj+F5", "Exécuter"),
    c!(Clean, "Nettoyer", "", "Exécuter"),
    c!(NewTerminal, "Terminal", "Ctrl+`", "Terminal"),
    c!(AskCopilot, "Demander au copilote", "Ctrl+I", "Copilote"),
    c!(ExplainSelection, "Expliquer la sélection", "", "Copilote"),
    c!(FixProblems, "Corriger les problèmes", "", "Copilote"),
    c!(NewChat, "Nouvelle conversation", "", "Copilote"),
    c!(About, "À propos de KorinStudio", "", "Aide"),
];

pub fn info(c: Command) -> &'static CmdInfo {
    COMMANDS.iter().find(|i| i.cmd == c).unwrap()
}

pub enum Dialog {
    NewProject { template: usize, name: String, location: String, error: Option<String> },
    Browse { purpose: BrowsePurpose, dir: PathBuf, name: String, error: Option<String> },
    Confirm { title: String, text: String, action: ConfirmAction },
    NewEntry { dir: PathBuf, folder: bool, name: String, error: Option<String> },
    Rename { path: PathBuf, name: String, error: Option<String> },
    About,
}

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum BrowsePurpose {
    OpenFolder,
    OpenFile,
    SaveAs(u64),
    Location,
}

pub enum ConfirmAction {
    CloseDoc(u64),
    Quit,
    Delete(PathBuf),
    SwitchProject(PathBuf),
}

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum PaletteMode {
    Files,
    Commands,
    Line,
}

pub struct PaletteState {
    pub mode: PaletteMode,
    pub query: String,
    pub selected: usize,
    pub files: Vec<PathBuf>,
    pub focus: bool,
}

#[derive(Default)]
pub struct SearchState {
    pub query: String,
    pub replace: String,
    pub case: bool,
    pub regex: bool,
    pub whole: bool,
    pub results: Vec<(PathBuf, Vec<(usize, usize, String)>)>,
    pub rx: Option<Receiver<Vec<(PathBuf, Vec<(usize, usize, String)>)>>>,
    pub error: Option<String>,
    pub focus: bool,
    pub collapsed: std::collections::HashSet<PathBuf>,
}

#[derive(Default)]
pub struct GitState {
    pub branch: Option<String>,
    pub changes: Vec<(String, String)>,
    pub is_repo: bool,
    pub message: String,
    pub rx: Option<Receiver<(bool, Option<String>, Vec<(String, String)>)>>,
    pub last: Option<Instant>,
}

/// Une réponse d'outil MCP qui attend la fin d'une tâche.
pub struct PendingCall {
    pub reply: Sender<ToolResult>,
    pub task: u64,
    pub then_run: Option<Vec<String>>, // construire, puis lancer (args)
    pub deadline: Option<Instant>,
    pub what: &'static str,
}

pub struct Toast {
    pub text: String,
    pub at: f64,
    pub error: bool,
}

pub struct Studio {
    pub pal: Palette,
    pub applied: Option<(bool, f32)>,
    pub settings: Settings,
    pub root: Option<PathBuf>,
    pub kind: ProjectKind,
    pub tree: Tree,
    pub docs: Vec<Doc>,
    pub active: Option<usize>,
    pub next_id: u64,
    pub untitled_n: usize,
    pub side: SideView,
    pub sidebar_open: bool,
    pub panel_open: bool,
    pub panel_tab: PanelTab,
    pub tasks: Vec<Task>,
    pub output_task: Option<u64>,
    pub build_task: Option<u64>,
    pub run_task: Option<u64>,
    pub next_task: u64,
    pub diagnostics: Vec<Diagnostic>,
    pub term_input: String,
    pub term_history: Vec<String>,
    pub term_hist_pos: Option<usize>,
    pub run_args: String,
    pub search: SearchState,
    pub git: GitState,
    pub palette: Option<PaletteState>,
    pub dialog: Option<Dialog>,
    pub copilot: Copilot,
    pub mcp: Option<McpServer>,
    pub mcp_rx: Option<Receiver<ToolCall>>,
    pub mcp_error: Option<String>,
    pub pending: Vec<PendingCall>,
    pub toasts: Vec<Toast>,
    pub last_disk_check: f64,
    pub studio_exe: PathBuf,
    pub allow_close: bool,
    pub now: f64,
    pub menu_sent: String,
    pub focus_editor: bool,
    pub agent_activity: Option<(String, f64)>,
}

impl Studio {
    pub fn new(ctx: &egui::Context, open: Option<PathBuf>) -> Studio {
        let settings = Settings::load();
        let (mcp, mcp_rx, mcp_error) = match mcp::start(ctx.clone()) {
            Ok((s, rx)) => (Some(s), Some(rx), None),
            Err(e) => (None, None, Some(format!("serveur MCP de l'IDE : {e}"))),
        };
        let mut s = Studio {
            pal: Palette::dark(),
            applied: None,
            copilot: Copilot::new(settings.agent),
            sidebar_open: true,
            panel_open: false,
            settings,
            root: None,
            kind: ProjectKind::Folder,
            tree: Tree::default(),
            docs: Vec::new(),
            active: None,
            next_id: 1,
            untitled_n: 0,
            side: SideView::Explorer,
            panel_tab: PanelTab::Terminal,
            tasks: Vec::new(),
            output_task: None,
            build_task: None,
            run_task: None,
            next_task: 1,
            diagnostics: Vec::new(),
            term_input: String::new(),
            term_history: Vec::new(),
            term_hist_pos: None,
            run_args: String::new(),
            search: SearchState::default(),
            git: GitState::default(),
            palette: None,
            dialog: None,
            mcp,
            mcp_rx,
            mcp_error,
            pending: Vec::new(),
            toasts: Vec::new(),
            last_disk_check: 0.0,
            studio_exe: studio_exe(),
            allow_close: false,
            now: 0.0,
            menu_sent: String::new(),
            focus_editor: false,
            agent_activity: None,
        };
        // l'adresse et le jeton du serveur MCP, pour un agent lancé ailleurs (Claude Code au terminal :
        // claude mcp add --transport http korinstudio <url> --header "Authorization: Bearer <jeton>")
        if let Some(m) = &s.mcp {
            let d = workspace::config_dir();
            let _ = std::fs::create_dir_all(&d);
            let p = d.join("mcp.json");
            let _ = std::fs::write(&p, json!({"url": m.url(), "token": m.token, "pid": std::process::id()}).to_string());
            #[cfg(unix)]
            {
                use std::os::unix::fs::PermissionsExt;
                let _ = std::fs::set_permissions(&p, std::fs::Permissions::from_mode(0o600));
            }
        }
        if let Some(m) = s.settings.agent_modes.get(&format!("{:?}", s.settings.agent)).cloned() {
            s.copilot.wanted_mode = Some(m);
        }
        let target = open.or_else(|| s.settings.last_project.clone().filter(|p| p.is_dir()));
        if let Some(p) = target {
            if p.is_dir() {
                s.open_project(&p);
            } else if p.is_file() {
                s.open_project(&workspace::find_project_root(&p));
                s.open_path(&p, None);
            }
        }
        if let Some(e) = s.mcp_error.clone() {
            s.toast(e, true);
        }
        s
    }

    pub fn toast(&mut self, text: impl Into<String>, error: bool) {
        self.toasts.push(Toast { text: text.into(), at: self.now, error });
    }

    // -----------------------------------------------------------------------------------------------------------
    // Le thème

    pub fn sync_theme(&mut self, ctx: &egui::Context) {
        let dark = match self.settings.theme {
            ThemeChoice::Dark => true,
            ThemeChoice::Light => false,
            ThemeChoice::System => ctx.input(|i| i.raw.system_theme).map(|t| t == egui::Theme::Dark).unwrap_or(true),
        };
        let key = (dark, self.settings.code_size);
        if self.applied != Some(key) {
            self.pal = if dark { Palette::dark() } else { Palette::light() };
            crate::theme::apply(ctx, &self.pal, self.settings.code_size);
            self.applied = Some(key);
        }
    }

    // -----------------------------------------------------------------------------------------------------------
    // Le projet et les documents

    pub fn open_project(&mut self, p: &Path) {
        let p = p.canonicalize().unwrap_or_else(|_| p.to_path_buf());
        if self.root.as_deref() == Some(&p) {
            return;
        }
        self.root = Some(p.clone());
        self.kind = ProjectKind::detect(&p);
        self.tree = Tree::default();
        self.tree.expanded.insert(p.clone());
        self.diagnostics.clear();
        self.search.results.clear();
        self.git = GitState::default();
        self.settings.add_recent(&p);
        self.settings.save();
        self.refresh_git();
    }

    pub fn close_project(&mut self) {
        self.root = None;
        self.kind = ProjectKind::Folder;
        self.diagnostics.clear();
        self.git = GitState::default();
        self.settings.last_project = None;
        self.settings.save();
    }

    pub fn resolve(&self, p: &str) -> PathBuf {
        let pb = PathBuf::from(p.trim());
        if pb.is_absolute() {
            pb
        } else {
            self.root.clone().unwrap_or_else(workspace::projects_dir).join(pb)
        }
    }

    pub fn doc_index(&self, p: &Path) -> Option<usize> {
        let canon = p.canonicalize().ok();
        self.docs.iter().position(|d| d.path.as_deref().is_some_and(|dp| dp == p || canon.as_deref() == Some(dp) || dp.canonicalize().ok() == canon))
    }

    pub fn active_doc(&mut self) -> Option<&mut Doc> {
        let i = self.active?;
        self.docs.get_mut(i)
    }

    /// Ouvre (ou montre) un fichier, à la ligne voulue.
    pub fn open_path(&mut self, p: &Path, at: Option<(usize, usize)>) -> Result<usize, String> {
        let i = match self.doc_index(p) {
            Some(i) => i,
            None => {
                let d = Doc::open(self.next_id, &p.canonicalize().unwrap_or_else(|_| p.to_path_buf()))?;
                self.next_id += 1;
                // un onglet d'aperçu non modifié est remplacé (comme VS Code)
                if let Some(a) = self.active {
                    if a < self.docs.len() && !self.docs[a].dirty && !self.docs[a].pinned && self.docs[a].path.is_some() && self.docs[a].version == 1 && at.is_none() && false {
                        self.docs[a] = d;
                        a
                    } else {
                        self.docs.push(d);
                        self.docs.len() - 1
                    }
                } else {
                    self.docs.push(d);
                    self.docs.len() - 1
                }
            }
        };
        self.active = Some(i);
        if let Some((l, c)) = at {
            let now = self.now;
            self.docs[i].goto(l, c, now);
        }
        if let (Some(root), Some(path)) = (self.root.clone(), self.docs[i].path.clone()) {
            self.tree.reveal(&root, &path);
        }
        self.focus_editor = true;
        Ok(i)
    }

    pub fn new_file(&mut self) {
        self.untitled_n += 1;
        let d = Doc::new_untitled(self.next_id, self.untitled_n);
        self.next_id += 1;
        self.docs.push(d);
        self.active = Some(self.docs.len() - 1);
        self.focus_editor = true;
    }

    pub fn close_doc(&mut self, id: u64, force: bool) {
        let Some(i) = self.docs.iter().position(|d| d.id == id) else { return };
        if self.docs[i].dirty && !force {
            self.dialog = Some(Dialog::Confirm {
                title: "Modifications non enregistrées".into(),
                text: format!("« {} » a été modifié. Fermer sans enregistrer ?", self.docs[i].title()),
                action: ConfirmAction::CloseDoc(id),
            });
            return;
        }
        self.docs.remove(i);
        self.active = match self.active {
            Some(a) if a > i => Some(a - 1),
            Some(a) if a == i => {
                if self.docs.is_empty() {
                    None
                } else {
                    Some(i.min(self.docs.len() - 1))
                }
            }
            a => a,
        };
    }

    pub fn save_doc(&mut self, i: usize) {
        if i >= self.docs.len() {
            return;
        }
        if self.docs[i].path.is_none() {
            let id = self.docs[i].id;
            let dir = self.root.clone().unwrap_or_else(workspace::projects_dir);
            self.dialog = Some(Dialog::Browse { purpose: BrowsePurpose::SaveAs(id), dir, name: String::new(), error: None });
            return;
        }
        if let Err(e) = self.docs[i].save() {
            self.toast(format!("Enregistrement impossible : {e}"), true);
        }
    }

    pub fn save_all(&mut self) {
        for i in 0..self.docs.len() {
            if self.docs[i].dirty && self.docs[i].path.is_some() {
                self.save_doc(i);
            }
        }
    }

    pub fn dirty_count(&self) -> usize {
        self.docs.iter().filter(|d| d.dirty).count()
    }

    // -----------------------------------------------------------------------------------------------------------
    // Les tâches

    pub fn start_task(&mut self, kind: TaskKind, cmd: Cmd, ctx: &egui::Context) -> u64 {
        let id = self.next_task;
        self.next_task += 1;
        let title = cmd.label.clone();
        let t = Task::start(id, kind, title, cmd, ctx);
        self.tasks.push(t);
        // garder les 30 dernières
        while self.tasks.len() > 30 {
            if let Some(pos) = self.tasks.iter().position(|t| !t.running()) {
                self.tasks.remove(pos);
            } else {
                break;
            }
        }
        if kind != TaskKind::Shell {
            self.output_task = Some(id);
        }
        id
    }

    pub fn task(&self, id: u64) -> Option<&Task> {
        self.tasks.iter().find(|t| t.id == id)
    }

    pub fn build(&mut self, ctx: &egui::Context) -> Result<Option<u64>, String> {
        let Some(root) = self.root.clone() else { return Err("aucun projet ouvert".into()) };
        if self.build_task.and_then(|i| self.task(i)).is_some_and(|t| t.running()) {
            return Err("une construction est déjà en cours".into());
        }
        self.save_all();
        self.kind = ProjectKind::detect(&root);
        match workspace::build_cmd(self.kind, &root)? {
            Some(c) => {
                let id = self.start_task(TaskKind::Build, c, ctx);
                self.build_task = Some(id);
                self.panel_open = true;
                self.panel_tab = PanelTab::Output;
                Ok(Some(id))
            }
            None => Ok(None),
        }
    }

    pub fn run(&mut self, ctx: &egui::Context, args: Vec<String>) -> Result<u64, String> {
        let Some(root) = self.root.clone() else { return Err("aucun projet ouvert".into()) };
        self.save_all();
        self.kind = ProjectKind::detect(&root);
        if let Some(old) = self.run_task {
            if let Some(t) = self.tasks.iter_mut().find(|t| t.id == old) {
                t.kill();
            }
        }
        let c = workspace::run_cmd(self.kind, &root, &args)?;
        let id = self.start_task(TaskKind::Run, c, ctx);
        self.run_task = Some(id);
        self.panel_open = true;
        self.panel_tab = PanelTab::Output;
        Ok(id)
    }

    /// Construire puis lancer quand la construction est séparée (app Korin, C#, FreeBASIC) ; sinon lancer.
    pub fn build_and_run(&mut self, ctx: &egui::Context, args: Vec<String>, reply: Option<(Sender<ToolResult>, Option<Instant>)>) {
        let separate = matches!(self.kind, ProjectKind::KorinApp | ProjectKind::CSharp | ProjectKind::FreeBasic | ProjectKind::Python);
        if separate {
            match self.build(ctx) {
                Ok(Some(id)) => {
                    let (reply, deadline) = match reply {
                        Some((r, d)) => (Some(r), d),
                        None => (None, None),
                    };
                    let (tx, _rx) = std::sync::mpsc::channel();
                    self.pending.push(PendingCall { reply: reply.unwrap_or(tx), task: id, then_run: Some(args), deadline, what: "run" });
                    return;
                }
                Ok(None) => {}
                Err(e) => {
                    if let Some((r, _)) = reply {
                        let _ = r.send(ToolResult::error(e.clone()));
                    }
                    self.toast(e, true);
                    return;
                }
            }
        }
        match self.run(ctx, args) {
            Ok(id) => {
                if let Some((r, d)) = reply {
                    self.pending.push(PendingCall { reply: r, task: id, then_run: None, deadline: d, what: "run" });
                }
            }
            Err(e) => {
                if let Some((r, _)) = reply {
                    let _ = r.send(ToolResult::error(e.clone()));
                }
                self.toast(e, true);
            }
        }
    }

    pub fn shell(&mut self, line: &str, cwd: Option<PathBuf>, ctx: &egui::Context) -> u64 {
        let cwd = cwd.or_else(|| self.root.clone()).unwrap_or_else(workspace::home_dir);
        let id = self.start_task(TaskKind::Shell, Cmd::shell(line, &cwd, line), ctx);
        self.panel_open = true;
        self.panel_tab = PanelTab::Terminal;
        id
    }

    /// Ce qui arrive aux tâches : diagnostics à la fin d'une construction, réponses MCP en attente.
    pub fn poll_tasks(&mut self, ctx: &egui::Context) {
        // diagnostics
        let mut new_diags = None;
        for t in &mut self.tasks {
            if !t.diagnostics_taken && !t.running() && t.kind == TaskKind::Build {
                t.diagnostics_taken = true;
                new_diags = Some((crate::tasks::parse_diagnostics(&t.text(), &t.cmd.cwd), t.success()));
            }
        }
        if let Some((d, ok)) = new_diags {
            let errors = d.iter().filter(|x| x.severity == Severity::Error).count();
            self.diagnostics = d;
            if ok {
                self.toast(if self.diagnostics.is_empty() { "Construction réussie".to_string() } else { format!("Construction réussie ({} avertissement(s))", self.diagnostics.len()) }, false);
            } else {
                self.toast(format!("La construction a échoué : {errors} erreur(s)"), true);
                if errors > 0 {
                    self.panel_tab = PanelTab::Problems;
                }
            }
        }
        // réponses en attente
        let mut i = 0;
        while i < self.pending.len() {
            let p = &self.pending[i];
            let task = self.tasks.iter().find(|t| t.id == p.task);
            let done = task.map(|t| !t.running()).unwrap_or(true);
            let late = p.deadline.is_some_and(|d| Instant::now() >= d);
            if !done && !late {
                i += 1;
                continue;
            }
            let p = self.pending.remove(i);
            let (ok, tail, code) = match task {
                Some(t) => (t.success(), t.tail(120), t.exit()),
                None => (false, String::new(), None),
            };
            if let Some(args) = p.then_run {
                if ok {
                    let deadline = p.deadline;
                    self.build_and_run_after_build(ctx, args, p.reply, deadline);
                } else {
                    let _ = p.reply.send(ToolResult { content: vec![mcp::Content::Text(format!("La construction a échoué.\n\n{tail}\n{}", self.diag_text(None)))], is_error: true });
                }
                continue;
            }
            let status = match code {
                Some(Some(c)) => format!("terminé, code {c}"),
                Some(None) => "terminé (arrêté)".into(),
                None => "toujours en cours (il continue dans l'IDE)".into(),
            };
            let text = match p.what {
                "build" => format!("Construction {} ({status}).\n\nSortie (fin) :\n{tail}\n{}", if ok { "réussie" } else { "échouée" }, self.diag_text(None)),
                _ => format!("Programme {status}.\n\nSortie :\n{tail}"),
            };
            let is_error = p.what == "build" && !ok;
            let _ = p.reply.send(ToolResult { content: vec![mcp::Content::Text(text)], is_error });
        }
    }

    fn build_and_run_after_build(&mut self, ctx: &egui::Context, args: Vec<String>, reply: Sender<ToolResult>, deadline: Option<Instant>) {
        match self.run(ctx, args) {
            Ok(id) => self.pending.push(PendingCall { reply, task: id, then_run: None, deadline: deadline.or(Some(Instant::now() + std::time::Duration::from_secs(30))), what: "run" }),
            Err(e) => {
                let _ = reply.send(ToolResult::error(e));
            }
        }
    }

    pub fn diag_text(&self, path: Option<&Path>) -> String {
        let ds: Vec<&Diagnostic> = self.diagnostics.iter().filter(|d| path.is_none_or(|p| d.path == p)).collect();
        if ds.is_empty() {
            return "Aucun diagnostic.".into();
        }
        let mut s = format!("{} diagnostic(s) :\n", ds.len());
        for d in ds {
            s.push_str(&format!(
                "{}:{}:{}: {}: {}\n",
                workspace::rel(self.root.as_deref(), &d.path),
                d.line,
                d.column,
                match d.severity {
                    Severity::Error => "error",
                    Severity::Warning => "warning",
                    Severity::Info => "note",
                },
                d.message
            ));
        }
        s
    }

    pub fn next_problem(&mut self) {
        if self.diagnostics.is_empty() {
            return;
        }
        let cur = self.active.and_then(|i| self.docs.get(i)).and_then(|d| d.path.clone().map(|p| (p, d.cursor.0)));
        let idx = match &cur {
            Some((p, l)) => self.diagnostics.iter().position(|d| &d.path == p && d.line > *l).unwrap_or(0),
            None => 0,
        };
        let d = self.diagnostics[idx].clone();
        if let Err(e) = self.open_path(&d.path, Some((d.line, d.column))) {
            self.toast(e, true);
        }
    }

    // -----------------------------------------------------------------------------------------------------------
    // La recherche dans les fichiers, Git

    pub fn start_search(&mut self, ctx: &egui::Context) {
        let Some(root) = self.root.clone() else { return };
        let q = self.search.query.clone();
        self.search.results.clear();
        self.search.error = None;
        if q.is_empty() {
            return;
        }
        let re = match build_regex(&q, self.search.regex, self.search.case, self.search.whole) {
            Ok(r) => r,
            Err(e) => {
                self.search.error = Some(e);
                return;
            }
        };
        let (tx, rx) = std::sync::mpsc::channel();
        self.search.rx = Some(rx);
        let ctx = ctx.clone();
        std::thread::spawn(move || {
            let res = search_files(&root, &re, 2000);
            let _ = tx.send(res);
            ctx.request_repaint();
        });
    }

    pub fn poll_search(&mut self) {
        if let Some(rx) = &self.search.rx {
            if let Ok(r) = rx.try_recv() {
                self.search.results = r;
                self.search.rx = None;
            }
        }
    }

    pub fn refresh_git(&mut self) {
        let Some(root) = self.root.clone() else { return };
        if self.git.rx.is_some() {
            return;
        }
        self.git.last = Some(Instant::now());
        let (tx, rx) = std::sync::mpsc::channel();
        self.git.rx = Some(rx);
        std::thread::spawn(move || {
            let (prog, pre) = crate::tasks::resolve_program("git");
            let out = std::process::Command::new(prog).args(pre).args(["status", "--porcelain=v1", "-b"]).current_dir(&root).stdin(std::process::Stdio::null()).output();
            let mut branch = None;
            let mut changes = Vec::new();
            let mut ok = false;
            if let Ok(o) = out {
                if o.status.success() {
                    ok = true;
                    for l in String::from_utf8_lossy(&o.stdout).lines() {
                        if let Some(b) = l.strip_prefix("## ") {
                            branch = Some(b.split("...").next().unwrap_or(b).replace("No commits yet on ", "").to_string());
                        } else if l.len() > 3 {
                            changes.push((l[..2].trim().to_string(), l[3..].trim_matches('"').to_string()));
                        }
                    }
                }
            }
            let _ = tx.send((ok, branch, changes));
        });
    }

    pub fn poll_git(&mut self) {
        if let Some(rx) = &self.git.rx {
            if let Ok((ok, b, c)) = rx.try_recv() {
                self.git.is_repo = ok;
                self.git.branch = b;
                self.git.changes = c;
                self.git.rx = None;
            }
        }
        if self.root.is_some() && self.git.last.is_none_or(|t| t.elapsed().as_secs() >= 5) {
            self.refresh_git();
        }
    }

    // -----------------------------------------------------------------------------------------------------------
    // Le copilote

    pub fn launch(&self) -> Launch {
        let (url, token) = match &self.mcp {
            Some(m) => (m.url(), m.token.clone()),
            None => (String::new(), String::new()),
        };
        let cwd = self.root.clone().unwrap_or_else(workspace::projects_dir);
        let _ = std::fs::create_dir_all(&cwd); // (le dossier des projets, à la première fois)
        Launch { cwd, mcp_url: url, mcp_token: token, studio_exe: self.studio_exe.clone(), hint: self.agent_hint() }
    }

    fn agent_hint(&self) -> String {
        let os = if workspace::KORIN {
            "Korin OS (PC x86-64): not Linux. Toolchains are in /devtools (gcc 14.2, fbc, cargo/rustc, dotnet/csbuild). A folder with app.txt is a Korin GUI app built by `pibuild app <dir> <name>` (installed to /apps/<name>.app); C apps use UIKit (/devtools/gcc/include/kits). The user's home is /home (alias HOME:/). No symlinks, no /proc. Load the korin-* skills when relevant."
        } else if cfg!(windows) {
            "Windows"
        } else if cfg!(target_os = "macos") {
            "macOS"
        } else {
            "Linux"
        };
        format!(
            "You are the copilot inside KorinStudio, the IDE (code editor) the user is looking at right now. OS: {os}.\n\
             You are connected to the IDE through the MCP server named \"korinstudio\". Prefer its tools so the user sees what you do: \
             create_project to start a new project (it opens in the IDE; then work in that folder with absolute paths), open_project, open_file to show a file, \
             write_file / replace_in_file to edit (the editor updates live; unsaved editor content is what read_file returns), \
             build and run to compile and run with the project's build system (the output appears in the IDE), get_diagnostics for compiler errors, \
             run_command for other shell commands (visible in the IDE terminal), get_workspace to know the open project, active file and selection.\n\
             Open project: {}. Projects folder (default location for new projects): {}.\n\
             Answer in the user's language (French by default), concisely.",
            self.root.as_ref().map(|r| format!("{} ({})", r.display(), self.kind.label())).unwrap_or_else(|| "none".into()),
            workspace::projects_dir().display()
        )
    }

    /// Le contexte joint à un message : le fichier actif, la position, la sélection.
    pub fn context_text(&self) -> Option<String> {
        let d = self.active.and_then(|i| self.docs.get(i))?;
        let p = d.path.as_ref().map(|p| p.display().to_string()).unwrap_or_else(|| d.title());
        let mut s = format!("[Contexte de l'IDE] Fichier actif : {p} (ligne {}, colonne {})", d.cursor.0, d.cursor.1);
        if !d.selection_text.is_empty() {
            let sel: String = d.selection_text.chars().take(8000).collect();
            s.push_str(&format!("\nSélection :\n```\n{sel}\n```"));
        }
        Some(s)
    }

    pub fn context_label(&self) -> Option<String> {
        let d = self.active.and_then(|i| self.docs.get(i))?;
        let mut s = d.title();
        if let Some((a, b)) = d.sel {
            let _ = (a, b);
            s.push_str(" · sélection");
        } else {
            s.push_str(&format!(":{}", d.cursor.0));
        }
        Some(s)
    }

    pub fn ask(&mut self, text: String, ctx: &egui::Context) {
        self.settings.copilot_open = true;
        let l = self.launch();
        let c = self.context_text();
        self.copilot.send(text, c, &l, ctx);
    }

    pub fn set_agent(&mut self, k: AgentKind) {
        self.copilot.set_kind(k);
        self.settings.agent = k;
        self.copilot.wanted_mode = self.settings.agent_modes.get(&format!("{k:?}")).cloned();
        self.settings.save();
    }

    // -----------------------------------------------------------------------------------------------------------
    // Les outils MCP : le copilote agit dans l'IDE

    pub fn poll_mcp(&mut self, ctx: &egui::Context) {
        let mut calls = Vec::new();
        if let Some(rx) = &self.mcp_rx {
            while let Ok(c) = rx.try_recv() {
                calls.push(c);
            }
        }
        for c in calls {
            let name = c.name.clone();
            if name != "approve" {
                self.agent_activity = Some((name.clone(), self.now));
            }
            self.handle_tool(c, ctx);
        }
    }

    fn handle_tool(&mut self, c: ToolCall, ctx: &egui::Context) {
        let a = &c.args;
        let s = |k: &str| a.get(k).and_then(|v| v.as_str()).map(|s| s.to_string());
        let n = |k: &str| a.get(k).and_then(|v| v.as_u64()).map(|v| v as usize);
        let b = |k: &str| a.get(k).and_then(|v| v.as_bool()).unwrap_or(false);
        let reply = c.reply.clone();
        let res: Result<String, String> = match c.name.as_str() {
            "get_workspace" => Ok(self.workspace_json().to_string()),
            "list_project_templates" => Ok(json!(workspace::TEMPLATES
                .iter()
                .map(|t| json!({"id": t.id, "label": t.label, "description": t.description, "available": !t.korin_only || workspace::KORIN}))
                .collect::<Vec<_>>())
            .to_string()),
            "create_project" => {
                let tpl = s("template").unwrap_or_default();
                let name = s("name").unwrap_or_default();
                let loc = s("location").map(|l| self.resolve(&l)).unwrap_or_else(workspace::projects_dir);
                let _ = std::fs::create_dir_all(&loc);
                match workspace::create_project(&tpl, &name, &loc) {
                    Ok(root) => {
                        self.open_project(&root);
                        if let Some(m) = workspace::main_file(&root) {
                            let _ = self.open_path(&m, None);
                        }
                        self.toast(format!("Projet créé par le copilote : {}", root.display()), false);
                        let files: Vec<String> = workspace::walk_files(&root, 50).iter().map(|f| workspace::rel(Some(&root), f)).collect();
                        Ok(format!("Projet créé et ouvert dans l'IDE : {}\nGenre : {}\nFichiers : {}", root.display(), self.kind.label(), files.join(", ")))
                    }
                    Err(e) => Err(e),
                }
            }
            "open_project" => {
                let p = self.resolve(&s("path").unwrap_or_default());
                if p.is_dir() {
                    self.open_project(&p);
                    Ok(format!("Projet ouvert : {} ({})", p.display(), self.kind.label()))
                } else {
                    Err(format!("pas un dossier : {}", p.display()))
                }
            }
            "open_file" => {
                let p = self.resolve(&s("path").unwrap_or_default());
                let at = n("line").map(|l| (l, n("column").unwrap_or(1)));
                self.open_path(&p, at).map(|_| format!("Ouvert : {}", p.display()))
            }
            "read_file" => {
                let p = self.resolve(&s("path").unwrap_or_default());
                let text = match self.doc_index(&p) {
                    Some(i) => Ok(self.docs[i].text.clone()),
                    None => std::fs::read(&p).map(|b| String::from_utf8_lossy(&b).to_string()).map_err(|e| format!("{} : {e}", p.display())),
                };
                text.map(|t| {
                    let start = n("start_line").unwrap_or(1).max(1);
                    let end = n("end_line").unwrap_or(usize::MAX);
                    let mut out = String::new();
                    for (i, l) in t.lines().enumerate() {
                        let ln = i + 1;
                        if ln >= start && ln <= end {
                            out.push_str(&format!("{ln}\t{l}\n"));
                        }
                        if out.len() > 400_000 {
                            out.push_str("[… tronqué]\n");
                            break;
                        }
                    }
                    if out.is_empty() { "(fichier vide)".to_string() } else { out }
                })
            }
            "write_file" => {
                let p = self.resolve(&s("path").unwrap_or_default());
                let content = s("content").unwrap_or_default();
                self.agent_write(&p, content)
            }
            "replace_in_file" => {
                let p = self.resolve(&s("path").unwrap_or_default());
                let old = s("old_text").unwrap_or_default();
                let new = s("new_text").unwrap_or_default();
                let all = b("replace_all");
                let text = match self.doc_index(&p) {
                    Some(i) => Ok(self.docs[i].text.clone()),
                    None => std::fs::read(&p).map(|b| String::from_utf8_lossy(&b).to_string()).map_err(|e| format!("{} : {e}", p.display())),
                };
                match text {
                    Err(e) => Err(e),
                    Ok(t) => {
                        let count = if old.is_empty() { 0 } else { t.matches(&old).count() };
                        if count == 0 {
                            Err("old_text introuvable dans le fichier (le texte doit correspondre exactement, espaces compris)".into())
                        } else if count > 1 && !all {
                            Err(format!("old_text apparaît {count} fois : donnez plus de contexte, ou replace_all"))
                        } else {
                            let nt = if all { t.replace(&old, &new) } else { t.replacen(&old, &new, 1) };
                            self.agent_write(&p, nt).map(|_| format!("{count} remplacement(s) dans {}", p.display()))
                        }
                    }
                }
            }
            "list_files" => {
                let p = s("path").map(|p| self.resolve(&p)).or_else(|| self.root.clone()).unwrap_or_else(workspace::projects_dir);
                let depth = n("depth").unwrap_or(3);
                Ok(list_tree(&p, depth))
            }
            "search" => {
                let base = s("path").map(|p| self.resolve(&p)).or_else(|| self.root.clone()).unwrap_or_else(workspace::projects_dir);
                let max = n("max_results").unwrap_or(100);
                match build_regex(&s("query").unwrap_or_default(), b("regex"), b("case_sensitive"), false) {
                    Err(e) => Err(e),
                    Ok(re) => {
                        let res = search_files(&base, &re, max);
                        let mut out = String::new();
                        let mut k = 0;
                        for (f, hits) in res {
                            for (l, col, t) in hits {
                                out.push_str(&format!("{}:{l}:{col}: {}\n", workspace::rel(self.root.as_deref(), &f), t.trim()));
                                k += 1;
                            }
                        }
                        Ok(if k == 0 { "Aucun résultat.".into() } else { out })
                    }
                }
            }
            "get_diagnostics" => {
                let p = s("path").map(|p| self.resolve(&p));
                Ok(self.diag_text(p.as_deref()))
            }
            "build" => match self.build(ctx) {
                Ok(Some(id)) => {
                    self.pending.push(PendingCall { reply: reply.clone(), task: id, then_run: None, deadline: Some(Instant::now() + std::time::Duration::from_secs(600)), what: "build" });
                    return;
                }
                Ok(None) => Ok("Ce projet n'a rien à construire.".into()),
                Err(e) => Err(e),
            },
            "run" => {
                let args: Vec<String> = a.get("args").and_then(|v| v.as_array()).map(|v| v.iter().filter_map(|x| x.as_str().map(|s| s.to_string())).collect()).unwrap_or_default();
                let t = n("timeout_s").unwrap_or(30) as u64;
                self.build_and_run(ctx, args, Some((reply.clone(), Some(Instant::now() + std::time::Duration::from_secs(t)))));
                return;
            }
            "run_command" => {
                let cmd = s("command").unwrap_or_default();
                let cwd = s("cwd").map(|c| self.resolve(&c));
                let t = n("timeout_s").unwrap_or(120) as u64;
                let id = self.shell(&cmd, cwd, ctx);
                self.pending.push(PendingCall { reply: reply.clone(), task: id, then_run: None, deadline: Some(Instant::now() + std::time::Duration::from_secs(t)), what: "command" });
                return;
            }
            "approve" => {
                let tool = s("tool_name").unwrap_or_default();
                let input = a.get("input").cloned().unwrap_or(Value::Null);
                self.settings.copilot_open = true;
                self.copilot.mcp_permission(tool, input, reply.clone());
                return;
            }
            other => Err(format!("outil inconnu : {other}")),
        };
        let _ = reply.send(match res {
            Ok(t) => ToolResult::text(t),
            Err(e) => ToolResult::error(e),
        });
    }

    /// L'agent écrit un fichier : sur le disque, et dans son onglet (montré à l'utilisateur).
    fn agent_write(&mut self, p: &Path, content: String) -> Result<String, String> {
        if let Some(d) = p.parent() {
            std::fs::create_dir_all(d).map_err(|e| format!("{} : {e}", d.display()))?;
        }
        let existed = p.exists();
        std::fs::write(p, content.as_bytes()).map_err(|e| format!("{} : {e}", p.display()))?;
        let now = self.now;
        match self.doc_index(p) {
            Some(i) => {
                let d = &mut self.docs[i];
                d.set_text(content);
                d.dirty = false;
                d.conflict = false;
                d.disk_mtime = crate::editor::mtime(p);
                d.agent_touched = Some(now);
                self.active = Some(i);
            }
            None => {
                if let Ok(i) = self.open_path(p, None) {
                    self.docs[i].agent_touched = Some(now);
                }
            }
        }
        self.tree.invalidate();
        Ok(format!("{} {}", if existed { "Modifié :" } else { "Créé :" }, p.display()))
    }

    fn workspace_json(&self) -> Value {
        let d = self.active.and_then(|i| self.docs.get(i));
        json!({
            "project": self.root.as_ref().map(|r| r.display().to_string()),
            "project_kind": self.root.as_ref().map(|_| self.kind.label()),
            "projects_folder": workspace::projects_dir().display().to_string(),
            "os": if workspace::KORIN { "Korin OS" } else { std::env::consts::OS },
            "open_editors": self.docs.iter().map(|d| json!({
                "path": d.path.as_ref().map(|p| p.display().to_string()).unwrap_or_else(|| d.title()),
                "unsaved": d.dirty,
                "language": d.lang.name(),
            })).collect::<Vec<_>>(),
            "active_file": d.map(|d| d.path.as_ref().map(|p| p.display().to_string()).unwrap_or_else(|| d.title())),
            "cursor": d.map(|d| json!({"line": d.cursor.0, "column": d.cursor.1})),
            "selection": d.map(|d| d.selection_text.clone()).filter(|s| !s.is_empty()),
            "diagnostics": self.diagnostics.len(),
            "git_branch": self.git.branch,
        })
    }

    // -----------------------------------------------------------------------------------------------------------
    // Les commandes

    pub fn exec(&mut self, cmd: Command, ctx: &egui::Context) {
        match cmd {
            Command::NewFile => self.new_file(),
            Command::NewProject => {
                self.dialog = Some(Dialog::NewProject { template: 0, name: String::new(), location: workspace::projects_dir().display().to_string(), error: None });
            }
            Command::OpenFolder => {
                let dir = self.root.clone().and_then(|r| r.parent().map(|p| p.to_path_buf())).unwrap_or_else(workspace::projects_dir);
                let dir = if dir.is_dir() { dir } else { workspace::home_dir() };
                self.dialog = Some(Dialog::Browse { purpose: BrowsePurpose::OpenFolder, dir, name: String::new(), error: None });
            }
            Command::OpenFile => {
                let dir = self.root.clone().unwrap_or_else(workspace::home_dir);
                self.dialog = Some(Dialog::Browse { purpose: BrowsePurpose::OpenFile, dir, name: String::new(), error: None });
            }
            Command::Save => {
                if let Some(i) = self.active {
                    self.save_doc(i);
                }
            }
            Command::SaveAs => {
                if let Some(i) = self.active {
                    let id = self.docs[i].id;
                    let dir = self.docs[i].path.as_ref().and_then(|p| p.parent().map(|p| p.to_path_buf())).or_else(|| self.root.clone()).unwrap_or_else(workspace::home_dir);
                    self.dialog = Some(Dialog::Browse { purpose: BrowsePurpose::SaveAs(id), dir, name: self.docs[i].title(), error: None });
                }
            }
            Command::SaveAll => self.save_all(),
            Command::CloseTab => {
                if let Some(i) = self.active {
                    let id = self.docs[i].id;
                    self.close_doc(id, false);
                }
            }
            Command::CloseFolder => self.close_project(),
            Command::Quit => ctx.send_viewport_cmd(egui::ViewportCommand::Close),
            Command::Undo => ctx.input_mut(|i| i.events.push(egui::Event::Key { key: egui::Key::Z, physical_key: None, pressed: true, repeat: false, modifiers: egui::Modifiers::COMMAND })),
            Command::Redo => ctx.input_mut(|i| i.events.push(egui::Event::Key { key: egui::Key::Y, physical_key: None, pressed: true, repeat: false, modifiers: egui::Modifiers::COMMAND })),
            Command::Find | Command::Replace => {
                if let Some(d) = self.active_doc() {
                    d.find.open = true;
                    d.find.focus = true;
                    d.find.replace_open = cmd == Command::Replace;
                    if !d.selection_text.is_empty() && !d.selection_text.contains('\n') {
                        d.find.query = d.selection_text.clone();
                    }
                }
            }
            Command::FindInFiles | Command::ShowSearch => {
                self.side = SideView::Search;
                self.sidebar_open = true;
                self.search.focus = true;
                if let Some(d) = self.active.and_then(|i| self.docs.get(i)) {
                    if !d.selection_text.is_empty() && !d.selection_text.contains('\n') {
                        self.search.query = d.selection_text.clone();
                    }
                }
            }
            Command::ToggleComment => {
                if let Some(d) = self.active_doc() {
                    d.toggle_comment();
                }
            }
            Command::DeleteLine => {
                if let Some(d) = self.active_doc() {
                    d.delete_line();
                }
            }
            Command::DuplicateLine => {
                if let Some(d) = self.active_doc() {
                    d.duplicate_line();
                }
            }
            Command::QuickOpen => self.open_palette(PaletteMode::Files),
            Command::CommandPalette => self.open_palette(PaletteMode::Commands),
            Command::GotoLine => self.open_palette(PaletteMode::Line),
            Command::NextProblem => self.next_problem(),
            Command::NextTab | Command::PrevTab => {
                if !self.docs.is_empty() {
                    let n = self.docs.len();
                    let a = self.active.unwrap_or(0);
                    self.active = Some(if cmd == Command::NextTab { (a + 1) % n } else { (a + n - 1) % n });
                }
            }
            Command::ToggleSidebar => self.sidebar_open = !self.sidebar_open,
            Command::TogglePanel => self.panel_open = !self.panel_open,
            Command::ToggleCopilot => {
                self.settings.copilot_open = !self.settings.copilot_open;
                if self.settings.copilot_open {
                    self.copilot.focus_input = true;
                }
            }
            Command::ShowExplorer => self.show_side(SideView::Explorer),
            Command::ShowGit => {
                self.show_side(SideView::Git);
                self.refresh_git();
            }
            Command::ShowRun => self.show_side(SideView::Run),
            Command::ShowTerminal | Command::NewTerminal => {
                self.panel_open = true;
                self.panel_tab = PanelTab::Terminal;
            }
            Command::ShowProblems => {
                self.panel_open = true;
                self.panel_tab = PanelTab::Problems;
            }
            Command::ShowOutput => {
                self.panel_open = true;
                self.panel_tab = PanelTab::Output;
            }
            Command::ZoomIn => {
                self.settings.code_size = (self.settings.code_size + 1.0).min(32.0);
                self.settings.save();
            }
            Command::ZoomOut => {
                self.settings.code_size = (self.settings.code_size - 1.0).max(8.0);
                self.settings.save();
            }
            Command::ToggleWrap => {
                self.settings.word_wrap = !self.settings.word_wrap;
                self.settings.save();
            }
            Command::ThemeSystem | Command::ThemeDark | Command::ThemeLight => {
                self.settings.theme = match cmd {
                    Command::ThemeDark => ThemeChoice::Dark,
                    Command::ThemeLight => ThemeChoice::Light,
                    _ => ThemeChoice::System,
                };
                self.settings.save();
            }
            Command::Build => {
                if let Err(e) = self.build(ctx) {
                    self.toast(e, true);
                }
            }
            Command::Run => {
                let args = split_args(&self.run_args);
                self.build_and_run(ctx, args, None);
            }
            Command::Stop => {
                for t in &mut self.tasks {
                    if t.running() && t.kind != TaskKind::Shell {
                        t.kill();
                    }
                }
            }
            Command::Clean => {
                if let Some(root) = self.root.clone() {
                    if let Some(c) = workspace::clean_cmd(ProjectKind::detect(&root), &root) {
                        self.start_task(TaskKind::Build, c, ctx);
                        self.panel_open = true;
                        self.panel_tab = PanelTab::Output;
                    }
                }
            }
            Command::AskCopilot => {
                self.settings.copilot_open = true;
                self.copilot.focus_input = true;
            }
            Command::ExplainSelection => {
                self.ask("Explique ce code (la sélection, ou le fichier actif s'il n'y a pas de sélection).".into(), ctx);
            }
            Command::FixProblems => {
                let d = self.diag_text(None);
                self.ask(format!("Corrige les problèmes de construction du projet, puis reconstruis pour vérifier.\n{d}"), ctx);
            }
            Command::NewChat => self.copilot.reset(),
            Command::About => self.dialog = Some(Dialog::About),
        }
    }

    pub fn show_side(&mut self, v: SideView) {
        if self.side == v && self.sidebar_open {
            self.sidebar_open = false;
        } else {
            self.side = v;
            self.sidebar_open = true;
        }
    }

    pub fn open_palette(&mut self, mode: PaletteMode) {
        let files = if mode == PaletteMode::Files { self.root.as_deref().map(|r| workspace::walk_files(r, 20_000)).unwrap_or_default() } else { Vec::new() };
        self.palette = Some(PaletteState { mode, query: String::new(), selected: 0, files, focus: true });
    }

    /// Relit les fichiers ouverts que d'autres (l'agent) ont changés sur le disque.
    pub fn check_disk(&mut self) {
        if self.now - self.last_disk_check < 0.8 {
            return;
        }
        self.last_disk_check = self.now;
        let now = self.now;
        for d in &mut self.docs {
            d.check_disk(now);
        }
    }
}

/// Le chemin de KorinStudio (pour le relais MCP de Vibe). Sous Korin il vient d'argv[0] (libposix) : lancé du menu
/// de Korin, ce n'est qu'un nom ; seul un chemin absolu qui existe est pris.
fn studio_exe() -> PathBuf {
    std::env::current_exe().ok().filter(|p| p.is_absolute() && p.is_file()).unwrap_or_else(|| {
        PathBuf::from(if workspace::KORIN { "/apps/korinstudio.app/main" } else { "korinstudio" })
    })
}

pub fn split_args(s: &str) -> Vec<String> {
    let mut out = Vec::new();
    let mut cur = String::new();
    let mut q: Option<char> = None;
    for c in s.chars() {
        match (q, c) {
            (None, '"' | '\'') => q = Some(c),
            (Some(x), c2) if c2 == x => q = None,
            (None, ' ' | '\t') => {
                if !cur.is_empty() {
                    out.push(std::mem::take(&mut cur));
                }
            }
            _ => cur.push(c),
        }
    }
    if !cur.is_empty() {
        out.push(cur);
    }
    out
}

pub fn build_regex(q: &str, regex: bool, case: bool, whole: bool) -> Result<regex::Regex, String> {
    if q.is_empty() {
        return Err("recherche vide".into());
    }
    let mut pat = if regex { q.to_string() } else { regex::escape(q) };
    if whole {
        pat = format!(r"\b{pat}\b");
    }
    if !case {
        pat = format!("(?i){pat}");
    }
    regex::Regex::new(&pat).map_err(|e| e.to_string().lines().last().unwrap_or("").to_string())
}

pub fn search_files(root: &Path, re: &regex::Regex, max: usize) -> Vec<(PathBuf, Vec<(usize, usize, String)>)> {
    let files = if root.is_file() { vec![root.to_path_buf()] } else { workspace::walk_files(root, 50_000) };
    let mut out = Vec::new();
    let mut total = 0;
    for f in files {
        let Ok(meta) = std::fs::metadata(&f) else { continue };
        if meta.len() > 4 << 20 {
            continue;
        }
        let Ok(bytes) = std::fs::read(&f) else { continue };
        if bytes.iter().take(4000).any(|&b| b == 0) {
            continue;
        }
        let text = String::from_utf8_lossy(&bytes);
        let mut hits = Vec::new();
        for (i, line) in text.lines().enumerate() {
            if let Some(m) = re.find(line) {
                let col = line[..m.start()].chars().count() + 1;
                let shown: String = line.chars().take(240).collect();
                hits.push((i + 1, col, shown));
                total += 1;
                if total >= max {
                    break;
                }
            }
        }
        if !hits.is_empty() {
            out.push((f, hits));
        }
        if total >= max {
            break;
        }
    }
    out
}

fn list_tree(root: &Path, depth: usize) -> String {
    fn rec(dir: &Path, prefix: &str, depth: usize, out: &mut String, count: &mut usize) {
        if depth == 0 || *count > 2000 {
            return;
        }
        let Ok(rd) = std::fs::read_dir(dir) else { return };
        let mut es: Vec<_> = rd.flatten().filter(|e| !workspace::hidden(&e.file_name().to_string_lossy())).collect();
        es.sort_by_key(|e| (!e.path().is_dir(), e.file_name()));
        for e in es {
            let name = e.file_name().to_string_lossy().to_string();
            let is_dir = e.path().is_dir();
            *count += 1;
            out.push_str(&format!("{prefix}{name}{}\n", if is_dir { "/" } else { "" }));
            if is_dir && !matches!(name.as_str(), "target" | "node_modules" | "obj" | "bin" | "__pycache__") {
                rec(&e.path(), &format!("{prefix}  "), depth - 1, out, count);
            }
        }
    }
    let mut out = format!("{}/\n", root.display());
    let mut count = 0;
    rec(root, "  ", depth, &mut out, &mut count);
    out
}

#[allow(dead_code)]
pub fn lang_name(l: Lang) -> &'static str {
    l.name()
}
