//! Le fil d'une conversation : ce que l'utilisateur a demandé, ce que Claude a répondu (texte, réflexion), les outils
//! appelés avec leur résultat, les demandes d'autorisation, et des notes (fin d'un tour, erreurs). Construit depuis
//! le fichier de la session (`from_entries`) puis tenu à jour par les évènements de la CLI (`Thread::apply`).

use egui::{Frame, Label, Margin, RichText, Stroke, Ui};
use serde_json::Value;

use crate::md;
use crate::store;
use crate::theme::{self, Tokens};

#[derive(Clone, Debug, PartialEq)]
pub enum PermState {
    Pending,
    Allowed,
    Denied,
}

#[derive(Clone, Debug)]
pub enum Item {
    User(String),
    Assistant { text: String, live: bool },
    Thinking { text: String, live: bool },
    Tool { id: String, name: String, input: Value, result: Option<(String, bool)> },
    Permission { request_id: String, tool: String, input: Value, suggestions: Option<Value>, state: PermState },
    Note { text: String, error: bool },
}

/// Ce que l'utilisateur fait dans le fil (l'app le transmet à la CLI).
pub enum Action {
    Allow { request_id: String, input: Value, remember: Option<Value> },
    Deny { request_id: String },
    Login,
}

#[derive(Default)]
pub struct Thread {
    pub items: Vec<Item>,
    /// Le tour en cours (entre l'envoi et le `result`).
    pub busy: bool,
    pub session_id: Option<String>,
    pub model: String,
    /// Le dernier tour a fini sur « pas connecté ».
    pub needs_login: bool,
}

impl Thread {
    pub fn from_entries(entries: &[Value]) -> Thread {
        let mut th = Thread::default();
        for e in entries {
            if e.get("isSidechain").and_then(Value::as_bool) == Some(true) {
                continue;
            }
            match e.get("type").and_then(Value::as_str) {
                Some("user") => th.add_user_message(e, true),
                Some("assistant") => th.add_assistant_message(e),
                _ => {}
            }
        }
        th
    }

    /// Un message « user » : une demande (le texte), ou des résultats d'outils.
    fn add_user_message(&mut self, e: &Value, from_file: bool) {
        if e.get("isMeta").and_then(Value::as_bool) == Some(true) {
            return;
        }
        let content = e.get("message").and_then(|m| m.get("content"));
        if let Some(arr) = content.and_then(Value::as_array) {
            for b in arr {
                if b.get("type").and_then(Value::as_str) == Some("tool_result") {
                    let id = b.get("tool_use_id").and_then(Value::as_str).unwrap_or("");
                    let err = b.get("is_error").and_then(Value::as_bool).unwrap_or(false);
                    let text = tool_result_text(b.get("content"));
                    self.attach_result(id, text, err);
                }
            }
        }
        if from_file
            && let Some(t) = store::user_text(e)
        {
            let t = t.trim();
            if t.is_empty() {
                return;
            }
            // (les commandes et leurs sorties, que la CLI range en balises : une note courte)
            if t.starts_with("<command-name>") || t.starts_with("<local-command") || t.starts_with("<command-message>") {
                let name = between(t, "<command-name>", "</command-name>").unwrap_or("commande");
                self.items.push(Item::Note { text: format!("Commande {name}"), error: false });
                return;
            }
            if t.starts_with("[Request interrupted") {
                self.items.push(Item::Note { text: "Interrompu".into(), error: false });
                return;
            }
            self.items.push(Item::User(t.to_owned()));
        }
    }

    fn add_assistant_message(&mut self, e: &Value) {
        let Some(arr) = e.get("message").and_then(|m| m.get("content")).and_then(Value::as_array) else { return };
        for b in arr {
            match b.get("type").and_then(Value::as_str) {
                Some("text") => {
                    let t = b.get("text").and_then(Value::as_str).unwrap_or("").to_owned();
                    if !t.trim().is_empty() {
                        self.items.push(Item::Assistant { text: t, live: false });
                    }
                }
                Some("thinking") => {
                    let t = b.get("thinking").and_then(Value::as_str).unwrap_or("").to_owned();
                    if !t.trim().is_empty() {
                        self.items.push(Item::Thinking { text: t, live: false });
                    }
                }
                Some("tool_use") => self.items.push(Item::Tool {
                    id: b.get("id").and_then(Value::as_str).unwrap_or("").to_owned(),
                    name: b.get("name").and_then(Value::as_str).unwrap_or("outil").to_owned(),
                    input: b.get("input").cloned().unwrap_or(Value::Null),
                    result: None,
                }),
                _ => {}
            }
        }
    }

