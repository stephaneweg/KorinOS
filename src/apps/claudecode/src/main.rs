//! Claude Code pour Korin (/apps/claudecode.app) : le frontal de la CLI officielle de Claude Code, en Rust sur egui,
//! dans le style des apps Craft (docs/etude/LINUX-HOST.md, jalon 4). La CLI (paquet claude-code) fait tout le
//! travail et garde les conversations ; l'app les montre, les range et les pilote (cli.rs).

#![cfg_attr(not(target_os = "korin"), allow(dead_code))]

mod app;
mod chat;
mod cli;
mod md;
mod organize;
mod store;
mod theme;

#[cfg(target_os = "korin")]
fn main() {
    let ctx = egui::Context::default();
    let mut app = app::App::new(&ctx, Box::new(|| egui_korin::system_theme() == egui::Theme::Dark));
    let mut options = egui_korin::Options::new("Claude Code").size(1180, 780);
    options.min_size = Some([720, 480]);
    let r = egui_korin::run_with(options, &ctx, move |ctx, input, host| {
        // la barre de menus de Korin : Fichier, Conversation, Affichage (app.rs)
        egui_korin::menu::set(app.menu_text());
        for id in egui_korin::menu::take_chosen() {
            if app.menu_command(ctx, id) {
                host.close();
            }
        }
        let out = ctx.run_ui(input, |ui| app.ui(ui));
        if let Some(dir) = app.folder_request.take() {
            // (le sélecteur de dossier d'UIKit, demandé par la boîte « Nouvelle conversation »)
            if let Some(d) = host.folder_choose(&dir) {
                app.folder_chosen(d);
            }
            ctx.request_repaint();
        }
        egui_korin::menu::set_states(&app.menu_states());
        out
    });
    if let Err(e) = r {
        eprintln!("claudecode : {e}");
        std::process::exit(1);
    }
}

/// Ailleurs que sur Korin : rien à montrer (les modules s'essaient par `cargo test`).
#[cfg(not(target_os = "korin"))]
fn main() {
    let ctx = egui::Context::default();
    let _ = app::App::new(&ctx, Box::new(|| true));
    eprintln!("claudecode : une app de Korin (egui-korin) ; ici, seulement cargo test");
}
