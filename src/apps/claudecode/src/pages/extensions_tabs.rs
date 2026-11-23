//! Le dessin de la page Extensions (`pages/extensions/{mcp,skills,agents,plugins,hooks,permissions}.rs` et les
//! inspecteurs de `mod.rs` de ClaudeCodeUI) : l'en-tête et le choix de la session, les onglets et leurs tableaux,
//! l'inspecteur d'un serveur MCP et celui des options d'un plugin (à droite). Les clics deviennent des `A`, appliqués
//! à l'état (`extensions.rs`) après le dessin.

use ccui_shared::hooks::HookRow;
use ccui_shared::permission_rules::{BEHAVIORS, FILES, visible};
use ccui_shared::plugins::SCOPES;
use ccui_shared::session::SessionHeavy;
use ccui_shared::{fmt, t};
use egui::epaint::RectShape;
use egui::{Align, Color32, CornerRadius, Frame, Layout, Margin, RichText, ScrollArea, Sense, Stroke, TextEdit, Ui, UiBuilder, Vec2};
use serde_json::Value;

use crate::app::Cx;
use crate::pages::extensions::{
    self as x, Extensions, PState, RowAct, Srv, agents, catalog_matches, describe, failed_where, has_process, hook_editable, hook_haystack, hook_key, hook_meta, hook_name,
    matches, plugin_skills, plugin_state, plural, row_act, scope_label, servers, signs_in_here, source_tag, split_source, tools_cell,
};
use crate::pages::new_session::html_to_md;
use crate::theme::{self, Tokens};
use crate::widgets::{self, icon};

