//! Un sous-agent ou un workflow choisi dans le fil (`session/agent_panel.rs`, `session/workflow_panel.rs` de
//! ClaudeCodeUI).
//!
//! - **L'agent** (outil Agent) : son dernier texte, type, modèle, effort, outils, jetons, sa transcription entière
//!   (en lecture : ses messages, ses appels d'outils qui s'ouvrent sur leur entrée et leur sortie, les agents qu'il a
//!   lancés) et la carte de tous les sous-agents de la session. La CLI ne donne pas de coût par agent.
//! - **Le workflow** (outil Workflow) : le déroulement, ses phases, les agents de la phase choisie, et pour l'agent
//!   choisi son dernier texte, son résultat, sa liste de tâches et ses appels d'outils (son `agent-<id>.jsonl`, lu par
//!   `AgentLogs` : seulement les lignes nouvelles, rendu au moteur quand l'agent n'est plus montré). Relu chaque
//!   seconde tant que le déroulement vit.

use std::collections::HashMap;
use std::sync::Arc;

use ccui_shared::api::WorkflowAgentLog;
use ccui_shared::model::{Item, Time, ToolItem, ToolState};
use ccui_shared::todo_list::{self, TodoStatus};
use ccui_shared::workflow::{self, WfAgent, WfState, WorkflowRun};
use ccui_shared::{ask_user, fmt, i18n, json, session_rules, t, tool_kinds};
use egui::{RichText, ScrollArea, Ui};
use serde_json::Value;

use crate::app::Cx;
use crate::backend::Task;
use crate::md;
use crate::session::inspector::{self as insp, kind_color, val};
use crate::session::tasks::{self, TaskActions};
use crate::theme::{self, Tokens};
use crate::widgets;

/// Les agents imbriqués au plus à cette profondeur (une chaîne de parents malformée s'arrête là).
pub const MAX_DEPTH: usize = 5;

// ------------------------------------------------------------------------------------------- règles pures

/// Les messages de l'agent (parent_tool_use_id), sinon son rapport une fois fini, sinon son résultat.
pub fn last_text(children: &[&Item], agent: &ToolItem) -> Option<String> {
    children
        .iter()
        .rev()
        .find_map(|i| match i {
            Item::Text(x) => Some(x.markdown.clone()),
            _ => None,
        })
        .or_else(|| {
            let h = json::prop(agent.structured.as_ref()?, "handbackReport")?;
            json::str(h, "text").map(str::to_string)
        })
        .or_else(|| agent.result_text.clone())
}

/// En profondeur d'abord depuis les appels Agent du haut ; `(indice de l'élément, profondeur)`.
pub fn agent_map(items: &[Item]) -> Vec<(usize, usize)> {
    let children = session_rules::children(items);
    let mut list = Vec::new();
    fn walk(items: &[Item], idx: &[usize], children: &HashMap<String, Vec<usize>>, depth: usize, list: &mut Vec<(usize, usize)>) {
        if depth > MAX_DEPTH {
            return;
        }
        for &i in idx {
            let Some(a) = items[i].as_tool().filter(|t| tool_kinds::is_agent(&t.name)) else { continue };
            list.push((i, depth));
            if let Some(c) = children.get(&a.id) {
                walk(items, c, children, depth + 1, list);
            }
        }
    }
    let top: Vec<usize> = (0..items.len()).filter(|&i| items[i].as_tool().is_some_and(|t| t.parent_tool_use_id.is_none())).collect();
    walk(items, &top, &children, 0, &mut list);
    list
}

/// `Workflow.Dot(a)` : fini « done », en cours « run », en attente « wait », sinon « err ».
pub fn agent_dot(agent: &ToolItem) -> &'static str {
    match agent.state {
        ToolState::Done => "done",
        ToolState::Running => "run",
        ToolState::Waiting => "wait",
        _ => "err",
    }
}

