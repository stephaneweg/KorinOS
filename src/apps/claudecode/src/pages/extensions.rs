//! Extensions (`pages/extensions/mod.rs` de ClaudeCodeUI) : ce que charge une session — serveurs MCP (état,
//! activer ici, reconnecter, se connecter, effacer l'authentification), skills, agents, plugins et places de marché
//! (installer, activer, mettre à jour, désinstaller, options), hooks (liste et éditeur, par `claude edit-hook`),
//! règles d'autorisation (ajouter, retirer, par fichier de réglages). La session montrée est choisie dans l'en-tête
//! (par défaut la plus récente qui a un process `claude`) et regardée par `load_live` / `release` ; sans session
//! vivante, la page montre les plugins du dossier personnel.
//!
//! Ce fichier tient l'état de la page et ses actions (les appels au moteur, lancés sur le runtime : `Task`) ; le
//! dessin des onglets et des inspecteurs est dans `extensions_tabs.rs`. Les aides pures viennent telles quelles de
//! ClaudeCodeUI (avec leurs essais).

use std::collections::HashSet;
use std::sync::Arc;
use std::time::{Duration, Instant};

use ccui_core::live::LiveSession;
use ccui_shared::hooks::{HookListing, HookRow};
use ccui_shared::mcp::{ToolInfo, tools as mcp_tools};
use ccui_shared::model::InitInfo;
use ccui_shared::permission_rules::PermissionRulesState;
use ccui_shared::plugins::{ListedPlugin, Market, Outcome, PluginList, PluginOption, PluginRow};
use ccui_shared::session::SessionHeavy;
use ccui_shared::{cli_edit, i18n, json, permission_rules, t};
use egui::Ui;
use serde_json::{Map, Value, json};

use crate::app::Cx;
use crate::backend::{Backend, Task};

/// Les onglets, dans l'ordre.
pub const TABS: [&str; 6] = ["mcp", "skills", "agents", "plugins", "hooks", "permissions"];

// ============================================================================================ aides pures

/// Un serveur MCP (`Srv`).
#[derive(Clone, Debug, Default, PartialEq)]
pub struct Srv {
    pub name: String,
    pub status: String,
    pub short: String,
    pub origin: String,
    pub tag: String,
    pub error: Option<String>,
    pub transport: Option<String>,
    pub target: Option<String>,
    pub tools: Option<Vec<ToolInfo>>,
    pub scope: Option<String>,
    pub version: Option<String>,
}

/// `Servers(s)` : mcp_status quand il est là (erreur, transport, outils), sinon le résumé de l'init.
pub fn servers(heavy: &SessionHeavy) -> Vec<Srv> {
    if let Some(Value::Array(arr)) = heavy.mcp_status.as_ref().and_then(|st| json::prop(st, "mcpServers")) {
        return arr
            .iter()
            .map(|m| {
                let name = json::string(m, "name");
                let scope = json::str(m, "scope").map(str::to_string);
                let cfg = json::get(m, "config");
                let (short, origin, tag) = split_server(&name, scope.as_deref(), json::str(m, "source"));
                Srv {
                    status: json::string(m, "status"),
                    short,
                    origin,
                    tag,
                    error: json::str(m, "error").map(str::to_string),
                    transport: json::str(cfg, "type").map(str::to_string),
                    target: target(cfg),
                    tools: matches!(json::prop(m, "tools"), Some(Value::Array(_))).then(|| mcp_tools(m)),
                    scope,
                    version: json::prop(m, "serverInfo").and_then(|si| json::str(si, "version")).map(str::to_string),
                    name,
                }
            })
            .collect();
    }
    heavy
        .init
        .as_ref()
        .map(|i| {
            i.mcp
                .iter()
                .map(|m| {
                    let (short, origin, tag) = split_server(&m.name, None, None);
                    Srv { name: m.name.clone(), status: m.status.clone(), short, origin, tag, ..Srv::default() }
                })
                .collect()
        })
        .unwrap_or_default()
}

/// L'adresse (http, sse) ou la ligne de commande (stdio).
fn target(cfg: &Value) -> Option<String> {
    if let Some(url) = json::str(cfg, "url").filter(|u| !u.is_empty()) {
        return Some(url.to_string());
    }
    let cmd = json::str(cfg, "command")?;
    Some(match json::prop(cfg, "args") {
        Some(Value::Array(a)) => {
            let args: Vec<String> = a.iter().map(|x| x.as_str().map_or_else(|| x.to_string(), str::to_string)).collect();
            format!("{cmd} {}", args.join(" "))
        }
        _ => cmd.to_string(),
    })
}

/// `SignsInHere(m)` : ni le mandataire de claude.ai, ni un serveur de claude.ai.
pub fn signs_in_here(transport: Option<&str>, tag: &str) -> bool {
    transport != Some("claudeai-proxy") && tag != "claude.ai"
}

/// `Split(name)` : `plugin:P:reste` → (reste, "plugin:P", "plugin") ; `claude.ai X` → (X, "claude.ai", "claude.ai") ;
/// sinon (nom, portée, source).
pub fn split_server(name: &str, scope: Option<&str>, source: Option<&str>) -> (String, String, String) {
    if name.starts_with("plugin:") {
        let parts: Vec<&str> = name.splitn(3, ':').collect();
        if let [_, p, rest] = parts[..] {
            return (rest.to_string(), format!("plugin:{p}"), "plugin".to_string());
        }
    }
    if let Some(rest) = name.strip_prefix("claude.ai ") {
        return (rest.to_string(), "claude.ai".to_string(), "claude.ai".to_string());
    }
    let origin = match scope {
        Some("user") => t!("Ext.Scope.User"),
        Some("project") => t!("Ext.Scope.Project"),
        Some("local") => t!("Ext.Scope.Local"),
        _ => scope.or(source).unwrap_or_default().to_string(),
    };
    let tag = match source {
        None | Some("user") => t!("Ext.Scope.User"),
        Some("claudeai") => "claude.ai".to_string(),
        Some(s) => s.to_string(),
    };
    (name.to_string(), origin, tag)
}

/// `Agents(s)` : `agents[]` de l'initialize (nom, description, modèle), sinon les noms de l'init.
pub fn agents(heavy: &SessionHeavy) -> Vec<(String, Option<String>, Option<String>)> {
    if let Some(Value::Array(a)) = heavy.initialize_info.as_ref().and_then(|i| json::prop(i, "agents")) {
        return a.iter().map(|x| (json::string(x, "name"), json::str(x, "description").map(str::to_string), json::str(x, "model").map(str::to_string))).collect();
    }
    heavy.init.as_ref().map(|i| i.agents.iter().map(|n| (n.clone(), None, None)).collect()).unwrap_or_default()
}

/// `Describe(s, skill)` : la description de la commande dans l'initialize.
pub fn describe(heavy: &SessionHeavy, skill: &str) -> Option<String> {
    match heavy.initialize_info.as_ref().and_then(|i| json::prop(i, "commands")) {
        Some(Value::Array(c)) => c.iter().find(|x| json::str(x, "name") == Some(skill)).and_then(|x| json::str(x, "description")).map(str::to_string),
        _ => None,
    }
}

