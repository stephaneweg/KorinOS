//! Ce que l'interface demande à la fenêtre : ouvrir une adresse, choisir un dossier, changer le titre, fermer. Une
//! file à part (l'interface n'a pas l'hôte sous la main) que la boucle relève après chaque passe : sous Korin
//! `egui_korin::Host` (main.rs), sur l'hôte de développement eframe et `zenity` (dev.rs).

use std::sync::Mutex;

#[derive(Clone, Debug, PartialEq)]
pub enum Req {
    /// Une adresse (http, https, mailto) ou un dossier à montrer.
    Open(String),
    /// Le sélecteur de dossier, ouvert sur `start` ; la réponse revient par `picked(tag)`.
    PickFolder { start: String, tag: String },
    Title(String),
    Close,
}

static QUEUE: Mutex<Vec<Req>> = Mutex::new(Vec::new());
static PICKED: Mutex<Vec<(String, String)>> = Mutex::new(Vec::new());

pub fn open(target: &str) {
    QUEUE.lock().unwrap().push(Req::Open(target.to_string()));
}

pub fn pick_folder(start: &str, tag: &str) {
    QUEUE.lock().unwrap().push(Req::PickFolder { start: start.to_string(), tag: tag.to_string() });
}

pub fn set_title(t: &str) {
    QUEUE.lock().unwrap().push(Req::Title(t.to_string()));
}

pub fn close() {
    QUEUE.lock().unwrap().push(Req::Close);
}

/// Les demandes depuis la dernière passe (la boucle de la fenêtre).
pub fn take() -> Vec<Req> {
    std::mem::take(&mut *QUEUE.lock().unwrap())
}

/// La boucle de la fenêtre : le dossier choisi pour `tag`.
pub fn answer_folder(tag: &str, path: String) {
    PICKED.lock().unwrap().push((tag.to_string(), path));
}

/// Le dossier choisi pour `tag`, une fois.
pub fn picked(tag: &str) -> Option<String> {
    let mut p = PICKED.lock().unwrap();
    let i = p.iter().position(|(t, _)| t == tag)?;
    Some(p.remove(i).1)
}