/// `Workflow.State(a)` : les jetons, puis le mot d'état (et la durée en cours ou finie).
pub fn agent_state(agent: &ToolItem, now: Time) -> String {
    let el = || fmt::dur((agent.ended_at.unwrap_or(now) - agent.started_at).num_milliseconds());
    let tokens = if agent.tokens > 0 { format!("{} · ", t!("Workflow.Tokens", fmt::tokens(agent.tokens))) } else { String::new() };
    let word = match agent.state {
        ToolState::Running => format!("{} · {}", t!("Status.Running"), el()),
        ToolState::Done => format!("{} · {}", t!("Tool.Done"), el()),
        ToolState::Waiting => t!("Tool.Waiting"),
        ToolState::Denied => t!("Tool.Denied"),
        ToolState::NotRun => t!("Tool.NotRun"),
        ToolState::Error => t!("Tool.Error"),
    };
    format!("{tokens}{word}")
}

/// Ce qu'une ligne d'outil montre à droite (`ToolRow.Meta`) : (texte, couleur ; None : rien).
pub fn tool_meta(t: &Tokens, tool: &ToolItem) -> Option<(String, egui::Color32)> {
    match tool.state {
        ToolState::Waiting => Some((t!("Tool.Waiting"), t.warning)),
        ToolState::Denied => Some((t!("Tool.Denied"), t.danger)),
        ToolState::Error => Some((t!("Tool.Error"), t.danger)),
        ToolState::NotRun => Some((t!("Tool.NotRun"), t.text_faint)),
        ToolState::Running => Some(("…".into(), t.text_faint)),
        ToolState::Done => {
            if tool.name == "Write" && tool.structured.as_ref().and_then(|s| json::str(s, "type")) == Some("create") {
                return Some((t!("Tool.Created"), t.success));
            }
            Some(done_meta(tool)).filter(|m| !m.is_empty()).map(|m| (m, t.text_faint))
        }
    }
}

fn num(v: &Value, name: &str) -> Option<i64> {
    json::prop(v, name).and_then(Value::as_f64).map(|d| d as i64)
}

/// `ToolRow.Compute` : le résumé d'un outil fini (lignes, résultats, +n −m, réponses, durée).
pub fn done_meta(t: &ToolItem) -> String {
    let null = Value::Null;
    let s = t.structured.as_ref().unwrap_or(&null);
    match t.name.as_str() {
        ask_user::TOOL => {
            let qa = ask_user::answered(t.structured.as_ref());
            if !qa.is_empty() {
                return qa.into_iter().map(|(_, a)| a).collect::<Vec<_>>().join(" · ");
            }
        }
        "Read" => {
            let n = json::prop(s, "file")
                .and_then(|f| num(f, "numLines"))
                .or_else(|| t.result_text.as_ref().map(|r| r.trim_end_matches('\n').split('\n').count() as i64))
                .unwrap_or(0);
            return i18n::plural(n, "Tool.Lines");
        }
        "Grep" | "Glob" => {
            let r = match json::str(s, "mode") {
                Some("content") => num(s, "numLines"),
                Some("count") => num(s, "numMatches"),
                _ => num(s, "numFiles"),
            }
            .or_else(|| t.result_text.as_ref().map(|r| r.split('\n').filter(|l| !l.is_empty()).count() as i64))
            .unwrap_or(0);
            return i18n::plural(r, "Tool.Results");
        }
        "Edit" | "MultiEdit" | "Write" => {
            if let Some(Value::Array(patch)) = json::prop(s, "structuredPatch") {
                let lines: Vec<&str> = patch
                    .iter()
                    .filter_map(|h| match json::prop(h, "lines") {
                        Some(Value::Array(l)) => Some(l),
                        _ => None,
                    })
                    .flatten()
                    .map(|l| l.as_str().unwrap_or(""))
                    .collect();
                let (add, del) = (lines.iter().filter(|l| l.starts_with('+')).count(), lines.iter().filter(|l| l.starts_with('-')).count());
                return format!("+{add} \u{2212}{del}");
            }
        }
        _ => {}
    }
    match t.ended_at.map(|e| (e - t.started_at).num_milliseconds()) {
        Some(ms) if ms >= 50 => fmt::dur(ms),
        _ => String::new(),
    }
}

/// `Key(a)` : agent_id, sinon « phase/label ».
pub fn agent_key(a: &WfAgent) -> String {
    a.agent_id.clone().unwrap_or_else(|| format!("{}/{}", a.phase, a.label))
}

