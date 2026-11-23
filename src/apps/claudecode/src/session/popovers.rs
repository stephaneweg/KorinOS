//! Les listes qui s'ouvrent au-dessus du composeur pendant la frappe : les commandes « / » (`slash_popover.rs` de
//! ClaudeCodeUI : les commandes et skills de la CLI, intégrées d'abord) et les mentions « @ » (`mention_popover.rs` :
//! les sous-agents de la session, puis les fichiers et dossiers de son dossier, parcourus une fois par ouverture).
//! Le composeur leur relaie ↑ ↓ et Tab (choisir) ; un clic choisit aussi.

use std::sync::Arc;
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};

use ccui_shared::api::FileIndexResult;
use ccui_shared::session::SessionHeavy;
use ccui_shared::{file_index, json, list_nav, t, text};
use egui::text::LayoutJob;
use egui::{Align, Align2, Color32, CornerRadius, Frame, Layout, Margin, Pos2, Rect, RichText, Sense, Stroke, TextFormat, Ui};
use serde_json::Value;

use crate::backend::{Backend, Task};
use crate::theme::{self, Tokens};
use crate::widgets;

// ------------------------------------------------------------------------------------------- commandes « / »

/// Une commande « / » : nom, description, étiquette (Intégrée, ou le plugin de « plugin:cmd »).
#[derive(Clone, Debug, PartialEq)]
pub struct Cmd {
    pub name: String,
    pub description: String,
    pub tag: Option<String>,
}

impl Cmd {
    /// La partie après le dernier « : » (les commandes des plugins se cherchent et se montrent par elle).
    pub fn show(&self) -> &str {
        self.name.rfind(':').map_or(&self.name, |i| &self.name[i + 1..])
    }
}

fn plugin(name: &str) -> Option<String> {
    name.find(':').map(|i| name[..i].to_string())
}

/// `initialize.commands` (`{name, description, builtin}`), sinon les noms `slash_commands` de l'init.
pub fn commands(info: Option<&Value>, init_slash: &[String], builtin: &str) -> Vec<Cmd> {
    if let Some(Value::Array(cmds)) = info.and_then(|i| json::prop(i, "commands")) {
        return cmds
            .iter()
            .filter_map(|c| {
                let name = json::str(c, "name")?;
                Some(Cmd {
                    name: name.to_string(),
                    description: json::string(c, "description"),
                    tag: if matches!(json::prop(c, "builtin"), Some(Value::Bool(true))) { Some(builtin.to_string()) } else { plugin(name) },
                })
            })
            .collect();
    }
    init_slash.iter().map(|n| Cmd { name: n.clone(), description: String::new(), tag: plugin(n) }).collect()
}

/// Les commandes dont la partie montrée commence par `q` (sans casse) : intégrées, puis simples, puis des plugins
/// (ordre gardé), 8 au plus.
pub fn matches(cmds: Vec<Cmd>, q: &str, builtin: &str) -> Vec<Cmd> {
    let n = q.chars().count();
    let mut m: Vec<Cmd> = cmds
        .into_iter()
        .filter(|c| {
            let head: String = c.show().chars().take(n).collect();
            head.chars().count() == n && head.chars().zip(q.chars()).all(|(a, b)| text::eq_ci(a, b))
        })
        .collect();
    m.sort_by_key(|c| if c.tag.as_deref() == Some(builtin) { 0 } else if c.name.contains(':') { 2 } else { 1 });
    m.truncate(8);
    m
}

/// Le curseur d'une liste, avec la requête à laquelle il appartient (une nouvelle requête repart du haut).
#[derive(Default)]
pub struct Cursor {
    query: String,
    sel: usize,
}

impl Cursor {
    fn get(&self, q: &str, len: usize) -> usize {
        if self.query == q && self.sel < len { self.sel } else { 0 }
    }
    fn set(&mut self, q: &str, sel: usize) {
        self.query = q.to_string();
        self.sel = sel;
    }
}

