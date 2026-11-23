//! Les agents dans le fil : les appels Agent qui se suivent en arbre (`workflow.rs` de ClaudeCodeUI : « Sous-agents »,
//! une ligne par agent qui s'ouvre sur ses propres outils, jusqu'à cinq niveaux) et la carte d'un Workflow
//! d'ultracode (`workflow_card.rs` : ses phases et leurs agents faits / prévus, relue chaque seconde tant qu'il
//! tourne). Un clic choisit l'agent ou la phase dans l'inspecteur.

use std::collections::{HashMap, HashSet};

use ccui_shared::model::{Time, ToolItem, ToolState};
use ccui_shared::workflow::{TOOL, WfState, WorkflowRun};
use ccui_shared::{fmt, i18n, json, t, tool_kinds};
use egui::{Color32, CornerRadius, FontId, Frame, Rect, Sense, Stroke, Ui, Vec2};

use super::tool_row::{self, ROW_H, kind_color, line_galley, paint_left, paint_meta};
use crate::backend::{Backend, Task};
use crate::state::{SessionHandle, UiState};
use crate::theme::{self, Tokens};
use crate::widgets::icon;

pub const MAX_DEPTH: usize = 5;

fn elapsed_ms(a: &ToolItem, now: Time) -> i64 {
    (a.ended_at.unwrap_or(now) - a.started_at).num_milliseconds()
}

/// `Head` : « n agents · durée » (de la plus ancienne entrée à la dernière fin, ou maintenant).
pub fn head(agents: &[&ToolItem], now: Time) -> String {
    let n = i18n::plural(agents.len() as i64, "Agents");
    let end = agents.iter().map(|a| a.ended_at.unwrap_or(now)).max();
    let start = agents.iter().map(|a| a.started_at).min();
    let ms = match (end, start) {
        (Some(e), Some(s)) => (e - s).num_milliseconds(),
        _ => 0,
    };
    format!("{n} · {}", fmt::dur(ms))
}

/// `Workflow.Dot(a)` : fait « done », en cours « run », attend « wait », sinon « err ».
pub fn dot(agent: &ToolItem) -> &'static str {
    match agent.state {
        ToolState::Done => "done",
        ToolState::Running => "run",
        ToolState::Waiting => "wait",
        _ => "err",
    }
}

/// `Workflow.State(a)` : « 2k jetons · » quand il y en a, puis l'état (et la durée en cours ou finie).
pub fn state(agent: &ToolItem, now: Time) -> String {
    let tokens = if agent.tokens > 0 { format!("{} · ", t!("Workflow.Tokens", fmt::tokens(agent.tokens))) } else { String::new() };
    let word = match agent.state {
        ToolState::Running => format!("{} · {}", t!("Status.Running"), fmt::dur(elapsed_ms(agent, now))),
        ToolState::Done => format!("{} · {}", t!("Tool.Done"), fmt::dur(elapsed_ms(agent, now))),
        ToolState::Waiting => t!("Tool.Waiting"),
        ToolState::Denied => t!("Tool.Denied"),
        ToolState::NotRun => t!("Tool.NotRun"),
        ToolState::Error => t!("Tool.Error"),
    };
    format!("{tokens}{word}")
}

/// La couleur d'un point d'état (« done », « run », « wait », « err », « exited », « idle »).
pub fn dot_color(t: &Tokens, d: &str) -> (Color32, bool) {
    match d {
        "done" => (t.success, false),
        "run" => (t.success, true),
        "wait" => (t.warning, true),
        "err" => (t.danger, true),
        _ => (t.text_faint, false),
    }
}

fn paint_dot(ui: &Ui, center: egui::Pos2, c: (Color32, bool)) {
    if c.1 {
        ui.painter().circle_filled(center, 3.5, c.0);
    } else {
        ui.painter().circle_stroke(center, 3.2, Stroke::new(1.3, c.0));
    }
}