/// La phase montrée : la choisie, sinon la première en cours, sinon la dernière qui a des agents, sinon la première.
pub fn pick_phase(run: &WorkflowRun, selected: Option<&str>) -> Option<usize> {
    let ps = &run.phases;
    selected
        .and_then(|s| ps.iter().position(|p| p.title == s))
        .or_else(|| ps.iter().position(|p| p.state() == WfState::Running))
        .or_else(|| ps.iter().rposition(|p| p.total > 0))
        .or(if ps.is_empty() { None } else { Some(0) })
}

/// `WorkflowCard.Dot` (sans l'outil).
pub fn wf_dot(state: WfState) -> &'static str {
    match state {
        WfState::Done => "done",
        WfState::Running => "run",
        WfState::Failed => "err",
        WfState::Unknown => "exited",
        WfState::Waiting => "idle",
    }
}

/// `WorkflowCard.Label(s, raw)`.
pub fn wf_label(state: WfState, raw: Option<&str>) -> String {
    if let Some(r) = raw {
        return r.to_string();
    }
    t!(match state {
        WfState::Done => "Tool.Done",
        WfState::Running => "Status.Running",
        WfState::Failed => "Tool.Error",
        WfState::Unknown => "Wf.State.Unknown",
        WfState::Waiting => "Wf.State.Waiting",
    })
}

fn wf_state_label(a: &WfAgent) -> String {
    if a.state == WfState::Waiting { t!("Wf.Queued") } else { wf_label(a.state, a.raw_state.as_deref()) }
}

fn wf_meta(a: &WfAgent) -> String {
    let mut parts = Vec::new();
    if a.tokens > 0 {
        parts.push(format!("{}{}", if a.approx { "≈" } else { "" }, t!("Workflow.Tokens", fmt::tokens(a.tokens))));
    }
    if let Some(d) = a.duration_ms {
        parts.push(fmt::dur(d));
    }
    if !matches!(a.state, WfState::Done | WfState::Running) {
        parts.push(wf_state_label(a));
    }
    parts.join(" · ")
}

// ------------------------------------------------------------------------------------------- pièces

/// Une ligne d'outil d'un sous-agent, en lecture : nom, cible, résumé ; elle s'ouvre sur son entrée et sa sortie.
pub fn sub_tool(ui: &mut Ui, t: &Tokens, tool: &ToolItem, cwd: &str) {
    let cid = ui.id().with(("sub-tool", &tool.id));
    egui::collapsing_header::CollapsingState::load_with_default_open(ui.ctx(), cid, false)
        .show_header(ui, |ui| {
            ui.add_sized([72.0, 16.0], egui::Label::new(RichText::new(tool_kinds::label(&tool.name)).font(theme::mono(theme::CAPTION)).color(kind_color(t, &tool.name))).truncate());
            ui.add(egui::Label::new(RichText::new(tool_kinds::target(&tool.name, &tool.input, cwd)).font(theme::mono(theme::CAPTION)).color(t.text_dim)).truncate());
            if let Some((m, c)) = tool_meta(t, tool) {
                ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                    ui.label(RichText::new(m).font(theme::mono(theme::CAPTION)).color(c));
                });
            }
        })
        .body(|ui| {
            insp::pre(ui, t, ("sub-in", &tool.id), &insp::pretty(&tool.input));
            if let Some(r) = tool.result_text.as_ref().filter(|r| !r.is_empty()) {
                insp::pre(ui, t, ("sub-out", &tool.id), r);
            }
        });
}

/// Une ligne cliquable d'agent (carte des agents, agents d'une phase) : point, nom, type, état à droite.
fn agent_row(ui: &mut Ui, t: &Tokens, sel: bool, indent: f32, dot: &str, name: &str, kind: &str, state: &str) -> egui::Response {
    widgets::row(ui, t, sel, |ui| {
        ui.horizontal(|ui| {
            ui.add_space(indent);
            insp::state_dot(ui, t, dot);
            ui.add(egui::Label::new(RichText::new(name).font(theme::medium(theme::LABEL)).color(t.text)).truncate().selectable(false));
            if !kind.is_empty() {
                ui.add(egui::Label::new(RichText::new(kind).font(theme::mono(theme::CAPTION)).color(t.text_faint)).truncate().selectable(false));
            }
            ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                ui.add(egui::Label::new(RichText::new(state).font(theme::mono(theme::CAPTION)).color(t.text_faint)).selectable(false));
            });
        });
    })
}