/// Ce qu'une liste a donné à cette image : ouverte (elle prend les flèches), un choix.
#[derive(Default)]
pub struct PopOut {
    pub open: bool,
    pub picked: Option<String>,
}

/// La liste des commandes « / » au-dessus de `anchor` ; `key` : la touche relayée (ArrowUp, ArrowDown, Tab).
pub fn slash(ctx: &egui::Context, t: &Tokens, anchor: Rect, heavy: &SessionHeavy, starting: bool, query: &str, cur: &mut Cursor, key: Option<&str>) -> PopOut {
    let q = query.trim_start_matches('/');
    let builtin = t!("Slash.Builtin");
    let init = heavy.init.as_ref().map(|i| i.slash_commands.as_slice()).unwrap_or(&[]);
    let list = matches(commands(heavy.initialize_info.as_ref(), init, &builtin), q, &builtin);
    let mut out = PopOut { open: !list.is_empty(), picked: None };
    if list.is_empty() {
        // la liste vient avec la réponse à initialize : un démarrage à froid prend quelques secondes
        if starting {
            pop_frame(ctx, t, anchor, "slash-pop", |ui| {
                ui.label(RichText::new(t!("Slash.Loading")).font(theme::medium(theme::LABEL)).color(t.text_dim));
            });
        }
        return out;
    }
    let len = list.len();
    let mut s = cur.get(q, len);
    match key {
        Some("ArrowDown") => s = (s + 1) % len,
        Some("ArrowUp") => s = (s + len - 1) % len,
        Some("Tab") | Some("Enter") => out.picked = Some(list[s].name.clone()),
        _ => {}
    }
    cur.set(q, s);
    let n = q.chars().count();
    pop_frame(ctx, t, anchor, "slash-pop", |ui| {
        header(ui, t, &t!("Slash.Title"), "");
        for (i, c) in list.iter().enumerate() {
            let show = c.show();
            let head: String = show.chars().take(n).collect();
            let rest: String = show.chars().skip(n).collect();
            let mut job = LayoutJob::default();
            let f = theme::mono(theme::LABEL);
            job.append("/", 0.0, TextFormat::simple(f.clone(), t.text));
            job.append(&head, 0.0, TextFormat::simple(f.clone(), t.accent_ink));
            job.append(&rest, 0.0, TextFormat::simple(f, t.text));
            if option_row(ui, t, i == s, job, &c.description, c.tag.as_deref(), &format!("/{}", c.name)) {
                out.picked = Some(c.name.clone());
            }
        }
    });
    out
}

// ------------------------------------------------------------------------------------------- mentions « @ »

static SCAN: AtomicU64 = AtomicU64::new(0);

/// `"agent-<nom>"` → description, depuis les agents d'initialize (avec descriptions), sinon les noms de l'init ;
/// seulement les noms que la CLI sait lire (`@agent-[\w:.@-]+`). Ordre gardé, doublons ôtés.
pub fn agents(info: Option<&Value>, init_agents: &[String]) -> Vec<(String, String)> {
    let raw: Vec<(String, String)> = match info.and_then(|i| json::prop(i, "agents")) {
        Some(Value::Array(a)) => a.iter().filter_map(|x| json::str(x, "name").map(|n| (format!("agent-{n}"), json::string(x, "description")))).collect(),
        _ => init_agents.iter().map(|n| (format!("agent-{n}"), String::new())).collect(),
    };
    let mut out: Vec<(String, String)> = Vec::new();
    for (k, d) in raw {
        let ok = k.len() > 6 && k[6..].chars().all(|c| c.is_alphanumeric() || c == '_' || matches!(c, ':' | '.' | '@' | '-'));
        if ok && !out.iter().any(|(x, _)| *x == k) {
            out.push((k, d));
        }
    }
    out
}

