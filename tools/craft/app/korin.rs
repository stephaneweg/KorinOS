//! Korin OS (ajouté par tools/craft/korin_app.py, docs/CRAFT.md) : le presse-papiers de l'app sur celui de Korin.
//!
//! Les copies de l'app gardent tous leurs formats en mémoire (le document, HTML, RTF, image) et donnent leur texte au
//! presse-papiers de Korin ; une copie faite ailleurs dans Korin (son texte change) se colle comme du texte.

use std::path::PathBuf;

use craft::platform::clipboard::{ClipboardBackend, ClipboardError, ClipboardImage, Flavor, MemoryClipboard};

const TEXT: &str = "text/plain";

#[derive(Default)]
pub struct KorinClipboard {
    memory: MemoryClipboard,
    /// le texte que l'app a mis dans le presse-papiers de Korin (None : la copie n'avait pas de texte)
    ours: Option<String>,
}

impl KorinClipboard {
    /// Le presse-papiers de Korin a changé depuis la dernière copie de l'app : son texte.
    fn foreign(&self) -> Option<String> {
        let now = egui_korin::clipboard_get()?;
        (self.ours.as_deref() != Some(now.as_str())).then_some(now)
    }
}

impl ClipboardBackend for KorinClipboard {
    fn write(&mut self, flavors: &[Flavor]) -> Result<usize, ClipboardError> {
        let r = self.memory.write(flavors);
        self.ours = flavors.iter().find(|f| f.mime == TEXT).map(|f| String::from_utf8_lossy(&f.data).into_owned());
        if let Some(t) = &self.ours {
            egui_korin::clipboard_set(t);
        }
        r
    }

    fn read(&mut self, mimes: &[&str], limit: u64) -> Result<Option<Flavor>, ClipboardError> {
        if let Some(t) = self.foreign() {
            return Ok(mimes.contains(&TEXT).then(|| Flavor::text(&t)));
        }
        self.memory.read(mimes, limit)
    }

    fn read_image(&mut self, limit: u64) -> Result<Option<ClipboardImage>, ClipboardError> {
        if self.foreign().is_some() {
            return Ok(None);
        }
        self.memory.read_image(limit)
    }

    fn read_files(&mut self, limit: usize) -> Result<Option<Vec<PathBuf>>, ClipboardError> {
        if self.foreign().is_some() {
            return Ok(None);
        }
        self.memory.read_files(limit)
    }

    fn has(&mut self, mimes: &[&str]) -> bool {
        if self.foreign().is_some() {
            return mimes.contains(&TEXT);
        }
        self.memory.has(mimes)
    }

    fn formats(&mut self) -> Vec<String> {
        if self.foreign().is_some() {
            return vec![TEXT.to_owned()];
        }
        self.memory.formats()
    }

    fn owns(&mut self) -> Option<bool> {
        Some(self.foreign().is_none())
    }
}
