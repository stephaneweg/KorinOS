//! L'outil choisi dans le fil (`session/tool_detail.rs`, `common/file_view.rs` de ClaudeCodeUI) : trois onglets,
//! Sortie (ou Diff pour une édition, et ce qu'une commande Bash a changé : `bashEditDiff`), Entrée, JSON brut ; en
//! bas l'outil, le fichier, les lignes lues, le diff, l'état et la durée. Un appel MCP refusé faute d'accès propose de
//! se reconnecter (`mcp_clear_auth` puis `mcp_authenticate`) et la page des serveurs.

use ccui_shared::edit_diff::{self, DiffKind, DiffLine, FileDiff};
use ccui_shared::model::{McpAuthStart, ToolItem, ToolState};
use ccui_shared::{ask_user, fmt, json, mcp, t, tool_kinds};
use egui::{CornerRadius, Frame, Margin, RichText, ScrollArea, Ui};
use serde_json::{Value, json};

use crate::app::{Cx, Route};
use crate::backend::Task;
use crate::session::inspector::{self as insp, val};
use crate::session::tasks::{self, TaskActions};
use crate::theme;
use crate::widgets;

/// Le bloc tool_use et son résultat, tels que la CLI les a envoyés (refaits : les lignes brutes ne sont pas gardées).
pub fn raw(tool: &ToolItem) -> String {
    let mut o = serde_json::Map::new();
    o.insert("tool_use".into(), json!({"type": "tool_use", "id": tool.id, "name": tool.name, "input": tool.input}));
    if let Some(r) = &tool.result_text {
        let err = matches!(tool.state, ToolState::Error | ToolState::Denied | ToolState::NotRun);
        o.insert("tool_result".into(), json!({"type": "tool_result", "tool_use_id": tool.id, "content": r, "is_error": err}));
    }
    if let Some(s) = &tool.structured {
        o.insert("tool_use_result".into(), s.clone());
    }
    insp::pretty(&Value::Object(o))
}

/// `(numéro, texte)` de chaque ligne `^\s*(\d+)[→\t](.*?)\r?$` d'un résultat de Read.
pub fn numbered(text: &str) -> Vec<(String, String)> {
    text.split('\n')
        .filter_map(|line| {
            let line = line.strip_suffix('\r').unwrap_or(line).trim_start();
            let digits = line.len() - line.trim_start_matches(|c: char| c.is_ascii_digit()).len();
            if digits == 0 {
                return None;
            }
            let (n, rest) = line.split_at(digits);
            let body = rest.strip_prefix('→').or_else(|| rest.strip_prefix('\t'))?;
            Some((n.to_string(), body.to_string()))
        })
        .collect()
}

/// Les lignes lues par un Read, « première–dernière ».
pub fn read_range(tool: &ToolItem) -> Option<String> {
    if tool.name != "Read" {
        return None;
    }
    let lines = numbered(tool.result_text.as_deref().unwrap_or(""));
    Some(format!("{}–{}", lines.first()?.0, lines.last()?.0))
}

/// Le diff qu'une édition finie (ou rejouée) montre sans demander au moteur : le structuredPatch de la CLI, sinon
/// le contenu d'un fichier créé. None : depuis l'entrée (`edit_diff::from_input`).
pub fn known_diff(structured: Option<&Value>) -> Option<Vec<DiffLine>> {
    let s = structured?;
    if let Some(Value::Array(p)) = json::prop(s, "structuredPatch")
        && !p.is_empty()
    {
        return Some(edit_diff::from_patch(&Value::Array(p.clone())));
    }
    (json::str(s, "type") == Some("create")).then(|| edit_diff::created(json::str(s, "content").unwrap_or("")))
}

fn file_name(path: &str) -> String {
    path.rsplit(['/', '\\']).next().unwrap_or("").to_string()
}

/// Un résultat de Read en lignes numérotées (`FileView`), sinon un `pre`.
pub fn file_view(ui: &mut Ui, t: &theme::Tokens, salt: impl std::hash::Hash + std::fmt::Debug, text: &str) {
    let lines = numbered(text);
    if lines.is_empty() {
        insp::pre(ui, t, salt, text);
        return;
    }
    let font = theme::mono(theme::CODE - 1.0);
    ScrollArea::horizontal().id_salt(egui::Id::new(salt)).auto_shrink([false, true]).show(ui, |ui| {
        ui.spacing_mut().item_spacing = egui::vec2(0.0, 0.0);
        for (n, line) in lines {
            ui.horizontal(|ui| {
                ui.add_sized([40.0, 18.0], egui::Label::new(RichText::new(n).font(font.clone()).color(t.text_faint)));
                ui.add_space(12.0);
                ui.add(egui::Label::new(RichText::new(line).font(font.clone()).color(t.text_dim)).extend());
            });
        }
    });
}