/// Ce qu'un clic demande.
enum A {
    Pick(String),
    Show(&'static str),
    Filter(String),
    Select(String),
    Toggle(String, bool),
    Reconnect(String),
    ReconnectAll,
    SignIn(String),
    SignOut(String),
    Start,
    PluginAct(Vec<String>),
    Configure(String),
    ReloadNow,
    Accept,
    SaveOptions(String),
    EditHook(HookRow),
    SaveHook(Option<HookRow>, bool),
    Rule(String, String, String, bool),
    Open(String),
    NewSession,
}

pub fn show(e: &mut Extensions, ui: &mut Ui, cx: &mut Cx) {
    let t = cx.t;
    let mut acts: Vec<A> = Vec::new();
    let live = e.live_id(cx);
    let heavy: SessionHeavy = live.as_deref().and_then(|id| cx.app.session(id)).map(|h| h.heavy.clone()).unwrap_or_default();
    let all = servers(&heavy);

    // ---- l'en-tête : le titre, la session montrée
    widgets::pane_header(
        ui,
        &t,
        |ui| {
            ui.label(RichText::new(t!("Nav.Extensions")).font(theme::semibold(theme::BODY)).color(t.text));
            if let Some(id) = &live {
                let home = cx.app.boot.home.clone();
                let others: Vec<(String, String, String)> = cx.app.summary.sessions.iter().filter(|s| s.has_process || s.id == *id).map(|s| (s.id.clone(), s.name.clone(), s.cwd.clone())).collect();
                let Some((_, name, cwd)) = others.iter().find(|s| s.0 == *id).cloned() else { return };
                if others.len() > 1 {
                    egui::ComboBox::from_id_salt("ext-session").selected_text(format!("{name} · {}", fmt::home(&cwd, &home))).show_ui(ui, |ui| {
                        for (oid, oname, ocwd) in &others {
                            if ui.selectable_label(oid == id, format!("{oname} · {}", fmt::home(ocwd, &home))).clicked() {
                                acts.push(A::Pick(oid.clone()));
                            }
                        }
                    });
                } else {
                    if ui.link(name).clicked() {
                        acts.push(A::Open(id.clone()));
                    }
                    widgets::mono_dim(ui, &t, &fmt::home(&cwd, &home)).on_hover_text(cwd.replace('\\', "/"));
                }
            }
        },
        |_| {},
    );

    // ---- l'inspecteur, à droite (sa largeur est retirée du corps)
    let mut side_w = 0.0;
    if cx.ui.inspector_open {
        if e.tab == "mcp"
            && live.is_some()
            && let Some(sel) = e.selected.as_ref().and_then(|n| all.iter().find(|s| s.name == *n)).cloned()
        {
            side_w = side(ui, &t, |ui| mcp_inspector(e, ui, cx, &sel, &mut acts));
        } else if (e.tab == "plugins" || live.is_none())
            && e.options.is_some()
            && let Some(cid) = e.configuring.clone()
        {
            side_w = side(ui, &t, |ui| options_inspector(e, ui, cx, &cid, &mut acts));
        }
    }

    // ---- le corps
    let body_w = (ui.max_rect().width() - side_w).max(200.0);
    ui.scope_builder(UiBuilder::new().max_rect(egui::Rect::from_min_size(ui.cursor().min, Vec2::new(body_w, ui.available_height()))), |ui| {
        ScrollArea::vertical().auto_shrink([false, false]).show(ui, |ui| {
            Frame::new().inner_margin(Margin { left: 28, right: 28, top: 18, bottom: 32 }).show(ui, |ui| {
                ui.set_max_width((body_w - 56.0).min(1000.0));
                match &live {
                    None => no_live(e, ui, cx, &mut acts),
                    Some(id) => live_body(e, ui, cx, id, &heavy, &all, &mut acts),
                }
            });
        });
    });

    for a in acts {
        apply(e, cx, a, &all);
    }
}

fn apply(e: &mut Extensions, cx: &mut Cx, a: A, all: &[Srv]) {
    match a {
        A::Pick(id) => e.pick = Some(id),
        A::Show(k) => e.show(cx, k),
        A::Filter(f) => e.set_filter(all, &f),
        A::Select(n) => e.select(cx, n),
        A::Toggle(n, on) => e.toggle(cx, n, on),
        A::Reconnect(n) => e.reconnect(cx, n),
        A::ReconnectAll => e.reconnect_all(cx, all.to_vec()),
        A::SignIn(n) => e.sign_in(cx, n),
        A::SignOut(n) => e.sign_out(cx, n),
        A::Start => e.start(cx),
        A::PluginAct(args) => e.plugin_act(cx, args),
        A::Configure(id) => e.configure(cx, id),
        A::ReloadNow => e.reload_now(cx),
        A::Accept => e.accept(cx),
        A::SaveOptions(id) => e.save_options(cx, id),
        A::EditHook(h) => e.edit_hook(h),
        A::SaveHook(target, remove) => e.save_hook(cx, target, remove),
        A::Rule(source, behavior, rule, add) => e.edit_rule(cx, source, behavior, rule, add),
        A::Open(id) => cx.open_session(&id),
        A::NewSession => cx.navigate(crate::app::Route::NewSession),
    }
}

// ============================================================================================ pièces

/// Un panneau à droite (l'inspecteur de la page).
fn side(ui: &mut Ui, t: &Tokens, add: impl FnOnce(&mut Ui)) -> f32 {
    egui::Panel::right("ext-inspector")
        .resizable(true)
        .default_size(380.0)
        .size_range(300.0..=620.0)
        .frame(Frame::new().fill(t.chrome).stroke(Stroke::new(1.0, t.separator)))
        .show(ui, |ui| {
            ScrollArea::vertical().auto_shrink([false, false]).show(ui, |ui| {
                Frame::new().inner_margin(Margin::same(16)).show(ui, add);
            });
        })
        .response
        .rect
        .width()
}

fn h2(ui: &mut Ui, t: &Tokens, text: &str) {
    ui.label(RichText::new(text).font(theme::semibold(theme::TITLE)).color(t.text));
}

fn h3(ui: &mut Ui, t: &Tokens, text: &str) {
    ui.add_space(10.0);
    ui.label(RichText::new(text).font(theme::semibold(theme::SECTION)).color(t.text));
}

fn lead(ui: &mut Ui, t: &Tokens, text: &str) {
    ui.add(egui::Label::new(RichText::new(text).color(t.text_dim)).wrap());
}

fn hint(ui: &mut Ui, t: &Tokens, text: &str) {
    ui.add(egui::Label::new(RichText::new(text).font(egui::FontId::proportional(theme::CAPTION)).color(t.text_faint)).wrap());
}

/// Une boîte d'erreur (`.errbox`).
fn errbox(ui: &mut Ui, t: &Tokens, text: &str) {
    Frame::new()
        .fill(t.danger.gamma_multiply(0.10))
        .stroke(Stroke::new(1.0, t.danger.gamma_multiply(0.5)))
        .corner_radius(CornerRadius::same(theme::R))
        .inner_margin(Margin::symmetric(10, 8))
        .show(ui, |ui| {
            ui.set_width(ui.available_width());
            ui.add(egui::Label::new(RichText::new(text.trim_end()).font(theme::mono(theme::CAPTION)).color(t.danger)).wrap());
        });
    ui.add_space(4.0);
}

/// Un point et son texte (`.ms ok|auth|fail|off|wait`).
fn dot(ui: &mut Ui, t: &Tokens, cls: &str, text: &str) -> egui::Response {
    let c = match cls {
        "ok" => t.success,
        "auth" => t.warning,
        "fail" => t.danger,
        "wait" => t.accent_ink,
        _ => t.text_faint,
    };
    ui.horizontal(|ui| {
        ui.spacing_mut().item_spacing.x = 4.0;
        widgets::dot(ui, c, true);
        ui.label(RichText::new(text).font(theme::medium(theme::LABEL)).color(c));
    })
    .response
}

/// `State(status)`.
fn state_view(ui: &mut Ui, t: &Tokens, status: &str) {
    let (cls, text) = match status {
        "connected" => ("ok", t!("Ext.State.Connected")),
        "needs-auth" => ("auth", t!("Ext.State.NeedsAuth")),
        "failed" => ("fail", t!("Ext.State.Failed")),
        "disabled" => ("off", t!("Ext.State.Disabled")),
        "pending" => ("wait", t!("Ext.State.Pending")),
        _ => ("off", status.to_string()),
    };
    dot(ui, t, cls, &text);
}

/// Une puce de filtre (`Filter(key, label, n)`) : cliquée ?
fn filter_chip(ui: &mut Ui, t: &Tokens, label: &str, n: usize, on: bool) -> bool {
    let text = RichText::new(format!("{label}  {n}")).font(theme::mono(theme::CAPTION)).color(if on { t.accent_ink } else { t.text_dim });
    ui.add(egui::Button::new(text).fill(if on { t.accent_soft } else { Color32::TRANSPARENT }).stroke(Stroke::new(1.0, if on { t.accent_ink.gamma_multiply(0.5) } else { t.card_border })).corner_radius(CornerRadius::same(10)))
        .clicked()
}

/// `FindBox(shown, all)`.
fn find_box(ui: &mut Ui, t: &Tokens, query: &mut String, shown: usize, all: usize) {
    ui.horizontal(|ui| {
        ui.label(RichText::new(icon("search")).color(t.text_faint));
        ui.add(TextEdit::singleline(query).hint_text(t!("Ext.Find")).desired_width(280.0));
        if !query.trim().is_empty() {
            widgets::dim(ui, t, &t!("Ext.FindCount", shown, all));
        }
    });
    ui.add_space(4.0);
}

/// Les largeurs des colonnes (fractions de la largeur) ; la dernière colonne (actions) est alignée à droite.
fn cells(ui: &mut Ui, fr: &[f32], right_last: bool, mut cell: impl FnMut(&mut Ui, usize)) {
    let gap = 10.0;
    let inner = ui.available_width() - gap * (fr.len().saturating_sub(1)) as f32;
    ui.horizontal_top(|ui| {
        ui.spacing_mut().item_spacing.x = gap;
        for (i, f) in fr.iter().enumerate() {
            let w = (inner * f).max(10.0);
            let layout = if right_last && i + 1 == fr.len() { Layout::right_to_left(Align::Min) } else { Layout::top_down(Align::Min) };
            ui.allocate_ui_with_layout(Vec2::new(w, 0.0), layout, |ui| {
                ui.set_width(w);
                cell(ui, i);
            });
        }
    });
}

/// L'en-tête d'un tableau (petites capitales).
fn thead(ui: &mut Ui, t: &Tokens, fr: &[f32], names: &[String]) {
    Frame::new().inner_margin(Margin::symmetric(10, 4)).show(ui, |ui| {
        cells(ui, fr, false, |ui, i| {
            if let Some(n) = names.get(i) {
                widgets::cap(ui, t, n);
            }
        });
    });
    let y = ui.cursor().top();
    ui.painter().hline(ui.max_rect().x_range(), y, Stroke::new(1.0, t.separator));
}

/// Une ligne de tableau, cliquable derrière ses boutons ; choisie : sur fond.
fn trow(ui: &mut Ui, t: &Tokens, sel: bool, fr: &[f32], cell: impl FnMut(&mut Ui, usize)) -> egui::Response {
    let bg = ui.painter().add(egui::Shape::Noop);
    let r = ui
        .scope_builder(UiBuilder::new().sense(Sense::click()), |ui| {
            Frame::new().inner_margin(Margin::symmetric(10, 8)).show(ui, |ui| {
                ui.set_width(ui.available_width());
                cells(ui, fr, true, cell);
            });
        })
        .response;
    let fill = if sel { t.hover } else if r.hovered() { t.hover.gamma_multiply(0.5) } else { Color32::TRANSPARENT };
    ui.painter().set(bg, RectShape::filled(r.rect, CornerRadius::same(theme::R), fill));
    if sel {
        ui.painter().vline(r.rect.left() + 1.0, r.rect.y_range(), Stroke::new(2.0, t.accent_ink));
    }
    ui.painter().hline(r.rect.x_range(), r.rect.bottom(), Stroke::new(1.0, t.separator));
    r
}

fn name_cell(ui: &mut Ui, t: &Tokens, name: &str, sub: &str) {
    ui.add(egui::Label::new(RichText::new(name).font(theme::medium(theme::SMALL)).color(t.text)).truncate());
    if !sub.is_empty() {
        ui.add(egui::Label::new(RichText::new(sub).font(theme::mono(theme::CAPTION)).color(t.text_faint)).truncate()).on_hover_text(sub);
    }
}

/// Un bouton de retrait armé : le premier clic arme (en rouge), le second agit.
fn armed_button(e: &mut Extensions, ui: &mut Ui, t: &Tokens, key: &str, label: &str, confirm: &str) -> bool {
    let busy = e.busy();
    let armed = e.armed(key);
    let r = if armed {
        ui.add_enabled_ui(!busy, |ui| widgets::danger(ui, t, confirm)).inner
    } else {
        ui.add_enabled(!busy, egui::Button::new(RichText::new(label).color(t.text_dim)).frame(false))
    };
    r.clicked() && e.arm_or_go(key)
}

fn disabled_button(ui: &mut Ui, enabled: bool, text: &str) -> bool {
    ui.add_enabled(enabled, egui::Button::new(text)).clicked()
}

// ============================================================================================ sans session

fn no_live(e: &mut Extensions, ui: &mut Ui, cx: &mut Cx, acts: &mut Vec<A>) {
    let t = cx.t;
    h2(ui, &t, &t!("Nav.Extensions"));
    lead(ui, &t, &t!("Ext.NoSession"));
    ui.add_space(8.0);
    if widgets::primary(ui, &t, &format!("{} {}", icon("plus"), t!("Rail.NewSession"))).clicked() {
        acts.push(A::NewSession);
    }
    h3(ui, &t, &t!("Ext.Plugins.ForHome"));
    hint(ui, &t, &t!("Ext.Plugins.NoSessionNote"));
    ui.add_space(6.0);
    plugins_tab(e, ui, cx, None, &[], acts);
}

// ============================================================================================ avec une session

fn live_body(e: &mut Extensions, ui: &mut Ui, cx: &mut Cx, id: &str, heavy: &SessionHeavy, all: &[Srv], acts: &mut Vec<A>) {
    let t = cx.t;
    let proc = has_process(cx, id);
    h2(ui, &t, &t!("Nav.Extensions"));
    lead(ui, &t, &t!("Ext.Lead"));
    ui.add_space(10.0);

    // les onglets et leurs nombres
    let srv_n = (proc || heavy.mcp_status.is_some()).then_some(all.len());
    let skills_n = e.skills(heavy).map(|s| s.len());
    let agents_n = (heavy.initialize_info.is_some() || heavy.init.is_some()).then(|| agents(heavy).len());
    let plugins_n = e.installed.as_ref().map(Vec::len);
    let hooks_n = e.hooks.as_ref().map(|l| l.rows.len());
    let perms_n = e.perms.as_ref().map(|p| p.rules.len());
    let tabs: [(&'static str, String, Option<usize>); 6] = [
        ("mcp", "MCP".into(), srv_n),
        ("skills", t!("Ext.Tab.Skills"), skills_n),
        ("agents", t!("Ext.Tab.Agents"), agents_n),
        ("plugins", t!("Ext.Tab.Plugins"), plugins_n),
        ("hooks", t!("Ext.Tab.Hooks"), hooks_n),
        ("permissions", t!("Ext.Tab.Permissions"), perms_n),
    ];
    ui.horizontal(|ui| {
        ui.spacing_mut().item_spacing.x = 14.0;
        for (key, label, n) in tabs {
            let on = e.tab == key;
            let text = RichText::new(format!("{label}  {}", n.map_or_else(|| "—".to_string(), |n| n.to_string())))
                .font(if on { theme::semibold(theme::SMALL) } else { theme::medium(theme::SMALL) })
                .color(if on { t.text } else { t.text_dim });
            let r = ui.add(egui::Button::new(text).frame(false).min_size(Vec2::new(0.0, 30.0)));
            if on {
                ui.painter().hline(r.rect.x_range(), r.rect.bottom() + 2.0, Stroke::new(2.0, t.accent_ink));
            }
            if r.clicked() && !on {
                acts.push(A::Show(key));
            }
        }
    });
    let y = ui.cursor().top() + 2.0;
    ui.painter().hline(ui.max_rect().x_range(), y, Stroke::new(1.0, t.separator));
    ui.add_space(12.0);

    if !proc && e.tab != "plugins" {
        let exit = cx.app.summary.sessions.iter().find(|x| x.id == id).and_then(|x| x.exit_code);
        lead(ui, &t, &exit.map_or_else(|| t!("Ext.NoProcess"), |c| t!("Ext.NoProcessExited", c)));
        ui.add_space(6.0);
        if ui.add_enabled_ui(!e.busy(), |ui| widgets::primary(ui, &t, &t!("Ext.StartProcess"))).inner.clicked() {
            acts.push(A::Start);
        }
        return;
    }
    match e.tab.as_str() {
        "skills" => skills_tab(e, ui, &t, heavy),
        "agents" => agents_tab(e, ui, &t, heavy),
        "plugins" => plugins_tab(e, ui, cx, Some(heavy), all, acts),
        "hooks" => hooks_tab(e, ui, cx, id, acts),
        "permissions" => perms_tab(e, ui, cx, id, acts),
        _ => mcp_tab(e, ui, &t, heavy, all, acts),
    }
}

// ============================================================================================ MCP

const MCP_FILTERS: [(&str, &str); 4] = [("connected", "Ext.Filter.Connected"), ("needs-auth", "Ext.Filter.Auth"), ("failed", "Ext.Filter.Failed"), ("disabled", "Ext.Filter.Disabled")];

fn mcp_tab(e: &mut Extensions, ui: &mut Ui, t: &Tokens, heavy: &SessionHeavy, all: &[Srv], acts: &mut Vec<A>) {
    if let Some(n) = &e.note {
        errbox(ui, t, n);
    }
    let count = |st: &str| all.iter().filter(|x| x.status == st).count();
    ui.horizontal_wrapped(|ui| {
        if filter_chip(ui, t, &t!("Filter.All"), all.len(), e.filter.is_empty()) {
            acts.push(A::Filter(String::new()));
        }
        for (st, lbl) in MCP_FILTERS {
            if (count(st) != 0 || e.filter == st) && filter_chip(ui, t, &t!(lbl), count(st), e.filter == st) {
                acts.push(A::Filter(st.to_string()));
            }
        }
        if count("failed") > 1 && disabled_button(ui, !e.busy(), &format!("{} {}", icon("refresh"), t!("Ext.RetryAll"))) {
            acts.push(A::ReconnectAll);
        }
    });
    for s in heavy.init.as_ref().and_then(|i| i.mcp_errors.clone()).unwrap_or_default() {
        errbox(ui, t, &t!("Ext.Skipped", s.name, s.message));
    }
    if all.iter().any(|x| x.status == "connected" || x.status == "disabled") {
        hint(ui, t, &t!("Ext.SwitchesHint"));
    }
    ui.add_space(6.0);
    let shown: Vec<&Srv> = all.iter().filter(|x| e.filter.is_empty() || x.status == e.filter).collect();
    let fr = [0.42, 0.26, 0.12, 0.20];
    thead(ui, t, &fr, &[t!("Ext.Col.Server"), t!("Col.State"), t!("Col.Tools"), String::new()]);
    let busy = e.busy();
    for m in &shown {
        let sel = e.selected.as_deref() == Some(m.name.as_str());
        let r = trow(ui, t, sel, &fr, |ui, i| match i {
            0 => name_cell(ui, t, &m.short, &m.origin),
            1 => state_view(ui, t, &m.status),
            2 => {
                ui.label(RichText::new(tools_cell(m)).font(theme::mono(theme::LABEL)).color(t.text_dim));
            }
            _ => match row_act(m) {
                RowAct::Retry => {
                    if disabled_button(ui, !busy, &t!("Ext.Retry")) {
                        acts.push(A::Reconnect(m.name.clone()));
                    }
                }
                RowAct::SignIn => {
                    if disabled_button(ui, !busy, &t!("Ext.SignIn")) {
                        acts.push(A::SignIn(m.name.clone()));
                    }
                }
                RowAct::Toggle => {
                    let on = m.status == "connected";
                    if crate::pages::form::switch(ui, t, on, "", !busy).on_hover_text(t!("Ext.EnableHereHint")).clicked() {
                        acts.push(A::Toggle(m.name.clone(), !on));
                    }
                }
                RowAct::None => {}
            },
        });
        if r.clicked() {
            acts.push(A::Select(m.name.clone()));
        }
    }
    if shown.is_empty() {
        ui.add_space(6.0);
        lead(ui, t, &t!("Ext.FilterEmpty"));
    }
}

/// L'inspecteur MCP : le serveur choisi (échec, connexion, détails, outils).
fn mcp_inspector(e: &mut Extensions, ui: &mut Ui, cx: &mut Cx, s: &Srv, acts: &mut Vec<A>) {
    let t = cx.t;
    let busy = e.busy();
    ui.horizontal(|ui| {
        ui.label(RichText::new(&s.short).font(theme::semibold(theme::SECTION)).color(t.text));
        widgets::mono_dim(ui, &t, &s.origin);
        ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
            if widgets::ghost(ui, &t, icon("x")).on_hover_text(t!("Ext.CloseInspector")).clicked() {
                cx.ui.inspector_open = false;
            }
        });
    });
    ui.add_space(8.0);
    let turn_off = |ui: &mut Ui, acts: &mut Vec<A>| {
        if ui.add_enabled(!busy, egui::Button::new(t!("Ext.TurnOff")).frame(false)).on_hover_text(t!("Ext.EnableHereHint")).clicked() {
            acts.push(A::Toggle(s.name.clone(), false));
        }
    };
    match s.status.as_str() {
        "failed" => {
            ui.label(RichText::new(t!("Ext.FailedTitle")).font(theme::semibold(theme::BODY)).color(t.text));
            if let Some(err) = &s.error {
                errbox(ui, &t, err);
            }
            lead(ui, &t, &format!("{} {}", t!("Ext.FailedText"), failed_where(s, e.installed.as_deref())));
            ui.horizontal(|ui| {
                if ui.add_enabled_ui(!busy, |ui| widgets::primary(ui, &t, &format!("{} {}", icon("refresh"), t!("Ext.Retry")))).inner.clicked() {
                    acts.push(A::Reconnect(s.name.clone()));
                }
                turn_off(ui, acts);
            });
        }
        "needs-auth" => {
            ui.label(RichText::new(t!("Ext.AuthTitle")).font(theme::semibold(theme::BODY)).color(t.text));
            lead(ui, &t, &t!("Ext.AuthText"));
            if !signs_in_here(s.transport.as_deref(), &s.tag) {
                lead(ui, &t, &t!("Ext.AuthHow"));
            } else {
                match e.auth.as_ref().filter(|a| a.name == s.name) {
                    Some(a) => {
                        lead(ui, &t, &t!("Ext.AuthWaiting"));
                        if widgets::primary(ui, &t, &t!("Ext.OpenSignIn")).on_hover_text(&a.url).clicked() {
                            crate::backend::open_external(&a.url);
                        }
                    }
                    None => {
                        if ui.add_enabled_ui(!busy, |ui| widgets::primary(ui, &t, &format!("{} {}", icon("plug"), t!("Ext.SignIn")))).inner.clicked() {
                            acts.push(A::SignIn(s.name.clone()));
                        }
                    }
                }
                lead(ui, &t, &t!("Ext.AuthHowAlt"));
            }
            ui.horizontal(|ui| {
                ui.label(RichText::new("/mcp").font(theme::mono(theme::CODE)));
                if ui.button(format!("{} {}", icon("copy"), t!("Md.Copy"))).clicked() {
                    ui.ctx().copy_text("/mcp".into());
                }
            });
            turn_off(ui, acts);
        }
        "disabled" => lead(ui, &t, &t!("Ext.DisabledText")),
        _ => {}
    }
    ui.add_space(10.0);
    ui.separator();
    let tools = s.tools.as_ref().map(|t| t.len().to_string()).unwrap_or_else(|| if s.status == "failed" { "0".into() } else { "—".into() });
    egui::Grid::new("mcp-kv").num_columns(2).spacing([16.0, 6.0]).show(ui, |ui| {
        let mut kv = |k: String, v: String| {
            widgets::mono_dim(ui, &t, &k);
            ui.add(egui::Label::new(RichText::new(v).font(theme::mono(theme::CAPTION)).color(t.text)).wrap());
            ui.end_row();
        };
        kv(t!("Ext.Kv.Source"), s.origin.clone());
        kv(t!("Ext.Kv.Transport"), s.transport.clone().unwrap_or_else(|| "—".into()));
        if let Some(tg) = &s.target {
            kv(t!("Ext.Kv.Target"), tg.clone());
        }
        kv(t!("Kv.Tools"), tools);
        kv(t!("Ext.Kv.Scope"), s.scope.as_deref().map_or_else(|| "—".to_string(), scope_label));
        if let Some(v) = &s.version {
            kv(t!("Ext.Kv.Version"), v.clone());
        }
    });
    if s.status == "connected" && matches!(s.transport.as_deref(), Some("http" | "sse")) && disabled_button(ui, !busy, &t!("Ext.SignOut")) {
        acts.push(A::SignOut(s.name.clone()));
    }
    if let Some(tools) = s.tools.as_ref().filter(|t| !t.is_empty()) {
        ui.add_space(10.0);
        ui.separator();
        ui.label(RichText::new(t!("Col.Tools")).font(theme::semibold(theme::BODY)).color(t.text));
        for tl in tools {
            ui.horizontal_wrapped(|ui| {
                ui.label(RichText::new(&tl.name).font(theme::mono(theme::LABEL)).color(t.text));
                if tl.read_only {
                    widgets::tag(ui, &t, &t!("Ext.Tool.ReadOnly"));
                }
                if tl.destructive {
                    widgets::tag(ui, &t, &t!("Ext.Tool.Destructive"));
                }
                if tl.open_world {
                    widgets::tag(ui, &t, &t!("Ext.Tool.OpenWorld"));
                }
            });
            if let Some(d) = tl.description.as_ref().filter(|d| !d.is_empty()) {
                hint(ui, &t, d);
            }
            ui.add_space(4.0);
        }
    }
}

