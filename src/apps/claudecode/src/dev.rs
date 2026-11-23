//! L'hôte de développement Linux (fonction cargo `dev`, hors de Korin) : la même interface egui, sur le `claude`
//! installé (le PATH, ou `CLAUDECODE_CLI`), pour essayer la disposition et les fonctions sans Korin.
//!
//! - `claudecode` : une fenêtre eframe (glow) ;
//! - `claudecode --shot FICHIER.png [options]` : sans fenêtre, l'interface dessinée par le rendu CPU de Korin
//!   (`hdrshot-egui-backend`, celui d'egui-korin) dans une image PNG, après quelques images et des gestes scriptés :
//!   - `--size 1440x900` (défaut), `--theme dark|light`, `--route new|overview|worktrees|background|extensions|settings|session:<id>` ;
//!   - `--wait S` : laisser S secondes au moteur avant la capture (défaut 1,5) ;
//!   - puis des gestes, dans l'ordre : `--click X,Y`, `--rclick X,Y`, `--type TEXTE`, `--key NOM` (Enter, Escape, Tab,
//!     ArrowDown, ctrl+k, ctrl+shift+a…), `--sleep S`, `--palette`, `--shot-also FICHIER.png` (une capture en route).
//!
//! tools/claudecode/dev.sh le construit et le lance.

use std::time::{Duration, Instant};

use egui::{Event, Key, Modifiers, PointerButton, Pos2, RawInput, Rect, Vec2};

use crate::app::{App, Route};

struct Win {
    app: App,
}

impl eframe::App for Win {
    fn ui(&mut self, ui: &mut egui::Ui, _frame: &mut eframe::Frame) {
        let focused = ui.ctx().input(|i| i.focused);
        self.app.set_focused(focused);
        self.app.ui(ui);
        let ctx = ui.ctx().clone();
        for r in crate::host::take() {
            match r {
                crate::host::Req::Open(t) => {
                    let _ = std::process::Command::new("xdg-open").arg(&t).spawn();
                }
                crate::host::Req::PickFolder { start, tag } => {
                    // (zenity s'il est là ; sinon le dossier de départ)
                    let out = std::process::Command::new("zenity")
                        .args(["--file-selection", "--directory", "--filename"])
                        .arg(format!("{}/", start.trim_end_matches('/')))
                        .output();
                    if let Ok(o) = out
                        && o.status.success()
                    {
                        let p = String::from_utf8_lossy(&o.stdout).trim().to_string();
                        if !p.is_empty() {
                            crate::host::answer_folder(&tag, p);
                        }
                    }
                }
                crate::host::Req::Title(t) => ctx.send_viewport_cmd(egui::ViewportCommand::Title(t)),
                crate::host::Req::Close => ctx.send_viewport_cmd(egui::ViewportCommand::Close),
            }
        }
    }

    fn on_exit(&mut self, _gl: Option<&eframe::glow::Context>) {
        self.app.shutdown();
    }
}

pub fn main() {
    let args: Vec<String> = std::env::args().skip(1).collect();
    if let Some(i) = args.iter().position(|a| a == "--shot") {
        let out = args.get(i + 1).cloned().unwrap_or_else(|| "shot.png".into());
        if let Err(e) = shot(&out, &args) {
            eprintln!("claudecode --shot : {e}");
            std::process::exit(1);
        }
        return;
    }
    let dark = !args.iter().any(|a| a == "--light");
    let route = arg(&args, "--route").map(|r| parse_route(&r));
    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default().with_title("Claude Code").with_inner_size([1280.0, 820.0]).with_min_inner_size([860.0, 540.0]),
        ..Default::default()
    };
    let r = eframe::run_native(
        "Claude Code",
        options,
        Box::new(move |cc| {
            let mut app = App::new(&cc.egui_ctx, Box::new(move || dark));
            if let Some(r) = route {
                app.navigate(r);
            }
            Ok(Box::new(Win { app }))
        }),
    );
    if let Err(e) = r {
        eprintln!("claudecode : {e}");
        std::process::exit(1);
    }
}

