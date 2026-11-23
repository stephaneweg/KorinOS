//! Les tâches d'arrière-plan de la session (`session/tasks_panel.rs` et `task_actions.rs` de ClaudeCodeUI).
//!
//! - Les commandes shell et les Monitor (task_type local_bash) : commande, état, durée et la fin de leur sortie, lue
//!   par `get_task_output` toutes les 2 s tant qu'une tourne (et une fois encore à la fin), et Arrêter
//!   (`stop_task`). Les autres (un appel MCP, un moniteur MCP ou WebSocket, un coéquipier) montrent leur type et
//!   Arrêter seulement. Celles qui tournent d'abord, puis les 5 dernières finies, qu'« Effacer » retire.
//! - `TaskActions` : une commande Bash ou un sous-agent en cours se met en arrière-plan (Ctrl B du terminal), un
//!   sous-agent s'arrête (en-têtes de l'outil et de l'agent choisis).

use std::collections::{HashMap, HashSet};

use ccui_shared::model::{BgTask, TaskOutput, ToolItem, ToolState};
use ccui_shared::{fmt, i18n, json, session_rules, t, tool_kinds};
use egui::{RichText, Ui};

use crate::app::Cx;
use crate::backend::Task;
use crate::session::inspector as insp;
use crate::theme;
use crate::widgets;

/// Celles qui tournent d'abord, puis les 5 dernières finies.
pub fn shown(tasks: &[BgTask]) -> Vec<&BgTask> {
    let ended: Vec<&BgTask> = tasks.iter().filter(|t| !t.running()).collect();
    tasks.iter().filter(|t| t.running()).chain(ended[ended.len().saturating_sub(5)..].iter().copied()).collect()
}

/// Un arrêt demandé n'est pas un échec : point neutre, rouge seulement pour « failed » et les états inconnus.
pub fn dot(t: &BgTask) -> &'static str {
    if t.running() {
        return "run";
    }
    match t.status.as_str() {
        "completed" => "idle",
        "killed" | "stopped" => "exited",
        _ => "err",
    }
}

/// La clé du mot d'état.
pub fn label_key(status: &str) -> &'static str {
    match status {
        "running" | "pending" => "Bg.Running",
        "paused" => "Bg.Paused",
        "completed" => "Bg.Completed",
        "failed" => "Bg.Failed",
        "killed" | "stopped" => "Bg.Stopped",
        _ => "Bg.Ended",
    }
}

/// Ce que fait une réponse de `session_task_output` : (la sortie à garder, la tâche est lue pour de bon).
pub fn poll_step(out: &TaskOutput, running: bool) -> (Option<&str>, bool) {
    match out {
        TaskOutput::Text(o) => (Some(o), !running),
        TaskOutput::RequestFailed(_) => (None, !running),
        TaskOutput::Timeout | TaskOutput::NoProcess => (None, false),
    }
}

type Polled = Vec<(String, bool, Result<TaskOutput, String>)>;

#[derive(Default)]
pub struct TasksState {
    output: HashMap<String, String>,
    stopping: HashSet<String>,
    /// Les tâches finies dont la dernière sortie est lue (ou perdue).
    fin: HashSet<String>,
    error: Option<String>,
    poll: Option<Task<Polled>>,
    last_poll: Option<f64>,
    stops: Vec<(String, Task<Result<(), String>>)>,
}

