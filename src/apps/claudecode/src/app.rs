//! La fenêtre : la disposition de ClaudeCodeUI (`MainLayout.razor`) aux couleurs de l'app de Korin. À gauche le rail
//! (marque, nouvelle session, recherche, vue d'ensemble, worktrees, arrière-plan, extensions, réglages ; les sessions
//! actives par dépôt ; puis le rangement de Korin : épinglées, dossiers, toutes les conversations par date ; quotas
//! 5 h / 7 j), au centre la page (session, nouvelle session, vue d'ensemble…), à droite l'inspecteur de la session
//! (redimensionnables tous deux, largeurs gardées dans les préférences). Par-dessus : la palette (Ctrl K).
//!
//! Raccourcis (`MainLayout.shortcut`) : Ctrl K palette, Ctrl I inspecteur, Ctrl N (et Alt N) nouvelle session,
//! Ctrl ⇧ O vue d'ensemble, Ctrl ⇧ A la décision suivante ; Échap interrompt le tour (page de session).

use std::collections::HashMap;
use std::time::Duration;

use ccui_shared::api::{PREF_INSPECTOR_WIDTH, PREF_RAIL_WIDTH};
use ccui_shared::t;
use egui::{Frame, Key, Margin, Modifiers, Stroke, Ui};

use crate::backend::Backend;
use crate::layout::palette::Palette;
use crate::layout::rail::Rail;
use crate::organize::Organize;
use crate::pages;
use crate::session::SessionView;
use crate::state::{AppStore, UiState};
use crate::theme::{self, Tokens};
use crate::widgets::tr;

/// Les pages (le routeur de ClaudeCodeUI : `/`, `/overview`, `/session/:id`, `/worktrees`, `/background`,
/// `/extensions`, `/settings/appearance`).
#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Route {
    NewSession,
    Overview,
    Session(String),
    Worktrees,
    Background,
    Extensions,
    Settings,
}

impl Route {
    /// Depuis un chemin du moteur (`Push::Navigate`, notifications : `session/<id>`).
    pub fn from_path(p: &str) -> Route {
        let p = p.trim_start_matches('/');
        match p {
            "overview" => Route::Overview,
            "worktrees" => Route::Worktrees,
            "background" => Route::Background,
            "extensions" => Route::Extensions,
            "settings/appearance" | "settings" => Route::Settings,
            _ => match p.strip_prefix("session/") {
                Some(id) if !id.is_empty() => Route::Session(id.to_string()),
                _ => Route::NewSession,
            },
        }
    }
}

/// Ce que chaque écran reçoit : le moteur, le miroir de son état, l'état de la fenêtre, le rangement de Korin, les
/// couleurs, et la navigation demandée (appliquée après la passe).
pub struct Cx<'a> {
    pub be: &'a Backend,
    pub app: &'a mut AppStore,
    pub ui: &'a mut UiState,
    pub org: &'a mut Organize,
    pub t: Tokens,
    pub route: &'a Route,
    pub go: &'a mut Option<Route>,
}

impl Cx<'_> {
    pub fn navigate(&mut self, r: Route) {
        *self.go = Some(r);
    }

    /// Ouvre une session (vivante ou passée) dans sa page.
    pub fn open_session(&mut self, id: &str) {
        self.navigate(Route::Session(id.to_string()));
    }
}

pub struct App {
    be: Backend,
    store: AppStore,
    ui: UiState,
    org: Organize,
    route: Route,
    /// La session dont la page tient la copie (watch), pour la rendre (release) en partant.
    held: Option<String>,
    rail: Rail,
    palette: Palette,
    new_session: pages::new_session::NewSession,
    overview: pages::overview::Overview,
    worktrees: pages::worktrees::Worktrees,
    background: pages::background::Background,
    extensions: pages::extensions::Extensions,
    settings: pages::settings::Settings,
    /// L'état d'interface de chaque session ouverte (composeur, inspecteur, menus…).
    views: HashMap<String, SessionView>,
    t: Tokens,
    dark: Option<bool>,
    pub system_dark: Box<dyn Fn() -> bool>,
    /// Les erreurs et notifications à montrer en bas (texte, depuis quand).
    toasts: Vec<(String, f64)>,
    ctx: egui::Context,
}

impl App {
    pub fn new(ctx: &egui::Context, system_dark: Box<dyn Fn() -> bool>) -> App {
        theme::install_fonts(ctx);
        let be = Backend::new();
        be.set_context(ctx);
        let store = AppStore::new(&be);
        let t = Tokens::for_dark(system_dark());
        App {
            store,
            ui: UiState::default(),
            org: Organize::load(),
            route: Route::NewSession,
            held: None,
            rail: Rail::default(),
            palette: Palette::default(),
            new_session: Default::default(),
            overview: Default::default(),
            worktrees: Default::default(),
            background: Default::default(),
            extensions: Default::default(),
            settings: Default::default(),
            views: HashMap::new(),
            t,
            dark: None,
            system_dark,
            toasts: Vec::new(),
            ctx: ctx.clone(),
            be,
        }
    }

