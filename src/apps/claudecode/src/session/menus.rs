//! Les menus du composeur, ouverts au-dessus de lui : le modèle (`model_menu.rs` de ClaudeCodeUI : modèles, conseiller
//! `/advisor`, styles de sortie `/output-style`, « par défaut »), le mode d'autorisation (`mode_menu.rs` : un mode
//! risqué hors d'un worktree isolé demande une seconde confirmation) et les réglages `/config` (`config_menu.rs` : les
//! clés lues dans la réponse de la CLI à un « /config » nu, envoyées en « /config clé=valeur »).
//!
//! Chaque menu prend le clavier tant qu'il est ouvert (↑ ↓ Début Fin, Entrée ou Espace, Échap ou Tab pour fermer) :
//! le composeur lui passe les touches de l'image.

use ccui_shared::model::Item;
use ccui_shared::session::{SessionHeavy, SessionMeta};
use ccui_shared::session_rules::{CONFIG_USAGE, MODES, config_keys, risky};
use ccui_shared::{i18n, json, list_nav, t};
use egui::text::LayoutJob;
use egui::{Rect, RichText, TextFormat};
use serde_json::Value;

use crate::backend::{Backend, Task};
use crate::session::composer::act;
use crate::session::popovers::{option_row, pop_frame};
use crate::theme::{self, Tokens};

/// Ce que rend un menu à chaque image.
pub struct MenuOut {
    pub rect: Rect,
    pub close: bool,
}

fn name_job(text: &str, color: egui::Color32) -> LayoutJob {
    let mut job = LayoutJob::default();
    job.append(text, 0.0, TextFormat::simple(theme::medium(theme::LABEL), color));
    job
}

fn status_line(ui: &mut egui::Ui, t: &Tokens, title: &str, status: Option<(&str, bool)>) {
    ui.horizontal(|ui| {
        ui.add_space(6.0);
        ui.label(RichText::new(title).font(theme::medium(theme::LABEL)).color(t.text_dim));
        if let Some((s, err)) = status {
            ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                ui.add_space(4.0);
                ui.label(RichText::new(s).font(theme::medium(theme::CAPTION)).color(if err { t.danger } else { t.warning }));
            });
        }
    });
    ui.add_space(3.0);
}

// ------------------------------------------------------------------------------------------- le modèle

/// `ModelMenu.Models` : `initialize_info.models[]` (`{value, displayName, description, resolvedModel,
/// supportsEffort, supportedEffortLevels[], supportsFastMode}`).
pub fn models(info: Option<&Value>) -> Vec<Value> {
    match info.and_then(|i| json::prop(i, "models")) {
        Some(Value::Array(a)) => a.clone(),
        _ => Vec::new(),
    }
}

/// `ModelMenu.Advisors` : l'argumentHint de la commande /advisor (« [fable|opus|sonnet|off] ») ; pas de commande, pas
/// de conseiller.
pub fn advisors(info: Option<&Value>) -> Vec<String> {
    let Some(Value::Array(cmds)) = info.and_then(|i| json::prop(i, "commands")) else { return Vec::new() };
    let Some(hint) = cmds.iter().find(|c| json::str(c, "name") == Some("advisor")).and_then(|c| json::str(c, "argumentHint")) else {
        return Vec::new();
    };
    hint.trim_matches(['[', ']', '<', '>', ' ']).split('|').map(str::trim).filter(|s| !s.is_empty()).map(str::to_string).collect()
}

/// `ModelMenu.Current` : `model` est un alias de lancement (« haiku »), une valeur de set_model ou l'identifiant résolu
/// de l'init. Par valeur, sinon les modèles dont le resolvedModel préfixe celui-ci (sans casse), de préférence pas
/// « default ».
pub fn current_model(model: Option<&str>, info: Option<&Value>) -> Option<Value> {
    let ms = models(info);
    if let Some(m) = ms.iter().find(|m| model.is_some() && json::str(m, "value") == model) {
        return Some(m.clone());
    }
    let x = model?.to_lowercase();
    let resolved: Vec<&Value> = ms.iter().filter(|m| json::str(m, "resolvedModel").is_some_and(|r| x.starts_with(&r.to_lowercase()))).collect();
    resolved.iter().find(|m| json::str(m, "value") != Some("default")).or(resolved.first()).map(|m| (*m).clone())
}

