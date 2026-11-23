//! Claude Code pour Korin (/apps/claudecode.app) : le frontal de la CLI officielle de Claude Code, en Rust sur egui.
//! L'interface et les fonctions sont celles de ClaudeCodeUI (Atypical Consulting, MIT : rail des sessions, fil,
//! inspecteur, palette Ctrl K, vue d'ensemble, worktrees, extensions… ; docs/etude/CLAUDECODE-UI.md), réécrites en
//! egui avec l'apparence de l'app de Korin (theme.rs) ; le moteur est le sien (`crates/core`, backend.rs).
//!
//! - sous Korin : la fenêtre d'egui-korin (docs/EGUI.md), la barre de menus de Korin ;
//! - sur Linux, la fonction `dev` : la même interface dans une fenêtre eframe, ou en image sans fenêtre (`--shot`),
//!   sur le `claude` installé (dev.rs, tools/claudecode/dev.sh) ;
//! - ailleurs, sans `dev` : seulement `cargo test`.

#![cfg_attr(not(any(target_os = "korin", feature = "dev")), allow(dead_code))]

mod app;
mod backend;
mod host;
mod layout;
mod md;
mod organize;
mod pages;
mod session;
mod state;
mod store;
mod term;
mod theme;
mod widgets;

#[cfg(all(feature = "dev", not(target_os = "korin")))]
mod dev;

#[cfg(target_os = "korin")]
fn main() {
    let ctx = egui::Context::default();
    let mut app = app::App::new(&ctx, Box::new(|| egui_korin::system_theme() == egui::Theme::Dark));
    let mut options = egui_korin::Options::new("Claude Code").size(1280, 820);
    options.min_size = Some([860, 540]);
    let r = egui_korin::run_with(options, &ctx, move |ctx, input, host| {
        // la barre de menus de Korin (app.rs : menu_text, menu_command)
        egui_korin::menu::set(&app.menu_text());
        for id in egui_korin::menu::take_chosen() {
            app.menu_command(id);
        }
        app.set_focused(input.focused);
        let out = ctx.run_ui(input, |ui| app.ui(ui));
        for r in host::take() {
            match r {
                host::Req::Open(t) => host.open(&t),
                host::Req::PickFolder { start, tag } => {
                    // (le sélecteur de dossier d'UIKit)
                    if let Some(d) = host.folder_choose(&start) {
                        host::answer_folder(&tag, d);
                    }
                    ctx.request_repaint();
                }
                host::Req::Title(t) => host.set_title(&t),
                host::Req::Close => {
                    app.shutdown();
                    host.close();
                }
            }
        }
        egui_korin::menu::set_states(&app.menu_states());
        out
    });
    if let Err(e) = r {
        eprintln!("claudecode : {e}");
        std::process::exit(1);
    }
}

#[cfg(all(feature = "dev", not(target_os = "korin")))]
fn main() {
    dev::main();
}

/// Ailleurs que sur Korin, sans la fonction `dev` : rien à montrer (les modules s'essaient par `cargo test`).
#[cfg(not(any(target_os = "korin", feature = "dev")))]
fn main() {
    eprintln!("claudecode : une app de Korin (egui-korin) ; ici : cargo run --features dev, ou cargo test");
}