// ============================================================================================ skills, agents

fn skills_tab(e: &mut Extensions, ui: &mut Ui, t: &Tokens, heavy: &SessionHeavy) {
    let Some(skills) = e.skills(heavy) else {
        lead(ui, t, &t!("Ext.Plugins.Loading"));
        return;
    };
    let rows: Vec<(String, Option<String>)> = skills.iter().map(|k| (k.clone(), describe(heavy, k))).filter(|(k, d)| matches(&e.query, k, d.as_deref())).collect();
    find_box(ui, t, &mut e.query, rows.len(), skills.len());
    if rows.is_empty() {
        lead(ui, t, &t!("Ext.FindEmpty"));
        return;
    }
    let fr = [0.32, 0.68];
    thead(ui, t, &fr, &[t!("Ext.Col.Skill"), t!("Ext.Col.Description")]);
    for (name, d) in rows {
        let key = format!("k:{name}");
        let open = e.expanded.contains(&key);
        let (text, origin) = split_source(d.as_deref());
        let r = trow(ui, t, open, &fr, |ui, i| {
            if i == 0 {
                ui.label(RichText::new(&name).font(theme::mono(theme::LABEL)).color(t.text));
                if let Some(o) = origin.as_ref().filter(|_| !name.contains(':')) {
                    widgets::tag(ui, t, o);
                }
            } else {
                desc(ui, t, text.as_deref().unwrap_or("—"), open);
            }
        });
        if r.clicked() {
            e.expand(key);
        }
    }
}