/// Le modèle en vigueur : (valeur, nom montré).
#[allow(dead_code)] // (ModelMenu.Current de l'original, gardé pour l'en-tête et ses essais)
pub fn current(meta: &SessionMeta, heavy: &SessionHeavy) -> Option<(String, String)> {
    let m = current_model(meta.model.as_deref(), heavy.initialize_info.as_ref())?;
    let value = json::string(&m, "value");
    let name = json::str(&m, "displayName").map_or_else(|| value.clone(), str::to_string);
    Some((value, name))
}

/// Le displayName d'un modèle, sinon la valeur.
fn label(models: &[Value], value: &str) -> String {
    models.iter().find(|m| json::str(m, "value") == Some(value)).and_then(|m| json::str(m, "displayName")).unwrap_or(value).to_string()
}

fn resolved(models: &[Value], value: &str) -> Option<String> {
    models.iter().find(|m| json::str(m, "value") == Some(value)).and_then(|m| json::str(m, "resolvedModel")).map(str::to_string)
}

/// Ce qui suit la réponse du moteur.
enum After {
    Close,
    /// Le conseiller choisi (son nom) : refusé si la session n'en a toujours pas.
    Advisor(String),
    /// « Par défaut » enregistré (l'effort était max : il ne l'est pas).
    Default { max: bool },
}

/// Le menu du modèle : une seule liste au clavier (modèles, conseillers, styles, « par défaut »).
pub struct ModelMenu {
    models: Vec<Value>,
    advisors: Vec<String>,
    styles: Vec<String>,
    sel: i32,
    error: Option<String>,
    note: Option<String>,
    job: Option<(Task<Result<(), String>>, After)>,
}

impl ModelMenu {
    /// Les listes, lues à l'ouverture.
    pub fn new(meta: &SessionMeta, heavy: &SessionHeavy) -> ModelMenu {
        let info = heavy.initialize_info.as_ref();
        let models = models(info);
        let cur = current_model(meta.model.as_deref(), info).map(|c| json::string(&c, "value"));
        let sel = models.iter().position(|m| json::str(m, "value") == cur.as_deref()).unwrap_or(0) as i32;
        ModelMenu {
            advisors: advisors(info),
            styles: info.map(|i| json::names(i, "available_output_styles")).unwrap_or_default(),
            models,
            sel,
            error: None,
            note: None,
            job: None,
        }
    }