fn arg(args: &[String], name: &str) -> Option<String> {
    args.iter().position(|a| a == name).and_then(|i| args.get(i + 1)).cloned()
}

fn parse_route(r: &str) -> Route {
    match r {
        "new" => Route::NewSession,
        "overview" => Route::Overview,
        "worktrees" => Route::Worktrees,
        "background" => Route::Background,
        "extensions" => Route::Extensions,
        "settings" => Route::Settings,
        s => match s.strip_prefix("session:") {
            Some(id) => Route::Session(id.to_string()),
            None => Route::NewSession,
        },
    }
}

/// Le nom d'une touche d'egui (`Enter`, `ArrowDown`, `k`…).
fn key_of(name: &str) -> Option<Key> {
    Key::from_name(name).or_else(|| Key::from_name(&name.to_uppercase()))
}

/// Une capture sans fenêtre : des images de l'app, les gestes, puis le rendu CPU dans un PNG.
fn shot(out: &str, args: &[String]) -> Result<(), String> {
    let (w, h) = arg(args, "--size")
        .and_then(|s| {
            let (a, b) = s.split_once('x')?;
            Some((a.parse::<u32>().ok()?, b.parse::<u32>().ok()?))
        })
        .unwrap_or((1440, 900));
    let dark = arg(args, "--theme").as_deref() != Some("light");
    let ctx = egui::Context::default();
    let mut app = App::new(&ctx, Box::new(move || dark));
    if let Some(r) = arg(args, "--route") {
        app.navigate(parse_route(&r));
    }
    let wait = arg(args, "--wait").and_then(|s| s.parse::<f64>().ok()).unwrap_or(1.5);
    let screen = Rect::from_min_size(Pos2::ZERO, Vec2::new(w as f32, h as f32));
    let start = Instant::now();
    let mut pointer = Pos2::new(w as f32 / 2.0, h as f32 / 2.0);
    // (les textures de chaque image, gardées pour le rendu : il ne voit que les images capturées)
    let textures = std::cell::RefCell::new(egui::TexturesDelta::default());
    let mut frame = |app: &mut App, events: Vec<Event>, _mods: Modifiers| -> egui::FullOutput {
        let input = RawInput {
            screen_rect: Some(screen),
            time: Some(start.elapsed().as_secs_f64()),
            events,
            focused: true,
            ..Default::default()
        };
        let mut out = ctx.run_ui(input, |ui| app.ui(ui));
        let _ = crate::host::take();
        textures.borrow_mut().append(std::mem::take(&mut out.textures_delta));
        out
    };
    let settle = |app: &mut App, frame: &mut dyn FnMut(&mut App, Vec<Event>, Modifiers) -> egui::FullOutput, secs: f64| -> egui::FullOutput {
        let until = Instant::now() + Duration::from_secs_f64(secs);
        let mut last = frame(app, vec![], Modifiers::NONE);
        while Instant::now() < until {
            std::thread::sleep(Duration::from_millis(40));
            last = frame(app, vec![], Modifiers::NONE);
        }
        last
    };
    let mut last = settle(&mut app, &mut frame, wait);
    let mut i = 0;
    while i < args.len() {
        let a = args[i].as_str();
        let v = args.get(i + 1).cloned().unwrap_or_default();
        match a {
            "--click" | "--rclick" => {
                let (x, y) = v.split_once(',').ok_or("--click X,Y")?;
                pointer = Pos2::new(x.trim().parse().map_err(|_| "x")?, y.trim().parse().map_err(|_| "y")?);
                let button = if a == "--click" { PointerButton::Primary } else { PointerButton::Secondary };
                frame(&mut app, vec![Event::PointerMoved(pointer)], Modifiers::NONE);
                frame(&mut app, vec![Event::PointerButton { pos: pointer, button, pressed: true, modifiers: Modifiers::NONE }], Modifiers::NONE);
                frame(&mut app, vec![Event::PointerButton { pos: pointer, button, pressed: false, modifiers: Modifiers::NONE }], Modifiers::NONE);
                last = settle(&mut app, &mut frame, 0.3);
                i += 2;
            }
            "--type" => {
                frame(&mut app, vec![Event::Text(v.clone())], Modifiers::NONE);
                last = settle(&mut app, &mut frame, 0.2);
                i += 2;
            }
            "--key" => {
                let mut mods = Modifiers::NONE;
                let mut name = v.as_str();
                for part in v.split('+') {
                    match part.to_ascii_lowercase().as_str() {
                        "ctrl" => mods = mods | Modifiers::COMMAND,
                        "shift" => mods = mods | Modifiers::SHIFT,
                        "alt" => mods = mods | Modifiers::ALT,
                        _ => name = part,
                    }
                }
                let key = key_of(name).ok_or_else(|| format!("touche inconnue : {name}"))?;
                frame(&mut app, vec![Event::Key { key, physical_key: None, pressed: true, repeat: false, modifiers: mods }], mods);
                frame(&mut app, vec![Event::Key { key, physical_key: None, pressed: false, repeat: false, modifiers: mods }], mods);
                last = settle(&mut app, &mut frame, 0.3);
                i += 2;
            }
            "--sleep" => {
                last = settle(&mut app, &mut frame, v.parse::<f64>().unwrap_or(1.0));
                i += 2;
            }
            "--palette" => {
                app.ui_state().palette_open = true;
                last = settle(&mut app, &mut frame, 0.3);
                i += 1;
            }
            "--shot-also" => {
                last = frame(&mut app, vec![Event::PointerMoved(pointer)], Modifiers::NONE);
                render(&ctx, last, std::mem::take(&mut *textures.borrow_mut()), w, h, &v)?;
                last = frame(&mut app, vec![], Modifiers::NONE);
                i += 2;
            }
            _ => i += 1,
        }
    }
    // (une dernière image complète : les textures des polices déjà là)
    let _ = last;
    let full = frame(&mut app, vec![Event::PointerMoved(pointer)], Modifiers::NONE);
    let tex = std::mem::take(&mut *textures.borrow_mut());
    render(&ctx, full, tex, w, h, out)?;
    app.shutdown();
    Ok(())
}