/// Le fond d'une ligne cliquable (choisie : relevée et marquée de l'accent ; survolée).
fn row_bg(ui: &Ui, t: &Tokens, rect: Rect, selected: bool, hovered: bool) {
    if selected {
        ui.painter().rect_filled(rect, CornerRadius::ZERO, t.raised);
        ui.painter().rect_filled(Rect::from_min_size(rect.min, Vec2::new(2.0, rect.height())), CornerRadius::ZERO, t.accent);
    } else if hovered {
        ui.painter().rect_filled(rect, CornerRadius::ZERO, t.hover.gamma_multiply(0.6));
    }
}

/// Ce que partagent les lignes d'un arbre d'agents.
pub struct Tree<'a> {
    pub t: &'a Tokens,
    pub h: &'a SessionHandle,
    pub children: &'a HashMap<String, Vec<usize>>,
    pub open: &'a mut HashSet<String>,
    pub uis: &'a mut UiState,
    pub now: Time,
}

impl Tree<'_> {
    fn click(&mut self, id: &str) {
        if !self.open.remove(id) {
            self.open.insert(id.to_string());
        }
        // (l'inspecteur n'est pas forcé ouvert, à la différence d'une ligne d'outil)
        self.uis.selected_tool_id = Some(id.to_string());
    }

    /// Les outils d'un agent, sans SubagentHandback.
    fn kids(&self, id: &str) -> Vec<&ToolItem> {
        self.children.get(id).map_or_else(Vec::new, |ix| ix.iter().filter_map(|&i| self.h.tool_at(i)).filter(|t| t.name != "SubagentHandback").collect())
    }

    /// Une ligne d'agent de tête : chevron, point, description et genre, état à droite.
    fn agent_row(&mut self, ui: &mut Ui, a: &ToolItem) {
        let t = self.t;
        let is_open = self.open.contains(&a.id);
        let sel = self.uis.selected_tool_id.as_deref() == Some(a.id.as_str());
        let (rect, resp) = ui.allocate_exact_size(Vec2::new(ui.available_width(), ROW_H + 4.0), Sense::click());
        row_bg(ui, t, rect, sel, resp.hovered());
        let mut x = rect.left() + 12.0;
        paint_left(ui, rect, x, line_galley(ui, icon(if is_open { "chev-down" } else { "chev-right" }), FontId::proportional(theme::CAPTION), t.text_faint, 20.0));
        x += 20.0;
        paint_dot(ui, egui::pos2(x + 4.0, rect.center().y), dot_color(t, dot(a)));
        x += 16.0;
        let meta = state(a, self.now);
        let mg = line_galley(ui, &meta, theme::mono(theme::CAPTION), t.text_faint, 260.0);
        let right = rect.right() - 12.0 - mg.size().x;
        paint_left(ui, rect, right, mg);
        let desc = json::str(&a.input, "description").map_or_else(|| t!("Agent.Title"), str::to_string);
        let dg = line_galley(ui, &desc, theme::medium(theme::LABEL), t.text, (right - x - 16.0).max(20.0));
        x = paint_left(ui, rect, x, dg) + 8.0;
        if let Some(k) = json::str(&a.input, "subagent_type") {
            paint_left(ui, rect, x, line_galley(ui, k, theme::mono(theme::CAPTION), t.text_faint, (right - x - 12.0).max(0.0)));
        }
        if resp.clicked() {
            self.click(&a.id);
        }
        if is_open && !self.kids(&a.id).is_empty() {
            self.kids_rows(ui, &a.id, 1);
        }
    }

    /// `Kids(a, depth)` : les outils d'un agent ; un agent enfant s'ouvre à son tour, un niveau plus bas.
    fn kids_rows(&mut self, ui: &mut Ui, id: &str, depth: usize) {
        let t = self.t;
        let kids: Vec<ToolItem> = self.kids(id).into_iter().cloned().collect();
        let indent = 36.0 + (depth - 1) as f32 * 16.0;
        for k in kids {
            let (rect, resp) = ui.allocate_exact_size(Vec2::new(ui.available_width(), 26.0), Sense::click());
            let agent = tool_kinds::is_agent(&k.name) && depth < MAX_DEPTH;
            let sel = self.uis.selected_tool_id.as_deref() == Some(k.id.as_str());
            if agent {
                row_bg(ui, t, rect, sel, resp.hovered());
            }
            let mut x = rect.left() + indent;
            let is_open = agent && self.open.contains(&k.id);
            if agent {
                paint_left(ui, rect, x - 14.0, line_galley(ui, icon(if is_open { "chev-down" } else { "chev-right" }), FontId::proportional(theme::CAPTION), t.text_faint, 14.0));
            }
            paint_left(ui, rect, x, line_galley(ui, &tool_kinds::label(&k.name), theme::medium(theme::CODE - 0.5), kind_color(t, &k.name), 70.0));
            x += 76.0;
            let (meta, right) = if agent {
                let g = line_galley(ui, &state(&k, self.now), theme::mono(theme::CAPTION), t.text_faint, 240.0);
                let r = rect.right() - 12.0 - g.size().x;
                paint_left(ui, rect, r, g);
                (None, r)
            } else {
                let m = tool_row::meta(&k);
                let r = rect.right() - 12.0 - tool_row::meta_width(ui, t, &m);
                (Some(m), r)
            };
            let target = if agent {
                let d = json::string(&k.input, "description");
                match json::str(&k.input, "subagent_type") {
                    Some(s) => format!("{d}  {s}"),
                    None => d,
                }
            } else {
                tool_kinds::target(&k.name, &k.input, &self.h.meta.cwd)
            };
            paint_left(ui, rect, x, line_galley(ui, &target, theme::mono(theme::CODE - 0.5), t.text, (right - x - 12.0).max(10.0)));
            if let Some(m) = meta {
                paint_meta(ui, t, rect, rect.right() - 12.0, &m);
            }
            if agent {
                if resp.clicked() {
                    self.click(&k.id);
                }
                if is_open && !self.kids(&k.id).is_empty() {
                    self.kids_rows(ui, &k.id, depth + 1);
                }
            }
        }
    }
}