#[derive(Default)]
pub struct ToolDetailState {
    tool_id: String,
    tab: usize,
    diffs: Vec<FileDiff>,
    diff_state: Option<ToolState>,
    diff_task: Option<(String, Task<Vec<DiffLine>>)>,
    pub actions: TaskActions,
    auth_task: Option<(String, Task<Result<McpAuthStart, String>>)>,
    auth_url: Option<String>,
    auth_note: Option<String>,
}

pub fn show(ui: &mut Ui, cx: &mut Cx, st: &mut ToolDetailState, id: &str, tool_id: &str) {
    let t = cx.t;
    let Some(h) = cx.app.session(id) else { return };
    let Some(tool) = h.tool_by_id(tool_id).cloned() else { return };
    let cwd = h.meta.cwd.clone();
    let servers = mcp::names(&h.heavy);
    if st.tool_id != tool.id {
        *st = ToolDetailState { tool_id: tool.id.clone(), ..Default::default() };
    }

    // le diff : celui d'une commande Bash, le structuredPatch, sinon depuis l'entrée (une fois par état)
    let bash = tool.structured.as_ref().map(edit_diff::from_bash).unwrap_or_default();
    let edit = tool_kinds::is_edit(&tool.name) || !bash.is_empty();
    if edit && st.diff_state != Some(tool.state) {
        st.diff_state = Some(tool.state);
        let path = json::string(&tool.input, "file_path");
        if !bash.is_empty() {
            st.diffs = bash;
        } else {
            match known_diff(tool.structured.as_ref()) {
                Some(lines) => st.diffs = vec![FileDiff { path, lines }],
                None => {
                    let (name, input) = (tool.name.clone(), tool.input.clone());
                    st.diff_task = Some((path, cx.be.blocking(move || ccui_core::edit_diff::from_input(&name, &input))));
                }
            }
        }
    }
    if let Some((path, tk)) = &st.diff_task
        && let Some(lines) = tk.take()
    {
        st.diffs = vec![FileDiff { path: path.clone(), lines }];
        st.diff_task = None;
    }
    if let Some((server, tk)) = &st.auth_task
        && let Some(r) = tk.take()
    {
        match r {
            Ok(s) => match (s.url, s.callback_expected) {
                (Some(u), true) => st.auth_url = Some(u),
                (None, _) => st.auth_note = Some(t!("Mcp.SignedIn", server)),
                (Some(_), false) => st.auth_note = Some(t!("Ext.AuthTerminal", server)),
            },
            Err(e) => st.auth_note = Some(t!("Ext.ActionFailed", e.trim_end())),
        }
        st.auth_task = None;
    }

    let sub = match json::str(&tool.input, "file_path") {
        Some(fp) => format!("{} · {}", tool.name, file_name(fp)),
        None => format!("{} · {}", tool.name, tool_kinds::target(&tool.name, &tool.input, &cwd)),
    };
    let mut close = false;
    egui::Panel::top(egui::Id::new(("td-top", id))).frame(Frame::new()).show_inside(ui, |ui| {
        insp::ph(ui, &t, &t!("Inspector"), &sub, |ui| {
            close = insp::close_button(ui, &t);
            tasks::task_actions(ui, cx, &mut st.actions, id, &tool);
        });
        let first = if tool_kinds::is_edit(&tool.name) { t!("Tool.Diff") } else { t!("Tool.Output") };
        insp::tabs(ui, &t, &[first, t!("Tool.Input"), t!("Tool.RawJson")], &mut st.tab);
    });
    if close {
        cx.ui.selected_tool_id = None;
    }
    egui::Panel::bottom(egui::Id::new(("td-kv", id))).frame(Frame::new().inner_margin(Margin::symmetric(20, 14)).stroke(egui::Stroke::new(1.0, t.separator))).show_inside(ui, |ui| {
        footer(ui, &t, st, &tool, &cwd, edit);
    });
    egui::CentralPanel::default().frame(Frame::new()).show_inside(ui, |ui| {
        ScrollArea::vertical().id_salt(("td-panel", id, st.tab)).auto_shrink(false).show(ui, |ui| {
            Frame::new().inner_margin(Margin::same(16)).show(ui, |ui| {
                ui.set_width(ui.available_width());
                panel(ui, cx, st, id, &tool, &cwd, edit, &servers);
            });
        });
    });
}