/// `SplitSource(d)` : « (commit-commands) … » → (reste, tête) ; « … (user) » → (texte, user) ; sinon (d, None).
pub fn split_source(d: Option<&str>) -> (Option<String>, Option<String>) {
    let Some(d) = d else { return (None, None) };
    if let Some(rest) = d.strip_prefix('(')
        && let Some(end) = rest.find(')')
    {
        let lead = &rest[..end];
        if !lead.is_empty() && !lead.contains('(') {
            return (Some(rest[end + 1..].trim_start().to_string()), Some(lead.to_string()));
        }
    }
    if let Some(body) = d.strip_suffix(')')
        && let Some(open) = body.rfind('(')
    {
        let inner = &body[open + 1..];
        let word = |c: char| c.is_alphanumeric() || matches!(c, '_' | '.' | ':' | '@' | '-');
        if !inner.is_empty() && inner.chars().all(word) {
            return (Some(body[..open].trim_end().to_string()), Some(inner.to_string()));
        }
    }
    (Some(d.to_string()), None)
}

/// userSettings / projectSettings / localSettings / pluginHook… ; le reste tel que la CLI le nomme.
pub fn source_tag(source: &str) -> String {
    match source {
        "userSettings" => t!("Ext.Scope.User"),
        "projectSettings" => t!("Ext.Scope.Project"),
        "localSettings" => t!("Ext.Scope.Local"),
        "pluginHook" => t!("Ext.Scope.Plugin"),
        "session" => t!("Ext.Scope.Session"),
        "policySettings" => t!("Ext.Scope.Policy"),
        "cliArg" => t!("Ext.Scope.Cli"),
        _ => source.to_string(),
    }
}

/// Une portée telle que la CLI l'écrit, dans les mots de la page.
pub fn scope_label(scope: &str) -> String {
    match scope {
        "dynamic" => t!("Ext.Scope.Plugin"),
        "claudeai" => "claude.ai".to_string(),
        "user" | "project" | "local" | "policy" => source_tag(&format!("{scope}Settings")),
        _ => scope.to_string(),
    }
}

/// Un hook distingué de ses voisins : SessionStart (startup), PreToolUse (Bash)…
pub fn hook_name(h: &HookRow) -> String {
    if h.matcher.is_empty() { h.event.clone() } else { format!("{} ({})", h.event, h.matcher) }
}

pub fn hook_key(h: &HookRow) -> String {
    format!("{}|{}|{}|{}", h.event, h.source, h.matcher, h.command)
}

/// La signature de la liste : une écriture se voit quand elle change.
pub fn hook_rows_sig(l: Option<&HookListing>) -> String {
    l.map(|l| l.rows.iter().map(|r| format!("{}|{}|{}|{}|{:?}|{}", r.event, r.source, r.matcher, r.command, r.timeout, r.blocks_on_failure())).collect::<Vec<_>>().join("\n"))
        .unwrap_or_default()
}

/// Le filtre des Skills / Agents / Hooks : dans le nom ou la description, casse ignorée.
pub fn matches(query: &str, name: &str, desc: Option<&str>) -> bool {
    let q = query.trim().to_lowercase();
    q.is_empty() || name.to_lowercase().contains(&q) || desc.is_some_and(|d| d.to_lowercase().contains(&q))
}

/// Où un serveur en échec est défini, pour savoir où le corriger.
pub fn failed_where(m: &Srv, installed: Option<&[PluginRow]>) -> String {
    if let Some(p) = m.origin.strip_prefix("plugin:") {
        let opts = installed.is_some_and(|rows| rows.iter().any(|r| r.name() == p && r.has_options));
        return t!(if opts { "Ext.FailedWhere.PluginOptions" } else { "Ext.FailedWhere.Plugin" }, p);
    }
    if m.tag == "claude.ai" {
        return t!("Ext.FailedWhere.Claude");
    }
    t!(match m.scope.as_deref() {
        Some("user") => "Ext.FailedWhere.User",
        Some("project") => "Ext.FailedWhere.Project",
        Some("local") => "Ext.FailedWhere.Local",
        _ => "Ext.FailedWhere.Other",
    })
}

/// L'état d'un plugin dans cette session (`PluginState`).
#[derive(Clone, Debug, PartialEq)]
pub enum PState {
    Off,
    Failed,
    ServerFailing(usize, Srv),
    NeedsAuth(Srv),
    ServersOff(usize, Srv),
    Loaded,
    Reload,
    On,
}

/// Éteint, en échec de chargement, un de ses serveurs en échec ou à connecter, chargé, ou activé après le début de
/// la session (à recharger). `live` : une session est montrée.
pub fn plugin_state(p: &PluginRow, init: Option<&InitInfo>, servers: Option<&[Srv]>, skills: Option<&[String]>, hooks: Option<&HookListing>, live: bool) -> PState {
    let name = p.name();
    let origin = format!("plugin:{name}");
    let mine: Vec<&Srv> = servers.unwrap_or_default().iter().filter(|x| x.origin == origin).collect();
    if !p.enabled {
        return PState::Off;
    }
    let init_err = init.and_then(|i| i.plugin_errors.as_ref()).is_some_and(|e| e.iter().any(|e| e.plugin == name));
    if !p.errors.is_empty() || init_err {
        return PState::Failed;
    }
    let failed: Vec<&&Srv> = mine.iter().filter(|x| x.status == "failed").collect();
    if let Some(f) = failed.first() {
        return PState::ServerFailing(failed.len(), (**f).clone());
    }
    if let Some(na) = mine.iter().find(|x| x.status == "needs-auth") {
        return PState::NeedsAuth((*na).clone());
    }
    if !mine.is_empty() && mine.iter().all(|x| x.status == "disabled") {
        return PState::ServersOff(mine.len(), mine[0].clone());
    }
    if let Some(i) = init {
        return if i.plugins.iter().any(|x| x == name) { PState::Loaded } else { PState::Reload };
    }
    let prefix = format!("{name}:");
    let from_hook = live && hooks.is_some_and(|h| h.rows.iter().any(|r| r.plugin_name.as_deref().is_some_and(|x| x == p.id || x == name)));
    if mine.iter().any(|x| x.status == "connected") || skills.is_some_and(|k| k.iter().any(|k| k.starts_with(&prefix))) || from_hook {
        return PState::Loaded;
    }
    PState::On
}

/// Ce que contient la case d'action d'une ligne MCP.
#[derive(Clone, Copy, Debug, PartialEq)]
pub enum RowAct {
    Retry,
    SignIn,
    Toggle,
    None,
}

pub fn row_act(m: &Srv) -> RowAct {
    match m.status.as_str() {
        "failed" => RowAct::Retry,
        "needs-auth" if signs_in_here(m.transport.as_deref(), &m.tag) => RowAct::SignIn,
        "connected" | "disabled" => RowAct::Toggle,
        _ => RowAct::None,
    }
}

/// `failed` → son nombre d'outils ou 0 ; `connected` → son nombre ou — ; sinon —.
pub fn tools_cell(m: &Srv) -> String {
    let n = m.tools.as_ref().map(Vec::len);
    match m.status.as_str() {
        "failed" => n.unwrap_or(0).to_string(),
        "connected" => n.map_or_else(|| "—".to_string(), |n| n.to_string()),
        _ => "—".to_string(),
    }
}

/// Un hook modifiable ici : il nomme son entrée, les hooks ne sont pas verrouillés, son fichier est inscriptible.
pub fn hook_editable(h: &HookRow, locked: bool) -> bool {
    h.edit.is_some() && !locked && permission_rules::FILES.contains(&h.source.as_str())
}

/// `type · « {délai} s » · affichage`, les parties vides retirées.
pub fn hook_meta(h: &HookRow) -> String {
    [Some(h.kind.clone()), h.timeout.map(|t| format!("{t} s")), h.display.clone()].into_iter().flatten().filter(|x| !x.is_empty()).collect::<Vec<_>>().join(" · ")
}