// ------------------------------------------------------------------------------------------- l'agent

#[derive(Default)]
pub struct AgentState {
    pub actions: TaskActions,
    agent: String,
}

pub fn show_agent(ui: &mut Ui, cx: &mut Cx, st: &mut AgentState, id: &str, agent_id: &str) {
    let t = cx.t;
    if st.agent != agent_id {
        *st = AgentState { agent: agent_id.to_string(), ..Default::default() };
    }
    let Some(h) = cx.app.session(id) else { return };
    let Some(a) = h.tool_by_id(agent_id).cloned() else { return };
    let cwd = h.meta.cwd.clone();
    let now = chrono::Utc::now();
    let kids: Vec<usize> = session_rules::children(&h.items).remove(agent_id).unwrap_or_default();
    let kid_items: Vec<&Item> = kids.iter().filter_map(|&i| h.items.get(i)).collect();
    let text = last_text(&kid_items, &a);
    let child_tools = kid_items.iter().filter(|i| i.as_tool().is_some()).count();
    let kid_items: Vec<Item> = kid_items.into_iter().cloned().collect();
    let map: Vec<(String, usize, &'static str, String, String, String)> = agent_map(&h.items)
        .into_iter()
        .filter_map(|(i, depth)| {
            let x = h.items[i].as_tool()?;
            Some((
                x.id.clone(),
                depth,
                agent_dot(x),
                json::str(&x.input, "description").map(str::to_string).unwrap_or_else(|| t!("Agent.Title")),
                json::string(&x.input, "subagent_type"),
                agent_state(x, now),
            ))
        })
        .collect();
    if a.state == ToolState::Running {
        ui.ctx().request_repaint_after(std::time::Duration::from_secs(1));
    }

    let mut close = false;
    let mut select: Option<String> = None;
    insp::ph(ui, &t, &t!("Agent.Title"), &json::string(&a.input, "description"), |ui| {
        close = insp::close_button(ui, &t);
        tasks::task_actions(ui, cx, &mut st.actions, id, &a);
    });
    ScrollArea::vertical().id_salt(("agent", id)).auto_shrink(false).show(ui, |ui| {
        insp::section(ui, &t, false, |ui| {
            widgets::cap(ui, &t, &t!("Agent.Writes"));
            match text.filter(|x| !x.is_empty()) {
                Some(x) => md::show(ui, &t, &x),
                None => insp::para(ui, &t, &t!(if a.state == ToolState::Running { "Agent.NothingYet" } else { "Agent.NoText" })),
            }
            let s = |k: &str| json::str(&a.input, k).map(str::to_string);
            // (le modèle ou l'effort demandés par l'appel, sinon le modèle de ses messages)
            let model = s("model").or_else(|| a.model.clone()).unwrap_or_else(|| "—".into());
            let mut rows = vec![(t!("Kv.Type"), val(&t, s("subagent_type").unwrap_or_else(|| "—".into()))), (t!("Kv.Model"), val(&t, model))];
            if let Some(e) = s("effort") {
                rows.push((t!("Kv.Effort"), val(&t, i18n::name("Effort.", Some(&e)))));
            }
            rows.push((t!("Kv.Tools"), val(&t, (a.sub_tool_uses.max(0) as usize).max(child_tools).to_string())));
            rows.push((t!("Kv.Tokens"), val(&t, if a.tokens > 0 { fmt::tokens(a.tokens) } else { "—".into() })));
            insp::kv(ui, &t, ("agent-kv", agent_id), rows);
        });
        if !kid_items.is_empty() {
            insp::section(ui, &t, false, |ui| {
                egui::CollapsingHeader::new(RichText::new(format!("{} · {}", t!("Agent.Transcript"), kid_items.len()).to_uppercase()).font(theme::semibold(theme::CAPTION - 1.0)).color(t.text_faint))
                    .id_salt(("agent-tr", agent_id))
                    .default_open(false)
                    .show(ui, |ui| {
                        for it in &kid_items {
                            match it {
                                Item::Text(x) => md::show(ui, &t, &x.markdown),
                                Item::Tool(x) if x.name == "SubagentHandback" => {}
                                Item::Tool(x) if tool_kinds::is_agent(&x.name) => {
                                    let r = widgets::row(ui, &t, false, |ui| {
                                        ui.horizontal(|ui| {
                                            ui.label(RichText::new(tool_kinds::label(&x.name)).font(theme::mono(theme::CAPTION)).color(kind_color(&t, &x.name)));
                                            ui.add(egui::Label::new(RichText::new(json::string(&x.input, "description")).font(theme::mono(theme::CAPTION)).color(t.text_dim)).truncate().selectable(false));
                                            if let Some((m, c)) = tool_meta(&t, x) {
                                                ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                                                    ui.label(RichText::new(m).font(theme::mono(theme::CAPTION)).color(c));
                                                });
                                            }
                                        });
                                    });
                                    if r.clicked() {
                                        select = Some(x.id.clone());
                                    }
                                }
                                Item::Tool(x) => sub_tool(ui, &t, x, &cwd),
                                _ => {}
                            }
                        }
                    });
            });
        }
        // la carte : chaque sous-agent de la session, sous celui qui l'a lancé
        if map.len() > 1 {
            insp::section(ui, &t, true, |ui| {
                widgets::cap(ui, &t, &format!("{} · {}", t!("Agent.Map"), map.len()));
                for (aid, depth, dot, name, kind, state) in &map {
                    if agent_row(ui, &t, aid == agent_id, 16.0 * *depth as f32, dot, name, kind, state).clicked() {
                        select = Some(aid.clone());
                    }
                }
            });
        }
    });
    if close {
        cx.ui.selected_tool_id = None;
    }
    if let Some(s) = select {
        cx.ui.selected_tool_id = Some(s);
    }
}