/// Une description, sur une ligne (ouverte : en entier).
fn desc(ui: &mut Ui, t: &Tokens, text: &str, open: bool) {
    // (sur la largeur de la cellule, sans la déborder)
    ui.with_layout(Layout::top_down_justified(Align::Min), |ui| {
        let l = egui::Label::new(RichText::new(text).color(t.text_dim));
        ui.add(if open { l.wrap() } else { l.truncate() });
    });
}

fn agents_tab(e: &mut Extensions, ui: &mut Ui, t: &Tokens, heavy: &SessionHeavy) {
    let all = agents(heavy);
    let rows: Vec<_> = all.iter().filter(|a| matches(&e.query, &a.0, a.1.as_deref())).cloned().collect();
    find_box(ui, t, &mut e.query, rows.len(), all.len());
    if rows.is_empty() {
        lead(ui, t, &t!("Ext.FindEmpty"));
        return;
    }
    let fr = [0.28, 0.56, 0.16];
    thead(ui, t, &fr, &[t!("Agent.Title"), t!("Ext.Col.Description"), t!("Model")]);
    for (name, d, model) in rows {
        let key = format!("a:{name}");
        let open = e.expanded.contains(&key);
        let r = trow(ui, t, open, &fr, |ui, i| match i {
            0 => {
                ui.label(RichText::new(&name).font(theme::mono(theme::LABEL)).color(t.text));
            }
            1 => desc(ui, t, d.as_deref().unwrap_or("—"), open),
            _ => {
                widgets::tag(ui, t, model.as_deref().unwrap_or("—"));
            }
        });
        if r.clicked() {
            e.expand(key);
        }
    }
}

