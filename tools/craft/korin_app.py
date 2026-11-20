"""korin_app : les retouches d'une app Craft pour Korin (docs/CRAFT.md), sur la copie de travail que
tools/craft/build.sh remet à la révision épinglée à chaque lancement (git checkout) ; chaque retouche vérifie
qu'elle a trouvé son texte, pour qu'une nouvelle révision de l'app ne passe pas sans qu'on le voie.

    python3 -I tools/craft/korin_app.py <dossier de l'app> <racine du dépôt Korin>

- eframe : le [patch.crates-io] de l'app pointe sur la fourche de storytold (ou rien) ; il prend
  src/lib/eframe-korin (docs/EGUI.md), qui a aussi des modules wgpu et egui_wgpu factices (le réglage du GPU au
  démarrage, sans effet) ;
- (les branches X11 et Wayland : tools/craft/korin_cfg.py, lancé après) ;
- WordCraft : sans `gpu-startup` (wgpu) et sans le réglage de wgpu de main.rs.
"""
import os
import re
import sys

app, root = sys.argv[1], sys.argv[2]


def edit(rel, pairs, count_min=1):
    p = os.path.join(app, rel)
    s = open(p, encoding="utf-8").read()
    for a, b in pairs:
        n = s.count(a) if isinstance(a, str) else len(re.findall(a, s))
        if n < count_min:
            sys.exit("korin_app : %s : texte absent : %r" % (rel, a if isinstance(a, str) else a.pattern))
        s = s.replace(a, b) if isinstance(a, str) else a.sub(b, s)
    open(p, "w", encoding="utf-8", newline="\n").write(s)


# eframe -> l'eframe de Korin : la ligne de la fourche dans [patch.crates-io], ou une ligne de plus (l'app prend
# l'eframe de crates.io)
EFRAME = 'eframe = { path = "%s/src/lib/eframe-korin" }' % root
p = os.path.join(app, "Cargo.toml")
s = open(p, encoding="utf-8").read()
fork = re.compile(r'^eframe = \{ git = "https://github\.com/storytold/egui\.git".*$', re.M)
if fork.search(s):
    s = fork.sub(EFRAME, s)
elif "\n[patch.crates-io]\n" in s:
    s = s.replace("\n[patch.crates-io]\n", "\n[patch.crates-io]\n" + EFRAME + "\n", 1)
else:
    s += "\n# Korin (tools/craft/korin_app.py)\n[patch.crates-io]\n" + EFRAME + "\n"
open(p, "w", encoding="utf-8", newline="\n").write(s)

name = os.path.basename(os.path.normpath(app))
if name == "wordcraft":
    edit("apps/wordcraft/Cargo.toml", [
        ('    "gpu-startup",\n', ""),
        ('    "platform-clipboard-system",\n', ""),
        ("[target.'cfg(windows)'.dependencies]",
         "# Korin : pas de wgpu ; le presse-papiers de Korin (src/korin.rs) (tools/craft/korin_app.py)\n"
         "[target.'cfg(not(target_os = \"korin\"))'.dependencies]\n"
         "craft = { workspace = true, features = [\"gpu-startup\", \"platform-clipboard-system\"] }\n\n"
         "[target.'cfg(target_os = \"korin\")'.dependencies]\n"
         "egui-korin = { path = \"%s/src/lib/egui-korin\" }\n\n"
         "[target.'cfg(windows)'.dependencies]" % root),
    ])
    edit("apps/wordcraft/src/main.rs", [
        ("    if let eframe::egui_wgpu::WgpuSetup::CreateNew(setup) = &mut options.wgpu_options.wgpu_setup {",
         '    #[cfg(not(target_os = "korin"))]\n'
         "    if let eframe::egui_wgpu::WgpuSetup::CreateNew(setup) = &mut options.wgpu_options.wgpu_setup {"),
        ("use craft::platform::clipboard::{Clipboard, ClipboardImage, SystemClipboard};",
         "#[cfg(not(target_os = \"korin\"))]\n"
         "use craft::platform::clipboard::SystemClipboard;\n"
         "use craft::platform::clipboard::{Clipboard, ClipboardImage};"),
        ("Clipboard::new(SystemClipboard::new().decoder(decode_png))", "Clipboard::new(system_clipboard())"),
        ("mod region;\n", "mod region;\n#[cfg(target_os = \"korin\")]\nmod korin;\n"),
    ])
    with open(os.path.join(app, "apps/wordcraft/src/main.rs"), "a", encoding="utf-8", newline="\n") as f:
        f.write("\n/// Le presse-papiers du système (Korin : tools/craft/app/korin.rs).\n"
                "#[cfg(not(target_os = \"korin\"))]\n"
                "fn system_clipboard() -> SystemClipboard {\n    SystemClipboard::new().decoder(decode_png)\n}\n\n"
                "#[cfg(target_os = \"korin\")]\n"
                "fn system_clipboard() -> korin::KorinClipboard {\n    let _ = decode_png;\n    korin::KorinClipboard::default()\n}\n")
    import shutil
    shutil.copy(os.path.join(root, "tools", "craft", "app", "korin.rs"), os.path.join(app, "apps/wordcraft/src/korin.rs"))
print("korin_app : %s retouchée" % name)