#[allow(clippy::too_many_arguments)]
fn panel(ui: &mut Ui, cx: &mut Cx, st: &mut ToolDetailState, id: &str, tool: &ToolItem, cwd: &str, edit: bool, servers: &[String]) {
    let t = cx.t;
    match st.tab {
        1 => return crate::md::code_block(ui, &t, "json", &insp::pretty(&tool.input)),
        2 => return crate::md::code_block(ui, &t, "json", &raw(tool)),
        _ => {}
    }
    let result = tool.result_text.clone();
    if edit && !matches!(tool.state, ToolState::Denied | ToolState::Error | ToolState::NotRun) {
        // une commande Bash qui a changé des fichiers : sa sortie d'abord, puis ce qu'elle a changé
        if !tool_kinds::is_edit(&tool.name) {
            let r = result.clone().filter(|r| !r.is_empty()).unwrap_or_else(|| t!("Tool.NoOutput"));
            insp::pre(ui, &t, ("td-out", &tool.id), &r);
        }
        if st.diffs.iter().any(|d| !d.lines.is_empty()) {
            for (i, d) in st.diffs.iter().enumerate() {
                insp::diff_view(ui, &t, ("td-diff", &tool.id, i), &tool_kinds::relative(&d.path, cwd), &d.lines);
            }
        } else if st.diff_task.is_some() {
            ui.spinner();
        } else {
            widgets::dim(ui, &t, &t!("Tool.NoChanges"));
        }
        let more = tool.structured.as_ref().and_then(|s| json::prop(s, "bashEditDiff")).map(|b| json::long(b, "moreFiles")).filter(|m| *m > 0);
        if let Some(m) = more {
            widgets::dim(ui, &t, &t!("Diff.MoreFiles", m));
        }
        return;
    }
    match tool.state {
        ToolState::NotRun => {
            insp::para(ui, &t, &t!("Tool.NotRunWhy"));
            insp::pre(ui, &t, ("td-out", &tool.id), result.as_deref().unwrap_or(""));
            return;
        }
        ToolState::Error | ToolState::Denied => {
            insp::errbox(ui, &t, result.as_deref().unwrap_or(""));
            mcp_block(ui, cx, st, id, tool, servers);
            return;
        }
        _ => {}
    }
    if tool.name == ask_user::TOOL {
        let answered = ask_user::answered(tool.structured.as_ref());
        if !answered.is_empty() {
            for (q, a) in answered {
                ui.label(RichText::new(q).font(theme::medium(theme::LABEL)).color(t.text_dim));
                ui.add(egui::Label::new(RichText::new(a).color(t.text)).wrap());
            }
            return;
        }
    }
    if tool.name == "Read"
        && let Some(read) = &result
    {
        return file_view(ui, &t, ("td-file", &tool.id), read);
    }
    let r = match result.filter(|r| !r.is_empty()) {
        Some(r) => r,
        None if tool.state == ToolState::Done => t!("Tool.NoOutput"),
        None => t!("Bg.NoOutput"),
    };
    insp::pre(ui, &t, ("td-out", &tool.id), &r);
}

/// Un appel MCP en échec : la page de son serveur, et pour un accès refusé une nouvelle connexion (les connecteurs
/// claude.ai se gèrent sur claude.ai : pas de reconnexion d'ici).
fn mcp_block(ui: &mut Ui, cx: &mut Cx, st: &mut ToolDetailState, id: &str, tool: &ToolItem, servers: &[String]) {
    let t = cx.t;
    if tool.state != ToolState::Error {
        return;
    }
    let Some(server) = mcp::server_of(&tool.name, servers) else { return };
    let reauth = mcp::auth_error(tool.result_text.as_deref()) && !server.starts_with("claude.ai ");
    ui.add_space(8.0);
    if reauth {
        insp::para(ui, &t, &t!("Mcp.AuthFailed", &server));
        match (&st.auth_url, &st.auth_note) {
            (Some(u), _) => {
                insp::para(ui, &t, &t!("Mcp.AuthWaiting"));
                if widgets::primary(ui, &t, &t!("Ext.OpenSignIn")).clicked() {
                    crate::host::open(u);
                }
            }
            (None, Some(n)) => insp::para(ui, &t, n),
            _ => {}
        }
    }
    ui.horizontal(|ui| {
        if reauth
            && st.auth_url.is_none()
            && ui.add_enabled(st.auth_task.is_none(), egui::Button::new(RichText::new(format!("{} {}", widgets::icon("plug"), t!("Mcp.SignInAgain"))).color(t.on_accent)).fill(t.accent)).clicked()
        {
            st.auth_note = None;
            match cx.be.session(id) {
                Ok(s) => {
                    let sv = server.clone();
                    st.auth_task = Some((server.clone(), cx.be.task(async move { s.sign_in_again(&sv).await })));
                }
                Err(e) => st.auth_note = Some(t!("Ext.ActionFailed", e)),
            }
        }
        if ui.button(t!("Mcp.Servers")).clicked() {
            cx.navigate(Route::Extensions);
        }
    });
}