    pub fn show(&mut self, ctx: &egui::Context, be: &Backend, t: &Tokens, anchor: Rect, id: &str, meta: &SessionMeta, heavy: &SessionHeavy, keys: &[&str]) -> MenuOut {
        let mut close = false;
        if let Some((task, _)) = &self.job
            && let Some(r) = task.take()
        {
            let (_, after) = self.job.take().unwrap();
            match (r, after) {
                (Ok(()), After::Close) => close = true,
                (Ok(()), After::Advisor(l)) => {
                    // plus faible que le modèle : la CLI ne garde pas de conseiller ; le dire et rester ouvert
                    if meta.advisor.is_none() {
                        self.error = Some(t!("Model.AdvisorRefused", l));
                    } else {
                        close = true;
                    }
                }
                (Ok(()), After::Default { max }) => self.note = Some(t!(if max { "Model.DefaultSetNoMax" } else { "Model.DefaultSet" })),
                (Err(e), After::Default { .. }) => self.error = Some(t!("Model.DefaultFailed", e)),
                (Err(e), _) => self.error = Some(t!("Model.SetFailed", e)),
            }
        }
        let cur = current_model(meta.model.as_deref(), heavy.initialize_info.as_ref()).map(|c| json::string(&c, "value"));
        let (nm, na, ns) = (self.models.len(), self.advisors.len(), self.styles.len());
        let count = (nm + na + ns + usize::from(cur.is_some())) as i32;
        let mut activate = None;
        for k in keys {
            match *k {
                "Enter" | " " => activate = Some(self.sel.max(0) as usize),
                "Escape" | "Tab" => close = true,
                k => self.sel = list_nav::next(k, self.sel, count),
            }
        }
        let output_style = meta.output_style.clone().unwrap_or_else(|| "default".into());
        let rect = pop_frame(ctx, t, anchor, "model-menu", |ui| {
            let status = self.error.as_deref().map(|e| (e, true)).or(self.note.as_deref().map(|n| (n, false)));
            status_line(ui, t, &t!("Model"), status);
            let tag = t!("Model.Current");
            for (i, m) in self.models.iter().enumerate() {
                let value = json::string(m, "value");
                let on = cur.as_deref() == Some(value.as_str());
                let name = json::str(m, "displayName").map_or_else(|| value.clone(), str::to_string);
                let desc = json::string(m, "description");
                if option_row(ui, t, i as i32 == self.sel, name_job(&name, if on { t.accent_ink } else { t.text }), &desc, on.then_some(tag.as_str()), &name) {
                    activate = Some(i);
                }
            }
            if na > 0 {
                ui.add_space(4.0);
                ui.label(RichText::new(t!("Model.Advisor")).font(theme::semibold(theme::CAPTION)).color(t.text_faint))
                    .on_hover_text(t!("Model.AdvisorHint"));
                for (j, a) in self.advisors.iter().enumerate() {
                    let n = nm + j;
                    let on = if a == "off" { meta.advisor.is_none() } else { resolved(&self.models, a).is_some_and(|r| Some(&r) == meta.advisor.as_ref()) };
                    let text = if a == "off" { t!("Model.AdvisorOff") } else { label(&self.models, a) };
                    if option_row(ui, t, n as i32 == self.sel, name_job(&text, if on { t.accent_ink } else { t.text }), "", on.then_some(tag.as_str()), &text) {
                        activate = Some(n);
                    }
                }
            }
            if ns > 0 {
                ui.add_space(4.0);
                ui.label(RichText::new(t!("Model.Style")).font(theme::semibold(theme::CAPTION)).color(t.text_faint)).on_hover_text(t!("Model.StyleHint"));
                for (j, st) in self.styles.iter().enumerate() {
                    let n = nm + na + j;
                    let on = *st == output_style;
                    if option_row(ui, t, n as i32 == self.sel, name_job(st, if on { t.accent_ink } else { t.text }), "", on.then_some(tag.as_str()), st) {
                        activate = Some(n);
                    }
                }
            }
            if let Some(cv) = &cur {
                let d = nm + na + ns;
                let l = label(&self.models, cv);
                let sub = match &meta.effort {
                    Some(e) => format!("{l} · {}", i18n::name("Effort.", Some(e))),
                    None => l,
                };
                ui.add_space(4.0);
                if option_row(ui, t, d as i32 == self.sel, name_job(&t!("Model.SetDefault"), t.text), &sub, None, &sub) {
                    activate = Some(d);
                }
            }
            // le cache des requêtes est par modèle : le tour suivant relit toute la conversation au prix fort
            if meta.context_tokens > 0 {
                ui.add_space(4.0);
                ui.label(RichText::new(t!("Model.CacheHint")).font(theme::medium(theme::CAPTION)).color(t.text_faint));
            }
        });
        if let Some(n) = activate
            && self.job.is_none()
        {
            self.error = None;
            self.note = None;
            let sid = id.to_string();
            if n < nm {
                let v = json::string(&self.models[n], "value");
                self.job = Some((act(be, &sid, move |s| async move { s.set_model(&v).await }), After::Close));
            } else if n < nm + na {
                let a = self.advisors[n - nm].clone();
                let l = label(&self.models, &a);
                let off = a == "off";
                let task = act(be, &sid, move |s| async move { s.set_advisor((a != "off").then_some(a)).await });
                self.job = Some((task, if off { After::Close } else { After::Advisor(l) }));
            } else if n < nm + na + ns {
                let st = self.styles[n - nm - na].clone();
                self.job = Some((act(be, &sid, move |s| async move { s.set_output_style(&st).await }), After::Close));
            } else if let Some(v) = cur.clone() {
                let effort = meta.effort.clone();
                let max = effort.as_deref() == Some("max");
                self.job = Some((act(be, &sid, move |s| async move { s.set_default(&v, effort).await }), After::Default { max }));
            }
        }
        MenuOut { rect, close }
    }
}

// ------------------------------------------------------------------------------------------- le mode

/// Ce que fait le choix de `mode` : un mode risqué (auto, acceptEdits) hors d'un worktree isolé demande une seconde
/// activation (`Confirm`), sinon il est appliqué.
#[derive(Debug, PartialEq, Eq)]
pub enum PickStep {
    Confirm,
    Set,
}

pub fn pick_step(mode: &str, current: &str, isolated: bool, confirm: Option<&str>) -> PickStep {
    if mode != current && risky(mode) && !isolated && confirm != Some(mode) { PickStep::Confirm } else { PickStep::Set }
}

/// Le menu du mode d'autorisation (`set_permission_mode` en cours de session : le Maj+Tab de la CLI).
pub struct ModeMenu {
    sel: i32,
    error: Option<String>,
    confirm: Option<String>,
    job: Option<Task<Result<(), String>>>,
}

impl ModeMenu {
    pub fn new(meta: &SessionMeta) -> ModeMenu {
        ModeMenu { sel: MODES.iter().position(|m| *m == meta.mode).unwrap_or(0) as i32, error: None, confirm: None, job: None }
    }