/// Ce sur quoi le filtre cherche un hook : évènement, filtre (ou « tout »), plugin ou source.
pub fn hook_haystack(h: &HookRow) -> String {
    let m = if h.matcher.is_empty() { t!("Ext.Hooks.AnyMatcher") } else { h.matcher.clone() };
    format!("{} {m} {}", h.event, h.plugin_name.clone().unwrap_or_else(|| source_tag(&h.source)))
}

/// Le catalogue compte des milliers d'entrées : les 30 plus installées qui contiennent ce qui est tapé.
pub fn catalog_matches(available: &[ListedPlugin], draft: &str) -> Vec<ListedPlugin> {
    let d = draft.to_lowercase();
    let mut m: Vec<ListedPlugin> = available.iter().filter(|a| d.is_empty() || a.id.to_lowercase().contains(&d)).cloned().collect();
    m.sort_by(|a, b| b.installs.cmp(&a.installs));
    m.truncate(30);
    m
}

/// Les skills qu'un plugin apporte (préfixe `nom:`).
pub fn plugin_skills(skills: &[String], name: &str) -> usize {
    let p = format!("{name}:");
    skills.iter().filter(|k| k.starts_with(&p)).count()
}

pub fn plural(n: usize, key: &str) -> String {
    i18n::plural(n as i64, key)
}

// ============================================================================================ l'état de la page

/// Ce qu'une action rapporte (appliqué à la page quand elle finit) ; None : inchangé.
#[derive(Default)]
pub(crate) struct Out {
    note: Option<String>,
    hooks: Option<Option<HookListing>>,
    hooks_note: Option<String>,
    perms: Option<Option<PermissionRulesState>>,
    perms_note: Option<String>,
    plugins_note: Option<String>,
    plugins_done: Option<String>,
    to_accept: Option<(Outcome, Vec<String>)>,
    options: Option<Vec<PluginOption>>,
    /// La liste des plugins est à relire (après installer, activer…).
    reload_plugins: bool,
    clear_install: bool,
    clear_market: bool,
    reset_hook: bool,
    clear_rule: bool,
    clear_option_draft: bool,
    /// Une connexion commence : (serveur, page de connexion).
    auth: Option<(String, String)>,
}

/// Une connexion à un serveur MCP en cours : sa page reste à un clic tant que le serveur n'est pas connecté.
pub(crate) struct Auth {
    pub name: String,
    pub url: String,
    since: Instant,
    last: Instant,
    polled: bool,
    poll: Option<Task<Result<(), String>>>,
}

type PluginsRead = Result<(PluginList, Vec<Market>), String>;

pub struct Extensions {
    /// La session choisie dans l'en-tête (None : la plus récente avec un process).
    pub(crate) pick: Option<String>,
    /// La session regardée (`load_live`), à rendre en partant.
    held: Option<String>,
    pub(crate) tab: String,
    pub(crate) filter: String,
    pub(crate) selected: Option<String>,
    pub(crate) note: Option<String>,
    pub(crate) query: String,
    pub(crate) expanded: HashSet<String>,
    /// Le retrait ou la désinstallation qui attend son second clic (et depuis quand : 5 s).
    pub(crate) confirm: Option<(String, Instant)>,
    refreshed_for: Option<String>,
    read_live: bool,
    autoselect: bool,
    pub(crate) skills_read: Option<Vec<String>>,
    pub(crate) hooks: Option<HookListing>,
    pub(crate) hooks_note: Option<String>,
    pub(crate) hook_editing: Option<HookRow>,
    pub(crate) hook_event: String,
    pub(crate) hook_matcher: String,
    pub(crate) hook_command: String,
    pub(crate) hook_file: String,
    pub(crate) hook_timeout: String,
    pub(crate) hook_block: bool,
    pub(crate) perms: Option<PermissionRulesState>,
    pub(crate) perms_note: Option<String>,
    pub(crate) rule_draft: String,
    pub(crate) rule_behavior: String,
    pub(crate) rule_file: String,
    pub(crate) auth: Option<Auth>,
    pub(crate) installed: Option<Vec<PluginRow>>,
    pub(crate) available: Vec<ListedPlugin>,
    pub(crate) markets: Option<Vec<Market>>,
    pub(crate) plugins_note: Option<String>,
    pub(crate) plugins_done: Option<String>,
    pub(crate) install_draft: String,
    pub(crate) install_scope: String,
    pub(crate) market_draft: String,
    pub(crate) to_accept: Option<(Outcome, Vec<String>)>,
    pub(crate) configuring: Option<String>,
    pub(crate) options: Option<Vec<PluginOption>>,
    pub(crate) option_draft: Map<String, Value>,
    // ---- les appels en cours
    job: Option<Task<Out>>,
    mcp_task: Option<(String, Task<Result<(), String>>)>,
    skills_task: Option<(String, Task<Option<Vec<String>>>)>,
    hooks_task: Option<(String, Task<Result<Option<HookListing>, String>>)>,
    perms_task: Option<(String, Task<Result<Option<PermissionRulesState>, String>>)>,
    plugins_task: Option<(Option<String>, Task<PluginsRead>)>,
    options_task: Option<(String, Task<Result<Vec<PluginOption>, String>>)>,
}

impl Default for Extensions {
    fn default() -> Self {
        Extensions {
            pick: None,
            held: None,
            tab: "mcp".into(),
            filter: String::new(),
            selected: None,
            note: None,
            query: String::new(),
            expanded: HashSet::new(),
            confirm: None,
            refreshed_for: None,
            read_live: false,
            autoselect: false,
            skills_read: None,
            hooks: None,
            hooks_note: None,
            hook_editing: None,
            hook_event: "PreToolUse".into(),
            hook_matcher: String::new(),
            hook_command: String::new(),
            hook_file: "localSettings".into(),
            hook_timeout: String::new(),
            hook_block: false,
            perms: None,
            perms_note: None,
            rule_draft: String::new(),
            rule_behavior: "allow".into(),
            rule_file: "localSettings".into(),
            auth: None,
            installed: None,
            available: Vec::new(),
            markets: None,
            plugins_note: None,
            plugins_done: None,
            install_draft: String::new(),
            install_scope: "user".into(),
            market_draft: String::new(),
            to_accept: None,
            configuring: None,
            options: None,
            option_draft: Map::new(),
            job: None,
            mcp_task: None,
            skills_task: None,
            hooks_task: None,
            perms_task: None,
            plugins_task: None,
            options_task: None,
        }
    }
}

/// Le process de la session `id` existe (le résumé le dit).
pub(crate) fn has_process(cx: &Cx, id: &str) -> bool {
    cx.app.summary.sessions.iter().any(|x| x.id == id && x.has_process)
}

impl Extensions {
    /// Une action en cours (les boutons attendent).
    pub(crate) fn busy(&self) -> bool {
        self.job.is_some()
    }

    /// La session montrée : celle choisie, sinon la plus récente qui a un process.
    pub(crate) fn live_id(&self, cx: &Cx) -> Option<String> {
        let s = &cx.app.summary.sessions;
        match &self.pick {
            Some(id) if s.iter().any(|x| x.id == *id) => Some(id.clone()),
            _ => s.iter().rev().find(|x| x.has_process).map(|x| x.id.clone()),
        }
    }