    fn attach_result(&mut self, id: &str, text: String, err: bool) {
        for it in self.items.iter_mut().rev() {
            if let Item::Tool { id: tid, result, .. } = it
                && tid == id
            {
                *result = Some((text, err));
                return;
            }
        }
    }

    /// Retire le texte arrivé au fil de l'eau : le message entier de la CLI le remplace.
    fn drop_live(&mut self) {
        self.items.retain(|i| !matches!(i, Item::Assistant { live: true, .. } | Item::Thinking { live: true, .. }));
    }

    pub fn push_user(&mut self, text: &str) {
        self.items.push(Item::User(text.to_owned()));
        self.busy = true;
        self.needs_login = false;
    }

    pub fn note(&mut self, text: impl Into<String>, error: bool) {
        self.items.push(Item::Note { text: text.into(), error });
    }

    /// Un évènement JSON de la CLI. Rend une demande d'autorisation à poser s'il en arrive une (déjà dans le fil).
    pub fn apply(&mut self, v: &Value) {
        match v.get("type").and_then(Value::as_str) {
            Some("system") => {
                if v.get("subtype").and_then(Value::as_str) == Some("init") {
                    if let Some(id) = v.get("session_id").and_then(Value::as_str) {
                        self.session_id = Some(id.to_owned());
                    }
                    if let Some(m) = v.get("model").and_then(Value::as_str) {
                        self.model = m.to_owned();
                    }
                }
            }
            Some("stream_event") => {
                let ev = &v["event"];
                if ev.get("type").and_then(Value::as_str) == Some("content_block_delta") {
                    let d = &ev["delta"];
                    match d.get("type").and_then(Value::as_str) {
                        Some("text_delta") => {
                            let s = d.get("text").and_then(Value::as_str).unwrap_or("");
                            if let Some(Item::Assistant { text, live: true }) = self.items.last_mut() {
                                text.push_str(s);
                            } else {
                                self.items.push(Item::Assistant { text: s.to_owned(), live: true });
                            }
                        }
                        Some("thinking_delta") => {
                            let s = d.get("thinking").and_then(Value::as_str).unwrap_or("");
                            if let Some(Item::Thinking { text, live: true }) = self.items.last_mut() {
                                text.push_str(s);
                            } else {
                                self.items.push(Item::Thinking { text: s.to_owned(), live: true });
                            }
                        }
                        _ => {}
                    }
                }
            }
            Some("assistant") => {
                if v.get("parent_tool_use_id").is_some_and(|p| !p.is_null()) {
                    return; // (un sous-agent : ses messages ne sont pas dans le fil principal)
                }
                self.drop_live();
                self.add_assistant_message(v);
            }
            Some("user") => {
                if v.get("parent_tool_use_id").is_some_and(|p| !p.is_null()) {
                    return;
                }
                self.add_user_message(v, false);
            }
            Some("result") => {
                self.drop_live_keep_text();
                self.busy = false;
                let is_err = v.get("is_error").and_then(Value::as_bool).unwrap_or(false);
                let sub = v.get("subtype").and_then(Value::as_str).unwrap_or("");
                let result = v.get("result").and_then(Value::as_str).unwrap_or("");
                if is_err || sub != "success" {
                    if result.contains("Not logged in") || result.contains("/login") || result.contains("Invalid API key") {
                        self.needs_login = true;
                    }
                    let text = if result.is_empty() { format!("Le tour a fini en erreur ({sub})") } else { result.to_owned() };
                    // (la CLI a souvent déjà dit la même chose dans sa réponse : pas deux fois)
                    let said = matches!(self.items.last(), Some(Item::Assistant { text: t, .. }) if t.trim() == text.trim());
                    if said {
                        if let Some(Item::Assistant { text: t, .. }) = self.items.pop() {
                            self.items.push(Item::Note { text: t, error: true });
                        }
                    } else {
                        self.items.push(Item::Note { text, error: true });
                    }
                } else {
                    let secs = v.get("duration_ms").and_then(Value::as_f64).unwrap_or(0.0) / 1000.0;
                    let turns = v.get("num_turns").and_then(Value::as_u64).unwrap_or(0);
                    let mut s = format!("Fini en {secs:.1} s");
                    if turns > 1 {
                        s.push_str(&format!(", {turns} étapes"));
                    }
                    if let Some(c) = v.get("total_cost_usd").and_then(Value::as_f64)
                        && c > 0.0
                    {
                        s.push_str(&format!(" · équivalent API {c:.3} $"));
                    }
                    self.items.push(Item::Note { text: s, error: false });
                }
            }
            Some("control_request") => {
                let req = &v["request"];
                if req.get("subtype").and_then(Value::as_str) == Some("can_use_tool") {
                    self.items.push(Item::Permission {
                        request_id: v.get("request_id").and_then(Value::as_str).unwrap_or("").to_owned(),
                        tool: req
                            .get("display_name")
                            .or_else(|| req.get("tool_name"))
                            .and_then(Value::as_str)
                            .unwrap_or("outil")
                            .to_owned(),
                        input: req.get("input").cloned().unwrap_or(Value::Null),
                        suggestions: req.get("permission_suggestions").filter(|s| s.as_array().is_some_and(|a| !a.is_empty())).cloned(),
                        state: PermState::Pending,
                    });
                }
            }
            _ => {}
        }
    }

