//! Le rangement des conversations, propre à cette app (la CLI n'en a pas) : des dossiers nommés, dans un ordre, et les
//! conversations épinglées. Un fichier JSON à l'utilisateur : `~/.config/claudecode/organize.json`. Une session qui
//! n'existe plus (supprimée ailleurs) est oubliée au prochain enregistrement.

use std::collections::HashSet;
use std::fs;
use std::path::PathBuf;

use serde::{Deserialize, Serialize};

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct Folder {
    pub name: String,
    #[serde(default)]
    pub sessions: Vec<String>,
    #[serde(default)]
    pub collapsed: bool,
}

#[derive(Clone, Debug, Default, Serialize, Deserialize)]
pub struct Organize {
    #[serde(default)]
    pub folders: Vec<Folder>,
    #[serde(default)]
    pub pinned: Vec<String>,
    /// Les derniers dossiers de travail choisis pour une nouvelle conversation (le plus récent d'abord).
    #[serde(default)]
    pub recent_dirs: Vec<String>,
    /// Le modèle et le mode d'autorisation choisis (vides : ceux de la CLI).
    #[serde(default)]
    pub model: String,
    #[serde(default)]
    pub permission_mode: String,
    /// Le thème : « system », « dark », « light ».
    #[serde(default)]
    pub theme: String,
    /// Le volet « Environnement » à droite (dossier, dépôt, utilisation de l'abonnement) est ouvert.
    #[serde(default)]
    pub env_panel: bool,
    /// La liste des sessions du cloud (Claude Code sur le web) est dépliée.
    #[serde(default)]
    pub cloud_open: bool,
    /// L'onglet du volet : « env » (vide) ou « term ».
    #[serde(default)]
    pub env_tab: String,
}

pub fn path() -> PathBuf {
    crate::store::home().join(".config").join("claudecode").join("organize.json")
}

impl Organize {
    pub fn load() -> Self {
        fs::read_to_string(path()).ok().and_then(|t| serde_json::from_str(&t).ok()).unwrap_or_default()
    }

    pub fn save(&self) {
        let p = path();
        if let Some(d) = p.parent() {
            let _ = fs::create_dir_all(d);
        }
        if let Ok(t) = serde_json::to_string_pretty(self) {
            let tmp = p.with_extension("json.tmp");
            if fs::write(&tmp, t).is_ok() {
                let _ = fs::rename(&tmp, &p);
            }
        }
    }

    /// Oublie les sessions qui n'existent plus.
    pub fn prune(&mut self, alive: &HashSet<String>) -> bool {
        let before = self.pinned.len() + self.folders.iter().map(|f| f.sessions.len()).sum::<usize>();
        self.pinned.retain(|s| alive.contains(s));
        for f in &mut self.folders {
            f.sessions.retain(|s| alive.contains(s));
        }
        before != self.pinned.len() + self.folders.iter().map(|f| f.sessions.len()).sum::<usize>()
    }

    pub fn is_pinned(&self, id: &str) -> bool {
        self.pinned.iter().any(|s| s == id)
    }

    pub fn toggle_pin(&mut self, id: &str) {
        if self.is_pinned(id) {
            self.pinned.retain(|s| s != id);
        } else {
            self.pinned.insert(0, id.to_owned());
        }
    }

    /// Le dossier d'une session (son rang), s'il en a un.
    pub fn folder_of(&self, id: &str) -> Option<usize> {
        self.folders.iter().position(|f| f.sessions.iter().any(|s| s == id))
    }

    /// Range une session dans un dossier (None : hors des dossiers).
    pub fn move_to(&mut self, id: &str, folder: Option<usize>) {
        for f in &mut self.folders {
            f.sessions.retain(|s| s != id);
        }
        if let Some(i) = folder
            && let Some(f) = self.folders.get_mut(i)
        {
            f.sessions.push(id.to_owned());
        }
    }

    pub fn add_folder(&mut self, name: &str) -> usize {
        self.folders.push(Folder { name: name.trim().to_owned(), ..Default::default() });
        self.folders.len() - 1
    }

    /// Supprime un dossier : ses conversations reviennent dans la liste (elles ne sont pas supprimées).
    pub fn remove_folder(&mut self, i: usize) {
        if i < self.folders.len() {
            self.folders.remove(i);
        }
    }

    /// Déplace un dossier d'un rang vers le haut (-1) ou le bas (+1).
    pub fn shift_folder(&mut self, i: usize, d: isize) {
        let j = i as isize + d;
        if j >= 0 && (j as usize) < self.folders.len() {
            self.folders.swap(i, j as usize);
        }
    }

    pub fn forget(&mut self, id: &str) {
        self.pinned.retain(|s| s != id);
        self.move_to(id, None);
    }

    pub fn note_dir(&mut self, dir: &str) {
        self.recent_dirs.retain(|d| d != dir);
        self.recent_dirs.insert(0, dir.to_owned());
        self.recent_dirs.truncate(8);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn folders_and_pins() {
        let mut o = Organize::default();
        let f = o.add_folder(" Korin ");
        o.move_to("a", Some(f));
        o.move_to("b", Some(f));
        o.toggle_pin("a");
        assert_eq!(o.folder_of("a"), Some(0));
        assert!(o.is_pinned("a"));
        o.move_to("a", None);
        assert_eq!(o.folder_of("a"), None);
        let alive: HashSet<String> = ["a".to_owned()].into();
        assert!(o.prune(&alive));
        assert!(o.folders[0].sessions.is_empty());
        assert_eq!(o.folders[0].name, "Korin");
        o.remove_folder(0);
        assert!(o.folders.is_empty());
    }
}
