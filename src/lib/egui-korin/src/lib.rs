//! egui sur Korin OS (docs/EGUI.md) : une fenêtre UIKit redimensionnable dont tous les pixels sont dessinés par
//! le CPU (`hdrshot-egui-backend`), le clavier, la souris, la molette, le presse-papiers, le curseur, le thème
//! clair ou sombre du système, les dialogues de fichiers et l'ouverture d'une adresse.
//!
//! ```no_run
//! egui_korin::run(egui_korin::Options::new("Bonjour"), |ui, _host| {
//!     ui.label("Bonjour de Korin");
//! }).unwrap();
//! ```

mod keys;
mod sys;

use std::ffi::CString;
use std::time::{Duration, Instant};

use egui::{CursorIcon, Event, Key, Modifiers, OutputCommand, PointerButton, Pos2, RawInput, Rect, Theme, Vec2, ViewportCommand, ViewportId};
use egui_software_backend::{BufferMutRef, ColorFieldOrder, EguiSoftwareRender};

/// La fenêtre : titre, taille, taille minimale, échelle.
#[derive(Clone, Debug)]
pub struct Options {
    pub title: String,
    pub size: [u32; 2],
    pub min_size: Option<[u32; 2]>,
    /// pixels par point d'egui (1 : un point d'egui = un pixel de l'écran)
    pub pixels_per_point: f32,
}

impl Options {
    pub fn new(title: &str) -> Self {
        Self { title: title.to_owned(), size: [960, 640], min_size: Some([320, 240]), pixels_per_point: 1.0 }
    }

    pub fn size(mut self, w: u32, h: u32) -> Self {
        self.size = [w, h];
        self
    }
}

#[derive(Debug)]
pub enum Error {
    /// UIKit ne s'ouvre pas, ou la fenêtre n'a pas été créée (hors du bureau ?)
    Window,
}

impl std::fmt::Display for Error {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            Error::Window => f.write_str("egui-korin : pas de fenêtre (UIKit, bureau)"),
        }
    }
}

impl std::error::Error for Error {}

/// Ce que l'app peut demander à Korin pendant une passe.
pub struct Host {
    close: bool,
}

impl Host {
    /// Ferme la fenêtre après cette passe (la fin de `run`).
    pub fn close(&mut self) {
        self.close = true;
    }

    pub fn set_title(&mut self, title: &str) {
        let t = cstr(title);
        unsafe { sys::kh_title(t.as_ptr()) }
    }

    /// Le dialogue Ouvrir : le chemin choisi. `filter` : « *.docx;*.odt » ou « Documents|*.docx|Tous|* ».
    pub fn file_open(&mut self, dir: &str, filter: Option<&str>) -> Option<String> {
        let (d, f) = (cstr(dir), filter.map(cstr));
        let mut buf = vec![0u8; 1024];
        let ok = unsafe {
            sys::kh_file_open(d.as_ptr(), f.as_ref().map_or(std::ptr::null(), |f| f.as_ptr()), buf.as_mut_ptr().cast(), buf.len() as _)
        };
        (ok != 0).then(|| from_c(&buf))
    }

    /// Le dialogue Enregistrer : le chemin choisi (« Remplacer ? » est demandé par Korin).
    pub fn file_save(&mut self, dir: &str, name: &str, filter: Option<&str>) -> Option<String> {
        let (d, n, f) = (cstr(dir), cstr(name), filter.map(cstr));
        let mut buf = vec![0u8; 1024];
        let ok = unsafe {
            sys::kh_file_save(d.as_ptr(), n.as_ptr(), f.as_ref().map_or(std::ptr::null(), |f| f.as_ptr()), buf.as_mut_ptr().cast(), buf.len() as _)
        };
        (ok != 0).then(|| from_c(&buf))
    }

    pub fn message(&mut self, title: &str, text: &str) {
        let (t, x) = (cstr(title), cstr(text));
        unsafe { sys::kh_message(t.as_ptr(), x.as_ptr()) }
    }