pub fn show(ui: &mut Ui, cx: &mut Cx, st: &mut TasksState, id: &str) {
    let t = cx.t;
    let Some(h) = cx.app.session(id) else { return };
    let now = ui.input(|i| i.time);
    let has_process = h.meta.has_process;

    // les sorties lues, les arrêts finis
    if let Some(res) = st.poll.as_ref().and_then(Task::take) {
        st.poll = None;
        for (task, running, out) in res {
            let Ok(out) = out else { continue };
            let (text, fin) = poll_step(&out, running);
            if let Some(o) = text {
                st.output.insert(task.clone(), o.to_string());
            }
            if fin {
                st.fin.insert(task);
            }
        }
    }
    st.stops.retain(|(task, tk)| match tk.take() {
        Some(r) => {
            if let Err(e) = r {
                st.error = Some(t!("Bg.StopFailed", e));
            }
            st.stopping.remove(task);
            false
        }
        None => true,
    });
    // toutes les 2 s : la sortie des shells et des Monitor
    if has_process && st.poll.is_none() && st.last_poll.is_none_or(|l| now - l >= 2.0) {
        let todo: Vec<(String, bool)> = h
            .meta
            .tasks
            .iter()
            .filter(|x| x.kind.is_none() && (x.running() || !st.fin.contains(&x.id)))
            .map(|x| (x.id.clone(), x.running()))
            .collect();
        if !todo.is_empty()
            && let Ok(s) = cx.be.session(id)
        {
            st.last_poll = Some(now);
            st.poll = Some(cx.be.task(async move {
                let mut out = Vec::new();
                for (task, running) in todo {
                    let r = s.task_output(&task).await;
                    out.push((task, running, r));
                }
                out
            }));
        }
    }

    let list = shown(&h.meta.tasks);
    let running = list.iter().filter(|x| x.running()).count();
    let ended = list.len() > running;
    if running > 0 {
        ui.ctx().request_repaint_after(std::time::Duration::from_secs(1));
    }
    let rows: Vec<(BgTask, Option<(String, Option<String>)>)> = list
        .into_iter()
        .map(|x| {
            let tool = session_rules::tool(&h.items, &x.tool_use_id).map(|tl| (tl.name.clone(), json::str(&tl.input, "command").map(str::to_string)));
            (x.clone(), tool)
        })
        .collect();

    let mut stop: Option<String> = None;
    let mut clear = false;
    insp::section(ui, &t, false, |ui| {
        ui.horizontal(|ui| {
            widgets::cap(ui, &t, &format!("{} · {}", t!("Bg.Tasks"), t!("Bg.RunningN", running)));
            if ended {
                ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                    if widgets::ghost(ui, &t, &t!("Bg.Clear")).clicked() {
                        clear = true;
                    }
                });
            }
        });
        for (task, tool) in rows {
            let nm = match (&task.kind, tool.as_ref().map(|x| x.0.clone())) {
                (Some(k), _) => format!("{} · {}", i18n::name("Bg.Type.", Some(k)), task.description),
                (None, Some(name)) => format!("{name} · {}", task.description),
                (None, None) => task.description.clone(),
            };
            let end = task.ended_at.unwrap_or_else(chrono::Utc::now);
            let elapsed = format!("{} · {}", t!(label_key(&task.status)), fmt::dur((end - task.started_at).num_milliseconds()));
            let cid = ui.id().with(("bg-row", &task.id));
            egui::collapsing_header::CollapsingState::load_with_default_open(ui.ctx(), cid, task.running())
                .show_header(ui, |ui| {
                    insp::state_dot(ui, &t, dot(&task));
                    ui.add(egui::Label::new(RichText::new(&nm).font(theme::medium(theme::LABEL)).color(t.text)).truncate());
                    ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                        widgets::mono_dim(ui, &t, &elapsed);
                    });
                })
                .body(|ui| {
                    if let Some(c) = tool.as_ref().and_then(|x| x.1.clone()) {
                        insp::pre(ui, &t, ("bg-cmd", &task.id), &format!("$ {c}"));
                    }
                    if task.kind.is_none() {
                        let body = match st.output.get(&task.id) {
                            Some(o) if !o.is_empty() => BgTask::tail(o, 12),
                            _ => t!("Bg.NoOutput"),
                        };
                        insp::pre(ui, &t, ("bg-out", &task.id), &body);
                    }
                    if task.running() && has_process {
                        let busy = st.stopping.contains(&task.id);
                        if ui.add_enabled(!busy, egui::Button::new(format!("{} {}", widgets::icon("stop"), t!("Bg.Stop")))).on_hover_text(t!("Bg.StopLabel", &task.description)).clicked() {
                            stop = Some(task.id.clone());
                        }
                    }
                });
        }
        if let Some(e) = &st.error {
            insp::errbox(ui, &t, e);
        }
    });

    // (le nouvel état arrive par task_updated ; la réponse dit seulement que la demande est prise)
    if let Some(task) = stop
        && st.stopping.insert(task.clone())
    {
        st.error = None;
        match cx.be.session(id) {
            Ok(s) => {
                let tk = task.clone();
                st.stops.push((task, cx.be.task(async move { s.stop_task(&tk).await })));
            }
            Err(e) => {
                st.error = Some(t!("Bg.StopFailed", e));
                st.stopping.remove(&task);
            }
        }
    }
    if clear && let Ok(s) = cx.be.session(id) {
        s.clear_ended_tasks();
    }
}