// ============================================================================================ plugins

fn plugins_tab(e: &mut Extensions, ui: &mut Ui, cx: &mut Cx, heavy: Option<&SessionHeavy>, all: &[Srv], acts: &mut Vec<A>) {
    let t = cx.t;
    let busy = e.busy();
    let live = e.live_id(cx);
    if let Some(id) = &live
        && heavy.is_some()
        && disabled_button(ui, !busy && has_process(cx, id), &format!("{} {}", icon("refresh"), t!("Ext.Plugins.Reload")))
    {
        acts.push(A::ReloadNow);
    }
    if let Some(d) = &e.plugins_done {
        lead(ui, &t, d);
    }
    if let Some(n) = &e.plugins_note {
        errbox(ui, &t, n);
    }
    for pe in heavy.and_then(|h| h.init.as_ref().and_then(|i| i.plugin_errors.clone())).unwrap_or_default() {
        errbox(ui, &t, &t!("Ext.Plugins.LoadError", pe.plugin, pe.message));
    }
    if let Some((o, a)) = e.to_accept.clone() {
        errbox(ui, &t, &format!("{}\n{}", t!("Ext.Plugins.Command", a.get(1).cloned().unwrap_or_default()), o.command.clone().unwrap_or_default()));
        ui.horizontal(|ui| {
            if ui.add_enabled_ui(!busy, |ui| widgets::primary(ui, &t, &t!("Ext.Plugins.Accept"))).inner.clicked() {
                acts.push(A::Accept);
            }
            if ui.button(t!("Action.Cancel")).clicked() {
                e.to_accept = None;
            }
        });
    }
    let skills = heavy.and_then(|h| e.skills(h));
    match e.installed.clone() {
        None => lead(ui, &t, &t!("Ext.Plugins.Loading")),
        Some(v) if v.is_empty() => lead(ui, &t, &t!("Ext.Plugins.None")),
        Some(installed) => {
            if heavy.is_some_and(|h| h.init.is_none()) {
                lead(ui, &t, &t!("Ext.Plugins.Lifecycle"));
            }
            let fr = [0.30, 0.20, 0.09, 0.06, 0.06, 0.29];
            thead(ui, &t, &fr, &[t!("Ext.Col.Plugin"), t!("Col.State"), t!("Ext.Col.Scope"), "MCP".into(), if skills.is_some() { t!("Ext.Tab.Skills") } else { String::new() }, String::new()]);
            let srv = heavy.map(|_| all.to_vec());
            let init = heavy.and_then(|h| h.init.clone());
            for p in installed {
                let st = plugin_state(&p, init.as_ref(), srv.as_deref(), skills.as_deref(), e.hooks.as_ref(), heavy.is_some());
                let editable = matches!(p.scope.as_str(), "user" | "project" | "local");
                let key = format!("u:{}|{}", p.id, p.scope);
                trow(ui, &t, false, &fr, |ui, i| match i {
                    0 => {
                        name_cell(ui, &t, p.name(), &format!("{} · {}", p.id, p.version));
                        for er in &p.errors {
                            errbox(ui, &t, er);
                        }
                        for n in &p.notes {
                            hint(ui, &t, n);
                        }
                    }
                    1 => plugin_state_cell(ui, &t, &st, acts),
                    2 => {
                        widgets::tag(ui, &t, &scope_label(&p.scope));
                    }
                    3 => {
                        ui.label(RichText::new(p.mcp_servers.len().to_string()).font(theme::mono(theme::LABEL)).color(t.text_dim));
                    }
                    4 => {
                        if let Some(sk) = &skills {
                            let n = if p.enabled { plugin_skills(sk, p.name()).to_string() } else { "—".into() };
                            ui.label(RichText::new(n).font(theme::mono(theme::LABEL)).color(t.text_dim));
                        }
                    }
                    _ => {
                        if editable {
                            let pact = |verb: &str| vec![verb.to_string(), p.id.clone(), "--scope".into(), p.scope.clone()];
                            if armed_button(e, ui, &t, &key, &t!("Ext.Plugins.Uninstall"), &t!("Ext.Plugins.ConfirmUninstall")) {
                                acts.push(A::PluginAct(pact("uninstall")));
                            }
                            if ui.add_enabled(!busy, egui::Button::new(RichText::new(t!("Ext.Plugins.Update")).color(t.text_dim)).frame(false)).clicked() {
                                acts.push(A::PluginAct(pact("update")));
                            }
                            if p.has_options && disabled_button(ui, !busy, &t!("Ext.Plugins.Options")) {
                                acts.push(A::Configure(p.id.clone()));
                            }
                            if crate::pages::form::switch(ui, &t, p.enabled, "", !busy).on_hover_text(t!("Ext.Plugins.Enable")).clicked() {
                                acts.push(A::PluginAct(pact(if p.enabled { "disable" } else { "enable" })));
                            }
                        }
                    }
                });
            }
        }
    }

    // installer
    crate::pages::form::field_label(ui, &t, &t!("Ext.Plugins.Install"));
    ui.horizontal(|ui| {
        let r = ui.add_enabled(!busy, TextEdit::singleline(&mut e.install_draft).hint_text("name@marketplace").font(theme::mono(theme::CODE)).desired_width(320.0));
        let cat = catalog_matches(&e.available, e.install_draft.trim());
        if !cat.is_empty() {
            ui.menu_button(icon("chev-down"), |ui| {
                ScrollArea::vertical().max_height(320.0).show(ui, |ui| {
                    for a in cat {
                        let b = ui.button(&a.id);
                        let b = match &a.description {
                            Some(d) => b.on_hover_text(d),
                            None => b,
                        };
                        if b.clicked() {
                            e.install_draft = a.id.clone();
                            ui.close();
                        }
                    }
                });
            });
        }
        egui::ComboBox::from_id_salt("plugin-scope").selected_text(source_tag(&format!("{}Settings", e.install_scope))).show_ui(ui, |ui| {
            for sc in SCOPES {
                ui.selectable_value(&mut e.install_scope, sc.to_string(), source_tag(&format!("{sc}Settings")));
            }
        });
        let go = ui.add_enabled_ui(!busy && !e.install_draft.trim().is_empty(), |ui| widgets::primary(ui, &t, &format!("{} {}", icon("plus"), t!("Ext.Plugins.InstallButton")))).inner.clicked();
        if go || (r.lost_focus() && ui.input(|i| i.key_pressed(egui::Key::Enter)) && !e.install_draft.trim().is_empty()) {
            acts.push(A::PluginAct(vec!["install".into(), e.install_draft.trim().to_string(), "--scope".into(), e.install_scope.clone()]));
        }
    });

    // les places de marché
    h3(ui, &t, &t!("Ext.Plugins.Markets"));
    let markets = e.markets.clone().unwrap_or_default();
    if !markets.is_empty() {
        let fr = [0.30, 0.45, 0.25];
        thead(ui, &t, &fr, &[t!("Ext.Plugins.Market"), t!("Ext.Col.Source"), String::new()]);
        for m in markets {
            let key = format!("m:{}", m.name);
            trow(ui, &t, false, &fr, |ui, i| match i {
                0 => {
                    ui.label(RichText::new(&m.name).font(theme::medium(theme::SMALL)).color(t.text));
                }
                1 => {
                    ui.add(egui::Label::new(RichText::new(&m.location).font(theme::mono(theme::CAPTION)).color(t.text_dim)).truncate());
                }
                _ => {
                    if armed_button(e, ui, &t, &key, &t!("Ext.Plugins.Remove"), &t!("Ext.Plugins.ConfirmRemove")) {
                        acts.push(A::PluginAct(vec!["marketplace".into(), "remove".into(), m.name.clone()]));
                    }
                    if ui.add_enabled(!busy, egui::Button::new(RichText::new(t!("Ext.Plugins.Update")).color(t.text_dim)).frame(false)).clicked() {
                        acts.push(A::PluginAct(vec!["marketplace".into(), "update".into(), m.name.clone()]));
                    }
                }
            });
        }
    }
    crate::pages::form::field_label(ui, &t, &t!("Ext.Plugins.AddMarket"));
    ui.horizontal(|ui| {
        let r = ui.add_enabled(!busy, TextEdit::singleline(&mut e.market_draft).hint_text("owner/repo").font(theme::mono(theme::CODE)).desired_width(320.0));
        let go = ui.add_enabled_ui(!busy && !e.market_draft.trim().is_empty(), |ui| widgets::primary(ui, &t, &format!("{} {}", icon("plus"), t!("Ext.Plugins.AddMarketButton")))).inner.clicked();
        if go || (r.lost_focus() && ui.input(|i| i.key_pressed(egui::Key::Enter)) && !e.market_draft.trim().is_empty()) {
            acts.push(A::PluginAct(vec!["marketplace".into(), "add".into(), e.market_draft.trim().to_string()]));
        }
    });
}