    /// Ouvre un fichier ou une adresse avec l'app associée (Jet pour une adresse web).
    pub fn open(&mut self, path_or_url: &str) {
        let p = cstr(path_or_url);
        unsafe { sys::kh_open_with(p.as_ptr()) }
    }
}

/// Le texte du presse-papiers de Korin.
pub fn clipboard_get() -> Option<String> {
    let mut buf = vec![0u8; 1 << 20];
    let n = unsafe { sys::kh_clip_get(buf.as_mut_ptr().cast(), buf.len() as _) };
    (n > 0).then(|| from_c(&buf))
}

pub fn clipboard_set(text: &str) {
    unsafe { sys::kh_clip_set(text.as_ptr().cast(), text.len() as _) }
}

/// Le thème du système : sombre ou clair.
pub fn system_theme() -> Theme {
    if unsafe { sys::kh_dark() } != 0 { Theme::Dark } else { Theme::Light }
}

fn cstr(s: &str) -> CString {
    CString::new(s.replace('\0', "")).unwrap_or_default()
}

fn from_c(buf: &[u8]) -> String {
    let n = buf.iter().position(|&b| b == 0).unwrap_or(buf.len());
    String::from_utf8_lossy(&buf[..n]).into_owned()
}

/// La boucle : ouvre la fenêtre, puis appelle `ui` à chaque passe jusqu'à la fermeture.
pub fn run(options: Options, mut ui: impl FnMut(&mut egui::Ui, &mut Host)) -> Result<(), Error> {
    let ctx = egui::Context::default();
    run_with(options, &ctx, move |ctx, input, host| ctx.run_ui(input, |u| ui(u, host)))
}

/// Comme `run`, avec le contexte de l'appelant et la passe à faire soi-même (`ctx.run_ui`, ou une passe à
/// deux temps comme celle d'eframe) : `pass` rend la sortie d'egui.
pub fn run_with(
    options: Options,
    ctx: &egui::Context,
    mut pass: impl FnMut(&egui::Context, RawInput, &mut Host) -> egui::FullOutput,
) -> Result<(), Error> {
    let title = cstr(&options.title);
    // (kh_open ramène la taille à la place de l'écran)
    let [w, h] = options.size;
    let [mw, mh] = options.min_size.unwrap_or([0, 0]);
    if unsafe { sys::kh_open(title.as_ptr(), w as _, h as _, mw as _, mh as _) } == 0 {
        return Err(Error::Window);
    }
    let mut win = Window::new(options.pixels_per_point);
    let start = Instant::now();
    let mut host = Host { close: false };
    let mut next_frame: Option<Instant> = Some(Instant::now());
    loop {
        // les évènements : on attend le premier jusqu'à la prochaine image demandée, puis on prend ceux qui suivent
        let timeout = match next_frame {
            Some(t) => t.saturating_duration_since(Instant::now()),
            None => Duration::from_millis(1000),
        };
        let mut wait = timeout.as_millis().min(1000) as i32;
        loop {
            let mut e = sys::Event::default();
            let t = unsafe { sys::kh_event(wait, &mut e) };
            if t <= 0 {
                break;
            }
            wait = 0;
            if t == sys::WEV_CLOSE {
                // une demande : la passe suivante la voit (ViewportEvent::Close), l'app peut l'annuler (CancelClose,
                // « Enregistrer les modifications ? ») ; sinon la fenêtre se ferme
                win.close_requested = true;
                next_frame = Some(Instant::now());
                continue;
            }
            // les raccourcis des menus (le muda de Korin) : comme sur macOS, la barre les prend avant la fenêtre ;
            // seulement avec Ctrl ou Alt, ou une touche F (une lettre seule reste au champ de texte)
            if t == sys::WEV_KEY
                && e.kind != 0
                && menu::active()
                && (e.mods & (sys::MOD_CTRL | sys::MOD_ALT) != 0 || (0x3A..=0x45).contains(&e.code))
            {
                let id = unsafe { sys::kh_menu_key() };
                if id >= 0 {
                    // (craft-menu, comme AppKit : un raccourci est rendu à egui, un clic lance la commande)
                    let m = (e.mods & sys::MOD_CTRL != 0) as i32 | ((e.mods & sys::MOD_ALT != 0) as i32) << 1 | ((e.mods & sys::MOD_SHIFT != 0) as i32) << 2;
                    menu::chosen_by_key(id as i64, m);
                    next_frame = Some(Instant::now());
                    continue;
                }
            }
            if t == sys::WEV_MENU {
                // un choix dans la barre de menus de Korin : gardé pour l'app (menu::take_chosen)
                menu::chosen(e.code as i64);
                next_frame = Some(Instant::now());
                continue;
            }
            if win.event(&e) {
                next_frame = Some(Instant::now());
            }
        }
        if next_frame.is_none_or(|t| t > Instant::now()) {
            continue;
        }
        // une passe d'egui (avec les commandes standard choisies dans la barre de menus)
        let mut input = win.take_input();
        input.time = Some(start.elapsed().as_secs_f64());
        for cmd in menu::take_injected() {
            input.events.extend(standard_events(cmd, win.modifiers));
        }
        let out = pass(ctx, input, &mut host);
        next_frame = win.output(ctx, out, &mut host);
        menu::after_pass();
        if host.close {
            return Ok(());
        }
    }
}