    pub fn show(&mut self, ctx: &egui::Context, be: &Backend, t: &Tokens, anchor: Rect, id: &str, meta: &SessionMeta, keys: &[&str]) -> MenuOut {
        let mut close = false;
        if let Some(task) = &self.job
            && let Some(r) = task.take()
        {
            self.job = None;
            match r {
                Ok(()) => close = true,
                Err(e) => self.error = Some(e),
            }
        }
        let mut pick = None;
        for k in keys {
            match *k {
                "Enter" | " " => pick = Some(MODES[self.sel.clamp(0, MODES.len() as i32 - 1) as usize]),
                "Escape" | "Tab" => close = true,
                k => self.sel = list_nav::next(k, self.sel, MODES.len() as i32),
            }
        }
        let rect = pop_frame(ctx, t, anchor, "mode-menu", |ui| {
            let status = self
                .error
                .clone()
                .map(|e| (e, true))
                .or_else(|| self.confirm.as_ref().map(|c| (t!("Mode.ConfirmAgain", i18n::name("Mode.Name.", Some(c))), false)));
            status_line(ui, t, &t!("Composer.PermissionMode"), status.as_ref().map(|(s, e)| (s.as_str(), *e)));
            for (i, &m) in MODES.iter().enumerate() {
                let on = m == meta.mode;
                let color = if risky(m) { t.warning } else if on { t.accent_ink } else { t.text };
                let tag = t!("Model.Current");
                let name = i18n::name("Mode.Name.", Some(m));
                if option_row(ui, t, i as i32 == self.sel, name_job(&name, color), &t!(&format!("Mode.{m}")), on.then_some(tag.as_str()), &name) {
                    pick = Some(m);
                }
            }
        });
        if let Some(mode) = pick
            && self.job.is_none()
        {
            if pick_step(mode, &meta.mode, meta.isolated, self.confirm.as_deref()) == PickStep::Confirm {
                self.confirm = Some(mode.to_string());
            } else {
                self.error = None;
                self.job = Some(act(be, id, move |s| async move { s.set_mode(mode).await }));
            }
        }
        MenuOut { rect, close }
    }
}

// ------------------------------------------------------------------------------------------- /config

/// La dernière réponse à un « /config » nu dans le fil (un texte de premier niveau qui commence par la ligne
/// d'usage), lue.
pub fn keys(items: &[&Item]) -> Option<Vec<(String, Vec<String>)>> {
    items
        .iter()
        .rev()
        .find_map(|i| match i {
            Item::Text(t) if t.parent_tool_use_id.is_none() && t.markdown.starts_with(CONFIG_USAGE) => Some(&t.markdown),
            _ => None,
        })
        .and_then(|md| config_keys(md))
}

/// Le menu `/config` : les réglages de la CLI par clé. Un choix part en « /config clé=valeur », comme tapé dans le
/// terminal : la CLI vérifie et enregistre, sa réponse arrive dans le fil. Pas de valeurs en cours (la réponse de la
/// CLI n'en donne pas). Échap le ferme (le composeur).
#[derive(Default)]
pub struct ConfigMenu {
    asked: bool,
    error: Option<String>,
    note: Option<String>,
    job: Option<(Task<Result<String, String>>, Option<String>)>,
    /// Les valeurs tapées des clés sans choix.
    values: std::collections::HashMap<String, String>,
}