    pub fn set_focused(&self, on: bool) {
        self.be.set_focused(on);
    }

    pub fn shutdown(&self) {
        self.be.shutdown();
    }

    /// Une page à ouvrir directement (l'hôte de développement : `--route`).
    pub fn navigate(&mut self, r: Route) {
        self.go_to(r);
    }

    pub fn ui_state(&mut self) -> &mut UiState {
        &mut self.ui
    }

    fn update_theme(&mut self, ctx: &egui::Context) {
        let dark = match self.org.theme.as_str() {
            "dark" => true,
            "light" => false,
            _ => (self.system_dark)(),
        };
        if self.dark != Some(dark) {
            self.dark = Some(dark);
            self.t = Tokens::for_dark(dark);
            theme::apply(ctx, &self.t);
        }
    }

    /// Change de page : la session quittée est rendue (release), la nouvelle regardée (load_session).
    fn go_to(&mut self, r: Route) {
        if r == self.route {
            return;
        }
        // (la page Extensions rend la session qu'elle regardait)
        if self.route == Route::Extensions {
            self.extensions.leave(&self.be, &mut self.store);
        }
        if let Some(old) = self.held.take() {
            self.store.release(&self.be, &old);
        }
        if let Route::Session(id) = &r {
            self.store.load_session(&self.be, id);
            self.held = Some(id.clone());
            self.views.entry(id.clone()).or_default();
        }
        self.ui.palette_open = false;
        self.route = r;
    }

    pub fn ui(&mut self, ui: &mut Ui) {
        let ctx = ui.ctx().clone();
        self.update_theme(&ctx);
        self.store.poll(&self.be);
        // une session finie ailleurs : la page relit sa transcription
        if let Some(id) = &self.held
            && !self.store.sessions.contains_key(id)
        {
            let id = id.clone();
            self.store.load_session(&self.be, &id);
        }
        if let Some(p) = self.store.navigate.take() {
            self.go_to(Route::from_path(&p));
        }
        for e in self.be.take_errors() {
            self.toasts.push((e, ctx.input(|i| i.time)));
        }
        for n in self.be.take_notices() {
            self.toasts.push((format!("{} — {}", n.title, n.body), ctx.input(|i| i.time)));
        }
        self.shortcuts(&ctx);
        // (le moteur réveille la fenêtre ; sous Korin l'hôte n'attend un évènement que jusqu'à l'image demandée : tant
        // qu'une session travaille, on redessine souvent)
        let live = self.store.summary.sessions.iter().any(|s| s.has_process);
        ctx.request_repaint_after(if self.be.busy() || live { Duration::from_millis(80) } else { Duration::from_secs(1) });

        let t = self.t;
        let mut go = None;
        let rail_w = self.store.prefs.rail_width.as_deref().and_then(|w| w.trim_end_matches("px").parse::<f32>().ok()).unwrap_or(256.0);
        let rail = egui::Panel::left("rail")
            .resizable(true)
            .default_size(rail_w)
            .size_range(220.0..=420.0)
            .frame(Frame::new().fill(t.chrome).stroke(Stroke::new(1.0, t.separator)))
            .show(ui, |ui| {
                let mut cx = Cx { be: &self.be, app: &mut self.store, ui: &mut self.ui, org: &mut self.org, t, route: &self.route, go: &mut go };
                self.rail.ui(ui, &mut cx);
            });
        self.remember_width(&ctx, PREF_RAIL_WIDTH, rail.response.rect.width(), rail_w);

        if let Route::Session(id) = self.route.clone() {
            let pending = self.store.session(&id).is_some_and(|h| !h.meta.pending.is_empty());
            if self.ui.inspector_open || pending {
                let insp_w = self.store.prefs.inspector_width.as_deref().and_then(|w| w.trim_end_matches("px").parse::<f32>().ok()).unwrap_or(420.0);
                let view = self.views.entry(id.clone()).or_default();
                let r = egui::Panel::right("inspector")
                    .resizable(true)
                    .default_size(insp_w)
                    .size_range(300.0..=760.0)
                    .frame(Frame::new().fill(t.chrome).stroke(Stroke::new(1.0, t.separator)))
                    .show(ui, |ui| {
                        let mut cx = Cx { be: &self.be, app: &mut self.store, ui: &mut self.ui, org: &mut self.org, t, route: &self.route, go: &mut go };
                        crate::session::inspector::show(ui, &mut cx, view, &id);
                    });
                self.remember_width(&ctx, PREF_INSPECTOR_WIDTH, r.response.rect.width(), insp_w);
            }
        }

        egui::CentralPanel::default().frame(Frame::new().fill(t.canvas)).show(ui, |ui| {
            let mut cx = Cx { be: &self.be, app: &mut self.store, ui: &mut self.ui, org: &mut self.org, t, route: &self.route, go: &mut go };
            match self.route.clone() {
                Route::NewSession => self.new_session.ui(ui, &mut cx),
                Route::Overview => self.overview.ui(ui, &mut cx),
                Route::Worktrees => self.worktrees.ui(ui, &mut cx),
                Route::Background => self.background.ui(ui, &mut cx),
                Route::Extensions => self.extensions.ui(ui, &mut cx),
                Route::Settings => self.settings.ui(ui, &mut cx),
                Route::Session(id) => {
                    let view = self.views.entry(id.clone()).or_default();
                    crate::session::page::show(ui, &mut cx, view, &id);
                }
            }
        });

        if self.ui.palette_open {
            let mut cx = Cx { be: &self.be, app: &mut self.store, ui: &mut self.ui, org: &mut self.org, t, route: &self.route, go: &mut go };
            self.palette.ui(&ctx, &mut cx);
        }
        self.toasts(&ctx);
        if let Some(r) = go {
            self.go_to(r);
        }
    }