/// La barre de menus de Korin, en haut de l'écran (UIKit `ui_menu`) : le texte des menus de la fenêtre
/// (lignes `M<titre>`, `I<id>[d][c]<tab><libellé><tab><raccourci>`, `-`, `S<titre>` … `E` pour un sous-menu),
/// les états des entrées, et les choix de l'utilisateur. À appeler depuis la passe d'egui (le fil de la fenêtre).
pub mod menu {
    use super::sys;
    use std::sync::Mutex;

    static CHOSEN: Mutex<Vec<i64>> = Mutex::new(Vec::new());
    /// Deux tampons à adresse fixe, en alternance : UIKit reconnaît un texte déjà traduit à son adresse, il faut
    /// donc une autre adresse pour un autre texte, et la même mémoire tant qu'il s'en sert.
    static BUFS: Mutex<(Vec<u8>, Vec<u8>, bool, String)> = Mutex::new((Vec::new(), Vec::new(), false, String::new()));

    /// Le plus long texte que la barre accepte (WS_MENU_MAX).
    pub const MAX: usize = 16384;

    /// Publie le texte des menus (vide : plus de menu). Sans effet s'il n'a pas changé. -> false : trop long.
    pub fn set(text: &str) -> bool {
        if text.len() >= MAX {
            return false;
        }
        let mut b = BUFS.lock().unwrap_or_else(|e| e.into_inner());
        if b.3 == text {
            return true;
        }
        if b.0.capacity() < MAX + 1 {
            b.0 = Vec::with_capacity(MAX + 1);
            b.1 = Vec::with_capacity(MAX + 1);
        }
        b.2 = !b.2;
        let buf = if b.2 { &mut b.0 } else { &mut b.1 };
        buf.clear();
        buf.extend_from_slice(text.as_bytes());
        buf.push(0);
        let ptr = buf.as_ptr();
        b.3 = text.to_owned();
        unsafe { sys::kh_menu_set(if text.is_empty() { std::ptr::null() } else { ptr.cast() }) >= 0 }
    }

    /// Les états de plusieurs entrées d'un coup : (id, 0 normale | 1 grisée | 2 cochée | 3 les deux).
    pub fn set_states(states: &[(i64, i64)]) {
        if states.is_empty() {
            return;
        }
        let flat: Vec<std::ffi::c_long> = states.iter().flat_map(|&(id, s)| [id as std::ffi::c_long, s as std::ffi::c_long]).collect();
        unsafe { sys::kh_menu_states(flat.as_ptr(), states.len() as std::ffi::c_long) };
    }

