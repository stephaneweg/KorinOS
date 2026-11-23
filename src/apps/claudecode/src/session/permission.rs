//! La demande d'autorisation en attente (`session/permission_card.rs` de ClaudeCodeUI) : le `can_use_tool` de la
//! session. Selon l'outil : les questions (`AskUserQuestion`, `forms.rs`), un formulaire MCP (elicitation,
//! `forms.rs`), le plan (`ExitPlanMode` : approuver en mode auto ou en relisant, ou continuer à planifier avec un
//! retour), sinon la carte ordinaire : titre, quel sous-agent demande, pourquoi la CLI demande, le diff (Edit, Write,
//! MultiEdit), la commande ou l'entrée, Autoriser / Toute la session (et où garder la règle : session, local, projet,
//! utilisateur) / Refuser. Les touches comme l'original : Entrée autorise, Maj Entrée toute la session, Suppr refuse
//! (inertes 300 ms sur une carte nouvelle, et seulement quand on ne tape pas dans un champ). En bas, les autres
//! décisions qui attendent, dans toutes les sessions.

use ccui_shared::api::AnswerArgs;
use ccui_shared::edit_diff::DiffLine;
use ccui_shared::model::{Decision, PendingPermission};
use ccui_shared::{ask_user, elicit, i18n, json, permission_rules, session_rules, t, tool_kinds};
use egui::{Key, Modifiers, RichText, Ui};
use serde_json::Value;

use crate::app::Cx;
use crate::backend::{Backend, Task};
use crate::md;
use crate::session::forms;
use crate::session::inspector::{self as insp, kind_color};
use crate::theme;
use crate::widgets::{self, Tone};

pub const DESTINATIONS: [&str; 4] = ["session", "localSettings", "projectSettings", "userSettings"];

// ------------------------------------------------------------------------------------------- règles pures

fn suggestions(p: &PendingPermission) -> &[Value] {
    p.suggestions.as_ref().and_then(Value::as_array).map_or(&[], Vec::as_slice)
}

/// « Toujours autoriser » : des suggestions, sauf si la CLI elle-même n'offre pas de « toujours » pour cet appel.
pub fn can_allow_session(p: &PendingPermission) -> bool {
    !suggestions(p).is_empty() && !p.info.as_ref().is_some_and(|i| i.no_always_allow)
}

/// Une suggestion setMode passe par set_permission_mode (pas de destination à choisir).
pub fn sets_mode(p: &PendingPermission) -> bool {
    suggestions(p).iter().any(|x| json::str(x, "type") == Some("setMode"))
}

/// Ce qu'accorde « Toujours autoriser » : un setMode d'abord, sinon les règles et les dossiers.
pub fn session_grant(p: &PendingPermission) -> String {
    let sg = suggestions(p);
    if let Some(mode) = sg.iter().find(|x| json::str(x, "type") == Some("setMode")).and_then(|x| json::str(x, "mode")) {
        return t!("Perm.GrantMode", i18n::name("Mode.Name.", Some(mode)));
    }
    let parts: Vec<String> = sg
        .iter()
        .flat_map(|x| {
            let key = if json::str(x, "type") == Some("addDirectories") { "directories" } else { "rules" };
            match json::prop(x, key) {
                Some(Value::Array(a)) => a
                    .iter()
                    .map(|v| match v {
                        Value::String(s) => s.clone(),
                        _ => {
                            let tool = json::string(v, "toolName");
                            match json::str(v, "ruleContent").filter(|c| !c.is_empty()) {
                                Some(c) => format!("{tool}({c})"),
                                None => tool,
                            }
                        }
                    })
                    .collect::<Vec<_>>(),
                _ => Vec::new(),
            }
        })
        .collect();
    t!("Perm.GrantRules", parts.join(", "))
}

