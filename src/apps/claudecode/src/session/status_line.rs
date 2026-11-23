//! La ligne d'état vivante (`session/status_line.rs` de ClaudeCodeUI, `StatusLine.razor`) : démarrage, écriture,
//! outil en cours, agents, crochets, compaction, nouvel essai de l'API, attente d'une décision, tour interrompu ou en
//! erreur ; puis le texte de la commande `statusLine` des réglages et les lignes d'état des plugins. Dans ClaudeCodeUI
//! elle est au pied du fil ; ici, juste au-dessus du composeur (le même endroit à l'écran).

use ccui_shared::model::*;
use ccui_shared::session::SessionMeta;
use ccui_shared::{ask_user, elicit, fmt, i18n, session_rules, t, tool_kinds};
use egui::{RichText, Ui};

use crate::app::Cx;
use crate::theme;
use crate::widgets::{self, icon};

/// Ce que montre la ligne (le premier cas qui s'applique de `StatusLine.razor`).
#[derive(Clone, Debug, PartialEq)]
pub enum Line {
    /// Une roue, un texte, un petit texte (qui avance avec l'horloge) ; `wait` : couleur d'attente.
    Spin { text: String, small: Option<String>, hidden: bool, wait: bool },
    /// Un point, un texte (`class` : « status wait » ou « status » ; `dot` : wait, exited).
    Dot { text: String, class: &'static str, dot: &'static str },
    Interrupted(String),
    Error { key: &'static str, subtype: String },
    TurnDone,
    None,
}

/// Ce que disent les éléments (les agents du tour en cours ; le dernier élément, hors crochet, un outil en cours).
#[derive(Clone, Debug, Default, PartialEq)]
pub struct ItemFacts {
    /// (finis, en tout, l'un en cours) des appels Agent de premier niveau après le dernier message.
    pub agents: (usize, usize, bool),
    /// Le libellé du dernier élément (hors crochet) quand c'est un outil en cours.
    pub running_tool: Option<String>,
}

pub fn item_facts(items: &[&Item]) -> ItemFacts {
    let from = items.iter().rposition(|i| matches!(i, Item::User(_))).map_or(0, |i| i + 1);
    let agents: Vec<&ToolItem> = items[from..]
        .iter()
        .filter_map(|i| i.as_tool())
        .filter(|t| t.parent_tool_use_id.is_none() && tool_kinds::is_agent(&t.name))
        .collect();
    let running_tool = match items.iter().rev().find(|i| !matches!(i, Item::Hook(_))) {
        Some(Item::Tool(t)) if t.state == ToolState::Running => Some(tool_kinds::label(&t.name)),
        _ => None,
    };
    ItemFacts {
        agents: (
            agents.iter().filter(|a| a.state == ToolState::Done).count(),
            agents.len(),
            agents.iter().any(|a| a.state == ToolState::Running),
        ),
        running_tool,
    }
}

fn since(t: Time, now: Time) -> String {
    fmt::dur((now - t).num_milliseconds())
}

/// « 529 · overloaded · dans 4 s » : ce qui a échoué, et quand part le prochain essai.
pub fn retry_text(r: &RetryEvt) -> String {
    [r.error_status.map(|s| s.to_string()), r.error.clone(), Some(t!("Status.RetryIn", fmt::dur(r.delay_ms as i64)))]
        .into_iter()
        .flatten()
        .filter(|s| !s.is_empty())
        .collect::<Vec<_>>()
        .join(" · ")
}

/// Le premier cas qui s'applique.
pub fn line(m: &SessionMeta, facts: &ItemFacts, elsewhere: bool, now: Time) -> Line {
    use SessionStatus::*;
    let spin = |text: String, small: Option<String>, hidden: bool| Line::Spin { text, small, hidden, wait: false };
    let elapsed = || m.turn_started_at.map(|t| since(t, now)).unwrap_or_default();
    match m.status {
        Starting => spin(t!("Status.StartingClaude"), None, false),
        Running | Waiting if m.compacting_since.is_some() => spin(t!("Status.Compacting"), m.compacting_since.map(|c| since(c, now)), true),
        Running if m.retry.is_some() => {
            let r = m.retry.as_ref().unwrap();
            Line::Spin { text: t!("Status.Retry", r.attempt, r.max_retries), small: Some(retry_text(r)), hidden: false, wait: true }
        }
        Running if !m.running_hooks.is_empty() => {
            let h = m.running_hooks.iter().min_by_key(|h| h.at).unwrap();
            let more = if m.running_hooks.len() > 1 { format!(" · {}", t!("Status.HooksMore", m.running_hooks.len() - 1)) } else { String::new() };
            spin(t!("Status.HookRunning", h.name), Some(format!("{}{more}", since(h.at, now))), true)
        }
        Running if facts.agents.1 > 0 && facts.agents.2 => spin(t!("Status.AgentsWorking"), Some(t!("OfN", facts.agents.0, facts.agents.1)), false),
        Running if facts.running_tool.is_some() => {
            spin(t!("Status.ToolRunning", facts.running_tool.as_deref().unwrap_or_default()), Some(elapsed()), true)
        }
        Running => {
            let thinking =
                if m.thinking_tokens > 0 { format!(" · {}", t!("Status.Thinking", fmt::tokens(m.thinking_tokens as i64))) } else { String::new() };
            spin(t!("Status.Writing"), Some(format!("{}{thinking}", elapsed())), true)
        }
        Waiting => {
            let tool = m.pending.first().map(|p| p.tool.as_str());
            if matches!(tool, Some(ask_user::TOOL) | Some(elicit::TOOL)) {
                return Line::Dot { text: t!("Status.PausedQuestion"), class: "status wait", dot: "wait" };
            }
            let what = match tool {
                Some(n) if tool_kinds::is_edit(n) => t!("Status.WhatEdit"),
                Some(n) if tool_kinds::is_shell(n) => t!("Status.WhatCommand"),
                _ => t!("Status.WhatTool"),
            };
            Line::Dot { text: t!("Status.Paused", what), class: "status wait", dot: "wait" }
        }
        Exited if elsewhere => Line::Dot { text: t!("Status.ElsewhereLine"), class: "status wait", dot: "wait" },
        Exited => Line::Dot { text: t!("Status.HistoryLine", i18n::name("Mode.Name.", Some(&m.mode))), class: "status", dot: "exited" },
        Idle if m.last_result_subtype.as_deref() == Some(session_rules::INTERRUPTED) => {
            Line::Interrupted(m.last_turn_ms.map(fmt::dur).unwrap_or_default())
        }
        Idle if m.last_result_subtype.as_deref().is_some_and(|s| s.starts_with("error")) => {
            let subtype = m.last_result_subtype.clone().unwrap_or_default();
            let key = match subtype.as_str() {
                "error_max_turns" => "Status.Err.MaxTurns",
                "error_max_budget_usd" => "Status.Err.Budget",
                "error_during_execution" => "Status.Err.Execution",
                _ => "Status.TurnError",
            };
            Line::Error { key, subtype }
        }
        Idle if m.last_turn_ms.is_some() => Line::TurnDone,
        _ => Line::None,
    }
}

/// La ligne d'état de la session `id` (rien quand il n'y a rien à dire). L'horloge avance d'une seconde tant qu'un
/// tour tourne.
pub fn show(ui: &mut Ui, cx: &mut Cx, id: &str) {
    let t = cx.t;
    let Some(h) = cx.app.session(id) else { return };
    let m = &h.meta;
    let facts = item_facts(&h.item_refs());
    let elsewhere = session_rules::running_elsewhere(&cx.app.summary, id);
    let now = chrono::Utc::now();
    let l = line(m, &facts, elsewhere, now);
    if matches!(m.status, SessionStatus::Running | SessionStatus::Waiting | SessionStatus::Starting) {
        ui.ctx().request_repaint_after(std::time::Duration::from_secs(1));
    }
    let mut plugins = m.plugin_status.clone();
    plugins.sort_by(|a, b| a.0.cmp(&b.0));
    let plugin_line = (!plugins.is_empty()).then(|| plugins.iter().map(|(k, v)| format!("{k}: {v}")).collect::<Vec<_>>().join(" · "));
    let custom = m.status_text.clone();
    if matches!(l, Line::None | Line::TurnDone) && plugin_line.is_none() && custom.is_none() {
        return;
    }
    let small = |s: &str| RichText::new(s).font(theme::mono(theme::CAPTION)).color(t.text_faint);
    ui.vertical(|ui| {
        ui.spacing_mut().item_spacing.y = 2.0;
        match &l {
            Line::Spin { text, small: sm, wait, .. } => {
                ui.horizontal(|ui| {
                    ui.add(egui::Spinner::new().size(12.0).color(if *wait { t.warning } else { t.accent }));
                    ui.label(RichText::new(text).font(theme::medium(theme::LABEL)).color(if *wait { t.warning } else { t.text }));
                    if let Some(s) = sm {
                        ui.label(small(s));
                    }
                });
            }
            Line::Dot { text, dot, .. } => {
                ui.horizontal(|ui| {
                    let (c, filled) = if *dot == "wait" { (t.warning, true) } else { (t.text_faint, false) };
                    widgets::dot(ui, c, filled);
                    ui.label(RichText::new(text).font(theme::medium(theme::LABEL)).color(if *dot == "wait" { t.warning } else { t.text_dim }));
                });
            }
            Line::Interrupted(d) => {
                ui.horizontal(|ui| {
                    ui.label(RichText::new(format!("{} {}", icon("stop"), t!("Status.Interrupted"))).font(theme::medium(theme::LABEL)).color(t.text_dim));
                    ui.label(small(d));
                });
            }
            Line::Error { key, subtype } => {
                ui.label(RichText::new(t!(key)).font(theme::medium(theme::LABEL)).color(t.danger)).on_hover_text(subtype);
            }
            Line::TurnDone | Line::None => {}
        }
        if let Some(c) = custom {
            ui.label(small(&c));
        }
        if let Some(p) = plugin_line {
            ui.label(small(&p));
        }
    });
    ui.add_space(4.0);
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    fn tool(name: &str, state: ToolState, parent: Option<&str>) -> Item {
        Item::Tool(ToolItem {
            id: "t".into(),
            name: name.into(),
            input: json!({}),
            parent_tool_use_id: parent.map(Into::into),
            state,
            result_text: None,
            structured: None,
            started_at: Time::default(),
            ended_at: None,
            task_id: None,
            task_status: None,
            progress: None,
            tokens: 0,
            sub_tool_uses: 0,
            background: false,
            model: None,
        })
    }

    #[test]
    fn facts() {
        let user = Item::User(UserItem { text: "x".into(), at: Time::default(), ultracode: false, images: None, uuid: None });
        let hook = Item::Hook(HookItem { hook: HookEvt::default() });
        let items = [
            tool("Agent", ToolState::Done, None),
            user,
            tool("Agent", ToolState::Done, None),
            tool("Agent", ToolState::Running, None),
            tool("Read", ToolState::Running, Some("a")),
            hook,
        ];
        let refs: Vec<&Item> = items.iter().collect();
        let f = item_facts(&refs);
        assert_eq!(f.agents, (1, 2, true));
        assert_eq!(f.running_tool, Some(tool_kinds::label("Read")));
    }

    #[test]
    fn lines() {
        let now = Time::default() + chrono::Duration::seconds(10);
        let mut m = SessionMeta { status: SessionStatus::Running, turn_started_at: Some(Time::default()), ..Default::default() };
        let none = ItemFacts::default();
        assert_eq!(line(&m, &none, false, now), Line::Spin { text: t!("Status.Writing"), small: Some(fmt::dur(10_000)), hidden: true, wait: false });
        m.retry = Some(RetryEvt { attempt: 1, max_retries: 3, delay_ms: 4000, error_status: Some(529), error: Some("overloaded".into()) });
        assert!(matches!(line(&m, &none, false, now), Line::Spin { wait: true, .. }));
        assert_eq!(retry_text(m.retry.as_ref().unwrap()), format!("529 · overloaded · {}", t!("Status.RetryIn", fmt::dur(4000))));
        m.status = SessionStatus::Idle;
        m.last_result_subtype = Some("error_max_turns".into());
        assert_eq!(line(&m, &none, false, now), Line::Error { key: "Status.Err.MaxTurns", subtype: "error_max_turns".into() });
        m.status = SessionStatus::Exited;
        assert!(matches!(line(&m, &none, true, now), Line::Dot { dot: "wait", .. }));
    }
}