    type Hook = std::sync::Arc<dyn Fn(i64) + Send + Sync>;
    type PassHook = std::sync::Arc<dyn Fn() + Send + Sync>;
    static HOOKS: Mutex<Option<(Hook, PassHook)>> = Mutex::new(None);

    /// Pour une couche de menus (le `muda` de Korin) : `on_chosen` reçoit chaque choix dans la barre dès son arrivée
    /// (au lieu de `take_chosen`), `after_pass` est appelé après chaque passe d'egui (le moment d'envoyer à la barre
    /// ce qui a changé pendant la passe).
    pub fn set_hooks(on_chosen: impl Fn(i64) + Send + Sync + 'static, after_pass: impl Fn() + Send + Sync + 'static) {
        *HOOKS.lock().unwrap_or_else(|e| e.into_inner()) = Some((std::sync::Arc::new(on_chosen), std::sync::Arc::new(after_pass)));
    }

    /// Le choix en cours vient d'un raccourci : ses touches (1 Ctrl, 2 Alt, 4 Maj) ; -1 : d'un clic dans la barre.
    static KEY_MODS: std::sync::atomic::AtomicI32 = std::sync::atomic::AtomicI32::new(-1);

    /// Un choix par un raccourci de la barre (la touche est prise avant la fenêtre).
    pub(super) fn chosen_by_key(id: i64, mods: i32) {
        KEY_MODS.store(mods, std::sync::atomic::Ordering::Relaxed);
        chosen(id);
        KEY_MODS.store(-1, std::sync::atomic::Ordering::Relaxed);
    }

    /// (le `muda` de Korin, pour craft-menu) pendant un choix : ses touches s'il vient d'un raccourci, -1 sinon ;
    /// l'équivalent du `currentEvent` d'AppKit (un raccourci est rendu à egui, un clic lance la commande)
    #[unsafe(no_mangle)]
    pub extern "C" fn egui_korin_menu_key_mods() -> i32 {
        KEY_MODS.load(std::sync::atomic::Ordering::Relaxed)
    }

    pub(super) fn chosen(id: i64) {
        let hook = HOOKS.lock().unwrap_or_else(|e| e.into_inner()).as_ref().map(|h| h.0.clone());
        match hook {
            Some(h) => h(id),
            None => CHOSEN.lock().unwrap_or_else(|e| e.into_inner()).push(id),
        }
    }

    /// Une couche de menus est installée (set_hooks) : les raccourcis passent par la barre.
    pub(super) fn active() -> bool {
        HOOKS.lock().unwrap_or_else(|e| e.into_inner()).is_some()
    }

    pub(super) fn after_pass() {
        let hook = HOOKS.lock().unwrap_or_else(|e| e.into_inner()).as_ref().map(|h| h.1.clone());
        if let Some(h) = hook {
            h();
        }
    }

    /// Les commandes standard d'une barre de menus (Édition), données à egui comme les touches qu'elles remplacent.
    #[derive(Clone, Copy, Debug, PartialEq, Eq)]
    pub enum Standard {
        Undo,
        Redo,
        Cut,
        Copy,
        Paste,
        SelectAll,
    }

    static INJECTED: Mutex<Vec<Standard>> = Mutex::new(Vec::new());

    /// Une commande standard choisie dans la barre : egui la reçoit à la passe suivante.
    pub fn inject(cmd: Standard) {
        INJECTED.lock().unwrap_or_else(|e| e.into_inner()).push(cmd);
    }

    pub(super) fn take_injected() -> Vec<Standard> {
        std::mem::take(&mut *INJECTED.lock().unwrap_or_else(|e| e.into_inner()))
    }