    /// Le dossier de la session (le résumé d'abord).
    pub(crate) fn cwd(&self, cx: &Cx, id: &str) -> String {
        cx.app.summary.sessions.iter().find(|x| x.id == id).map(|x| x.cwd.clone()).or_else(|| cx.app.session(id).map(|h| h.meta.cwd.clone())).unwrap_or_default()
    }

    /// Le dossier où écrire (celui que les règles rapportent, sinon celui de la session).
    fn write_cwd(&self, cx: &Cx, id: &str) -> String {
        self.perms.as_ref().and_then(|p| p.cwd.clone()).unwrap_or_else(|| self.cwd(cx, id))
    }

    /// `s.Init?.Skills ?? skillsRead`.
    pub(crate) fn skills(&self, heavy: &SessionHeavy) -> Option<Vec<String>> {
        heavy.init.as_ref().map(|i| i.skills.clone()).or_else(|| self.skills_read.clone())
    }

    /// La page s'en va : la session regardée est rendue (appelé par app.rs).
    pub fn leave(&mut self, be: &Backend, app: &mut crate::state::AppStore) {
        if let Some(old) = self.held.take() {
            app.release(be, &old);
        }
        self.auth = None;
        self.refreshed_for = None;
        self.clear_read();
    }

    // ---------- confirmations armées

    pub(crate) fn armed(&self, key: &str) -> bool {
        self.confirm.as_ref().is_some_and(|(k, _)| k == key)
    }

    /// Le premier clic arme, le second (même clé) répond true.
    pub(crate) fn arm_or_go(&mut self, key: &str) -> bool {
        if self.armed(key) {
            self.confirm = None;
            return true;
        }
        self.confirm = Some((key.to_string(), Instant::now()));
        false
    }

    pub(crate) fn expand(&mut self, key: String) {
        if !self.expanded.remove(&key) {
            self.expanded.insert(key);
        }
    }

    // ---------- navigation, onglets, filtres

    pub(crate) fn select(&mut self, cx: &mut Cx, name: String) {
        self.selected = Some(name);
        cx.ui.inspector_open = true;
    }

    pub(crate) fn show(&mut self, cx: &Cx, key: &str) {
        self.tab = key.to_string();
        self.filter.clear();
        self.confirm = None;
        self.query.clear();
        self.expanded.clear();
        let Some(id) = self.live_id(cx) else { return };
        match key {
            "hooks" => self.load_hooks(cx, &id),
            "permissions" => self.load_perms(cx, &id),
            "plugins" => {
                let cwd = self.cwd(cx, &id);
                self.load_plugins(cx, Some(id), cwd);
            }
            _ => {}
        }
    }

    /// Un filtre MCP qui cache le serveur choisi déplace le choix sur le premier montré.
    pub(crate) fn set_filter(&mut self, all: &[Srv], key: &str) {
        self.filter = key.to_string();
        if self.tab != "mcp" {
            return;
        }
        let shown: Vec<&Srv> = all.iter().filter(|x| key.is_empty() || x.status == key).collect();
        if !shown.iter().any(|x| Some(&x.name) == self.selected.as_ref()) {
            self.selected = shown.first().map(|x| x.name.clone());
        }
    }

    // ---------- lire la session

    fn try_autoselect(&mut self, heavy: &SessionHeavy) {
        if !self.autoselect {
            return;
        }
        if self.selected.is_some() {
            self.autoselect = false;
            return;
        }
        let all = servers(heavy);
        if let Some(s) = all.iter().find(|x| x.status == "failed").or(all.first()) {
            self.selected = Some(s.name.clone());
            self.autoselect = false;
        }
    }

    /// `Refresh` : une autre session efface ce que la précédente montrait ; avec un process, mcp_status, skills (avant
    /// l'init), hooks, règles ; toujours les plugins. Ne démarre jamais de process sans qu'on le demande.
    fn refresh(&mut self, cx: &Cx, id: &str) {
        if self.refreshed_for.as_deref() != Some(id) {
            if self.refreshed_for.is_some() {
                self.selected = None;
            }
            self.clear_read();
        }
        self.refreshed_for = Some(id.to_string());
        let proc = has_process(cx, id);
        if proc {
            self.read_live = true;
            if let Ok(s) = cx.be.session(id) {
                self.mcp_task = Some((id.to_string(), cx.be.task(async move { s.refresh_mcp().await })));
                if cx.app.session(id).is_none_or(|h| h.heavy.init.is_none()) {
                    let s = cx.be.session(id).ok();
                    self.skills_task = s.map(|s| (id.to_string(), cx.be.task(async move { s.request("reload_skills", json!({}), None).await.ok().map(|v| json::names(&v, "skills")) })));
                }
            }
            self.load_hooks(cx, id);
            self.load_perms(cx, id);
        }
        let cwd = self.cwd(cx, id);
        self.load_plugins(cx, Some(id.to_string()), cwd);
    }

    /// Ce que montrait la session précédente (un autre dossier) ne doit pas rester, ni son hook en édition.
    fn clear_read(&mut self) {
        self.hooks = None;
        self.hooks_note = None;
        self.perms = None;
        self.perms_note = None;
        self.installed = None;
        self.markets = None;
        self.skills_read = None;
        self.read_live = false;
        self.reset_hook_form();
    }

    /// `get_hooks_listing` relit les réglages à chaque appel : redemandé à chaque ouverture de l'onglet.
    fn load_hooks(&mut self, cx: &Cx, id: &str) {
        if !has_process(cx, id) {
            return;
        }
        if let Ok(s) = cx.be.session(id) {
            self.hooks_task = Some((id.to_string(), cx.be.task(async move { s.hooks_listing().await })));
        }
    }

    fn load_perms(&mut self, cx: &Cx, id: &str) {
        if !has_process(cx, id) {
            return;
        }
        if let Ok(s) = cx.be.session(id) {
            self.perms_task = Some((id.to_string(), cx.be.task(async move { s.permission_rules().await })));
        }
    }

    /// `claude plugin list` et les places de marché dans le dossier ; `sid` : la session pour laquelle (None : le
    /// dossier personnel).
    fn load_plugins(&mut self, cx: &Cx, sid: Option<String>, cwd: String) {
        self.plugins_task = Some((
            sid,
            cx.be.task(async move {
                let l = ccui_core::plugins::list(&cwd).await?;
                let m = ccui_core::plugins::markets(&cwd).await?;
                Ok((l, m))
            }),
        ));
    }