/// `Refilter` : sans requête, 3 agents au plus puis les chemins les moins profonds ; une requête classe agents et
/// fichiers ensemble.
pub fn refilter(agents: &[String], paths: &[String], q: &str) -> Vec<String> {
    if q.is_empty() {
        let mut found = file_index::filter(agents, "", 3);
        let rest = file_index::MAX_RESULTS - found.len();
        found.extend(file_index::filter(paths, "", rest));
        found
    } else {
        let all: Vec<String> = agents.iter().chain(paths).cloned().collect();
        file_index::filter(&all, q, file_index::MAX_RESULTS)
    }
}

/// Le parcours du dossier de la session pour « @ » (`file_index_scan`), une fois par ouverture de la liste ;
/// annulé (`file_index_cancel`) quand elle se ferme avant la fin.
pub struct MentionScan {
    request: u64,
    task: Task<FileIndexResult>,
    index: Option<FileIndexResult>,
    agents: Vec<(String, String)>,
    core: Arc<ccui_core::Core>,
    /// Le filtre de la dernière requête (20 000 chemins : refiltrer seulement quand elle change).
    cache: Option<(String, Vec<String>)>,
}

impl MentionScan {
    pub fn start(be: &Backend, cwd: &str, heavy: &SessionHeavy) -> MentionScan {
        let request = SCAN.fetch_add(1, Ordering::Relaxed) + 1;
        let core = be.core.clone();
        let c = core.clone();
        let cwd = cwd.to_string();
        let task = be.task(async move {
            let flag = Arc::new(AtomicBool::new(false));
            c.file_scans.running.lock().insert(request, flag.clone());
            let r = tokio::task::spawn_blocking(move || ccui_core::file_index::scan(&cwd, &flag))
                .await
                .unwrap_or(FileIndexResult { paths: Vec::new(), truncated: false });
            c.file_scans.running.lock().remove(&request);
            r
        });
        let agents = agents(heavy.initialize_info.as_ref(), heavy.init.as_ref().map(|i| i.agents.as_slice()).unwrap_or(&[]));
        MentionScan { request, task, index: None, agents, core, cache: None }
    }
}

impl Drop for MentionScan {
    fn drop(&mut self) {
        if self.index.is_none()
            && let Some(f) = self.core.file_scans.running.lock().get(&self.request)
        {
            f.store(true, Ordering::SeqCst);
        }
    }
}

/// La liste des mentions au-dessus de `anchor`.
pub fn mention(ctx: &egui::Context, t: &Tokens, anchor: Rect, scan: &mut MentionScan, query: &str, cur: &mut Cursor, key: Option<&str>) -> PopOut {
    if scan.index.is_none()
        && let Some(r) = scan.task.take()
    {
        scan.index = Some(r);
        scan.cache = None;
    }
    let scanning = scan.index.is_none();
    if scan.cache.as_ref().is_none_or(|(q, _)| q != query) {
        let keys: Vec<String> = scan.agents.iter().map(|(k, _)| k.clone()).collect();
        let paths = scan.index.as_ref().map_or(&[][..], |i| &i.paths[..]);
        scan.cache = Some((query.to_string(), refilter(&keys, paths, query)));
    }
    let items = scan.cache.as_ref().map(|(_, m)| m.clone()).unwrap_or_default();
    let mut out = PopOut { open: !items.is_empty(), picked: None };
    if !scanning && items.is_empty() && query.is_empty() {
        return out;
    }
    let len = items.len() as i32;
    let mut s = cur.get(query, items.len()) as i32;
    match key {
        Some("Tab") | Some("Enter") => out.picked = items.get(s as usize).cloned(),
        Some(k) if len > 0 => s = list_nav::next(k, s, len).max(0),
        _ => {}
    }
    cur.set(query, s as usize);
    let status = match &scan.index {
        None => t!("Mention.Scanning"),
        Some(i) if i.truncated => t!("Mention.Truncated", i.paths.len()),
        Some(_) => String::new(),
    };
    pop_frame(ctx, t, anchor, "mention-pop", |ui| {
        header(ui, t, &t!("Mention.Title"), &status);
        for (n, p) in items.iter().enumerate() {
            let name = file_index::name(p).to_string();
            let agent = scan.agents.iter().find(|(k, _)| k == p).map(|(_, d)| d.clone());
            let f = theme::mono(theme::LABEL);
            let mut job = LayoutJob::default();
            match (!query.is_empty()).then(|| text::find_ci(&name, query)).flatten() {
                Some(hit) => {
                    let chars: Vec<char> = name.chars().collect();
                    let l = query.chars().count().min(chars.len() - hit);
                    let a: String = chars[..hit].iter().collect();
                    let b: String = chars[hit..hit + l].iter().collect();
                    let c: String = chars[hit + l..].iter().collect();
                    job.append(&a, 0.0, TextFormat::simple(f.clone(), t.text));
                    job.append(&b, 0.0, TextFormat::simple(f.clone(), t.accent_ink));
                    job.append(&c, 0.0, TextFormat::simple(f, t.text));
                }
                None => job.append(&name, 0.0, TextFormat::simple(f, t.text)),
            }
            let side = match agent {
                Some(d) => d,
                None => p[..p.len() - name.len()].to_string(),
            };
            if option_row(ui, t, n as i32 == s, job, &side, None, p) {
                out.picked = Some(p.clone());
            }
        }
        if !scanning && items.is_empty() {
            ui.label(RichText::new(t!("Mention.NoMatch")).color(t.text_faint));
        }
    });
    out
}

