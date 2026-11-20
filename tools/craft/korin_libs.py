"""korin_libs : les retouches de craft-libs pour Korin (docs/CRAFT.md), après tools/craft/korin_cfg.py, sur les copies
de travail que tools/craft/build.sh refait (~/korin-craft/craft-libs-<rev>). craft-libs est prise à plusieurs
révisions d'une app à l'autre : un fichier absent est sauté, une retouche qui ne trouve pas son texte le dit, et la
compilation dit s'il manquait.

    python3 -I tools/craft/korin_libs.py <dossier de craft-libs>
"""
import os
import sys

top = sys.argv[1]


def edit(rel, pairs):
    p = os.path.join(top, rel)
    if not os.path.isfile(p):
        return
    s = open(p, encoding="utf-8").read()
    for a, b in pairs:
        if b and b in s:
            continue
        if a not in s:
            print("korin_libs : %s (%s) : texte absent : %r" % (rel, os.path.basename(top), a[:80]))
            continue
        s = s.replace(a, b)
    open(p, "w", encoding="utf-8", newline="\n").write(s)
    with open(os.path.join(top, ".korin-changed"), "a", encoding="utf-8") as f:
        f.write(rel + "\n")


# l'apparence du système : sur Korin pas de guetteur (start_active rend None), mais l'énumération des guetteurs
# devient vide et le match de Drop ne compile plus : une variante qui ne sert pas
edit("crates/platform/src/appearance.rs", [
    ('    #[cfg(any(windows, target_os = "macos"))]\n    Native { stop: std::sync::mpsc::Sender<()>',
     '    /// Korin : aucun guetteur (tools/craft/korin_libs.py)\n'
     '    #[cfg(target_os = "korin")]\n    #[allow(dead_code)]\n    Idle,\n'
     '    #[cfg(any(windows, target_os = "macos"))]\n    Native { stop: std::sync::mpsc::Sender<()>'),
    ('                #[cfg(any(windows, target_os = "macos"))]\n                Worker::Native { stop, finished, thread } => {',
     '                #[cfg(target_os = "korin")]\n                Worker::Idle => {}\n'
     '                #[cfg(any(windows, target_os = "macos"))]\n                Worker::Native { stop, finished, thread } => {'),
])
# craft-font-store : la suppression des packs de langue passe par des dossiers ouverts (cap-primitives, donc rustix,
# qui ne connaît pas Korin) ; sur Korin, les mêmes étapes par chemins (Korin n'a pas de liens symboliques à éviter).
# Le code est dans src/language_packs.rs ou src/language_packs/removal.rs selon la révision.
KORIN_REMOVAL = '''
/// Korin : pas de liens symboliques ni d'`openat` complet ; les mêmes étapes par chemins (tools/craft/korin_libs.py).
#[cfg(target_os = "korin")]
pub(crate) struct PackRemovalRoot {
    packs: std::path::PathBuf,
    recovery: std::path::PathBuf,
    stage: Option<std::path::PathBuf>,
}

#[cfg(target_os = "korin")]
#[allow(dead_code)]
impl PackRemovalRoot {
    pub(super) fn open(store: &Path) -> std::io::Result<Self> {
        let packs = store.join(LANGUAGE_PACKS);
        let recovery = packs.join(".removing");
        for dir in [store, packs.as_path(), recovery.as_path()] {
            if !std::fs::metadata(dir)?.is_dir() {
                return Err(std::io::Error::other(format!("{} is not a folder", dir.display())));
            }
        }
        Ok(Self { packs, recovery, stage: None })
    }

    pub(crate) fn create_stage(&self, id: &str) -> std::io::Result<()> {
        std::fs::create_dir(self.recovery.join(id))
    }

    pub(crate) fn with_stage(mut self, id: &str) -> std::io::Result<Self> {
        let dir = self.recovery.join(id);
        match std::fs::metadata(&dir) {
            Ok(meta) if meta.is_dir() => {
                self.stage = Some(dir);
                Ok(self)
            }
            other => {
                let _ = std::fs::remove_dir(&dir);
                Err(other.err().unwrap_or_else(|| std::io::Error::other("recovery stage is not a folder")))
            }
        }
    }

    fn move_pack(&self, locale: &str) -> std::io::Result<()> {
        let stage = self.stage.as_ref().ok_or_else(|| std::io::Error::other("missing recovery stage"))?;
        std::fs::rename(self.packs.join(locale), stage.join(locale))
    }

    fn restore_pack(&self, locale: &str) -> std::io::Result<()> {
        let stage = self.stage.as_ref().ok_or_else(|| std::io::Error::other("missing recovery stage"))?;
        std::fs::rename(stage.join(locale), self.packs.join(locale))
    }

    pub(super) fn retains_receipt(&self, id: &str) -> bool {
        !matches!(
            std::fs::symlink_metadata(self.recovery.join(id)),
            Err(error) if error.kind() == std::io::ErrorKind::NotFound
        )
    }

    pub(crate) fn remove_stage(mut self, id: &str, display_parent: &Path) -> std::io::Result<()> {
        drop(self.stage.take());
        self.remove(id, display_parent)
    }

    pub(crate) fn remove(&self, id: &str, _display_parent: &Path) -> std::io::Result<()> {
        std::fs::remove_dir_all(self.recovery.join(id))
    }
}
'''
for rel in ("crates/font-store/src/language_packs/removal.rs", "crates/font-store/src/language_packs.rs"):
    p = os.path.join(top, rel)
    if os.path.isfile(p) and "pub(crate) struct PackRemovalRoot {" in open(p, encoding="utf-8").read():
        s = open(p, encoding="utf-8").read()
        if 'target_os = "korin"' not in s:
            s = s.replace("pub(crate) struct PackRemovalRoot {", '#[cfg(not(target_os = "korin"))]\npub(crate) struct PackRemovalRoot {', 1)
            s = s.replace("impl PackRemovalRoot {", '#[cfg(not(target_os = "korin"))]\nimpl PackRemovalRoot {', 1)
            open(p, "w", encoding="utf-8", newline="\n").write(s + KORIN_REMOVAL)
            with open(os.path.join(top, ".korin-changed"), "a", encoding="utf-8") as f:
                f.write(rel + "\n")
        edit("crates/font-store/Cargo.toml", [
            ('cap-primitives = "4.0.3"\n', ""),
            ("[dev-dependencies]", "# Korin : PackRemovalRoot par chemins (tools/craft/korin_libs.py)\n"
             "[target.'cfg(not(any(target_arch = \"wasm32\", target_os = \"korin\")))'.dependencies]\n"
             "cap-primitives = \"4.0.3\"\n\n[dev-dependencies]"),
        ])
        break

print("korin_libs : %s" % os.path.basename(top))