    /// À chaque image : la session montrée (regardée, relue), les réponses arrivées.
    fn poll(&mut self, cx: &mut Cx) {
        let live = self.live_id(cx);
        if self.held != live {
            if let Some(old) = self.held.take() {
                cx.app.release(cx.be, &old);
            }
            if let Some(id) = &live {
                cx.app.load_live(cx.be, id);
            }
            self.held = live.clone();
        }
        match &live {
            None => {
                if self.refreshed_for.is_some() {
                    self.refreshed_for = None;
                    self.clear_read();
                }
                if self.installed.is_none() && self.plugins_task.is_none() && self.plugins_note.is_none() {
                    let home = cx.app.boot.home.clone();
                    self.load_plugins(cx, None, home);
                }
            }
            Some(id) => {
                let proc = has_process(cx, id);
                if self.refreshed_for.as_deref() != Some(id.as_str()) || proc && !self.read_live {
                    let id = id.clone();
                    self.refresh(cx, &id);
                }
            }
        }
        if let Some((_, at)) = &self.confirm
            && at.elapsed() > Duration::from_secs(5)
        {
            self.confirm = None;
        }

        let cur = self.refreshed_for.clone();
        if let Some((id, t)) = &self.mcp_task
            && let Some(r) = t.take()
        {
            let id = id.clone();
            self.mcp_task = None;
            if cur.as_deref() == Some(id.as_str()) {
                if let Err(e) = r {
                    self.note = Some(t!("Ext.ActionFailed", e.trim_end()));
                }
                self.autoselect = true;
            }
        }
        if let Some(id) = &live
            && let Some(h) = cx.app.session(id)
        {
            let heavy = h.heavy.clone();
            self.try_autoselect(&heavy);
        }
        if let Some((id, t)) = &self.skills_task
            && let Some(r) = t.take()
        {
            if cur.as_deref() == Some(id.as_str())
                && let Some(v) = r
            {
                self.skills_read = Some(v);
            }
            self.skills_task = None;
        }
        if let Some((id, t)) = &self.hooks_task
            && let Some(r) = t.take()
        {
            if cur.as_deref() == Some(id.as_str()) {
                match r {
                    Ok(Some(l)) => {
                        self.hooks = Some(l);
                        self.hooks_note = None;
                    }
                    Ok(None) => {}
                    Err(e) => {
                        self.hooks = None;
                        self.hooks_note = Some(t!("Ext.Hooks.Failed", e.trim_end()));
                    }
                }
            }
            self.hooks_task = None;
        }
        if let Some((id, t)) = &self.perms_task
            && let Some(r) = t.take()
        {
            if cur.as_deref() == Some(id.as_str()) {
                match r {
                    Ok(Some(p)) => {
                        self.perms = Some(p);
                        self.perms_note = None;
                    }
                    Ok(None) => {}
                    Err(e) => {
                        self.perms = None;
                        self.perms_note = Some(t!("Ext.Perm.Failed", e.trim_end()));
                    }
                }
            }
            self.perms_task = None;
        }
        if let Some((sid, t)) = &self.plugins_task
            && let Some(r) = t.take()
        {
            let sid = sid.clone();
            self.plugins_task = None;
            match r {
                Ok((l, m)) => {
                    if sid == self.refreshed_for {
                        if let Some(cid) = &self.configuring
                            && !l.installed.iter().any(|r| r.id == *cid)
                        {
                            self.configuring = None;
                            self.options = None;
                        }
                        self.installed = Some(l.installed);
                        self.available = l.available;
                        self.markets = Some(m);
                    }
                }
                Err(e) => self.plugins_note = Some(t!("Ext.Plugins.Failed", e.trim_end())),
            }
        }
        if let Some((cid, t)) = &self.options_task
            && let Some(r) = t.take()
        {
            let cid = cid.clone();
            self.options_task = None;
            if self.configuring.as_deref() == Some(cid.as_str()) {
                match r {
                    Ok(o) => self.options = Some(o),
                    Err(e) => {
                        self.configuring = None;
                        self.plugins_note = Some(t!("Ext.Plugins.Failed", e.trim_end()));
                    }
                }
            }
        }
        if let Some(t) = &self.job
            && let Some(out) = t.take()
        {
            self.job = None;
            self.apply(cx, out);
        }
        self.poll_auth(cx, live.as_deref());
    }

    /// Le résultat d'une action.
    fn apply(&mut self, cx: &Cx, o: Out) {
        if o.note.is_some() {
            self.note = o.note;
        }
        if let Some(h) = o.hooks {
            self.hooks = h;
        }
        if o.hooks_note.is_some() {
            self.hooks_note = o.hooks_note;
        }
        if let Some(p) = o.perms {
            self.perms = p;
        }
        if o.perms_note.is_some() {
            self.perms_note = o.perms_note;
        }
        if o.plugins_note.is_some() {
            self.plugins_note = o.plugins_note;
        }
        if o.plugins_done.is_some() {
            self.plugins_done = o.plugins_done;
        }
        if o.to_accept.is_some() {
            self.to_accept = o.to_accept;
        }
        if o.options.is_some() {
            self.options = o.options;
        }
        if o.clear_install {
            self.install_draft.clear();
        }
        if o.clear_market {
            self.market_draft.clear();
        }
        if o.reset_hook {
            self.reset_hook_form();
        }
        if o.clear_rule {
            self.rule_draft.clear();
        }
        if o.clear_option_draft {
            self.option_draft = Map::new();
        }
        if let Some((name, url)) = o.auth {
            let now = Instant::now();
            self.auth = Some(Auth { name, url, since: now, last: now, polled: false, poll: None });
        }
        if o.reload_plugins {
            let (sid, cwd) = match self.live_id(cx) {
                Some(id) => {
                    let cwd = self.cwd(cx, &id);
                    (Some(id), cwd)
                }
                None => (None, cx.app.boot.home.clone()),
            };
            self.load_plugins(cx, sid, cwd);
        }
    }

    /// mcp_status toutes les 2 s jusqu'à ce que le serveur quitte needs-auth / pending, 5 minutes au plus ; arrêté
    /// si la session n'a plus de process (mcp_status relancerait un claude sans l'écoute du retour de connexion).
    fn poll_auth(&mut self, cx: &mut Cx, live: Option<&str>) {
        let Some(a) = &mut self.auth else { return };
        let Some(id) = live.filter(|id| has_process(cx, id)) else {
            self.auth = None;
            return;
        };
        if let Some(t) = &a.poll
            && let Some(r) = t.take()
        {
            a.poll = None;
            a.polled = true;
            if let Err(e) = r {
                self.note = Some(t!("Ext.ActionFailed", e.trim_end()));
                self.auth = None;
                return;
            }
        }
        if a.polled && a.poll.is_none() {
            let st = cx.app.session(id).and_then(|h| servers(&h.heavy).into_iter().find(|x| x.name == a.name)).map(|x| x.status);
            if !matches!(st.as_deref(), Some("needs-auth" | "pending")) {
                self.auth = None;
                return;
            }
        }
        if a.since.elapsed() > Duration::from_secs(300) {
            self.note = Some(t!("Ext.AuthTimeout", a.name.clone()));
            self.auth = None;
            return;
        }
        if a.poll.is_none() && a.last.elapsed() >= Duration::from_secs(2)
            && let Ok(s) = cx.be.session(id)
        {
            a.last = Instant::now();
            a.poll = Some(cx.be.task(async move { s.refresh_mcp().await }));
        }
    }

    // ---------- les actions MCP

    /// `Run` : occupé, la note effacée ; une erreur devient la note.
    fn run<F>(&mut self, cx: &Cx, fut: F)
    where
        F: std::future::Future<Output = Result<Out, String>> + Send + 'static,
    {
        if self.busy() {
            return;
        }
        self.note = None;
        self.job = Some(cx.be.task(async move {
            match fut.await {
                Ok(o) => o,
                Err(e) => Out { note: Some(t!("Ext.ActionFailed", e.trim_end())), ..Out::default() },
            }
        }));
    }

    fn live(&self, cx: &Cx) -> Option<Arc<LiveSession>> {
        self.live_id(cx).and_then(|id| cx.be.session(&id).ok())
    }

    /// Une session sans process : la page demande avant d'en démarrer un (mcp_status le lance).
    pub(crate) fn start(&mut self, cx: &Cx) {
        let Some(id) = self.live_id(cx) else { return };
        let core = cx.be.core.clone();
        self.run(cx, async move {
            let s = core.sessions.get(&id).ok_or_else(|| t!("Session.NotFound"))?;
            s.refresh_mcp().await?;
            Ok(Out::default())
        });
        self.refreshed_for = None;
    }

