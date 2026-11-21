//! La démo d'egui sur Korin (/apps/eguidemo.app) : les fenêtres de démonstration d'egui (egui_demo_lib), et un
//! panneau qui essaie ce que l'hôte donne (presse-papiers, dialogues, thème).
fn main() {
    let mut demo = egui_demo_lib::DemoWindows::default();
    let mut text = String::from("Bonjour de Korin");
    let mut picked = String::new();
    let mut frames = 0u64;
    let options = egui_korin::Options::new("Démo egui").size(1100, 720);
    // la barre de menus de Korin : un menu avec un sous-menu, une entrée cochée, une grisée (egui_korin::menu)
    let mut chosen = String::new();
    let mut checked = false;
    let r = egui_korin::run(options, move |ui, host| {
        frames += 1;
        egui_korin::menu::set(
            "MDémo\nI1\tBonjour\tCtrl+B\nI2\tCochée\n-\nSSous-menu\nI3\tPremière\nI4\tSeconde\nE\nI5\tGrisée\n\
             MFenêtre\nI6\tQuitter la démo\n",
        );
        for id in egui_korin::menu::take_chosen() {
            chosen = format!("menu : {id}");
            if id == 2 {
                checked = !checked;
            }
            if id == 6 {
                host.close();
            }
        }
        egui_korin::menu::set_states(&[(2, if checked { 2 } else { 0 }), (5, 1)]);
        egui::Panel::top("korin").show(ui, |ui| {
            ui.horizontal(|ui| {
                ui.strong("egui sur Korin");
                ui.separator();
                ui.text_edit_singleline(&mut text);
                if ui.button("Copier").clicked() {
                    egui_korin::clipboard_set(&text);
                }
                if ui.button("Ouvrir…").clicked()
                    && let Some(p) = host.file_open("HOME:/", None)
                {
                    picked = p;
                }
                if !picked.is_empty() {
                    ui.label(&picked);
                }
                ui.separator();
                ui.label(format!("passe {frames}"));
                if !chosen.is_empty() {
                    ui.label(&chosen);
                }
                if ui.button("Quitter").clicked() {
                    host.close();
                }
            });
        });
        demo.ui(ui);
    });
    if let Err(e) = r {
        eprintln!("{e}");
        std::process::exit(1);
    }
}