/// Les appels Agent qui se suivent (`Workflow`) : l'en-tête « Sous-agents » et une ligne par agent.
pub fn agents(ui: &mut Ui, tree: &mut Tree<'_>, ix: &[usize]) {
    let t = tree.t;
    let list: Vec<ToolItem> = ix.iter().filter_map(|&i| tree.h.tool_at(i)).cloned().collect();
    if list.iter().any(|a| a.ended_at.is_none()) {
        ui.ctx().request_repaint_after(std::time::Duration::from_secs(1));
    }
    Frame::new().stroke(Stroke::new(1.0, t.card_border)).corner_radius(CornerRadius::same(theme::R_LG)).show(ui, |ui| {
        ui.set_width(ui.available_width());
        ui.spacing_mut().item_spacing.y = 0.0;
        let (rect, _) = ui.allocate_exact_size(Vec2::new(ui.available_width(), 40.0), Sense::hover());
        let mut x = paint_left(ui, rect, rect.left() + 14.0, line_galley(ui, icon("agents"), FontId::proportional(theme::BODY), t.text_dim, 30.0)) + 8.0;
        x = paint_left(ui, rect, x, line_galley(ui, &t!("Workflow.Subagents"), theme::semibold(theme::BODY), t.text, 300.0)) + 10.0;
        let tag = line_galley(ui, &t!("Workflow.Tag"), theme::mono(theme::CAPTION), t.text_dim, 200.0);
        let tr = Rect::from_center_size(egui::pos2(x + tag.size().x / 2.0 + 5.0, rect.center().y), tag.size() + Vec2::new(10.0, 2.0));
        ui.painter().rect_stroke(tr, CornerRadius::same(4), Stroke::new(1.0, t.card_border), egui::StrokeKind::Inside);
        paint_left(ui, rect, x + 5.0, tag);
        let refs: Vec<&ToolItem> = list.iter().collect();
        let hg = line_galley(ui, &head(&refs, tree.now), theme::mono(theme::CAPTION), t.text_faint, 300.0);
        paint_left(ui, rect, rect.right() - 14.0 - hg.size().x, hg);
        for a in &list {
            let line = ui.available_rect_before_wrap();
            ui.painter().hline(line.x_range(), line.top(), Stroke::new(1.0, t.separator));
            tree.agent_row(ui, a);
        }
    });
}