impl ConfigMenu {
    pub fn show(&mut self, ctx: &egui::Context, be: &Backend, t: &Tokens, anchor: Rect, id: &str, items: &[&Item]) -> MenuOut {
        let found = keys(items);
        if found.is_none() && !self.asked {
            self.asked = true;
            let s = be.session(id);
            self.job = Some((be.task(async move { s?.send("/config".into(), Vec::new()).await }), None));
        }
        if let Some((task, key)) = &self.job
            && let Some(r) = task.take()
        {
            match r {
                Ok(_) => self.note = key.as_ref().map(|k| t!("Config.Sent", k)),
                Err(e) => self.error = Some(t!("Composer.ActionFailed", e)),
            }
            self.job = None;
        }
        let busy = self.job.is_some();
        let mut set: Option<(String, String)> = None;
        let rect = pop_frame(ctx, t, anchor, "config-menu", |ui| {
            let status = self.error.as_deref().map(|e| (e, true)).or(self.note.as_deref().map(|n| (n, false)));
            status_line(ui, t, &t!("Config.Title"), status);
            match &found {
                None => {
                    ui.label(RichText::new(t!("Config.Loading")).font(theme::medium(theme::CAPTION)).color(t.text_faint));
                }
                Some(keys) => {
                    egui::Grid::new("config-grid").num_columns(2).spacing([12.0, 6.0]).show(ui, |ui| {
                        for (key, choices) in keys {
                            ui.label(RichText::new(key).font(theme::mono(theme::LABEL)).color(t.text)).on_hover_text(key);
                            ui.add_enabled_ui(!busy, |ui| {
                                if choices.is_empty() {
                                    let v = self.values.entry(key.clone()).or_default();
                                    let r = ui.add(egui::TextEdit::singleline(v).hint_text(t!("Config.Value")).desired_width(220.0));
                                    if r.lost_focus() && ui.input(|i| i.key_pressed(egui::Key::Enter)) && !v.trim().is_empty() {
                                        set = Some((key.clone(), std::mem::take(v)));
                                    }
                                } else {
                                    ui.horizontal_wrapped(|ui| {
                                        for c in choices {
                                            if ui.small_button(c).clicked() {
                                                set = Some((key.clone(), c.clone()));
                                            }
                                        }
                                    });
                                }
                            });
                            ui.end_row();
                        }
                    });
                }
            }
            ui.add_space(4.0);
            ui.label(RichText::new(t!("Config.Hint")).font(theme::medium(theme::CAPTION)).color(t.text_faint));
        });
        if let Some((key, value)) = set
            && !value.trim().is_empty()
            && self.job.is_none()
        {
            self.error = None;
            self.note = None;
            let s = be.session(id);
            let line = format!("/config {key}={}", value.trim());
            self.job = Some((be.task(async move { s?.send(line, Vec::new()).await }), Some(key)));
        }
        MenuOut { rect, close: false }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use ccui_shared::model::TextItem;
    use serde_json::json;

    fn info() -> Value {
        json!({
            "models": [
                {"value":"default","displayName":"Default","resolvedModel":"claude-opus-5"},
                {"value":"opus","displayName":"Opus","resolvedModel":"claude-opus-5"},
                {"value":"haiku","displayName":"Haiku","resolvedModel":"claude-haiku-5-5"}
            ],
            "commands": [{"name":"advisor","argumentHint":"[fable|opus| sonnet |off]"}]
        })
    }

    #[test]
    fn current_and_advisors() {
        let i = info();
        let v = |m: Option<&str>| current_model(m, Some(&i)).map(|c| json::string(&c, "value"));
        assert_eq!(v(Some("haiku")).as_deref(), Some("haiku"));
        assert_eq!(v(Some("claude-opus-5-20261101")).as_deref(), Some("opus"));
        assert_eq!(v(Some("CLAUDE-HAIKU-5-5")).as_deref(), Some("haiku"));
        assert_eq!(v(Some("gpt")), None);
        assert_eq!(v(None), None);
        assert_eq!(advisors(Some(&i)), ["fable", "opus", "sonnet", "off"]);
        assert!(advisors(Some(&json!({"commands":[]}))).is_empty());
        let meta = SessionMeta { model: Some("haiku".into()), ..Default::default() };
        let heavy = SessionHeavy { initialize_info: Some(i.clone()), ..Default::default() };
        assert_eq!(current(&meta, &heavy), Some(("haiku".into(), "Haiku".into())));
    }

    #[test]
    fn risky_needs_two() {
        assert_eq!(pick_step("auto", "default", false, None), PickStep::Confirm);
        assert_eq!(pick_step("auto", "default", false, Some("auto")), PickStep::Set);
        assert_eq!(pick_step("auto", "default", true, None), PickStep::Set);
        assert_eq!(pick_step("plan", "default", false, None), PickStep::Set);
        assert_eq!(pick_step("acceptEdits", "acceptEdits", false, None), PickStep::Set);
    }

    #[test]
    fn latest_usage() {
        let text = |md: &str, parent: Option<&str>| Item::Text(TextItem { markdown: md.into(), parent_tool_use_id: parent.map(Into::into), uuid: None, at: None });
        let items = [text("hello", None), text(&format!("{CONFIG_USAGE}\n"), Some("agent"))];
        let refs: Vec<&Item> = items.iter().collect();
        assert_eq!(keys(&refs), None);
        let items = [text(&format!("{CONFIG_USAGE}\n"), None), text("after", None)];
        let refs: Vec<&Item> = items.iter().collect();
        assert_eq!(keys(&refs), config_keys(&format!("{CONFIG_USAGE}\n")));
    }
}