    /// Une largeur de panneau lâchée : gardée dans les préférences (`app_pref_set(rail-width | inspector-width)`).
    fn remember_width(&self, ctx: &egui::Context, pref: &str, w: f32, held: f32) {
        if (w - held).abs() > 2.0 && !ctx.input(|i| i.pointer.any_down()) {
            self.be.set_pref(pref, Some(&format!("{}px", w.round())));
        }
    }

    fn toasts(&mut self, ctx: &egui::Context) {
        let now = ctx.input(|i| i.time);
        self.toasts.retain(|(_, at)| now - at < 8.0);
        if self.toasts.is_empty() {
            return;
        }
        let t = self.t;
        egui::Area::new(egui::Id::new("toasts")).anchor(egui::Align2::CENTER_BOTTOM, [0.0, -16.0]).show(ctx, |ui| {
            let mut close = None;
            for (i, (text, _)) in self.toasts.iter().enumerate() {
                Frame::new()
                    .fill(t.card)
                    .stroke(Stroke::new(1.0, t.danger.gamma_multiply(0.6)))
                    .corner_radius(theme::R_LG)
                    .inner_margin(Margin::symmetric(12, 8))
                    .show(ui, |ui| {
                        ui.set_max_width(560.0);
                        ui.horizontal(|ui| {
                            ui.add(egui::Label::new(text.as_str()).wrap());
                            if ui.small_button(crate::widgets::icon("x")).clicked() {
                                close = Some(i);
                            }
                        });
                    });
            }
            if let Some(i) = close {
                self.toasts.remove(i);
            }
        });
        ctx.request_repaint_after(Duration::from_millis(500));
    }

    /// Les raccourcis globaux (`MainLayout.shortcut`).
    fn shortcuts(&mut self, ctx: &egui::Context) {
        let ctrl = Modifiers::COMMAND;
        let ctrl_shift = Modifiers::COMMAND | Modifiers::SHIFT;
        let (k, i, n, alt_n, o, a, esc) = ctx.input_mut(|inp| {
            (
                inp.consume_key(ctrl, Key::K),
                inp.consume_key(ctrl, Key::I),
                inp.consume_key(ctrl, Key::N),
                inp.consume_key(Modifiers::ALT, Key::N),
                inp.consume_key(ctrl_shift, Key::O),
                inp.consume_key(ctrl_shift, Key::A),
                inp.key_pressed(Key::Escape),
            )
        });
        if k {
            self.ui.palette_open = !self.ui.palette_open;
        }
        if i {
            self.ui.inspector_open = !self.ui.inspector_open;
        }
        if n || alt_n {
            self.go_to(Route::NewSession);
        }
        if o {
            self.go_to(Route::Overview);
        }
        if a && let Some(d) = self.store.summary.decision_queue.first() {
            let id = d.session_id.clone();
            self.go_to(Route::Session(id));
        }
        if esc && self.ui.palette_open {
            self.ui.palette_open = false;
        }
    }

    // ---- la barre de menus de Korin ----