// ------------------------------------------------------------------------------------------- TaskActions

/// Mettre en arrière-plan, arrêter : l'appel en cours et son erreur.
#[derive(Default)]
pub struct TaskActions {
    busy: Option<Task<Result<(), String>>>,
    error: Option<String>,
}

/// Les boutons Arrière-plan (Ctrl B) / Arrêter d'une commande shell ou d'un sous-agent en cours (à droite d'un `.ph`).
pub fn task_actions(ui: &mut Ui, cx: &Cx, st: &mut TaskActions, id: &str, tool: &ToolItem) {
    let t = cx.t;
    if let Some(r) = st.busy.as_ref().and_then(Task::take) {
        st.busy = None;
        if let Err(e) = r {
            st.error = Some(t!("Composer.ActionFailed", e));
        }
    }
    let (shell, agent) = (tool_kinds::is_shell(&tool.name), tool_kinds::is_agent(&tool.name));
    if tool.state != ToolState::Running || !(shell || agent) {
        return;
    }
    let busy = st.busy.is_some();
    if let Some(e) = &st.error {
        ui.label(RichText::new(e).color(t.danger).font(egui::FontId::proportional(theme::CAPTION)));
    }
    if let Some(task) = tool.task_id.clone().filter(|_| agent)
        && ui.add_enabled(!busy, egui::Button::new(RichText::new(format!("{} {}", widgets::icon("stop"), t!("Bg.Stop"))).color(t.text_dim)).frame(false)).clicked()
    {
        st.error = None;
        match cx.be.session(id) {
            Ok(s) => st.busy = Some(cx.be.task(async move { s.stop_task(&task).await })),
            Err(e) => st.error = Some(t!("Composer.ActionFailed", e)),
        }
    }
    if !tool.background
        && ui
            .add_enabled(!busy, egui::Button::new(RichText::new(format!("{} {}", widgets::icon("arrow-down"), t!("Task.Background"))).color(t.text_dim)).frame(false))
            .on_hover_text(t!("Task.BackgroundHint"))
            .clicked()
    {
        st.error = None;
        let tid = tool.id.clone();
        match cx.be.session(id) {
            Ok(s) => st.busy = Some(cx.be.task(async move { s.background(&tid).await })),
            Err(e) => st.error = Some(t!("Composer.ActionFailed", e)),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use ccui_shared::model::Time;

    fn task(id: &str, status: &str, ended: Option<i64>) -> BgTask {
        BgTask {
            id: id.into(),
            tool_use_id: "t".into(),
            description: "d".into(),
            started_at: Time::default(),
            kind: None,
            status: status.into(),
            ended_at: ended.map(|s| Time::default() + chrono::Duration::seconds(s)),
        }
    }

    #[test]
    fn order_and_labels() {
        let mut ts = vec![task("a", "completed", Some(1)), task("r", "running", None)];
        for i in 0..6 {
            ts.push(task(&format!("e{i}"), "killed", Some(10 + i)));
        }
        let ids: Vec<&str> = shown(&ts).iter().map(|t| t.id.as_str()).collect();
        assert_eq!(ids, ["r", "e1", "e2", "e3", "e4", "e5"]);
        assert_eq!(dot(&ts[1]), "run");
        assert_eq!(dot(&ts[0]), "idle");
        assert_eq!(dot(&ts[2]), "exited");
        assert_eq!(dot(&task("x", "failed", Some(1))), "err");
        assert_eq!(label_key("pending"), "Bg.Running");
        assert_eq!(label_key("weird"), "Bg.Ended");
    }

    #[test]
    fn task_output_answers() {
        assert_eq!(poll_step(&TaskOutput::Text("x".into()), true), (Some("x"), false));
        assert_eq!(poll_step(&TaskOutput::Text("x".into()), false), (Some("x"), true));
        assert_eq!(poll_step(&TaskOutput::RequestFailed("gone".into()), false), (None, true));
        assert_eq!(poll_step(&TaskOutput::RequestFailed("gone".into()), true), (None, false));
        assert_eq!(poll_step(&TaskOutput::Timeout, false), (None, false));
        assert_eq!(poll_step(&TaskOutput::NoProcess, false), (None, false));
    }
}