    /// (le `muda` de Korin, sans dépendre d'egui) les crochets de `set_hooks`, par un symbole C
    #[unsafe(no_mangle)]
    pub extern "C" fn egui_korin_menu_hooks(on_chosen: extern "C" fn(i64), after_pass: extern "C" fn()) {
        set_hooks(move |id| on_chosen(id), move || after_pass());
    }

    /// (le `muda` de Korin) une commande standard : 0 Annuler, 1 Rétablir, 2 Couper, 3 Copier, 4 Coller, 5 Tout
    /// sélectionner
    #[unsafe(no_mangle)]
    pub extern "C" fn egui_korin_menu_inject(code: i32) {
        let cmd = match code {
            0 => Standard::Undo,
            1 => Standard::Redo,
            2 => Standard::Cut,
            3 => Standard::Copy,
            4 => Standard::Paste,
            5 => Standard::SelectAll,
            _ => return,
        };
        inject(cmd);
    }

    /// Les entrées choisies dans la barre depuis le dernier appel (leur id).
    pub fn take_chosen() -> Vec<i64> {
        std::mem::take(&mut *CHOSEN.lock().unwrap_or_else(|e| e.into_inner()))
    }
}

/// Une commande standard de la barre de menus -> les évènements d'egui (Copier : Event::Copy ; Coller : le texte du
/// presse-papiers ; Annuler, Rétablir, Tout sélectionner : Ctrl+Z, Ctrl+Maj+Z, Ctrl+A).
fn standard_events(cmd: menu::Standard, mods: Modifiers) -> Vec<Event> {
    let key = |key: Key, shift: bool| {
        let m = Modifiers { ctrl: true, command: true, shift, ..mods };
        [Event::Key { key, physical_key: None, pressed: true, repeat: false, modifiers: m },
         Event::Key { key, physical_key: None, pressed: false, repeat: false, modifiers: m }]
    };
    match cmd {
        menu::Standard::Copy => vec![Event::Copy],
        menu::Standard::Cut => vec![Event::Cut],
        menu::Standard::Paste => clipboard_get().map(Event::Paste).into_iter().collect(),
        menu::Standard::Undo => key(Key::Z, false).to_vec(),
        menu::Standard::Redo => key(Key::Z, true).to_vec(),
        menu::Standard::SelectAll => key(Key::A, false).to_vec(),
    }
}

/// L'état de la fenêtre entre deux passes.
struct Window {
    ppp: f32,
    width: usize,
    height: usize,
    pixels: *mut u32,
    events: Vec<Event>,
    modifiers: Modifiers,
    buttons: i32,
    pointer: Pos2,
    focused: bool,
    theme: Theme,
    cursor: i32,
    close_requested: bool,
    maximized: bool,
    render: EguiSoftwareRender,
    /// l'image se dessine ici, puis seules les lignes qui ont changé vont dans les pixels de la fenêtre : Elegant
    /// ne voit jamais une image à moitié faite (le fond seul : le scintillement au passage de la souris)
    back: Vec<[u8; 4]>,
    /// les pixels de la fenêtre ne correspondent plus à `back` (nouveau tampon) : tout recopier
    full: bool,
    /// les boutons du cadre (dessinés par Elegant sur le contenu) sont en clair : le fond sous eux est foncé
    glyphs_light: Option<bool>,
}

impl Window {
    fn new(ppp: f32) -> Self {
        let mut w = Self {
            ppp,
            width: 0,
            height: 0,
            pixels: std::ptr::null_mut(),
            back: Vec::new(),
            full: true,
            glyphs_light: None,
            events: Vec::new(),
            modifiers: Modifiers::default(),
            buttons: 0,
            pointer: Pos2::ZERO,
            focused: true,
            theme: system_theme(),
            cursor: -1,
            close_requested: false,
            maximized: false,
            render: EguiSoftwareRender::new(ColorFieldOrder::Bgra).with_caching(true),
        };
        w.read_pixels();
        w
    }

