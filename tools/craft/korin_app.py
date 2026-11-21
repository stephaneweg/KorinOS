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
# l'eframe de crates.io). Une app encore sur egui 0.36 prend la copie 0.36 de l'hôte (tools/craft/egui036.py), à côté
# des dépôts des apps
EFRAME = 'eframe = { path = "%s/src/lib/eframe-korin" }' % root
p = os.path.join(app, "Cargo.toml")
s = open(p, encoding="utf-8").read()
if re.search(r'^eframe = \{ version = "0\.36"', s, re.M):
    import glob
    import subprocess
    e036 = os.path.join(os.path.dirname(os.path.normpath(app)), "egui036")
    reg = (glob.glob(os.path.join(os.environ.get("CARGO_HOME", os.path.expanduser("~/.cargo")), "registry", "src",
                                  "index.crates.io-*")) or [""])[0]
    subprocess.run([sys.executable, "-I", os.path.join(root, "tools", "craft", "egui036.py"), root, e036, reg], check=True)
    EFRAME = 'eframe = { path = "%s/eframe-korin" }' % e036
fork = re.compile(r'^eframe = \{ git = "https://github\.com/storytold/egui\.git".*$', re.M)
if fork.search(s):
    s = fork.sub(EFRAME, s)
elif "\n[patch.crates-io]\n" in s:
    s = s.replace("\n[patch.crates-io]\n", "\n[patch.crates-io]\n" + EFRAME + "\n", 1)
else:
    s += "\n# Korin (tools/craft/korin_app.py)\n[patch.crates-io]\n" + EFRAME + "\n"
open(p, "w", encoding="utf-8", newline="\n").write(s)

name = os.path.basename(os.path.normpath(app))
# winit pris directement (la fenêtre native : ses écrans, sa poignée) : le winit de Korin (src/lib/winit-korin), sans
# fenêtre, à la place de la fourche de storytold ; eframe-korin le prend aussi (winit_window() rend None)
if name in ("vectorcraft", "effectcraft"):
    p = os.path.join(app, "Cargo.toml")
    s = open(p, encoding="utf-8").read()
    wfork = re.compile(r'^winit = \{ git = "https://github\.com/storytold/winit\.git".*$', re.M)
    if not wfork.search(s):
        sys.exit("korin_app : %s : la ligne winit du [patch.crates-io] est absente" % name)
    s = wfork.sub('winit = { path = "%s/src/lib/winit-korin" }' % root, s)
    open(p, "w", encoding="utf-8", newline="\n").write(s)
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
if name == "pdfcraft":
    # rustix ne sert qu'à tuer le groupe de processus de LibreOffice ; child.kill(), juste après, suffit sur Korin
    edit("crates/create/Cargo.toml", [("[target.'cfg(unix)'.dependencies]\n# Already in the graph (pdfcraft-platform, ui-egui); stops",
                                       "[target.'cfg(all(unix, not(target_os = \"korin\")))'.dependencies]\n# Already in the graph (pdfcraft-platform, ui-egui); stops")])
    edit("crates/create/src/office.rs", [("        #[cfg(unix)]\n        {\n            use rustix::process::",
                                          "        #[cfg(all(unix, not(target_os = \"korin\")))]\n        {\n            use rustix::process::")])
    # thread-priority ne connaît pas Korin : la priorité basse du fil d'optimisation n'est pas demandée
    edit("crates/optimize/Cargo.toml", [('thread-priority = "3.1"\n', "")])
    with open(os.path.join(app, "crates/optimize/Cargo.toml"), "a", encoding="utf-8", newline="\n") as f:
        f.write("\n# Korin (tools/craft/korin_app.py)\n[target.'cfg(not(target_os = \"korin\"))'.dependencies]\n"
                "thread-priority = \"3.1\"\n")
    edit("crates/optimize/src/pool.rs", [("    let _ = thread_priority::set_current_thread_priority(",
                                          "    #[cfg(not(target_os = \"korin\"))]\n"
                                          "    let _ = thread_priority::set_current_thread_priority(")])