    /// À la fin d'un tour, le texte venu au fil de l'eau sans message entier (un tour interrompu) reste, figé.
    fn drop_live_keep_text(&mut self) {
        for it in &mut self.items {
            match it {
                Item::Assistant { live, .. } | Item::Thinking { live, .. } => *live = false,
                _ => {}
            }
        }
    }

    pub fn set_permission(&mut self, request_id: &str, st: PermState) {
        for it in &mut self.items {
            if let Item::Permission { request_id: r, state, .. } = it
                && r == request_id
            {
                *state = st;
                return;
            }
        }
    }

    /// Toute la conversation en texte (Copier la conversation).
    pub fn as_text(&self) -> String {
        let mut out = String::new();
        for it in &self.items {
            match it {
                Item::User(t) => out.push_str(&format!("› {t}\n\n")),
                Item::Assistant { text, .. } => out.push_str(&format!("{text}\n\n")),
                Item::Tool { name, input, .. } => out.push_str(&format!("[{name}] {}\n\n", tool_summary(name, input))),
                Item::Note { text, .. } => out.push_str(&format!("— {text}\n\n")),
                _ => {}
            }
        }
        out
    }

    /// Dessine le fil. Rend ce que l'utilisateur a demandé (autoriser, refuser…).
    pub fn show(&self, ui: &mut Ui, t: &Tokens) -> Vec<Action> {
        let mut actions = Vec::new();
        let width = ui.available_width().min(860.0);
        for (i, it) in self.items.iter().enumerate() {
            ui.push_id(i, |ui| {
                ui.set_max_width(width);
                match it {
                    Item::User(text) => user_bubble(ui, t, text),
                    Item::Assistant { text, live } => {
                        let r = ui.vertical(|ui| {
                            md::show(ui, t, text);
                            if *live {
                                ui.label(RichText::new("▍").color(t.accent_ink));
                            }
                        });
                        if !*live {
                            r.response.context_menu(|ui| {
                                if ui.button("Copier la réponse").clicked() {
                                    ui.ctx().copy_text(text.clone());
                                    ui.close();
                                }
                            });
                        }
                    }
                    Item::Thinking { text, live } => {
                        egui::CollapsingHeader::new(RichText::new(if *live { "Réflexion…" } else { "Réflexion" }).font(theme::medium(theme::LABEL)).color(t.text_faint))
                            .id_salt(("think", i))
                            .default_open(false)
                            .show(ui, |ui| {
                                ui.add(Label::new(RichText::new(text).italics().color(t.text_dim)).selectable(true).wrap());
                            });
                    }
                    Item::Tool { name, input, result, .. } => tool_card(ui, t, i, name, input, result.as_ref()),
                    Item::Permission { request_id, tool, input, suggestions, state } => {
                        permission_card(ui, t, request_id, tool, input, suggestions.as_ref(), state, &mut actions)
                    }
                    Item::Note { text, error } => {
                        let c = if *error { t.danger } else { t.text_faint };
                        ui.add(Label::new(RichText::new(text).font(egui::FontId::proportional(theme::LABEL)).color(c)).selectable(true).wrap());

                    }
                }
            });
            ui.add_space(10.0);
        }
        if self.needs_login && !self.busy && accent_button(ui, t, "Se connecter à votre compte Claude…").clicked() {
            actions.push(Action::Login);
        }
        actions
    }
}

fn user_bubble(ui: &mut Ui, t: &Tokens, text: &str) {
    ui.with_layout(egui::Layout::right_to_left(egui::Align::Min), |ui| {
        let max = (ui.available_width() * 0.85).max(200.0);
        Frame::new().fill(t.user_bubble).corner_radius(theme::R_LG).inner_margin(Margin::symmetric(14, 10)).show(ui, |ui| {
            ui.set_max_width(max);
            let r = ui.add(Label::new(RichText::new(text).color(t.text)).selectable(true).wrap());
            r.context_menu(|ui| {
                if ui.button("Copier").clicked() {
                    ui.ctx().copy_text(text.to_owned());
                    ui.close();
                }
            });
        });
    });
}

