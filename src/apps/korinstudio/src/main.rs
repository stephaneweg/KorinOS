// KorinStudio : l'environnement de développement de Korin OS (docs/KORINSTUDIO.md), en Rust et egui, à la manière de
// VS Code, avec un copilote (Claude Code, Claude par l'API, Mistral Vibe, MAI) qui agit dans l'IDE par MCP.
//
//   korinstudio [dossier | fichier]
//   korinstudio --mcp-bridge <url> <jeton>     relais stdio -> MCP HTTP de l'IDE (pour les agents qui ne
//                                              parlent MCP que par stdio, Mistral Vibe)

#![allow(deprecated)]

mod agent;
mod app;
mod copilot;
mod editor;
mod highlight;
mod history;
mod icons;
mod mcp;
mod tasks;
mod theme;
mod ui;
mod workspace;

use std::path::PathBuf;

#[cfg(not(target_os = "korin"))]
struct App {
    studio: app::Studio,
}

#[cfg(not(target_os = "korin"))]
impl eframe::App for App {
    fn ui(&mut self, ui: &mut egui::Ui, _frame: &mut eframe::Frame) {
        self.studio.ui(ui);
    }

    fn on_exit(&mut self, _gl: Option<&eframe::glow::Context>) {
        self.studio.copilot.stop_agent();
        self.studio.settings.save();
    }
}

fn main() {
    let args: Vec<String> = std::env::args().collect();
    if args.get(1).map(|s| s.as_str()) == Some("--mcp-bridge") {
        let url = args.get(2).cloned().unwrap_or_default();
        let token = args.get(3).cloned().unwrap_or_default();
        std::process::exit(mcp::run_bridge(&url, &token));
    }
    let open = args.get(1).filter(|a| !a.starts_with("--")).map(PathBuf::from);
    if let Err(e) = run(open) {
        eprintln!("korinstudio : {e}");
        std::process::exit(1);
    }
}

/// Sous Korin : la fenêtre d'egui-korin, une passe d'egui par image.
#[cfg(target_os = "korin")]
fn run(open: Option<PathBuf>) -> Result<(), String> {
    let ctx = egui::Context::default();
    theme::install_fonts(&ctx);
    let mut studio = app::Studio::new(&ctx, open);
    let mut opts = egui_korin::Options::new("KorinStudio").size(1360, 860);
    opts.min_size = Some([760, 480]);
    let r = egui_korin::run_with(opts, &ctx, |ctx, raw, _host| ctx.run_ui(raw, |ui| studio.ui(ui)));
    studio.copilot.stop_agent();
    studio.settings.save();
    r.map_err(|e| e.to_string())
}

#[cfg(not(target_os = "korin"))]
fn run(open: Option<PathBuf>) -> Result<(), String> {
    let options = eframe::NativeOptions {
        viewport: egui::ViewportBuilder::default().with_title("KorinStudio").with_inner_size([1360.0, 860.0]).with_min_inner_size([760.0, 480.0]),
        ..Default::default()
    };
    eframe::run_native(
        "KorinStudio",
        options,
        Box::new(move |cc| {
            theme::install_fonts(&cc.egui_ctx);
            Ok(Box::new(App { studio: app::Studio::new(&cc.egui_ctx, open) }))
        }),
    )
    .map_err(|e| e.to_string())
}