    fn read_pixels(&mut self) {
        let (mut w, mut h) = (0, 0);
        self.pixels = unsafe { sys::kh_pixels(&mut w, &mut h) };
        self.width = w.max(0) as usize;
        self.height = h.max(0) as usize;
        self.full = true;
    }

    fn pos(&self, e: &sys::Event) -> Pos2 {
        Pos2::new(e.x as f32 / self.ppp, e.y as f32 / self.ppp)
    }

    fn set_modifiers(&mut self, mods: i32) {
        let m = Modifiers {
            alt: mods & sys::MOD_ALT != 0,
            ctrl: mods & sys::MOD_CTRL != 0,
            shift: mods & sys::MOD_SHIFT != 0,
            mac_cmd: false,
            command: mods & sys::MOD_CTRL != 0,
        };
        if m != self.modifiers {
            self.modifiers = m;
            self.events.push(Event::ModifiersChanged(m));
        }
    }

    /// Un évènement de UIKit -> ceux d'egui ; true : une passe est à faire.
    fn event(&mut self, e: &sys::Event) -> bool {
        match e.ty {
            sys::WEV_KEY => {
                self.set_modifiers(e.mods);
                let pressed = e.kind == 1;
                let m = self.modifiers;
                let key = keys::logical(e.code, e.ch);
                if let Some(key) = key {
                    if pressed && m.command && !m.alt {
                        match key {
                            egui::Key::C => self.events.push(Event::Copy),
                            egui::Key::X => self.events.push(Event::Cut),
                            egui::Key::V => {
                                if let Some(t) = clipboard_get() {
                                    self.events.push(Event::Paste(t));
                                }
                            }
                            _ => {}
                        }
                    }
                    self.events.push(Event::Key { key, physical_key: keys::physical(e.code), pressed, repeat: false, modifiers: m });
                }
                // le texte : un caractère imprimable, sans Ctrl (AltGr donne Ctrl+Alt : du texte)
                if pressed
                    && let Some(c) = char::from_u32(e.ch as u32)
                    && !c.is_control()
                    && (!m.ctrl || m.alt)
                {
                    self.events.push(Event::Text(c.to_string()));
                }
                true
            }
            sys::WEV_MOUSE => {
                self.set_modifiers(e.mods);
                let pos = self.pos(e);
                if pos != self.pointer || e.kind == 0 {
                    self.pointer = pos;
                    self.events.push(Event::PointerMoved(pos));
                }
                if e.kind == 1 || e.kind == 2 {
                    // les boutons qui ont changé (EV_CODE : bit 0 gauche, bit 1 droit, bit 2 milieu). À l'appui : ceux
                    // qui s'ajoutent ; au relâché, EV_CODE est le bouton relâché, ou ceux qui restent appuyés
                    let (changed, now) = if e.kind == 1 {
                        let now = self.buttons | e.code;
                        let c = now ^ self.buttons;
                        (if c == 0 { e.code } else { c }, now)
                    } else {
                        let rest = if e.code == self.buttons { 0 } else { e.code & self.buttons };
                        let c = self.buttons & !rest;
                        (if c == 0 { e.code.max(1) } else { c }, rest)
                    };
                    for (bit, button) in [(1, PointerButton::Primary), (2, PointerButton::Secondary), (4, PointerButton::Middle)] {
                        if changed & bit != 0 {
                            self.events.push(Event::PointerButton { pos, button, pressed: e.kind == 1, modifiers: self.modifiers });
                        }
                    }
                    self.buttons = now;
                }
                true
            }
            sys::WEV_WHEEL => {
                self.events.push(Event::MouseWheel {
                    unit: egui::MouseWheelUnit::Line,
                    delta: Vec2::new(0.0, e.y as f32),
                    phase: egui::TouchPhase::Move,
                    source: egui::MouseWheelSource::Wheel,
                    modifiers: self.modifiers,
                });
                true
            }
            sys::WEV_FOCUS => {
                self.focused = e.kind != 0;
                self.events.push(Event::WindowFocused(self.focused));
                if !self.focused {
                    self.buttons = 0;
                    self.events.push(Event::PointerGone);
                }
                true
            }
            sys::WEV_RESIZE | sys::WEV_SCREEN => {
                self.read_pixels();
                true
            }
            sys::WEV_THEME => {
                self.theme = system_theme();
                true
            }
            _ => false,
        }
    }