/// L'état d'un plugin ; ceux d'un serveur mènent à ce serveur, sur l'onglet MCP.
fn plugin_state_cell(ui: &mut Ui, t: &Tokens, st: &PState, acts: &mut Vec<A>) {
    let to_server = |ui: &mut Ui, m: &Srv, cls: &str, text: String, acts: &mut Vec<A>| {
        let r = dot(ui, t, cls, &text).interact(Sense::click()).on_hover_text(t!("Ext.Plugins.ShowServer", m.short.clone()));
        if r.clicked() {
            acts.push(A::Show("mcp"));
            acts.push(A::Select(m.name.clone()));
        }
    };
    match st {
        PState::Off => state_view(ui, t, "disabled"),
        PState::Failed => {
            dot(ui, t, "fail", &t!("Ext.Plugins.State.Failed"));
        }
        PState::ServerFailing(n, m) => to_server(ui, m, "fail", plural(*n, "Ext.Plugins.State.ServerFailing"), acts),
        PState::NeedsAuth(m) => to_server(ui, m, "auth", t!("Ext.State.NeedsAuth"), acts),
        PState::ServersOff(n, m) => to_server(ui, m, "off", plural(*n, "Ext.Plugins.State.ServersOff"), acts),
        PState::Loaded => {
            dot(ui, t, "ok", &t!("Ext.Plugins.State.Loaded"));
        }
        PState::Reload => {
            dot(ui, t, "wait", &t!("Ext.Plugins.State.Reload"));
        }
        PState::On => {
            dot(ui, t, "wait", &t!("Ext.Plugins.State.On"));
        }
    }
}

