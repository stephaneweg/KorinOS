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

use egui::{CursorIcon, Event, Modifiers, OutputCommand, PointerButton, Pos2, RawInput, Rect, Theme, Vec2, ViewportCommand, ViewportId};
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
            if win.event(&e) {
                next_frame = Some(Instant::now());
            }
        }
        if next_frame.is_none_or(|t| t > Instant::now()) {
            continue;
        }
        // une passe d'egui
        let mut input = win.take_input();
        input.time = Some(start.elapsed().as_secs_f64());
        let out = pass(ctx, input, &mut host);
        next_frame = win.output(ctx, out, &mut host);
        if host.close {
            return Ok(());
        }
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
}

impl Window {
    fn new(ppp: f32) -> Self {
        let mut w = Self {
            ppp,
            width: 0,
            height: 0,
            pixels: std::ptr::null_mut(),
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
        // les pixels de la fenêtre, 0x00RRGGBB : en mémoire B, G, R, 0 (BGRA)
        let px = unsafe { std::slice::from_raw_parts_mut(self.pixels as *mut [u8; 4], self.width * self.height) };
        let face = unsafe { sys::kh_token(sys::TJ_FACE) }.to_le_bytes();
        px.fill([face[0], face[1], face[2], 255]);
        let mut buf = BufferMutRef::new(px, self.width, self.height);
        self.render.render(&mut buf, &prims, &out.textures_delta, out.pixels_per_point);
        unsafe { sys::kh_update(0, 0, self.width as _, self.height as _) };
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