// ------------------------------------------------------------------------------------------- le workflow

/// Au plus les 40 derniers appels d'un agent (un long agent en fait des centaines).
const MAX_TOOLS: usize = 40;

#[derive(Default)]
pub struct WorkflowState {
    tool_id: String,
    run: Option<WorkflowRun>,
    run_task: Option<Task<Option<WorkflowRun>>>,
    agent_sel: Option<String>,
    log: WorkflowAgentLog,
    log_task: Option<(String, Task<WorkflowAgentLog>)>,
    /// L'agent dont le moteur garde le journal pour nous.
    held: Option<String>,
    last: Option<f64>,
    /// L'outil tel qu'il était à la dernière lecture (il bouge : relire).
    seen: Option<ToolItem>,
}

/// Rend au moteur le journal de l'agent montré (l'inspecteur ne montre plus ce workflow).
pub fn workflow_leave(cx: &Cx, st: &mut WorkflowState, id: &str) {
    if let Some(a) = st.held.take() {
        cx.be.core.agent_logs.release(id, &st.tool_id, &a);
    }
    if !st.tool_id.is_empty() {
        *st = WorkflowState::default();
    }
}

fn refresh(cx: &Cx, st: &mut WorkflowState, id: &str, tool: &ToolItem, now: f64) {
    st.last = Some(now);
    st.seen = Some(tool.clone());
    if st.run_task.is_none() {
        let tl = tool.clone();
        st.run_task = Some(cx.be.blocking(move || Some(ccui_core::workflow_runs::read(&tl, chrono::Utc::now()))));
    }
    if let Some(agent) = st.held.clone()
        && st.log_task.is_none()
    {
        let core = Arc::clone(&cx.be.core);
        let (sid, tl, ag) = (id.to_string(), tool.clone(), agent.clone());
        st.log_task = Some((agent, cx.be.blocking(move || core.agent_logs.read(&sid, &tl, &ag))));
    }
}