/// L'inspecteur des options d'un plugin (`configuring`).
fn options_inspector(e: &mut Extensions, ui: &mut Ui, cx: &mut Cx, cid: &str, acts: &mut Vec<A>) {
    let t = cx.t;
    let busy = e.busy();
    ui.horizontal(|ui| {
        ui.label(RichText::new(t!("Ext.Plugins.OptionsTitle", cid.split('@').next().unwrap_or_default())).font(theme::semibold(theme::SECTION)).color(t.text));
        ui.with_layout(Layout::right_to_left(Align::Center), |ui| {
            if widgets::ghost(ui, &t, icon("x")).on_hover_text(t!("Ext.CloseInspector")).clicked() {
                cx.ui.inspector_open = false;
            }
        });
    });
    widgets::mono_dim(ui, &t, cid);
    ui.add_space(8.0);
    for o in e.options.clone().unwrap_or_default() {
        ui.horizontal(|ui| {
            ui.label(RichText::new(&o.title).font(theme::semibold(theme::LABEL)).color(t.text));
            if o.required {
                widgets::tag(ui, &t, &t!("Ext.Plugins.Required"));
            }
            if !o.set {
                widgets::tag(ui, &t, &t!("Ext.Plugins.Unset"));
            }
        });
        let mut draft = e.option_draft.get(&o.key).and_then(Value::as_str).map(str::to_string).unwrap_or_else(|| o.value.clone());
        let before = draft.clone();
        ui.add_enabled_ui(!busy, |ui| {
            if !o.choices.is_empty() {
                egui::ComboBox::from_id_salt(("opt", &o.key)).selected_text(draft.clone()).show_ui(ui, |ui| {
                    ui.selectable_value(&mut draft, String::new(), "");
                    for c in &o.choices {
                        ui.selectable_value(&mut draft, c.clone(), c);
                    }
                });
            } else {
                let mut te = TextEdit::singleline(&mut draft).password(o.sensitive).desired_width(ui.available_width());
                if o.sensitive && o.set {
                    te = te.hint_text(t!("Ext.Plugins.SecretKept"));
                }
                ui.add(te);
            }
        });
        if draft != before {
            e.option_draft.insert(o.key.clone(), Value::String(draft));
        }
        if let Some(d) = o.description.as_ref().filter(|d| !d.is_empty()) {
            hint(ui, &t, d);
        }
        ui.add_space(8.0);
    }
    ui.horizontal(|ui| {
        if ui.add_enabled_ui(!busy && !e.option_draft.is_empty(), |ui| widgets::primary(ui, &t, &t!("Ext.Plugins.Save"))).inner.clicked() {
            acts.push(A::SaveOptions(cid.to_string()));
        }
        if ui.button(t!("Action.Close")).clicked() {
            e.configuring = None;
            e.options = None;
        }
    });
}

// ============================================================================================ hooks

fn hooks_tab(e: &mut Extensions, ui: &mut Ui, cx: &mut Cx, id: &str, acts: &mut Vec<A>) {
    let t = cx.t;
    let busy = e.busy();
    let cwd = e.cwd(cx, id);
    crate::md::show(ui, &t, &html_to_md(&t!("Ext.Hooks.Lead", format!("`{}`", fmt::home(&cwd, &cx.app.boot.home)))));
    match (&e.hooks_note, e.hooks.as_ref().is_some_and(|l| l.disabled)) {
        (Some(n), _) => errbox(ui, &t, n),
        (None, true) => errbox(ui, &t, &t!("Ext.Hooks.Disabled")),
        _ => {}
    }
    let locked = e.hooks.as_ref().is_some_and(|l| l.locked);
    if locked {
        errbox(ui, &t, &t!("Ext.Hooks.Locked"));
    }
    let Some(listing) = e.hooks.clone() else {
        if e.hooks_note.is_none() {
            lead(ui, &t, &t!("Ext.Plugins.Loading"));
        }
        return;
    };
    if listing.rows.is_empty() {
        lead(ui, &t, &t!("Ext.Hooks.None"));
    } else {
        let rows: Vec<HookRow> = listing.rows.iter().filter(|h| matches(&e.query, &hook_haystack(h), Some(&h.command))).cloned().collect();
        find_box(ui, &t, &mut e.query, rows.len(), listing.rows.len());
        if rows.is_empty() {
            lead(ui, &t, &t!("Ext.FindEmpty"));
        } else {
            let fr = [0.22, 0.16, 0.42, 0.20];
            thead(ui, &t, &fr, &[t!("Ext.Col.Event"), t!("Ext.Col.Matcher"), t!("Ext.Col.Command"), String::new()]);
            for h in rows {
                let editing = e.hook_editing.as_ref() == Some(&h);
                let hk = format!("h:{}", hook_key(&h));
                trow(ui, &t, editing, &fr, |ui, i| match i {
                    0 => {
                        ui.label(RichText::new(&h.event).font(theme::medium(theme::SMALL)).color(t.text));
                        ui.horizontal_wrapped(|ui| {
                            let tg = widgets::tag(ui, &t, &source_tag(&h.source));
                            if h.plugin_name.is_none()
                                && let Some(l) = &h.source_label
                            {
                                tg.on_hover_text(l);
                            }
                            if h.disabled {
                                widgets::tag(ui, &t, &t!("Ext.Hooks.RowDisabled"));
                            }
                        });
                        if let Some(p) = &h.plugin_name {
                            widgets::mono_dim(ui, &t, p);
                        }
                    }
                    1 => {
                        if h.matcher.is_empty() {
                            widgets::dim(ui, &t, &t!("Ext.Hooks.AnyMatcher"));
                        } else {
                            ui.add(egui::Label::new(RichText::new(&h.matcher).font(theme::mono(theme::CAPTION)).color(t.text)).wrap());
                        }
                    }
                    2 => {
                        ui.add(egui::Label::new(RichText::new(&h.command).font(theme::mono(theme::CAPTION)).color(t.text)).wrap());
                        hint(ui, &t, &hook_meta(&h));
                        if h.blocks_on_failure() {
                            widgets::tag(ui, &t, &t!("Ext.Hooks.Blocks")).on_hover_text(t!("Ext.Hooks.BlocksHint"));
                        }
                    }
                    _ => {
                        if hook_editable(&h, locked) {
                            if armed_button(e, ui, &t, &hk, &t!("Ext.Perm.Remove"), &t!("Ext.ConfirmRemove")) {
                                acts.push(A::SaveHook(Some(h.clone()), true));
                            }
                            if h.kind == "command" && ui.add_enabled(!busy, egui::Button::new(RichText::new(t!("Ext.Hooks.Edit")).color(t.text_dim)).frame(false)).on_hover_text(t!("Ext.Hooks.EditRow", hook_name(&h), h.command.clone())).clicked() {
                                acts.push(A::EditHook(h.clone()));
                            }
                        } else if h.plugin_name.is_some() {
                            widgets::dim(ui, &t, &t!("Ext.Hooks.FromPlugin"));
                        }
                    }
                });
            }
        }
    }
    if listing.locked || listing.catalog.is_empty() {
        return;
    }

    // le formulaire : ajouter, ou modifier la ligne choisie
    let editing = e.hook_editing.clone();
    crate::pages::form::field_label(ui, &t, &if editing.is_none() { t!("Ext.Hooks.AddTitle") } else { t!("Ext.Hooks.EditTitle") });
    let ev = listing.catalog.iter().find(|x| x.name == e.hook_event).or(listing.catalog.first()).cloned();
    let ev_matcher = ev.as_ref().is_some_and(|e| e.matcher);
    ui.add_enabled_ui(!busy, |ui| {
        ui.horizontal(|ui| {
            let r = egui::ComboBox::from_id_salt("hook-event").selected_text(ev.as_ref().map(|e| e.name.clone()).unwrap_or_default()).show_ui(ui, |ui| {
                for c in &listing.catalog {
                    ui.selectable_value(&mut e.hook_event, c.name.clone(), &c.name).on_hover_text(&c.summary);
                }
            });
            if let Some(ev) = &ev {
                r.response.on_hover_text(&ev.summary);
            }
            let hint = if ev_matcher { "Bash|Write".to_string() } else { t!("Ext.Hooks.NoMatcher") };
            ui.add_enabled(ev_matcher, TextEdit::singleline(&mut e.hook_matcher).hint_text(hint).font(theme::mono(theme::CODE)).desired_width(200.0));
            if editing.is_none() {
                egui::ComboBox::from_id_salt("hook-file").selected_text(t!(&format!("Perm.Dest.{}", e.hook_file))).show_ui(ui, |ui| {
                    for src in FILES {
                        ui.selectable_value(&mut e.hook_file, src.to_string(), t!(&format!("Perm.Dest.{src}")));
                    }
                });
            }
        });
        let r = ui.add(TextEdit::singleline(&mut e.hook_command).hint_text("npm run lint").font(theme::mono(theme::CODE)).desired_width(ui.available_width()));
        let enter = r.lost_focus() && ui.input(|i| i.key_pressed(egui::Key::Enter));
        ui.horizontal(|ui| {
            ui.add(TextEdit::singleline(&mut e.hook_timeout).hint_text(t!("Ext.Hooks.Timeout")).desired_width(110.0));
            ui.checkbox(&mut e.hook_block, t!("Ext.Hooks.Block"));
            let label = if editing.is_none() { format!("{} {}", icon("plus"), t!("Ext.Hooks.AddButton")) } else { format!("{} {}", icon("check"), t!("Ext.Hooks.SaveButton")) };
            let go = ui.add_enabled_ui(!e.hook_command.trim().is_empty(), |ui| widgets::primary(ui, &t, &label)).inner.clicked();
            if go || enter {
                acts.push(A::SaveHook(editing.clone(), false));
            }
            if editing.is_some() && ui.button(t!("Action.Cancel")).clicked() {
                e.reset_hook_form();
            }
        });
    });
}