    fn take_input(&mut self) -> RawInput {
        let size = Vec2::new(self.width as f32, self.height as f32) / self.ppp;
        let mut input = RawInput {
            screen_rect: Some(Rect::from_min_size(Pos2::ZERO, size)),
            events: std::mem::take(&mut self.events),
            focused: self.focused,
            system_theme: Some(self.theme),
            max_texture_side: Some(8192),
            ..Default::default()
        };
        let (mut sw, mut sh, mut bottom, mut top) = (0, 0, 0, 0);
        unsafe { sys::kh_screen(&mut sw, &mut sh, &mut bottom, &mut top) };
        if let Some(v) = input.viewports.get_mut(&ViewportId::ROOT) {
            v.native_pixels_per_point = Some(self.ppp);
            v.focused = Some(self.focused);
            v.inner_rect = input.screen_rect;
            v.maximized = Some(self.maximized);
            if sw > 0 && sh > 0 {
                v.monitor_size = Some(Vec2::new(sw as f32, (sh - bottom - top).max(1) as f32) / self.ppp);
            }
            if self.close_requested {
                v.events.push(egui::ViewportEvent::Close);
            }
        }
        input
    }

    /// La sortie d'une passe : dessin, curseur, presse-papiers, commandes -> la prochaine image (None : attendre).
    fn output(&mut self, ctx: &egui::Context, out: egui::FullOutput, host: &mut Host) -> Option<Instant> {
        let po = &out.platform_output;
        for c in &po.commands {
            match c {
                OutputCommand::CopyText(t) => clipboard_set(t),
                OutputCommand::OpenUrl(u) => host.open(&u.url),
                _ => {}
            }
        }
        let cur = cursor(po.cursor_icon);
        if cur != self.cursor {
            self.cursor = cur;
            unsafe { sys::kh_cursor(cur) };
        }
        let mut delay = Duration::MAX;
        let mut cancel_close = false;
        if let Some(vo) = out.viewport_output.get(&ViewportId::ROOT) {
            delay = vo.repaint_delay;
            for c in &vo.commands {
                match c {
                    ViewportCommand::Close => host.close = true,
                    ViewportCommand::CancelClose => cancel_close = true,
                    ViewportCommand::Title(t) => host.set_title(t),
                    ViewportCommand::Minimized(true) => unsafe { sys::kh_minimize() },
                    ViewportCommand::Maximized(m) if *m != self.maximized => {
                        self.maximized = *m;
                        unsafe { sys::kh_maximize() }
                    }
                    ViewportCommand::StartDrag => unsafe { sys::kh_drag() },
                    ViewportCommand::InnerSize(v) => {
                        unsafe { sys::kh_resize((v.x * self.ppp) as _, (v.y * self.ppp) as _) };
                        self.read_pixels();
                        delay = Duration::ZERO;
                    }
                    ViewportCommand::MinInnerSize(v) => unsafe { sys::kh_min_size((v.x * self.ppp) as _, (v.y * self.ppp) as _) },
                    _ => {}
                }
            }
        }
        if self.close_requested {
            self.close_requested = false;
            if !cancel_close {
                host.close = true;
            }
        }
        self.paint(ctx, out);
        if delay == Duration::MAX { None } else { Some(Instant::now() + delay.max(Duration::from_millis(15))) }
    }