// ------------------------------------------------------------------------------------------- Workflow (ultracode)

/// `Head(r)` : « n/m agents · durée », puis l'état quand il a échoué ou est inconnu.
pub fn wf_head(r: &WorkflowRun) -> String {
    let mut s = format!("{} · {}", t!("Wf.AgentsDone", r.done(), r.agents.len()), fmt::dur(r.elapsed_ms));
    if matches!(r.state, WfState::Failed | WfState::Unknown) {
        s.push_str(" · ");
        s.push_str(&wf_label(r.state, r.raw_status.as_deref()));
    }
    s
}

/// `WorkflowCard.Dot(s, t)` : fait, en cours, échoué, inconnu (« exited ») ; pas commencé : « wait » quand l'appel
/// attend une autorisation, « idle » sinon.
pub fn wf_dot(state: WfState, tool: Option<&ToolItem>) -> &'static str {
    match state {
        WfState::Done => "done",
        WfState::Running => "run",
        WfState::Failed => "err",
        WfState::Unknown => "exited",
        WfState::Waiting => {
            if tool.is_some_and(|t| t.state == ToolState::Waiting) {
                "wait"
            } else {
                "idle"
            }
        }
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

/// La lecture d'un Workflow (`workflow_run`) : relue quand l'appel change, puis chaque seconde tant qu'il tourne.
#[derive(Default)]
pub struct WfPoll {
    pub run: Option<WorkflowRun>,
    task: Option<Task<WorkflowRun>>,
    /// L'appel tel qu'il était à la dernière lecture (état, fin, progrès).
    seen: Option<(ToolState, Option<Time>, Option<String>)>,
    at: f64,
}

impl WfPoll {
    pub fn poll(&mut self, be: &Backend, ui: &Ui, tool: &ToolItem) {
        if let Some(r) = self.task.as_ref().and_then(Task::take) {
            self.task = None;
            self.run = Some(r);
        }
        let now = ui.input(|i| i.time);
        let sig = (tool.state, tool.ended_at, tool.progress.as_ref().map(|p| format!("{p:?}")));
        let live = self.run.as_ref().is_some_and(|r| r.live);
        let due = self.seen.as_ref() != Some(&sig) || (live && now - self.at >= 1.0);
        if due && self.task.is_none() {
            self.seen = Some(sig);
            self.at = now;
            let tool = tool.clone();
            self.task = Some(be.blocking(move || ccui_core::workflow_runs::read(&tool, chrono::Utc::now())));
        }
        if live {
            ui.ctx().request_repaint_after(std::time::Duration::from_millis(1000));
        }
    }
}

/// La carte d'un Workflow : l'en-tête (point, « workflow · nom », agents faits et durée) et une ligne par phase ;
/// l'en-tête ou une phase choisit l'appel (et la phase) dans l'inspecteur.
pub fn wf_card(ui: &mut Ui, t: &Tokens, uis: &mut UiState, tool: &ToolItem, run: Option<&WorkflowRun>) {
    let Some(r) = run else { return };
    let sel = uis.selected_tool_id.as_deref() == Some(tool.id.as_str());
    let mut pick: Option<Option<String>> = None;
    Frame::new().stroke(Stroke::new(1.0, t.card_border)).corner_radius(CornerRadius::same(theme::R_LG)).show(ui, |ui| {
        ui.set_width(ui.available_width());
        ui.spacing_mut().item_spacing.y = 0.0;
        let (rect, resp) = ui.allocate_exact_size(Vec2::new(ui.available_width(), 40.0), Sense::click());
        row_bg(ui, t, rect, sel && uis.selected_phase.is_none(), resp.hovered());
        paint_dot(ui, egui::pos2(rect.left() + 18.0, rect.center().y), dot_color(t, wf_dot(r.state, Some(tool))));
        let hg = line_galley(ui, &wf_head(r), theme::mono(theme::CAPTION), t.text_faint, 320.0);
        let right = rect.right() - 14.0 - hg.size().x;
        paint_left(ui, rect, right, hg);
        paint_left(ui, rect, rect.left() + 30.0, line_galley(ui, &format!("{} · {}", TOOL.to_lowercase(), r.name), theme::semibold(theme::BODY), t.text, (right - rect.left() - 44.0).max(20.0)));
        let lbl = match tool.state {
            ToolState::Waiting => t!("Tool.Waiting"),
            ToolState::Denied => t!("Tool.Denied"),
            _ => wf_label(r.state, r.raw_status.as_deref()),
        };
        let resp = resp.on_hover_text(lbl);
        if resp.clicked() {
            pick = Some(None);
        }
        for p in &r.phases {
            let line = ui.available_rect_before_wrap();
            ui.painter().hline(line.x_range(), line.top(), Stroke::new(1.0, t.separator));
            let on = sel && uis.selected_phase.as_deref() == Some(p.title.as_str());
            let (rect, resp) = ui.allocate_exact_size(Vec2::new(ui.available_width(), ROW_H), Sense::click());
            row_bg(ui, t, rect, on, resp.hovered());
            paint_dot(ui, egui::pos2(rect.left() + 18.0, rect.center().y), dot_color(t, wf_dot(p.state(), None)));
            let mg = line_galley(ui, &format!("{}/{}", p.done, p.total), theme::mono(theme::CAPTION), t.text_faint, 100.0);
            let right = rect.right() - 14.0 - mg.size().x;
            paint_left(ui, rect, right, mg);
            paint_left(ui, rect, rect.left() + 30.0, line_galley(ui, &p.title, theme::medium(theme::LABEL), t.text, (right - rect.left() - 44.0).max(20.0)));
            // (la barre d'avancement de la phase, sous la ligne)
            if p.total > 0 {
                let frac = (p.done as f32 / p.total as f32).clamp(0.0, 1.0);
                let bar = Rect::from_min_size(egui::pos2(rect.left() + 30.0, rect.bottom() - 4.0), Vec2::new((rect.width() - 44.0) * frac, 2.0));
                ui.painter().rect_filled(bar, CornerRadius::same(1), if p.failed > 0 { t.danger } else { t.accent });
            }
            if resp.on_hover_text(wf_label(p.state(), None)).clicked() {
                pick = Some(Some(p.title.clone()));
            }
        }
    });
    if let Some(phase) = pick {
        uis.selected_phase = phase;
        uis.selected_tool_id = Some(tool.id.clone());
        uis.inspector_open = true;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    fn agent(state: ToolState, tokens: i64, start: i64, end: Option<i64>) -> ToolItem {
        let at = |ms| Time::default() + chrono::Duration::milliseconds(ms);
        ToolItem {
            id: "a".into(),
            name: "Agent".into(),
            input: json!({}),
            parent_tool_use_id: None,
            state,
            result_text: None,
            structured: None,
            started_at: at(start),
            ended_at: end.map(at),
            task_id: None,
            task_status: None,
            progress: None,
            tokens,
            sub_tool_uses: 0,
            background: false,
            model: None,
        }
    }

    #[test]
    fn states_and_head() {
        let now = Time::default() + chrono::Duration::milliseconds(10_000);
        let done = agent(ToolState::Done, 1500, 0, Some(4500));
        assert_eq!(state(&done, now), format!("{} · {} · {}", t!("Workflow.Tokens", "2k"), t!("Tool.Done"), fmt::dur(4500)));
        assert_eq!(state(&agent(ToolState::Waiting, 0, 0, None), now), t!("Tool.Waiting"));
        assert_eq!(dot(&agent(ToolState::Denied, 0, 0, None)), "err");
        let running = agent(ToolState::Running, 0, 2000, None);
        assert_eq!(head(&[&done, &running], now), format!("{} · {}", i18n::plural(2, "Agents"), fmt::dur(10_000)));
    }

    #[test]
    fn wf_dots_and_labels() {
        assert_eq!(wf_dot(WfState::Unknown, None), "exited");
        assert_eq!(wf_dot(WfState::Waiting, None), "idle");
        assert_eq!(wf_label(WfState::Failed, Some("cancelled")), "cancelled");
        assert_eq!(wf_label(WfState::Done, None), t!("Tool.Done"));
    }
}