// ------------------------------------------------------------------------------------------- le dessin commun

/// Le cadre d'une liste ou d'un menu au-dessus du composeur : collé au bord haut de `anchor`, de sa largeur (au
/// plus 640). Rend le rectangle dessiné (pour fermer au clic à côté).
pub fn pop_frame(ctx: &egui::Context, t: &Tokens, anchor: Rect, id: &str, add: impl FnOnce(&mut Ui)) -> Rect {
    let w = anchor.width().min(640.0);
    egui::Area::new(egui::Id::new(id))
        .order(egui::Order::Foreground)
        .fixed_pos(Pos2::new(anchor.left(), anchor.top() - 6.0))
        .pivot(Align2::LEFT_BOTTOM)
        .show(ctx, |ui| {
            Frame::new()
                .fill(t.raised)
                .stroke(Stroke::new(1.0, t.card_border))
                .corner_radius(CornerRadius::same(theme::R_LG))
                .inner_margin(Margin::same(6))
                .shadow(egui::Shadow { offset: [0, 4], blur: 16, spread: 0, color: Color32::from_black_alpha(60) })
                .show(ui, |ui| {
                    ui.set_width(w - 12.0);
                    ui.spacing_mut().item_spacing.y = 1.0;
                    egui::ScrollArea::vertical().max_height(360.0).auto_shrink([false, true]).show(ui, add);
                })
                .response
                .rect
        })
        .inner
}

/// L'en-tête d'une liste : titre à gauche ; un état et les touches à droite.
pub fn header(ui: &mut Ui, t: &Tokens, title: &str, status: &str) {
    ui.horizontal(|ui| {
        ui.add_space(6.0);
        ui.label(RichText::new(title).font(theme::medium(theme::LABEL)).color(t.text_dim));
        ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
            ui.add_space(4.0);
            widgets::kbd(ui, t, "Tab");
            widgets::kbd(ui, t, "↓");
            widgets::kbd(ui, t, "↑");
            if !status.is_empty() {
                ui.label(RichText::new(status).font(theme::medium(theme::CAPTION)).color(t.text_faint));
            }
        });
    });
    ui.add_space(3.0);
}