    pub(crate) fn toggle(&mut self, cx: &Cx, name: String, enabled: bool) {
        let Some(s) = self.live(cx) else { return };
        self.run(cx, async move {
            s.request("mcp_toggle", json!({"serverName": name, "enabled": enabled}), None).await?;
            s.refresh_mcp().await?;
            Ok(Out::default())
        });
    }

    /// Un serveur qui échoue encore répond par son erreur : elle devient la note.
    pub(crate) fn reconnect(&mut self, cx: &Cx, name: String) {
        let Some(s) = self.live(cx) else { return };
        self.run(cx, async move {
            let r = s.request("mcp_reconnect", json!({"serverName": name}), None).await;
            let st = s.refresh_mcp().await;
            r?;
            st?;
            Ok(Out::default())
        });
    }

    /// Pas de reconnexion en bloc dans la CLI : un mcp_reconnect par serveur en échec, puis un mcp_status.
    pub(crate) fn reconnect_all(&mut self, cx: &Cx, all: Vec<Srv>) {
        let Some(s) = self.live(cx) else { return };
        self.run(cx, async move {
            let mut still = Vec::new();
            for m in all.iter().filter(|x| x.status == "failed") {
                if s.request("mcp_reconnect", json!({"serverName": m.name}), None).await.is_err() {
                    still.push(m.short.clone());
                }
            }
            s.refresh_mcp().await?;
            Ok(Out { note: (!still.is_empty()).then(|| t!("Ext.StillFailing", still.join(", "))), ..Out::default() })
        });
    }

    /// mcp_clear_auth oublie le jeton gardé par la CLI : le prochain appel se reconnecte.
    pub(crate) fn sign_out(&mut self, cx: &Cx, name: String) {
        let Some(s) = self.live(cx) else { return };
        self.run(cx, async move {
            let r = s.request("mcp_clear_auth", json!({"serverName": name}), None).await;
            let st = s.refresh_mcp().await;
            r?;
            st?;
            Ok(Out::default())
        });
    }

    /// mcp_authenticate répond tout de suite par la page de connexion ; la CLI attend ensuite le retour du navigateur
    /// et reconnecte le serveur elle-même.
    pub(crate) fn sign_in(&mut self, cx: &mut Cx, name: String) {
        let Some(s) = self.live(cx) else { return };
        self.auth = None;
        self.select(cx, name.clone());
        self.run(cx, async move {
            let start = s.mcp_authenticate(&name).await?;
            match start.url {
                None => {
                    s.refresh_mcp().await?;
                    Ok(Out::default())
                }
                Some(_) if !start.callback_expected => Ok(Out { note: Some(t!("Ext.AuthTerminal", name)), ..Out::default() }),
                Some(url) => Ok(Out { auth: Some((name, url)), ..Out::default() }),
            }
        });
    }

    // ---------- les hooks

    pub(crate) fn edit_hook(&mut self, h: HookRow) {
        let edit = h.edit.clone().unwrap_or_else(|| ccui_shared::hooks::HookEdit { matcher: h.matcher.clone(), config: Value::Null });
        self.hook_event = h.event.clone();
        self.hook_matcher = edit.matcher.clone();
        self.hook_command = json::str(&edit.config, "command").map_or_else(|| h.command.clone(), str::to_string);
        self.hook_timeout = h.timeout.map(|t| t.to_string()).unwrap_or_default();
        self.hook_block = h.blocks_on_failure();
        self.hook_editing = Some(h);
    }

    pub(crate) fn reset_hook_form(&mut self) {
        self.hook_editing = None;
        self.hook_matcher.clear();
        self.hook_command.clear();
        self.hook_timeout.clear();
        self.hook_block = false;
    }

    /// `claude edit-hook` écrit le fichier de réglages (l'écrivain et les vérifications de la CLI) ; un remplacement
    /// ou un retrait nomme l'entrée gardée, si bien qu'un hook changé entre-temps est refusé plutôt qu'écrasé. La
    /// liste suit quand la session relit ses réglages : relue jusqu'à ce qu'elle change (10 s au plus).
    pub(crate) fn save_hook(&mut self, cx: &Cx, target: Option<HookRow>, remove: bool) {
        let command = self.hook_command.trim().to_string();
        if self.busy() || !remove && command.is_empty() {
            return;
        }
        let Some(id) = self.live_id(cx) else { return };
        let Some(s) = self.live(cx) else { return };
        let target_edit = target.as_ref().and_then(|t| t.edit.clone().map(|e| (t.event.clone(), e)));
        if (remove || target.is_some()) && target_edit.is_none() {
            return;
        }
        self.hooks_note = None;
        let event = self.hook_event.clone();
        let no_matcher = self.hooks.as_ref().and_then(|l| l.catalog.iter().find(|x| x.name == event)).is_some_and(|x| !x.matcher);
        let matcher = if no_matcher { String::new() } else { self.hook_matcher.trim().to_string() };
        let tref = target_edit.as_ref().map(|(ev, e)| (ev.as_str(), e));
        let edit = match (&target, remove) {
            (Some(t), true) => cli_edit::hook("remove", &t.source, &t.event, &tref.expect("vérifié").1.matcher, None, tref),
            _ => {
                let timeout = self.hook_timeout.trim().parse::<i32>().unwrap_or(0);
                let hook = cli_edit::command_hook(tref.map(|(_, e)| &e.config), &command, timeout, self.hook_block);
                let source = target.as_ref().map_or_else(|| self.hook_file.clone(), |t| t.source.clone());
                cli_edit::hook(if target.is_none() { "add" } else { "replace" }, &source, &event, &matcher, Some(hook), tref)
            }
        };
        let before = hook_rows_sig(self.hooks.as_ref());
        let cwd = self.write_cwd(cx, &id);
        let reset = !remove || target == self.hook_editing;
        self.run(cx, async move {
            if let Some(err) = ccui_core::cli_edit::run(&cwd, "edit-hook", &edit).await {
                return Ok(Out { hooks_note: Some(t!("Ext.Hooks.EditFailed", err.trim_end())), ..Out::default() });
            }
            let mut listing = None;
            for _ in 0..20 {
                tokio::time::sleep(Duration::from_millis(500)).await;
                match s.hooks_listing().await {
                    Ok(l) => {
                        let changed = l.is_none() || hook_rows_sig(l.as_ref()) != before;
                        listing = Some(l);
                        if changed {
                            break;
                        }
                    }
                    Err(e) => return Ok(Out { hooks: Some(None), hooks_note: Some(t!("Ext.Hooks.Failed", e.trim_end())), reset_hook: reset, ..Out::default() }),
                }
            }
            Ok(Out { hooks: listing.filter(Option::is_some), reset_hook: reset, ..Out::default() })
        });
    }

    // ---------- les règles d'autorisation

    /// Écrit le fichier de réglages que la CLI lit pour cette source, puis relit jusqu'à ce que la règle y soit.
    pub(crate) fn edit_rule(&mut self, cx: &Cx, source: String, behavior: String, rule: String, add: bool) {
        if self.busy() || rule.is_empty() {
            return;
        }
        if !permission_rules::valid(&rule) {
            self.perms_note = Some(t!("Ext.Perm.Invalid", rule));
            return;
        }
        let Some(id) = self.live_id(cx) else { return };
        let Some(s) = self.live(cx) else { return };
        self.perms_note = None;
        let cwd = self.write_cwd(cx, &id);
        self.run(cx, async move {
            if let Err(e) = ccui_core::permission_rules::edit_rule(&cwd, &source, &behavior, &rule, add).await {
                return Ok(Out { perms_note: Some(t!("Ext.Perm.WriteFailed", e.trim_end())), ..Out::default() });
            }
            let mut perms = None;
            for _ in 0..20 {
                tokio::time::sleep(Duration::from_millis(500)).await;
                let p = s.permission_rules().await.ok().flatten();
                let done = match &p {
                    None => true,
                    Some(p) => p.rules.iter().any(|r| r.source == source && r.behavior == behavior && r.rule == rule) == add,
                };
                perms = Some(p);
                if done {
                    break;
                }
            }
            Ok(Out { perms: perms.filter(Option::is_some), clear_rule: add, ..Out::default() })
        });
    }