/// Pourquoi la CLI demande : sa raison, sinon la règle « ask » qui a joué, sinon ce que dit le type de raison.
pub fn why(p: &PendingPermission) -> Option<String> {
    let i = p.info.as_ref()?;
    if let Some(r) = i.reason.as_deref().filter(|r| !r.is_empty()) {
        return Some(permission_rules::plain(r));
    }
    if let Some(rule) = i.ask_rule.as_deref() {
        return Some(t!("Perm.WhyAskRule", permission_rules::plain(rule), i18n::name("Perm.Dest.", i.ask_rule_source.as_deref())));
    }
    match i.reason_type.as_deref() {
        Some("rule") => Some(t!("Perm.WhyRule")),
        Some("workingDir") => Some(t!("Perm.WhyWorkingDir")),
        _ => None,
    }
}

fn file_name(p: &PendingPermission) -> String {
    let f = json::str(&p.input, "file_path").unwrap_or("");
    f.rsplit(['/', '\\']).next().unwrap_or("").to_string()
}

/// Le titre de la carte ordinaire (`target_exists` a été vu une fois, à l'arrivée de la demande).
pub fn title(p: &PendingPermission) -> String {
    if let Some(t) = p.info.as_ref().and_then(|i| i.title.clone()).filter(|t| !t.is_empty()) {
        return t;
    }
    match p.tool.as_str() {
        "Edit" | "MultiEdit" => t!("Perm.Edit", file_name(p)),
        "Write" if p.target_exists == Some(true) => t!("Perm.Overwrite", file_name(p)),
        "Write" => t!("Perm.Create", file_name(p)),
        t if tool_kinds::is_shell(t) => t!("Perm.Run"),
        t => t!("Perm.AllowTool", t),
    }
}