/// Une ligne de liste : nom (mis en forme), description atténuée, étiquette à droite ; `on` : la ligne du curseur.
/// Rend vrai au clic.
pub fn option_row(ui: &mut Ui, t: &Tokens, on: bool, name: LayoutJob, desc: &str, tag: Option<&str>, hover: &str) -> bool {
    let fill = if on { t.hover } else { Color32::TRANSPARENT };
    let inner = Frame::new().fill(fill).corner_radius(CornerRadius::same(theme::R)).inner_margin(Margin::symmetric(8, 5)).show(ui, |ui| {
        ui.set_width(ui.available_width());
        ui.horizontal(|ui| {
            let w = ui.available_width();
            ui.allocate_ui_with_layout(egui::vec2(w * 0.3, 18.0), Layout::left_to_right(Align::Center), |ui| {
                ui.set_min_width(w * 0.3);
                ui.add(egui::Label::new(name).truncate()).on_hover_text(hover);
            });
            ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
                if let Some(tag) = tag {
                    widgets::tag(ui, t, tag);
                }
                ui.with_layout(Layout::left_to_right(Align::Center), |ui| {
                    ui.add(egui::Label::new(RichText::new(desc).font(theme::medium(theme::LABEL)).color(t.text_dim)).truncate())
                        .on_hover_text(desc);
                });
            });
        });
    });
    if on {
        let r = inner.response.rect;
        ui.painter().vline(r.left() + 1.0, r.y_range(), Stroke::new(2.0, t.accent));
        ui.scroll_to_rect(r, None);
    }
    let r = ui.interact(inner.response.rect, inner.response.id.with("opt"), Sense::click());
    if r.hovered() && !on {
        ui.painter().rect_filled(inner.response.rect, CornerRadius::same(theme::R), t.hover.gamma_multiply(0.5));
    }
    r.clicked()
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn filter_and_order() {
        let info = json!({"commands":[
            {"name":"pr:commit","description":"plugin"},
            {"name":"compact","description":"Compact","builtin":true},
            {"name":"commit-all","description":"user"},
            {"name":"clear","builtin":true},
            {"description":"no name"}
        ]});
        let cmds = commands(Some(&info), &[], "Built-in");
        assert_eq!(cmds.len(), 4);
        let m = matches(cmds.clone(), "CO", "Built-in");
        let names: Vec<&str> = m.iter().map(|c| c.name.as_str()).collect();
        assert_eq!(names, ["compact", "commit-all", "pr:commit"]);
        assert_eq!(m[2].tag.as_deref(), Some("pr"));
        assert_eq!(m[2].show(), "commit");
        assert_eq!(matches(cmds, "", "Built-in").len(), 4);
        let fallback = commands(None, &["a:b".into(), "c".into()], "Built-in");
        assert_eq!(fallback[0].tag.as_deref(), Some("a"));
        assert_eq!(fallback[1].tag, None);
    }

    #[test]
    fn agent_names() {
        let info = json!({"agents":[{"name":"review","description":"Reviews"},{"name":"review"},{"name":"stripe:Company Researcher"},{"name":"a:b.c@d-e_f"}]});
        let a = agents(Some(&info), &[]);
        assert_eq!(a, [("agent-review".to_string(), "Reviews".to_string()), ("agent-a:b.c@d-e_f".to_string(), String::new())]);
        assert_eq!(agents(None, &["x".into()]), [("agent-x".to_string(), String::new())]);
    }

    #[test]
    fn ranking() {
        let agents = vec!["agent-a".to_string(), "agent-b".into(), "agent-c".into(), "agent-d".into()];
        let paths = vec!["src/".to_string(), "README.md".into()];
        let r = refilter(&agents, &paths, "");
        assert_eq!(r.len(), 5);
        assert_eq!(&r[..3], ["agent-a", "agent-b", "agent-c"]);
        assert_eq!(refilter(&agents, &paths, "read"), ["README.md"]);
    }

    #[test]
    fn cursor_follows_query() {
        let mut c = Cursor::default();
        c.set("co", 2);
        assert_eq!(c.get("co", 5), 2);
        assert_eq!(c.get("com", 5), 0);
        assert_eq!(c.get("co", 2), 0);
    }
}