    // ---------- les plugins : `claude plugin … --json` dans le dossier, puis reload_plugins

    /// La CLI écrit réglages et caches ; la session vivante ne le voit qu'après reload_plugins.
    pub(crate) fn plugin_act(&mut self, cx: &Cx, args: Vec<String>) {
        if self.busy() {
            return;
        }
        let (cwd, s) = self.plugin_place(cx);
        self.plugins_note = None;
        self.plugins_done = None;
        self.confirm = None;
        self.to_accept = None;
        self.run(cx, async move {
            let r: Result<Out, String> = async {
                let r = ccui_core::plugins::act(&cwd, args.clone()).await?;
                if r.ok {
                    let mut done = r.message.clone();
                    if let Some(s) = &s {
                        done = format!("{done} {}", reload_text(s).await?);
                    }
                    let first = args.first().map(String::as_str);
                    let market_add = args.get(..2).is_some_and(|a| a[0] == "marketplace" && a[1] == "add");
                    Ok(Out { plugins_done: Some(done), clear_install: first == Some("install"), clear_market: market_add, ..Out::default() })
                } else if r.command.is_some() && r.command_sha.is_some() {
                    Ok(Out { to_accept: Some((r, args.clone())), ..Out::default() })
                } else {
                    Ok(Out { plugins_note: Some(r.message), ..Out::default() })
                }
            }
            .await;
            let mut o = r.unwrap_or_else(|e| Out { plugins_note: Some(t!("Ext.Plugins.Failed", e.trim_end())), ..Out::default() });
            o.reload_plugins = true;
            Ok(o)
        });
    }

    /// Le dossier des plugins et la session à recharger (si elle a un process).
    fn plugin_place(&self, cx: &Cx) -> (String, Option<Arc<LiveSession>>) {
        match self.live_id(cx) {
            Some(id) => (self.cwd(cx, &id), if has_process(cx, &id) { cx.be.session(&id).ok() } else { None }),
            None => (cx.app.boot.home.clone(), None),
        }
    }

    pub(crate) fn reload_now(&mut self, cx: &Cx) {
        let Some(s) = self.live(cx) else { return };
        self.run(cx, async move { Ok(Out { plugins_done: Some(reload_text(&s).await?), ..Out::default() }) });
    }

    /// La personne a lu la commande : la même installation, liée à l'empreinte de cette commande.
    pub(crate) fn accept(&mut self, cx: &Cx) {
        let Some((o, mut args)) = self.to_accept.clone() else { return };
        args.push("--accept-command".into());
        args.push(o.command_sha.unwrap_or_default());
        self.plugin_act(cx, args);
    }

    pub(crate) fn configure(&mut self, cx: &mut Cx, id: String) {
        let (cwd, _) = self.plugin_place(cx);
        self.configuring = Some(id.clone());
        self.options = None;
        self.plugins_note = None;
        self.option_draft = Map::new();
        cx.ui.inspector_open = true;
        let i = id.clone();
        self.options_task = Some((id, cx.be.task(async move { ccui_core::plugins::options(&cwd, &i).await })));
    }

    /// Seul ce qui a été modifié part : une option secrète laissée vide garde sa valeur.
    pub(crate) fn save_options(&mut self, cx: &Cx, id: String) {
        let (cwd, s) = self.plugin_place(cx);
        let draft = self.option_draft.clone();
        self.plugins_note = None;
        self.plugins_done = None;
        self.run(cx, async move {
            let r: Result<Out, String> = async {
                if let Some(refused) = ccui_core::plugins::save(&cwd, &id, draft).await? {
                    return Ok(Out { plugins_note: Some(refused), ..Out::default() });
                }
                let o = ccui_core::plugins::options(&cwd, &id).await?;
                let mut done = t!("Ext.Plugins.Saved");
                if let Some(s) = &s {
                    done = format!("{done} {}", reload_text(s).await?);
                }
                Ok(Out { options: Some(o), plugins_done: Some(done), clear_option_draft: true, ..Out::default() })
            }
            .await;
            Ok(r.unwrap_or_else(|e| Out { plugins_note: Some(t!("Ext.Plugins.Failed", e.trim_end())), ..Out::default() }))
        });
    }

    pub fn ui(&mut self, ui: &mut Ui, cx: &mut Cx) {
        self.poll(cx);
        crate::pages::extensions_tabs::show(self, ui, cx);
    }
}

/// reload_plugins, dit en une phrase.
async fn reload_text(s: &Arc<LiveSession>) -> Result<String, String> {
    let r = s.reload_plugins().await?;
    let text = t!("Ext.Plugins.Reloaded", r.plugins, r.skills, r.agents);
    Ok(if r.errors > 0 { format!("{text} {}", t!("Ext.Plugins.ReloadErrors", r.errors)) } else { text })
}

#[cfg(test)]
mod tests {
    use super::*;
    use ccui_shared::hooks::HookEdit;
    use ccui_shared::i18n::{Lang, set_thread_lang};
    use ccui_shared::model::McpBrief;

    #[test]
    fn split_names() {
        set_thread_lang(Some(Lang::En));
        assert_eq!(split_server("plugin:github:github", None, None), ("github".into(), "plugin:github".into(), "plugin".into()));
        assert_eq!(split_server("claude.ai Gmail", None, Some("claudeai")), ("Gmail".into(), "claude.ai".into(), "claude.ai".into()));
        assert_eq!(split_server("probe", Some("project"), Some("projectSettings")), ("probe".into(), "project".into(), "projectSettings".into()));
        assert_eq!(split_server("x", None, None), ("x".into(), "".into(), "user".into()));
        assert_eq!(split_server("plugin:only", Some("dynamic"), Some("claudeai")).1, "dynamic");
    }

    #[test]
    fn sign_in_here() {
        assert!(signs_in_here(Some("http"), "user"));
        assert!(signs_in_here(None, "plugin"));
        assert!(!signs_in_here(Some("claudeai-proxy"), "user"));
        assert!(!signs_in_here(None, "claude.ai"));
    }

    #[test]
    fn servers_from_status_or_init() {
        set_thread_lang(Some(Lang::En));
        let mut h = SessionHeavy::default();
        h.init = Some(InitInfo { mcp: vec![McpBrief { name: "plugin:gh:gh".into(), status: "failed".into() }], ..InitInfo::default() });
        let s = servers(&h);
        assert_eq!((s[0].short.as_str(), s[0].status.as_str(), s[0].tools.is_none()), ("gh", "failed", true));
        h.mcp_status = Some(serde_json::json!({"mcpServers":[
            {"name":"probe","status":"connected","scope":"local","config":{"type":"stdio","command":"node","args":["a.js",1]},
             "tools":[{"name":"ask"}],"serverInfo":{"version":"1.2"}},
            {"name":"web","status":"needs-auth","config":{"type":"http","url":"https://x"}}]}));
        let s = servers(&h);
        assert_eq!(s.len(), 2);
        assert_eq!(s[0].target.as_deref(), Some("node a.js 1"));
        assert_eq!((s[0].origin.as_str(), s[0].version.as_deref(), s[0].tools.as_ref().map(Vec::len)), ("local", Some("1.2"), Some(1)));
        assert_eq!((s[1].target.as_deref(), s[1].transport.as_deref(), s[1].tools.is_none()), (Some("https://x"), Some("http"), true));
        assert_eq!(row_act(&s[0]), RowAct::Toggle);
        assert_eq!(row_act(&s[1]), RowAct::SignIn);
    }