// ============================================================================================ règles d'autorisation

fn perms_tab(e: &mut Extensions, ui: &mut Ui, cx: &mut Cx, id: &str, acts: &mut Vec<A>) {
    let t = cx.t;
    let busy = e.busy();
    let home = cx.app.boot.home.clone();
    let cwd = e.cwd(cx, id);
    crate::md::show(ui, &t, &html_to_md(&t!("Ext.Perm.Lead", format!("`{}`", fmt::home(&cwd, &home)))));
    if let Some(n) = &e.perms_note {
        errbox(ui, &t, n);
    }
    let Some(perms) = e.perms.clone() else {
        if e.perms_note.is_none() {
            lead(ui, &t, &t!("Ext.Plugins.Loading"));
        }
        return;
    };
    if perms.managed_only {
        errbox(ui, &t, &t!("Ext.Perm.ManagedOnly"));
    }
    for er in &perms.errors {
        errbox(ui, &t, &t!("Ext.Perm.FileError", er));
    }
    let all = &perms.rules;
    let count = |b: &str| all.iter().filter(|r| r.behavior == b).count();
    ui.horizontal_wrapped(|ui| {
        if filter_chip(ui, &t, &t!("Filter.All"), all.len(), e.filter.is_empty()) {
            e.filter.clear();
        }
        for b in BEHAVIORS {
            if (count(b) != 0 || e.filter == b) && filter_chip(ui, &t, &t!(&format!("Ext.Perm.{b}")), count(b), e.filter == b) {
                e.filter = b.to_string();
            }
        }
    });
    ui.add_space(6.0);
    if all.is_empty() {
        lead(ui, &t, &t!("Ext.Perm.None"));
    } else {
        let fr = [0.50, 0.14, 0.18, 0.18];
        thead(ui, &t, &fr, &[t!("Ext.Col.Rule"), t!("Ext.Col.Behavior"), t!("Ext.Col.Source"), String::new()]);
        let f = e.filter.clone();
        for r in all.iter().filter(|r| f.is_empty() || r.behavior == f) {
            let rk = format!("r:{}|{}|{}", r.source, r.behavior, r.rule);
            trow(ui, &t, false, &fr, |ui, i| match i {
                0 => {
                    ui.add(egui::Label::new(RichText::new(visible(&r.rule)).font(theme::mono(theme::LABEL)).color(t.text)).wrap());
                    if let Some(x) = &r.text {
                        hint(ui, &t, &format!("{}{}{}", x.prefix, visible(&x.emphasis), x.suffix));
                    }
                }
                1 => {
                    widgets::tag(ui, &t, &t!(&format!("Ext.Perm.{}", r.behavior)));
                }
                2 => {
                    widgets::tag(ui, &t, &source_tag(&r.source));
                    if r.not_in_effect {
                        hint(ui, &t, &t!("Ext.Perm.NotInEffect"));
                    }
                }
                _ => {
                    if r.editable() && armed_button(e, ui, &t, &rk, &t!("Ext.Perm.Remove"), &t!("Ext.ConfirmRemove")) {
                        acts.push(A::Rule(r.source.clone(), r.behavior.clone(), r.rule.clone(), false));
                    }
                }
            });
        }
    }

    crate::pages::form::field_label(ui, &t, &t!("Ext.Perm.Add"));
    ui.add_enabled_ui(!busy, |ui| {
        let r = ui.add(TextEdit::singleline(&mut e.rule_draft).hint_text("Bash(npm run test:*)").font(theme::mono(theme::CODE)).desired_width(ui.available_width()));
        let enter = r.lost_focus() && ui.input(|i| i.key_pressed(egui::Key::Enter));
        ui.horizontal(|ui| {
            egui::ComboBox::from_id_salt("rule-behavior").selected_text(t!(&format!("Ext.Perm.{}", e.rule_behavior))).show_ui(ui, |ui| {
                for b in BEHAVIORS {
                    ui.selectable_value(&mut e.rule_behavior, b.to_string(), t!(&format!("Ext.Perm.{b}")));
                }
            });
            egui::ComboBox::from_id_salt("rule-file").selected_text(t!(&format!("Perm.Dest.{}", e.rule_file))).show_ui(ui, |ui| {
                for src in FILES {
                    ui.selectable_value(&mut e.rule_file, src.to_string(), t!(&format!("Perm.Dest.{src}")));
                }
            });
            let go = ui.add_enabled_ui(!e.rule_draft.trim().is_empty(), |ui| widgets::primary(ui, &t, &format!("{} {}", icon("plus"), t!("Ext.Perm.AddButton")))).inner.clicked();
            if (go || enter) && !e.rule_draft.trim().is_empty() {
                acts.push(A::Rule(e.rule_file.clone(), e.rule_behavior.clone(), e.rule_draft.trim().to_string(), true));
            }
        });
    });
    if !perms.workspace_dirs.is_empty() {
        ui.add_space(10.0);
        widgets::cap(ui, &t, &t!("Dirs.Label"));
        for d in &perms.workspace_dirs {
            ui.horizontal(|ui| {
                ui.label(RichText::new(format!("{} {}", icon("folder"), fmt::home(&d.path, &home))).font(theme::mono(theme::CAPTION)).color(t.text_dim)).on_hover_text(d.path.replace('\\', "/"));
                widgets::tag(ui, &t, &source_tag(&d.source));
            });
        }
    }
    let _ = x::TABS;
}