/// Une ligne qui dit ce que fait l'outil (la commande, le fichier, le motif…).
pub fn tool_summary(name: &str, input: &Value) -> String {
    let s = |k: &str| input.get(k).and_then(Value::as_str).unwrap_or("");
    let v = match name {
        "Bash" => s("command"),
        "Read" | "Write" | "Edit" | "MultiEdit" | "NotebookEdit" => s("file_path"),
        "Glob" | "Grep" => s("pattern"),
        "WebFetch" => s("url"),
        "WebSearch" => s("query"),
        "Task" | "Agent" => s("description"),
        "TodoWrite" => "liste des tâches",
        _ => "",
    };
    if v.is_empty() {
        let compact = input.to_string();
        store::one_line(&compact, 120)
    } else {
        store::one_line(v, 160)
    }
}

fn tool_card(ui: &mut Ui, t: &Tokens, i: usize, name: &str, input: &Value, result: Option<&(String, bool)>) {
    Frame::new()
        .fill(t.card)
        .stroke(Stroke::new(1.0, t.card_border))
        .corner_radius(theme::R)
        .inner_margin(Margin::symmetric(10, 6))
        .show(ui, |ui| {
            ui.set_width(ui.available_width());
            let dot = match result {
                None => "◌",
                Some((_, false)) => "●",
                Some((_, true)) => "✕",
            };
            let header = egui::CollapsingHeader::new(
                RichText::new(format!("{dot} {name}  {}", tool_summary(name, input))).font(theme::mono(theme::CAPTION + 0.5)).color(if result.is_some() { t.text_dim } else { t.text }),
            )
            .id_salt(("tool", i))
            .default_open(false);
            header.show(ui, |ui| {
                let pretty = serde_json::to_string_pretty(input).unwrap_or_default();
                ui.label(RichText::new("Entrée").font(theme::medium(theme::CAPTION)).color(t.text_faint));
                ui.add(Label::new(RichText::new(pretty).font(theme::mono(theme::CAPTION)).color(t.text_dim)).selectable(true).wrap());
                if let Some((r, err)) = result {
                    ui.label(RichText::new(if *err { "Erreur" } else { "Résultat" }).font(theme::medium(theme::CAPTION)).color(if *err { t.danger } else { t.text_faint }));
                    let shown: String = if r.len() > 6000 { format!("{}\n… ({} caractères en tout)", &r[..floor_char(r, 6000)], r.len()) } else { r.clone() };
                    ui.add(Label::new(RichText::new(shown).font(theme::mono(theme::CAPTION)).color(t.text_dim)).selectable(true).wrap());
                }
            });
        });
}

#[allow(clippy::too_many_arguments)]
fn permission_card(
    ui: &mut Ui,
    t: &Tokens,
    request_id: &str,
    tool: &str,
    input: &Value,
    suggestions: Option<&Value>,
    state: &PermState,
    actions: &mut Vec<Action>,
) {
    let border = if *state == PermState::Pending { t.warning } else { t.card_border };
    Frame::new()
        .fill(t.card)
        .stroke(Stroke::new(1.5, border))
        .corner_radius(theme::R_LG)
        .inner_margin(Margin::same(12))
        .show(ui, |ui| {
            ui.set_width(ui.available_width());
            ui.label(RichText::new(format!("Claude veut utiliser {tool}")).font(theme::semibold(theme::BODY)).color(t.text));
            ui.add(Label::new(RichText::new(tool_summary(tool, input)).font(theme::mono(theme::CODE)).color(t.text_dim)).selectable(true).wrap());
            if input.get("command").is_none() && input.get("file_path").is_none() {
                let pretty = serde_json::to_string_pretty(input).unwrap_or_default();
                if pretty.len() < 2000 {
                    ui.add(Label::new(RichText::new(pretty).font(theme::mono(theme::CAPTION)).color(t.text_faint)).selectable(true).wrap());
                }
            }
            ui.add_space(4.0);
            match state {
                PermState::Pending => {
                    ui.horizontal(|ui| {
                        if accent_button(ui, t, "Autoriser").clicked() {
                            actions.push(Action::Allow { request_id: request_id.to_owned(), input: input.clone(), remember: None });
                        }
                        if let Some(s) = suggestions
                            && ui.button("Autoriser et ne plus demander").on_hover_text("Retenir la règle proposée par Claude Code pour la suite").clicked()
                        {
                            actions.push(Action::Allow { request_id: request_id.to_owned(), input: input.clone(), remember: Some(s.clone()) });
                        }
                        if ui.button(RichText::new("Refuser").color(t.danger)).clicked() {
                            actions.push(Action::Deny { request_id: request_id.to_owned() });
                        }
                    });
                }
                PermState::Allowed => {
                    ui.label(RichText::new("Autorisé").font(theme::medium(theme::LABEL)).color(t.success));
                }
                PermState::Denied => {
                    ui.label(RichText::new("Refusé").font(theme::medium(theme::LABEL)).color(t.danger));
                }
            }
        });
}