    #[test]
    fn source_split() {
        assert_eq!(split_source(None), (None, None));
        assert_eq!(split_source(Some("(commit-commands) Commit all")), (Some("Commit all".into()), Some("commit-commands".into())));
        assert_eq!(split_source(Some("Review a PR (user)")), (Some("Review a PR".into()), Some("user".into())));
        assert_eq!(split_source(Some("Run it (a b)")), (Some("Run it (a b)".into()), None));
        assert_eq!(split_source(Some("Plain")), (Some("Plain".into()), None));
        assert_eq!(split_source(Some("x (plugin:p@m)")), (Some("x".into()), Some("plugin:p@m".into())));
    }

    #[test]
    fn labels_and_helpers() {
        set_thread_lang(Some(Lang::En));
        assert_eq!(scope_label("user"), "user");
        assert_eq!(scope_label("dynamic"), "plugin");
        assert_eq!(scope_label("claudeai"), "claude.ai");
        assert_eq!(scope_label("managed"), "managed");
        assert_eq!(source_tag("cliArg"), "command line");
        assert!(matches(" GIT ", "commit", Some("Git helper")) && matches("", "x", None) && !matches("q", "x", None));
    }

    fn hook(source: &str, plugin: Option<&str>) -> HookRow {
        HookRow {
            event: "PreToolUse".into(),
            matcher: "Bash".into(),
            kind: "command".into(),
            command: "x".into(),
            display: None,
            source: source.into(),
            source_label: None,
            plugin_name: plugin.map(String::from),
            timeout: Some(5),
            disabled: false,
            edit: Some(HookEdit { matcher: "Bash".into(), config: serde_json::json!({"onFailure":"block"}) }),
        }
    }

    #[test]
    fn hook_signature_and_names() {
        let l = HookListing { rows: vec![hook("localSettings", None)], ..HookListing::default() };
        assert_eq!(hook_rows_sig(Some(&l)), "PreToolUse|localSettings|Bash|x|Some(5)|true");
        assert_eq!(hook_rows_sig(None), "");
        assert_eq!(hook_name(&l.rows[0]), "PreToolUse (Bash)");
    }

    #[test]
    fn plugin_states() {
        let p = PluginRow { id: "gh@mk".into(), enabled: true, scope: "user".into(), ..PluginRow::default() };
        let srv = |st: &str| Srv { name: format!("plugin:gh:{st}"), status: st.into(), origin: "plugin:gh".into(), ..Srv::default() };
        assert_eq!(plugin_state(&PluginRow { enabled: false, ..p.clone() }, None, None, None, None, true), PState::Off);
        assert_eq!(plugin_state(&p, None, None, None, None, true), PState::On);
        let both = [srv("failed"), srv("needs-auth")];
        assert!(matches!(plugin_state(&p, None, Some(&both), None, None, true), PState::ServerFailing(1, _)));
        assert!(matches!(plugin_state(&p, None, Some(&both[1..]), None, None, true), PState::NeedsAuth(_)));
        assert!(matches!(plugin_state(&p, None, Some(&[srv("disabled")]), None, None, true), PState::ServersOff(1, _)));
        let init = InitInfo { plugins: vec!["gh".into()], ..InitInfo::default() };
        assert_eq!(plugin_state(&p, Some(&init), None, None, None, true), PState::Loaded);
        assert_eq!(plugin_state(&p, Some(&InitInfo::default()), None, None, None, true), PState::Reload);
        assert_eq!(plugin_state(&p, None, None, Some(&["gh:commit".into()]), None, true), PState::Loaded);
        let hooks = HookListing { rows: vec![hook("pluginHook", Some("gh@mk"))], ..HookListing::default() };
        assert_eq!(plugin_state(&p, None, None, None, Some(&hooks), true), PState::Loaded);
        assert_eq!(plugin_state(&p, None, None, None, Some(&hooks), false), PState::On);
    }

    #[test]
    fn failed_where_text() {
        set_thread_lang(Some(Lang::En));
        let m = Srv { origin: "plugin:gh".into(), ..Srv::default() };
        let rows = [PluginRow { id: "gh@mk".into(), has_options: true, ..PluginRow::default() }];
        assert_eq!(failed_where(&m, Some(&rows)), t!("Ext.FailedWhere.PluginOptions", "gh"));
        assert_eq!(failed_where(&m, None), t!("Ext.FailedWhere.Plugin", "gh"));
        assert_eq!(failed_where(&Srv { scope: Some("project".into()), ..Srv::default() }, None), t!("Ext.FailedWhere.Project"));
        assert_eq!(failed_where(&Srv { tag: "claude.ai".into(), ..Srv::default() }, None), t!("Ext.FailedWhere.Claude"));
    }

    #[test]
    fn tool_counts() {
        let s = |st: &str, n: Option<usize>| Srv { status: st.into(), tools: n.map(|n| vec![Default::default(); n]), ..Srv::default() };
        assert_eq!(tools_cell(&s("failed", None)), "0");
        assert_eq!(tools_cell(&s("connected", None)), "—");
        assert_eq!(tools_cell(&s("connected", Some(3))), "3");
        assert_eq!(tools_cell(&s("needs-auth", Some(3))), "—");
    }

    fn hrow(source: &str, edit: bool) -> HookRow {
        HookRow {
            event: "Stop".into(),
            matcher: String::new(),
            kind: "command".into(),
            command: "npm run lint".into(),
            display: Some("Lint".into()),
            source: source.into(),
            source_label: None,
            plugin_name: None,
            timeout: Some(30),
            disabled: false,
            edit: edit.then(|| HookEdit { matcher: String::new(), config: serde_json::json!({}) }),
        }
    }

    #[test]
    fn editable_rows() {
        assert!(hook_editable(&hrow("projectSettings", true), false));
        assert!(!hook_editable(&hrow("projectSettings", true), true));
        assert!(!hook_editable(&hrow("policySettings", true), false));
        assert!(!hook_editable(&hrow("localSettings", false), false));
    }

    #[test]
    fn meta_line() {
        assert_eq!(hook_meta(&hrow("x", false)), "command · 30 s · Lint");
        let mut r = hrow("x", false);
        r.timeout = None;
        r.display = None;
        assert_eq!(hook_meta(&r), "command");
    }

    #[test]
    fn catalog_top_30() {
        let a: Vec<ListedPlugin> = (0..40).map(|i| ListedPlugin { id: format!("p{i}@mk"), installs: i, ..ListedPlugin::default() }).collect();
        let m = catalog_matches(&a, "");
        assert_eq!((m.len(), m[0].id.as_str()), (30, "p39@mk"));
        let m = catalog_matches(&a, "P3");
        assert_eq!(m.len(), 11);
    }

    #[test]
    fn skills_of_a_plugin() {
        let k: Vec<String> = ["gh:a", "gh:b", "ghx:c", "gh"].map(String::from).to_vec();
        assert_eq!(plugin_skills(&k, "gh"), 2);
    }
}