    fn paint(&mut self, ctx: &egui::Context, out: egui::FullOutput) {
        if self.pixels.is_null() || self.width == 0 || self.height == 0 {
            return;
        }
        let prims = ctx.tessellate(out.shapes, out.pixels_per_point);
        let (w, h) = (self.width, self.height);
        // l'image dans le tampon de derrière (0x00RRGGBB : en mémoire B, G, R, 0, l'ordre BGRA)
        if self.back.len() != w * h {
            self.back = vec![[0; 4]; w * h];
            self.full = true;
        }
        let face = unsafe { sys::kh_token(sys::TJ_FACE) }.to_le_bytes();
        self.back.fill([face[0], face[1], face[2], 255]);
        let mut buf = BufferMutRef::new(&mut self.back, w, h);
        self.render.render(&mut buf, &prims, &out.textures_delta, out.pixels_per_point);
        // puis dans la fenêtre : seulement les lignes qui ont changé, et UIKit ne recopie que leur bande
        let px = unsafe { std::slice::from_raw_parts_mut(self.pixels as *mut [u8; 4], w * h) };
        let (mut y0, mut y1) = (h, 0);
        for y in 0..h {
            let r = y * w..(y + 1) * w;
            if self.full || px[r.clone()] != self.back[r.clone()] {
                px[r.clone()].copy_from_slice(&self.back[r]);
                y0 = y0.min(y);
                y1 = y + 1;
            }
        }
        self.full = false;
        // les boutons du cadre (réduire, agrandir, fermer) sont dessinés par Elegant sur le haut droit du contenu :
        // en clair sur un fond foncé (la luminance moyenne sous eux, avec une marge pour ne pas basculer sans cesse)
        if y1 > y0 && y0 < 40 {
            let (bw, bh, m) = (102usize, 26usize, 5usize); // FRAME_CAP_W, FRAME_CAP_H, FRAME_CAP_MARGIN (uk_consts.h)
            if w > bw + m && h > bh + m {
                let mut sum = 0u64;
                let mut n = 0u64;
                for y in (m..m + bh).step_by(2) {
                    for x in (w - m - bw..w - m).step_by(2) {
                        let [b, g, r, _] = self.back[y * w + x];
                        sum += (r as u64 * 299 + g as u64 * 587 + b as u64 * 114) / 1000;
                        n += 1;
                    }
                }
                let lum = sum / n.max(1);
                let light = match self.glyphs_light {
                    Some(true) => lum < 150,
                    Some(false) => lum < 110,
                    None => lum < 128,
                };
                if self.glyphs_light != Some(light) {
                    self.glyphs_light = Some(light);
                    unsafe { sys::kh_frame_glyphs(light as _) };
                }
            }
        }
        if y1 > y0 {
            unsafe { sys::kh_update(0, y0 as _, w as _, (y1 - y0) as _) };
        }
    }
}

fn cursor(c: CursorIcon) -> i32 {
    use CursorIcon::*;
    match c {
        Text | VerticalText => sys::UC_TEXT,
        PointingHand | Alias | ContextMenu => sys::UC_HAND,
        ResizeHorizontal | ResizeEast | ResizeWest | ResizeColumn => sys::UC_HRESIZE,
        ResizeVertical | ResizeNorth | ResizeSouth | ResizeRow => sys::UC_VRESIZE,
        ResizeNwSe | ResizeNorthWest | ResizeSouthEast => sys::UC_NWSE,
        ResizeNeSw | ResizeNorthEast | ResizeSouthWest => sys::UC_NESW,
        Wait | Progress => sys::UC_WAIT,
        Crosshair | Cell => sys::UC_CROSS,
        Grab | Grabbing => sys::UC_DRAG,
        Move | AllScroll => sys::UC_MOVE,
        NotAllowed | NoDrop => sys::UC_NO,
        _ => sys::UC_ARROW,
    }
}