/// Le rendu CPU de Korin dans un PNG (RGBA). Le rendu garde ses textures d'une capture à l'autre.
fn render(ctx: &egui::Context, out: egui::FullOutput, mut textures: egui::TexturesDelta, w: u32, h: u32, path: &str) -> Result<(), String> {
    use egui_software_backend::{BufferMutRef, ColorFieldOrder, EguiSoftwareRender};
    use std::sync::Mutex;
    static RENDER: Mutex<Option<EguiSoftwareRender>> = Mutex::new(None);
    let mut guard = RENDER.lock().unwrap();
    let render = guard.get_or_insert_with(|| EguiSoftwareRender::new(ColorFieldOrder::Rgba));
    let prims = ctx.tessellate(out.shapes, out.pixels_per_point);
    let mut px = vec![[0u8, 0, 0, 255]; (w * h) as usize];
    let mut buf = BufferMutRef::new(&mut px, w as usize, h as usize);
    render.render(&mut buf, &prims, &textures, out.pixels_per_point);
    textures.clear();
    let file = std::fs::File::create(path).map_err(|e| format!("{path} : {e}"))?;
    let mut enc = png::Encoder::new(std::io::BufWriter::new(file), w, h);
    enc.set_color(png::ColorType::Rgba);
    enc.set_depth(png::BitDepth::Eight);
    let mut wr = enc.write_header().map_err(|e| e.to_string())?;
    let bytes: Vec<u8> = px.iter().flat_map(|p| [p[0], p[1], p[2], 255]).collect();
    wr.write_image_data(&bytes).map_err(|e| e.to_string())?;
    Ok(())
}