if name == "pdfcraft":
    # la signature : aws-lc-rs (aws-lc, C et assembleur) ne se construit pas pour Korin ; Korin prend la branche du
    # navigateur (wasm32) : vérification complète, signature P-256 et P-384, mais ni signature ni création de clé RSA
    # (craft-pkcs7, sa copie dans craft-libs : tools/craft/korin_libs.py)
    NO_AWS = 'any(target_arch = "wasm32", target_os = "korin")'
    AWS = 'aws-lc-rs = { version = "1.18", default-features = false, features = ["aws-lc-sys", "prebuilt-nasm"] }\n'
    edit("crates/sign/Cargo.toml", [
        ("[target.'cfg(not(target_arch = \"wasm32\"))'.dependencies]\n" + AWS + "getrandom = \"0.4\"\n",
         "[target.'cfg(not(target_arch = \"wasm32\"))'.dependencies]\ngetrandom = \"0.4\"\n\n"
         "# Korin : sans aws-lc (tools/craft/korin_app.py)\n"
         "[target.'cfg(not(%s))'.dependencies]\n" % NO_AWS + AWS)])
    edit("crates/sign/src/keys.rs", [('#[cfg(not(target_arch = "wasm32"))]', '#[cfg(not(%s))]' % NO_AWS),
                                     ('#[cfg(target_arch = "wasm32")]', '#[cfg(%s)]' % NO_AWS)])
    edit("crates/sign/src/lib.rs", [("pub fn init_crypto() {\n    aws_lc_rs::init();\n}",
                                     "pub fn init_crypto() {\n    #[cfg(not(target_os = \"korin\"))]\n    aws_lc_rs::init();\n}")])
if name == "figcraft":
    # services.rs : le dossier des réglages (XDG_CONFIG_HOME) et la langue (LANGUAGE, LANG) sont du POSIX que Korin a :
    # leur branche Unix reste à Korin (écrite autrement, korin_cfg.py ne la réécrit pas) ; le mouvement réduit
    # (gsettings) prend le repli « ni Unix ni Windows » (non)
    keep = '#[cfg(all(unix, not(any(target_os = "macos"))))]'
    edit("apps/figcraft/src/services.rs", [
        ('    #[cfg(all(unix, not(target_os = "macos")))]\n    let base = std::env::var_os("XDG_CONFIG_HOME")',
         '    // Korin aussi (tools/craft/korin_app.py)\n    ' + keep + '\n    let base = std::env::var_os("XDG_CONFIG_HOME")'),
        ('    // gettext\'s `LANGUAGE` priority list ("fr:de"), then the locale ("fr_FR.UTF-8").\n'
         '    #[cfg(all(unix, not(target_os = "macos")))]',
         '    // gettext\'s `LANGUAGE` priority list ("fr:de"), then the locale ("fr_FR.UTF-8"). Korin aussi.\n    ' + keep),
        ('    #[cfg(not(any(windows, unix)))]\n    {\n        false',
         '    #[cfg(any(target_os = "korin", not(any(windows, unix))))]\n    {\n        false'),
    ])
if name == "soundcraft":
    # winit (pour X11 sur les BSD : « Unix ni macOS ni Linux », ce qu'est Korin) ne sert pas sur Korin
    edit("apps/soundcraft/Cargo.toml", [
        ("[target.'cfg(all(unix, not(any(target_os = \"macos\", target_os = \"linux\"))))'.dependencies]\nwinit",
         "[target.'cfg(all(unix, not(any(target_os = \"macos\", target_os = \"linux\", target_os = \"korin\"))))'.dependencies]\nwinit"),
    ])
    # l'hôte VST3 : pas de fenêtre pour l'éditeur d'un greffon sur Korin (« unsupported », comme les autres systèmes)
    edit("crates/vst3-host/src/ffi.rs", [
        ('#[cfg(not(any(target_os = "macos", windows, unix)))]\n#[path = "ffi/window/other.rs"]',
         '#[cfg(any(target_os = "korin", not(any(target_os = "macos", windows, unix))))]\n#[path = "ffi/window/other.rs"]'),
    ])
print("korin_app : %s retouchée" % name)