/// Ce que fait une touche sur la carte du plan ou la carte ordinaire (les formulaires prennent les leurs).
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum KeyAction {
    Answer(Decision, Option<&'static str>),
    FocusFeedback,
    None,
}

pub fn key_action(key: &str, plan: bool, delegated: bool) -> KeyAction {
    if delegated {
        return KeyAction::None;
    }
    match (key, plan) {
        ("Enter", true) => KeyAction::Answer(Decision::Allow, Some("acceptEdits")),
        ("Shift+Enter", true) => KeyAction::Answer(Decision::Allow, Some("default")),
        // (le retour se tape, puis part par Ctrl Entrée)
        ("Delete", true) => KeyAction::FocusFeedback,
        ("Enter", false) => KeyAction::Answer(Decision::Allow, None),
        ("Shift+Enter", false) => KeyAction::Answer(Decision::AllowSession, None),
        ("Delete", false) => KeyAction::Answer(Decision::Deny, None),
        _ => KeyAction::None,
    }
}

/// Les touches de la carte (Entrée, Maj Entrée, Suppr et celles que demande un formulaire) : seulement 300 ms après
/// son arrivée, quand on ne tape pas dans un champ et que la palette est fermée ; elles sont prises (consommées).
pub fn card_key(ui: &Ui, cx: &Cx, since: f64, extra: &[(Modifiers, Key, &'static str)]) -> Option<&'static str> {
    let now = ui.input(|i| i.time);
    if now - since < 0.3 {
        ui.ctx().request_repaint_after(std::time::Duration::from_millis(320));
        return None;
    }
    if cx.ui.palette_open || ui.ctx().egui_wants_keyboard_input() || ui.ctx().any_popup_open() {
        return None;
    }
    let base = [(Modifiers::SHIFT, Key::Enter, "Shift+Enter"), (Modifiers::NONE, Key::Enter, "Enter"), (Modifiers::NONE, Key::Delete, "Delete")];
    ui.input_mut(|i| base.iter().chain(extra).find(|(m, k, _)| i.consume_key(*m, *k)).map(|x| x.2))
}

// ------------------------------------------------------------------------------------------- la réponse

/// Une réponse à la CLI (`session_answer`) : une seule à la fois ; un échec la rend de nouveau possible et se montre.
#[derive(Default)]
pub struct Answer {
    pub answered: bool,
    pub error: Option<String>,
    task: Option<Task<Result<(), String>>>,
}

impl Answer {
    pub fn poll(&mut self) {
        if let Some(r) = self.task.as_ref().and_then(Task::take) {
            self.task = None;
            if let Err(e) = r {
                self.error = Some(t!("Perm.AnswerFailed", e));
                self.answered = false;
            }
        }
    }

    pub fn send(&mut self, be: &Backend, id: &str, args: AnswerArgs) {
        if self.answered {
            return;
        }
        self.answered = true;
        self.error = None;
        match be.session(id) {
            Ok(s) => self.task = Some(be.task(async move { s.answer(args).await })),
            Err(e) => {
                self.error = Some(t!("Perm.AnswerFailed", e));
                self.answered = false;
            }
        }
    }

    pub fn error_box(&self, ui: &mut Ui, t: &theme::Tokens) {
        if let Some(e) = &self.error {
            insp::errbox(ui, t, e);
        }
    }
}

// ------------------------------------------------------------------------------------------- la carte

/// L'état de la carte, remis à zéro pour chaque demande (son `request_id`).
#[derive(Default)]
pub struct PermissionState {
    rid: String,
    since: f64,
    pub answer: Answer,
    feedback: String,
    focus_feedback: bool,
    destination: String,
    diff: Option<Vec<DiffLine>>,
    diff_task: Option<Task<Vec<DiffLine>>>,
    pub question: forms::QuestionState,
    pub elicit: forms::ElicitState,
}

pub fn show(ui: &mut Ui, cx: &mut Cx, st: &mut PermissionState, id: &str, p: &PendingPermission) {
    let t = cx.t;
    let plan = p.tool == "ExitPlanMode";
    let question = p.tool == ask_user::TOOL && !ask_user::parse(&p.input).is_empty();
    let elicit = p.tool == elicit::TOOL;
    if st.rid != p.request_id {
        *st = PermissionState { rid: p.request_id.clone(), since: ui.input(|i| i.time), destination: "session".into(), ..Default::default() };
        if question {
            st.question = forms::QuestionState::new(p);
        }
        if elicit {
            st.elicit = forms::ElicitState::new(p);
        }
        if tool_kinds::is_edit(&p.tool) && !question && !elicit && !plan {
            let (tool, input) = (p.tool.clone(), p.input.clone());
            st.diff_task = Some(cx.be.blocking(move || ccui_core::edit_diff::from_input(&tool, &input)));
        }
    }
    st.answer.poll();
    if let Some(d) = st.diff_task.as_ref().and_then(Task::take) {
        st.diff_task = None;
        st.diff = Some(d);
    }

    // la file des décisions de toutes les sessions : la place de celle-ci, et les autres
    let queue = cx.app.summary.decision_queue.clone();
    let pos = queue.iter().position(|e| e.session_id == id && e.permission.request_id == p.request_id).map_or(0, |i| i + 1);
    let heading = if question {
        t!("Ask.Title")
    } else if elicit {
        t!("Elicit.Title")
    } else {
        t!("Perm.Title")
    };
    let sub = if elicit { json::string(&p.input, "mcp_server_name") } else { p.tool.clone() };
    insp::ph(ui, &t, &heading, &sub, |ui| {
        widgets::pill(ui, &t, &t!("OfN", pos, queue.len()), Tone::Wait);
    });

    if plan {
        plan_card(ui, cx, st, id, p);
    } else if question {
        insp::section(ui, &t, false, |ui| forms::question(ui, cx, &mut st.question, &mut st.answer, st.since, id, p));
    } else if elicit {
        insp::section(ui, &t, false, |ui| forms::elicit(ui, cx, &mut st.elicit, &mut st.answer, st.since, id, p));
    } else {
        generic(ui, cx, st, id, p);
    }

    let others: Vec<_> = queue.iter().filter(|e| !(e.session_id == id && e.permission.request_id == p.request_id)).collect();
    if !others.is_empty() {
        insp::section(ui, &t, true, |ui| {
            widgets::cap(ui, &t, &t!("Perm.AlsoWaiting"));
            for e in others {
                let q = &e.permission;
                let who = match &e.agent_label {
                    Some(a) => format!("{} · {a} · ", e.session_name),
                    None => format!("{} · ", e.session_name),
                };
                let target = tool_kinds::target(&q.tool, &q.input, &e.session_cwd);
                let r = widgets::row(ui, &t, false, |ui| {
                    ui.horizontal(|ui| {
                        widgets::dot(ui, t.warning, true);
                        ui.add_sized([84.0, 16.0], egui::Label::new(RichText::new(tool_kinds::label(&q.tool)).font(theme::mono(theme::LABEL)).color(kind_color(&t, &q.tool))).truncate());
                        ui.add(
                            egui::Label::new(
                                RichText::new(format!("{who}{target}")).font(theme::mono(theme::LABEL)).color(t.text_dim),
                            )
                            .truncate()
                            .selectable(false),
                        );
                    });
                });
                if r.clicked() {
                    cx.open_session(&e.session_id);
                }
            }
        });
    }
}

fn answer_args(rid: &str, d: Decision, mode: Option<&str>, message: Option<String>, destination: &str) -> AnswerArgs {
    AnswerArgs {
        request_id: rid.to_string(),
        decision: d,
        mode: mode.map(str::to_string),
        message,
        input: None,
        destination: Some(destination.to_string()),
    }
}

/// ExitPlanMode : approuver met le mode choisi (set_permission_mode, puis autoriser) ; « continuer à planifier »
/// refuse avec le retour, que le modèle reçoit tel quel.
fn plan_card(ui: &mut Ui, cx: &mut Cx, st: &mut PermissionState, id: &str, p: &PendingPermission) {
    let t = cx.t;
    let shift_enter = t!("Key.ShiftEnter");
    let mut act: Option<(Decision, Option<&'static str>, Option<String>)> = None;
    match card_key(ui, cx, st.since, &[]).map(|k| key_action(k, true, false)) {
        Some(KeyAction::Answer(d, m)) => act = Some((d, m, None)),
        Some(KeyAction::FocusFeedback) => st.focus_feedback = true,
        _ => {}
    }
    insp::section(ui, &t, false, |ui| {
        insp::h3(ui, &t, &t!("Plan.Title"));
        md::show(ui, &t, &json::string(&p.input, "plan"));
        let enabled = !st.answer.answered;
        ui.add_enabled_ui(enabled, |ui| {
            ui.horizontal_wrapped(|ui| {
                if widgets::primary(ui, &t, &format!("{}  ⏎", t!("Plan.ApproveAuto"))).clicked() {
                    act = Some((Decision::Allow, Some("acceptEdits"), None));
                }
                if ui.button(format!("{}  {shift_enter}", t!("Plan.ApproveReview"))).clicked() {
                    act = Some((Decision::Allow, Some("default"), None));
                }
            });
            ui.label(RichText::new(t!("Plan.Feedback")).font(theme::medium(theme::CAPTION)).color(t.text_dim));
            let fb_id = egui::Id::new(("plan-fb", &st.rid));
            let r = ui.add(egui::TextEdit::multiline(&mut st.feedback).id(fb_id).hint_text(t!("Plan.FeedbackHint")).desired_rows(3).desired_width(f32::INFINITY));
            if std::mem::take(&mut st.focus_feedback) {
                r.request_focus();
            }
            if r.has_focus() && ui.input_mut(|i| i.consume_key(Modifiers::COMMAND, Key::Enter)) {
                act = Some((Decision::Deny, None, Some(st.feedback.clone())));
            }
            if ui.button(format!("{}  {}", t!("Plan.Keep"), i18n::kbd("Ctrl ⏎"))).clicked() {
                act = Some((Decision::Deny, None, Some(st.feedback.clone())));
            }
        });
        st.answer.error_box(ui, &t);
        insp::keys(
            ui,
            &t,
            &[
                ("⏎".into(), t!("Plan.HintAuto")),
                (shift_enter.clone(), t!("Plan.HintReview")),
                (t!("Key.Del"), t!("Plan.HintFeedback")),
            ],
        );
    });
    if let Some((d, mode, msg)) = act {
        st.answer.send(cx.be, id, answer_args(&st.rid, d, mode, msg, &st.destination));
    }
}

/// La carte ordinaire : Autoriser / Toute la session (+ où garder la règle) / Refuser.
fn generic(ui: &mut Ui, cx: &mut Cx, st: &mut PermissionState, id: &str, p: &PendingPermission) {
    let t = cx.t;
    let can_session = can_allow_session(p);
    let mut act: Option<Decision> = None;
    if let Some(KeyAction::Answer(d, _)) = card_key(ui, cx, st.since, &[]).map(|k| key_action(k, false, false)) {
        act = Some(d);
    }
    let (asked_by, cwd) = match cx.app.session(id) {
        Some(h) => (session_rules::asking_agent(&h.items, p).map(session_rules::agent_label), h.meta.cwd.clone()),
        None => (None, String::new()),
    };
    insp::section(ui, &t, false, |ui| {
        insp::h3(ui, &t, &title(p));
        if let Some(a) = asked_by {
            insp::para(ui, &t, &t!("Perm.AskedBy", a));
        }
        if let Some(d) = json::str(&p.input, "description").filter(|d| !d.is_empty()) {
            insp::para(ui, &t, d);
        }
        if let Some(w) = why(p) {
            ui.add(egui::Label::new(RichText::new(w).font(egui::FontId::proportional(theme::LABEL)).color(t.warning)).wrap());
        }
        if let Some(b) = p.info.as_ref().and_then(|i| i.blocked_path.clone()) {
            ui.horizontal_wrapped(|ui| {
                ui.label(RichText::new(t!("Perm.BlockedPath")).font(egui::FontId::proportional(theme::LABEL)).color(t.warning));
                ui.label(RichText::new(b).font(theme::mono(theme::CAPTION)).color(t.text));
            });
        }
        if let Some(lines) = &st.diff {
            let path = tool_kinds::target(&p.tool, &p.input, &cwd);
            insp::diff_view(ui, &t, ("perm-diff", &st.rid), &path, lines);
        } else if tool_kinds::is_edit(&p.tool) {
            ui.spinner(); // (le diff se lit)
        } else if tool_kinds::is_shell(&p.tool) {
            insp::pre(ui, &t, ("perm-cmd", &st.rid), &json::string(&p.input, "command"));
        } else {
            md::code_block(ui, &t, "json", &insp::pretty(&p.input));
        }
        let enabled = !st.answer.answered;
        ui.add_enabled_ui(enabled, |ui| {
            ui.horizontal(|ui| {
                if widgets::primary(ui, &t, &format!("{}  ⏎", t!("Perm.Allow"))).clicked() {
                    act = Some(Decision::Allow);
                }
                if can_session && ui.button(t!("Perm.WholeSession")).clicked() {
                    act = Some(Decision::AllowSession);
                }
                ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                    if widgets::danger(ui, &t, &t!("Perm.Deny")).clicked() {
                        act = Some(Decision::Deny);
                    }
                });
            });
            // où garder une règle « toujours » : la session par défaut, un fichier de réglages si on le choisit
            if can_session && !sets_mode(p) {
                ui.horizontal(|ui| {
                    ui.label(RichText::new(t!("Perm.SaveIn")).font(egui::FontId::proportional(theme::CAPTION)).color(t.text_dim));
                    egui::ComboBox::from_id_salt(("perm-dest", &st.rid))
                        .selected_text(t!(&format!("Perm.Dest.{}", st.destination)))
                        .show_ui(ui, |ui| {
                            for d in DESTINATIONS {
                                ui.selectable_value(&mut st.destination, d.to_string(), t!(&format!("Perm.Dest.{d}")));
                            }
                        });
                });
            }
        });
        if can_session {
            ui.add(egui::Label::new(RichText::new(session_grant(p)).font(egui::FontId::proportional(theme::CAPTION)).color(t.text_dim)).wrap());
        }
        st.answer.error_box(ui, &t);
        let mut hint = vec![("⏎".to_string(), t!("Perm.HintAllow"))];
        if can_session {
            hint.push((t!("Key.ShiftEnter"), t!("Perm.HintSession")));
        }
        hint.push((t!("Key.Del"), t!("Perm.HintDeny")));
        insp::keys(ui, &t, &hint);
    });
    if let Some(d) = act
        && (d != Decision::AllowSession || can_session)
    {
        st.answer.send(cx.be, id, answer_args(&st.rid, d, None, None, &st.destination));
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use ccui_shared::model::{PermissionInfo, Time};
    use serde_json::json;

    fn perm(tool: &str, input: Value, suggestions: Option<Value>, info: Option<PermissionInfo>) -> PendingPermission {
        PendingPermission {
            request_id: "r".into(),
            tool: tool.into(),
            input,
            description: None,
            tool_use_id: None,
            suggestions,
            at: Time::default(),
            info,
            target_exists: None,
        }
    }

    #[test]
    fn always_allow() {
        i18n::set_thread_lang(Some(i18n::Lang::En));
        let rules = json!([{"type":"addRules","rules":[{"toolName":"Bash","ruleContent":"git push:*"},{"toolName":"Read"}]},
                           {"type":"addDirectories","directories":["/w/x"]}]);
        let p = perm("Bash", json!({"command":"git push"}), Some(rules), None);
        assert!(can_allow_session(&p) && !sets_mode(&p));
        assert_eq!(session_grant(&p), "Allows Bash(git push:*), Read, /w/x");
        let m = perm("Bash", json!({}), Some(json!([{"type":"setMode","mode":"acceptEdits"}])), None);
        assert!(sets_mode(&m));
        assert_eq!(session_grant(&m), "Switches the session to Accept edits mode");
        let no = perm("Bash", json!({}), Some(json!([{"type":"setMode"}])), Some(PermissionInfo { no_always_allow: true, ..Default::default() }));
        assert!(!can_allow_session(&no));
        assert!(!can_allow_session(&perm("Bash", json!({}), Some(json!([])), None)));
        i18n::set_thread_lang(None);
    }

    #[test]
    fn titles_and_reasons() {
        i18n::set_thread_lang(Some(i18n::Lang::En));
        let mut w = perm("Write", json!({"file_path":"/w/a/notes.txt"}), None, None);
        assert_eq!(title(&w), "Create notes.txt?");
        w.target_exists = Some(true);
        assert_eq!(title(&w), "Overwrite notes.txt?");
        assert_eq!(title(&perm("Bash", json!({}), None, None)), "Run this command?");
        assert_eq!(title(&perm("WebFetch", json!({}), None, None)), "Allow WebFetch?");
        let info = PermissionInfo { title: Some("Custom".into()), reason_type: Some("workingDir".into()), ..Default::default() };
        let p = perm("Edit", json!({}), None, Some(info));
        assert_eq!(title(&p), "Custom");
        assert_eq!(why(&p).as_deref(), Some("The path is outside the session's folders."));
        let r = PermissionInfo { reason: Some("\u{1b}[1mbecause\u{1b}[0m".into()), ask_rule: Some("x".into()), ..Default::default() };
        assert_eq!(why(&perm("Bash", json!({}), None, Some(r))).as_deref(), Some("because"));
        assert_eq!(why(&perm("Bash", json!({}), None, None)), None);
        i18n::set_thread_lang(None);
    }

    #[test]
    fn keys() {
        assert_eq!(key_action("Enter", true, false), KeyAction::Answer(Decision::Allow, Some("acceptEdits")));
        assert_eq!(key_action("Delete", true, false), KeyAction::FocusFeedback);
        assert_eq!(key_action("Shift+Enter", false, false), KeyAction::Answer(Decision::AllowSession, None));
        assert_eq!(key_action("Delete", false, false), KeyAction::Answer(Decision::Deny, None));
        assert_eq!(key_action("Enter", false, true), KeyAction::None);
        assert_eq!(key_action("Escape", false, false), KeyAction::None);
    }
}