pub fn show_workflow(ui: &mut Ui, cx: &mut Cx, st: &mut WorkflowState, id: &str, tool_id: &str) {
    let t = cx.t;
    let Some(tool) = cx.app.session(id).and_then(|h| h.tool_by_id(tool_id).cloned()) else { return };
    let cwd = cx.app.session(id).map(|h| h.meta.cwd.clone()).unwrap_or_default();
    if st.tool_id != tool.id {
        workflow_leave(cx, st, id);
        st.tool_id = tool.id.clone();
    }
    let now = ui.input(|i| i.time);
    if let Some(r) = st.run_task.as_ref().and_then(Task::take) {
        st.run_task = None;
        st.run = r;
    }
    if let Some((agent, tk)) = &st.log_task
        && let Some(l) = tk.take()
    {
        // (toujours l'agent montré : un clic pendant la lecture redemande à la lecture suivante)
        if st.held.as_deref() == Some(agent.as_str()) {
            st.log = l;
        }
        st.log_task = None;
    }
    let live = st.run.as_ref().is_some_and(|r| r.live);
    if st.seen.as_ref() != Some(&tool) || (live && st.last.is_none_or(|l| now - l >= 1.0)) {
        refresh(cx, st, id, &tool, now);
    }
    if live {
        ui.ctx().request_repaint_after(std::time::Duration::from_secs(1));
    }

    // la phase, ses agents, l'agent choisi
    let phase_idx = st.run.as_ref().and_then(|r| pick_phase(r, cx.ui.selected_phase.as_deref()));
    let phase_title = st.run.as_ref().and_then(|r| Some(r.phases.get(phase_idx?)?.title.clone()));
    let agents: Vec<WfAgent> = match (&st.run, &phase_title) {
        (Some(r), Some(title)) => r.agents.iter().filter(|a| &a.phase == title).cloned().collect(),
        _ => Vec::new(),
    };
    let agent = st.agent_sel.as_ref().and_then(|k| agents.iter().find(|a| &agent_key(a) == k).cloned());
    // garder le journal de l'agent montré ; rendre celui qui ne l'est plus
    let want = agent.as_ref().and_then(|a| a.agent_id.clone());
    if st.held != want {
        if let Some(h) = st.held.take() {
            cx.be.core.agent_logs.release(id, &st.tool_id, &h);
        }
        st.held = want.clone();
        st.log = WorkflowAgentLog::default();
        if want.is_some() {
            refresh(cx, st, id, &tool, now);
        }
    }

    let (name, label) = st.run.as_ref().map_or((String::new(), String::new()), |r| (r.name.clone(), wf_label(r.state, r.raw_status.as_deref())));
    let mut close = false;
    let mut pick_phase_to: Option<String> = None;
    insp::ph(ui, &t, &format!("{} · {name}", workflow::TOOL.to_lowercase()), &label, |ui| {
        close = insp::close_button(ui, &t);
    });
    ScrollArea::vertical().id_salt(("wf", id)).auto_shrink(false).show(ui, |ui| {
        if let Some(r) = &st.run {
            insp::section(ui, &t, false, |ui| {
                if let Some(d) = r.description.as_ref().filter(|d| !d.is_empty()) {
                    insp::para(ui, &t, d);
                }
                let mut rows = vec![
                    (t!("Kv.Status"), val(&t, wf_label(r.state, r.raw_status.as_deref()))),
                    (t!("Kv.Agents"), val(&t, format!("{}/{}", r.done(), r.agents.len()))),
                    (t!("Kv.Elapsed"), val(&t, fmt::dur(r.elapsed_ms))),
                    (t!("Kv.Tokens"), val(&t, if r.tokens > 0 { fmt::tokens(r.tokens) } else { "—".into() })),
                    (t!("Kv.Tools"), val(&t, r.tool_calls.to_string())),
                ];
                if let Some(m) = &r.model {
                    rows.push((t!("Kv.Model"), val(&t, m)));
                }
                insp::kv(ui, &t, ("wf-kv", tool_id), rows);
            });
        } else {
            insp::section(ui, &t, false, |ui| {
                ui.spinner();
            });
        }
        insp::section(ui, &t, false, |ui| {
            widgets::cap(ui, &t, &t!("Wf.Phases"));
            let phases = st.run.as_ref().map(|r| r.phases.clone()).unwrap_or_default();
            if phases.is_empty() {
                insp::para(ui, &t, &t!("Wf.NoPhases"));
            }
            for (i, p) in phases.iter().enumerate() {
                let title = match p.detail.as_ref().filter(|d| !d.is_empty()) {
                    Some(d) => format!("{} {d}", p.title),
                    None => p.title.clone(),
                };
                if agent_row(ui, &t, phase_idx == Some(i), 0.0, wf_dot(p.state()), &title, "", &format!("{}/{}", p.done, p.total)).clicked() {
                    pick_phase_to = Some(p.title.clone());
                }
            }
        });
        if let Some(title) = &phase_title {
            insp::section(ui, &t, agent.is_none(), |ui| {
                widgets::cap(ui, &t, &t!("Wf.PhaseAgents", title));
                if agents.is_empty() {
                    insp::para(ui, &t, &t!("Wf.NoAgents"));
                }
                for a in &agents {
                    let k = agent_key(a);
                    let open = st.agent_sel.as_deref() == Some(k.as_str());
                    let r = agent_row(ui, &t, open, 0.0, wf_dot(a.state), &a.label, a.model.as_deref().unwrap_or(""), &wf_meta(a));
                    let r = if a.approx { r.on_hover_text(t!("Wf.Approx")) } else { r };
                    if r.clicked() {
                        st.agent_sel = if open { None } else { Some(k) };
                    }
                }
            });
        }
        if let Some(a) = &agent {
            insp::section(ui, &t, true, |ui| {
                widgets::cap(ui, &t, &t!("Agent.Writes"));
                match st.log.text.clone().filter(|x| !x.is_empty()) {
                    Some(x) => md::show(ui, &t, &x),
                    None => insp::para(ui, &t, &t!(if a.state == WfState::Running { "Agent.NothingYet" } else { "Agent.NoText" })),
                }
                if let Some(res) = a.result.clone().filter(|r| !r.is_empty()) {
                    let shown = workflow::json_result(&res).map(|j| insp::pretty(&j)).unwrap_or(res);
                    widgets::cap(ui, &t, &t!("Wf.Result"));
                    insp::pre(ui, &t, ("wf-res", &a.label), &shown);
                }
                let items: Vec<Item> = st.log.tools.iter().cloned().map(Item::Tool).collect();
                let todos = todo_list::from(&items);
                if !todos.is_empty() {
                    widgets::cap(ui, &t, &format!("{} · {}", t!("Todo.Title"), todo_list::progress(&todos)));
                    for td in &todos {
                        let (mark, c) = match td.status {
                            TodoStatus::InProgress => (widgets::icon("play"), t.accent_ink),
                            TodoStatus::Completed => (widgets::icon("check"), t.success),
                            TodoStatus::Pending => (widgets::icon("ring"), t.text_faint),
                        };
                        ui.horizontal(|ui| {
                            ui.label(RichText::new(mark).color(c));
                            let text = RichText::new(td.label()).font(egui::FontId::proportional(theme::LABEL)).color(t.text_dim);
                            ui.add(egui::Label::new(if td.status == TodoStatus::Completed { text.strikethrough() } else { text }).wrap());
                        });
                    }
                }
                let n = st.log.tools.len();
                if n > 0 {
                    widgets::cap(ui, &t, &format!("{} · {n}", t!("Wf.AgentTools")));
                    if n > MAX_TOOLS {
                        widgets::dim(ui, &t, &t!("Wf.EarlierTools", n - MAX_TOOLS));
                    }
                    for x in st.log.tools.iter().skip(n.saturating_sub(MAX_TOOLS)) {
                        sub_tool(ui, &t, x, &cwd);
                    }
                }
            });
        }
    });
    if let Some(p) = pick_phase_to {
        cx.ui.selected_phase = Some(p);
    }
    if close {
        cx.ui.selected_tool_id = None;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use ccui_shared::model::TextItem;
    use ccui_shared::workflow::WfPhase;
    use serde_json::json;

    fn empty_like() -> ToolItem {
        ToolItem {
            id: String::new(),
            name: String::new(),
            input: Value::Null,
            parent_tool_use_id: None,
            state: ToolState::Running,
            result_text: None,
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

    fn tool(id: &str, name: &str, parent: Option<&str>) -> Item {
        let mut t = empty_like();
        t.id = id.into();
        t.name = name.into();
        t.parent_tool_use_id = parent.map(str::to_string);
        Item::Tool(t)
    }

    fn text(md: &str, parent: &str) -> Item {
        Item::Text(TextItem { markdown: md.into(), parent_tool_use_id: Some(parent.into()), uuid: None, at: None })
    }

    #[test]
    fn map_is_depth_first() {
        let items = vec![
            tool("a", "Agent", None),
            tool("b", "Bash", None),
            tool("a1", "Agent", Some("a")),
            tool("a11", "Task", Some("a1")),
            tool("c", "Agent", None),
            tool("x", "Read", Some("a")),
        ];
        assert_eq!(agent_map(&items), [(0, 0), (2, 1), (3, 2), (4, 0)]);
    }

    #[test]
    fn last_text_falls_back() {
        let mut a = empty_like();
        a.result_text = Some("result".into());
        let kids = [text("one", "a"), tool("t", "Bash", Some("a")), text("two", "a")];
        let refs: Vec<&Item> = kids.iter().collect();
        assert_eq!(last_text(&refs, &a).as_deref(), Some("two"));
        assert_eq!(last_text(&[], &a).as_deref(), Some("result"));
        a.structured = Some(json!({"handbackReport": {"text": "report"}}));
        assert_eq!(last_text(&[], &a).as_deref(), Some("report"));
    }

    #[test]
    fn metas() {
        i18n::set_thread_lang(Some(i18n::Lang::En));
        let mut r = empty_like();
        r.name = "Read".into();
        r.state = ToolState::Done;
        r.structured = Some(json!({"file":{"numLines":12.0}}));
        assert_eq!(done_meta(&r), i18n::plural(12, "Tool.Lines"));
        let mut e = empty_like();
        e.name = "Edit".into();
        e.structured = Some(json!({"structuredPatch":[{"lines":[" a","-b","+c","+d"]},{"nolines":1}]}));
        assert_eq!(done_meta(&e), "+2 \u{2212}1");
        let mut done = empty_like();
        done.name = "Agent".into();
        done.state = ToolState::Done;
        done.tokens = 1500;
        done.ended_at = Some(Time::default() + chrono::Duration::milliseconds(4500));
        assert_eq!(agent_state(&done, Time::default()), format!("{} · {} · {}", t!("Workflow.Tokens", "2k"), t!("Tool.Done"), fmt::dur(4500)));
        assert_eq!(agent_dot(&done), "done");
        i18n::set_thread_lang(None);
    }

    fn phase(title: &str, total: i32, done: i32, running: i32) -> WfPhase {
        WfPhase { title: title.into(), detail: None, total, done, running, failed: 0 }
    }

    fn run(phases: Vec<WfPhase>) -> WorkflowRun {
        WorkflowRun {
            name: "w".into(),
            description: None,
            run_dir: None,
            phases,
            agents: vec![],
            tokens: 0,
            tool_calls: 0,
            elapsed_ms: 0,
            state: WfState::Running,
            raw_status: None,
            model: None,
            live: true,
        }
    }

    #[test]
    fn phase_choice() {
        let r = run(vec![phase("a", 2, 2, 0), phase("b", 2, 1, 1), phase("c", 0, 0, 0)]);
        assert_eq!(pick_phase(&r, None), Some(1));
        assert_eq!(pick_phase(&r, Some("c")), Some(2));
        assert_eq!(pick_phase(&r, Some("zz")), Some(1));
        let done = run(vec![phase("a", 2, 2, 0), phase("b", 1, 1, 0), phase("c", 0, 0, 0)]);
        assert_eq!(pick_phase(&done, None), Some(1));
        assert_eq!(pick_phase(&run(vec![phase("a", 0, 0, 0)]), None), Some(0));
        assert_eq!(pick_phase(&run(vec![]), None), None);
    }

    #[test]
    fn keys_and_labels() {
        let mut a = WfAgent {
            label: "lint".into(),
            phase: "check".into(),
            state: WfState::Done,
            model: None,
            tokens: 0,
            approx: false,
            tool_calls: 0,
            duration_ms: None,
            agent_id: None,
            result: None,
            raw_state: None,
        };
        assert_eq!(agent_key(&a), "check/lint");
        a.agent_id = Some("a1".into());
        assert_eq!(agent_key(&a), "a1");
        assert_eq!(wf_dot(WfState::Unknown), "exited");
        assert_eq!(wf_label(WfState::Failed, Some("cancelled")), "cancelled");
    }
}