pub fn accent_button(ui: &mut Ui, t: &Tokens, text: &str) -> egui::Response {
    ui.add(egui::Button::new(RichText::new(text).color(t.on_accent).font(theme::medium(theme::SMALL))).fill(t.accent).corner_radius(theme::R))
}

fn tool_result_text(c: Option<&Value>) -> String {
    match c {
        Some(Value::String(s)) => s.clone(),
        Some(Value::Array(a)) => a
            .iter()
            .filter_map(|b| match b.get("type").and_then(Value::as_str) {
                Some("text") => b.get("text").and_then(Value::as_str).map(str::to_owned),
                Some("image") => Some("[image]".to_owned()),
                _ => None,
            })
            .collect::<Vec<_>>()
            .join("\n"),
        _ => String::new(),
    }
}

fn between<'a>(s: &'a str, a: &str, b: &str) -> Option<&'a str> {
    let i = s.find(a)? + a.len();
    let j = s[i..].find(b)? + i;
    Some(&s[i..j])
}

fn floor_char(s: &str, mut i: usize) -> usize {
    while i > 0 && !s.is_char_boundary(i) {
        i -= 1;
    }
    i
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn transcript_and_live() {
        let entries = vec![
            json!({"type":"user","message":{"role":"user","content":"Liste les fichiers"}}),
            json!({"type":"assistant","message":{"content":[{"type":"text","text":"Je regarde."},{"type":"tool_use","id":"t1","name":"Bash","input":{"command":"ls"}}]}}),
            json!({"type":"user","message":{"content":[{"type":"tool_result","tool_use_id":"t1","content":"a\nb"}]}}),
            json!({"type":"user","message":{"content":"<command-name>/clear</command-name>"}}),
        ];
        let mut th = Thread::from_entries(&entries);
        assert_eq!(th.items.len(), 4);
        assert!(matches!(&th.items[2], Item::Tool { result: Some((r, false)), .. } if r == "a\nb"));
        assert!(matches!(&th.items[3], Item::Note { text, .. } if text == "Commande /clear"));
        th.push_user("et encore");
        th.apply(&json!({"type":"system","subtype":"init","session_id":"S","model":"claude-opus"}));
        th.apply(&json!({"type":"stream_event","event":{"type":"content_block_delta","delta":{"type":"text_delta","text":"Bon"}}}));
        th.apply(&json!({"type":"stream_event","event":{"type":"content_block_delta","delta":{"type":"text_delta","text":"jour"}}}));
        assert!(matches!(th.items.last(), Some(Item::Assistant { text, live: true }) if text == "Bonjour"));
        th.apply(&json!({"type":"assistant","message":{"content":[{"type":"text","text":"Bonjour !"}]}}));
        assert!(matches!(th.items.last(), Some(Item::Assistant { text, live: false }) if text == "Bonjour !"));
        th.apply(&json!({"type":"control_request","request_id":"r1","request":{"subtype":"can_use_tool","tool_name":"Bash","input":{"command":"rm x"}}}));
        assert!(matches!(th.items.last(), Some(Item::Permission { state: PermState::Pending, .. })));
        th.set_permission("r1", PermState::Denied);
        th.apply(&json!({"type":"result","subtype":"success","is_error":false,"duration_ms":1500,"num_turns":1}));
        assert!(!th.busy);
        assert_eq!(th.session_id.as_deref(), Some("S"));
        th.apply(&json!({"type":"result","subtype":"success","is_error":true,"result":"Not logged in · Please run /login"}));
        assert!(th.needs_login);
        th.apply(&json!({"type":"assistant","message":{"content":[{"type":"text","text":"Not logged in"}]}}));
        th.apply(&json!({"type":"result","subtype":"success","is_error":true,"result":"Not logged in"}));
        assert!(matches!(th.items.last(), Some(Item::Note { error: true, .. })));
        assert!(!matches!(th.items[th.items.len() - 2], Item::Assistant { .. }));
    }
}