    /// Le texte des menus (egui_korin::menu : `M<titre>`, `I<id>\t<libellé>\t<raccourci>`, `-`).
    pub fn menu_text(&self) -> String {
        format!(
            "M{file}\nI1\t{new}\tCtrl+N\nI2\t{search}\tCtrl+K\nI3\t{folder}\n-\nI9\t{quit}\tCtrl+Q\n\
             M{session}\nI10\t{rename}\tF2\nI11\t{pin}\nI12\t{interrupt}\tÉchap\nI13\t{stop}\n\
             M{view}\nI20\t{overview}\tCtrl+Shift+O\nI21\t{next}\tCtrl+Shift+A\nI22\t{insp}\tCtrl+I\nI23\t{worktrees}\nI24\t{ext}\nI25\t{settings}\n-\n\
             I30\t{system}\nI31\t{dark}\nI32\t{light}\n",
            file = tr("Fichier", "File"),
            new = t!("Rail.NewSession"),
            search = t!("Rail.Search"),
            folder = tr("Nouveau dossier…", "New folder…"),
            quit = tr("Quitter", "Quit"),
            session = tr("Session", "Session"),
            rename = t!("Session.Rename"),
            pin = tr("Épingler", "Pin"),
            interrupt = t!("Composer.Interrupt"),
            stop = t!("Session.Stop"),
            view = tr("Affichage", "View"),
            overview = t!("Nav.Overview"),
            next = tr("Décision suivante", "Next decision"),
            insp = t!("Inspector"),
            worktrees = t!("Nav.Worktrees"),
            ext = t!("Nav.Extensions"),
            settings = t!("Settings.Title"),
            system = tr("Thème de Korin", "Korin theme"),
            dark = tr("Sombre", "Dark"),
            light = tr("Clair", "Light"),
        )
    }

    /// Les états des entrées : (id, 0 normale | 1 grisée | 2 cochée).
    pub fn menu_states(&self) -> Vec<(i64, i64)> {
        let cur = match &self.route {
            Route::Session(id) => Some(id.as_str()),
            _ => None,
        };
        let live = cur.and_then(|id| self.store.summary.sessions.iter().find(|s| s.id == id));
        let off = |b: bool| if b { 0 } else { 1 };
        let theme = if self.org.theme.is_empty() { "system" } else { self.org.theme.as_str() };
        vec![
            (10, off(cur.is_some())),
            (11, if cur.is_none() { 1 } else if cur.is_some_and(|c| self.org.is_pinned(c)) { 2 } else { 0 }),
            (12, off(live.is_some_and(|s| matches!(s.status, ccui_shared::model::SessionStatus::Running | ccui_shared::model::SessionStatus::Waiting)))),
            (13, off(live.is_some_and(|s| s.has_process))),
            (21, off(!self.store.summary.decision_queue.is_empty())),
            (22, if self.ui.inspector_open { 2 } else { 0 }),
            (30, if theme == "system" { 2 } else { 0 }),
            (31, if theme == "dark" { 2 } else { 0 }),
            (32, if theme == "light" { 2 } else { 0 }),
        ]
    }

    /// Un choix dans la barre de menus.
    pub fn menu_command(&mut self, id: i64) {
        let cur = match &self.route {
            Route::Session(id) => Some(id.clone()),
            _ => None,
        };
        match id {
            1 => self.go_to(Route::NewSession),
            2 => self.ui.palette_open = true,
            3 => self.rail.new_folder(),
            9 => crate::host::close(),
            10 => {
                if let Some(c) = cur {
                    self.rail.rename(&c);
                }
            }
            11 => {
                if let Some(c) = cur {
                    self.org.toggle_pin(&c);
                    self.org.save();
                }
            }
            12 => {
                if let Some(c) = cur
                    && let Ok(s) = self.be.session(&c)
                {
                    self.be.fire("", async move { s.interrupt().await });
                }
            }
            13 => {
                if let Some(c) = cur {
                    let core = self.be.core.clone();
                    self.be.fire("", async move {
                        core.sessions.stop(&c).await;
                        Ok(())
                    });
                }
            }
            20 => self.go_to(Route::Overview),
            21 => {
                if let Some(d) = self.store.summary.decision_queue.first() {
                    let id = d.session_id.clone();
                    self.go_to(Route::Session(id));
                }
            }
            22 => self.ui.inspector_open = !self.ui.inspector_open,
            23 => self.go_to(Route::Worktrees),
            24 => self.go_to(Route::Extensions),
            25 => self.go_to(Route::Settings),
            30..=32 => {
                self.org.theme = ["system", "dark", "light"][(id - 30) as usize].to_owned();
                self.org.save();
                self.dark = None;
                self.ctx.request_repaint();
            }
            _ => {}
        }
    }
}