fn footer(ui: &mut Ui, t: &theme::Tokens, st: &ToolDetailState, tool: &ToolItem, cwd: &str, edit: bool) {
    let mut rows = vec![(t!("Tool.Kv.Tool"), val(t, &tool.name))];
    if json::str(&tool.input, "file_path").is_some() {
        rows.push((t!("Tool.Kv.File"), val(t, tool_kinds::target(&tool.name, &tool.input, cwd))));
    }
    if let Some(r) = read_range(tool) {
        rows.push((t!("Tool.Kv.Lines"), val(t, r)));
    }
    let total: usize = st.diffs.iter().map(|d| d.lines.len()).sum();
    if edit && total > 0 {
        let count = |k| st.diffs.iter().map(|d| d.lines.iter().filter(|l| l.kind == k).count()).sum::<usize>();
        rows.push((t!("Tool.Kv.Diff"), val(t, format!("+{} −{}", count(DiffKind::Add), count(DiffKind::Del)))));
    }
    let status = match tool.state {
        ToolState::Done => RichText::new(t!(if tool_kinds::is_edit(&tool.name) { "Tool.Applied" } else { "Tool.Done" })).color(t.success),
        ToolState::Denied => RichText::new(t!("Tool.Denied")).color(t.danger),
        ToolState::Error => RichText::new(t!("Tool.Error")).color(t.danger),
        ToolState::NotRun => val(t, t!("Tool.NotRun")),
        ToolState::Waiting => RichText::new(t!("Tool.Waiting")).color(t.warning),
        ToolState::Running => val(t, t!("Status.Running")),
    };
    rows.push((t!("Tool.Kv.Status"), status));
    if let Some(end) = tool.ended_at {
        rows.push((t!("Tool.Kv.Duration"), val(t, fmt::dur((end - tool.started_at).num_milliseconds()))));
    }
    Frame::new().corner_radius(CornerRadius::ZERO).show(ui, |ui| {
        ui.set_width(ui.available_width());
        insp::kv(ui, t, ("td-kv", &tool.id), rows);
    });
}

#[cfg(test)]
mod tests {
    use super::*;

    fn tool(name: &str, result: Option<&str>) -> ToolItem {
        ToolItem {
            id: "t1".into(),
            name: name.into(),
            input: json!({"file_path": "/w/a.rs"}),
            parent_tool_use_id: None,
            state: ToolState::Error,
            result_text: result.map(str::to_string),
            structured: None,
            started_at: Default::default(),
            ended_at: None,
            task_id: None,
            task_status: None,
            progress: None,
            tokens: 0,
            sub_tool_uses: 0,
            background: false,
            model: None,
        }
    }

    #[test]
    fn raw_json() {
        let r = raw(&tool("Read", Some("x")));
        let v: Value = serde_json::from_str(&r).unwrap();
        let keys: Vec<&String> = v.as_object().unwrap().keys().collect();
        assert_eq!(keys, ["tool_use", "tool_result"]);
        assert_eq!(v["tool_result"]["is_error"], json!(true));
        assert!(!raw(&tool("Read", None)).contains("tool_result"));
    }

    #[test]
    fn read_lines_and_diffs() {
        assert_eq!(read_range(&tool("Read", Some("     3→a\n     4→b\n    12→c"))).as_deref(), Some("3–12"));
        assert_eq!(read_range(&tool("Read", Some("nothing"))), None);
        assert_eq!(read_range(&tool("Bash", Some("1→a"))), None);
        assert!(known_diff(None).is_none());
        assert!(known_diff(Some(&json!({"structuredPatch": []}))).is_none());
        assert_eq!(known_diff(Some(&json!({"type": "create", "content": "a\nb"}))).map(|d| d.len()), Some(edit_diff::created("a\nb").len()));
        let p = json!({"structuredPatch": [{"oldStart":1,"oldLines":1,"newStart":1,"newLines":1,"lines":["-a","+b"]}]});
        assert_eq!(known_diff(Some(&p)).map(|d| d.len()), Some(3));
    }

    #[test]
    fn numbered_lines() {
        let got = numbered("     1→fn main() {\r\n     2\t    x();\n   10→}");
        assert_eq!(got, [("1".into(), "fn main() {".into()), ("2".into(), "    x();".into()), ("10".into(), "}".into())] as [(String, String); 3]);
        assert!(numbered("plain output").is_empty());
    }
}
