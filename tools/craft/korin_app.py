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
    import tomllib
    lock = tomllib.load(open(os.path.join(app, "Cargo.lock"), "rb"))
    ev = [q["version"] for q in lock["package"] if q["name"] == "eframe" and q["version"].startswith("0.36.")]
    subprocess.run([sys.executable, "-I", os.path.join(root, "tools", "craft", "egui036.py"), root, e036, reg] + ev[:1],
                   check=True)
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
if name == "craft-anything":
    # nix ne connaît pas Korin ; il n'y sert qu'à O_NONBLOCK (killpg est pour Linux et macOS) : libc sur Korin
    edit("crates/platform/Cargo.toml", [
        ("[target.'cfg(unix)'.dependencies]\nnix = ",
         "# Korin : libc, sans nix (tools/craft/korin_app.py)\n[target.'cfg(target_os = \"korin\")'.dependencies]\nlibc = \"0.2\"\n\n"
         "[target.'cfg(all(unix, not(target_os = \"korin\")))'.dependencies]\nnix = "),
    ])
    # portable-pty (nix 0.28, serial2) : les terminaux de la capture, pour Linux, macOS et Windows ; Korin capture par
    # des tubes (le chemin « piped », comme FreeBSD)
    edit("crates/platform/Cargo.toml", [
        ('portable-pty = "0.9"\n', ""),
        ("# Korin : libc, sans nix (tools/craft/korin_app.py)\n",
         "# Korin : libc, sans nix ni portable-pty (tools/craft/korin_app.py)\n"
         "[target.'cfg(not(target_os = \"korin\"))'.dependencies]\nportable-pty = \"0.9\"\n\n"),
    ])
    edit("crates/platform/src/fs.rs", [
        ("        options.custom_flags(nix::libc::O_NONBLOCK);",
         "        #[cfg(target_os = \"korin\")]\n        options.custom_flags(libc::O_NONBLOCK);\n"
         "        #[cfg(not(target_os = \"korin\"))]\n        options.custom_flags(nix::libc::O_NONBLOCK);"),
    ])
# ---- les menus dans la barre de menus de Korin (docs/CRAFT.md) : l'app prend sur Korin son chemin « menu natif » de
# macOS (muda, que Korin a : tools/craft/crate/muda-korin.rs), son menu dans la fenêtre est caché ; ce qui reste propre
# à macOS (AppKit, objc2, événements Apple) le reste
MK = '#[cfg(any(target_os = "macos", target_os = "korin"))]'
MAC = '#[cfg(target_os = "macos")]'


def korin_dep(rel, lines):
    """une section de dépendances pour Korin, à la fin du Cargo.toml"""
    pth = os.path.join(app, rel)
    with open(pth, "a", encoding="utf-8", newline="\n") as f:
        f.write("\n# Korin : la barre de menus (tools/craft/korin_app.py)\n[target.'cfg(target_os = \"korin\")'.dependencies]\n%s\n" % lines)


if name == "gridcraft":
    korin_dep("apps/gridcraft/Cargo.toml", 'muda = { version = "0.21", default-features = false }')
    edit("apps/gridcraft/src/main.rs", [
        (MAC + "\nmod native_menu;", MK + "\nmod native_menu;"),
        (MAC + " Option<native_menu::NativeMenu>", MK + " Option<native_menu::NativeMenu>"),
        ("        #[cfg(target_os = \"macos\")]\n        {\n            if self.2.is_none()",
         "        " + MK + "\n        {\n            if self.2.is_none()"),
        ("            apple_events::poll(&self.3, &mut self.0, ctx);\n        }",
         "            #[cfg(target_os = \"macos\")]\n            apple_events::poll(&self.3, &mut self.0, ctx);\n        }"),
        ("                    windows,\n                    #[cfg(target_os = \"macos\")]\n                    None,",
         "                    windows,\n                    " + MK + "\n                    None,"),
    ])
if name == "designcraft":
    korin_dep("apps/designcraft/Cargo.toml", 'muda = { version = "0.21", default-features = false }')
    edit("apps/designcraft/src/main.rs", [
        (MAC + "\nmod native_menu;", MK + "\nmod native_menu;"),
        ("    " + MAC + "\n    menu: Option<native_menu::NativeMenu>,", "    " + MK + "\n    menu: Option<native_menu::NativeMenu>,"),
        ("            #[cfg(target_os = \"macos\")]\n            {\n                if self.menu.is_none()",
         "            " + MK + "\n            {\n                if self.menu.is_none()"),
        ("                self.startup.opening(apple_events::poll(&self.apple_events, &mut self.app, ui.ctx()));\n            }",
         "                #[cfg(target_os = \"macos\")]\n                self.startup.opening(apple_events::poll(&self.apple_events, &mut self.app, ui.ctx()));\n            }"),
        ("                    " + MAC + "\n                    menu: None,", "                    " + MK + "\n                    menu: None,"),
    ])
if name == "cadcraft":
    korin_dep("apps/cadcraft/Cargo.toml", 'muda = { version = "0.21", default-features = false }')
    edit("apps/cadcraft/src/main.rs", [
        (MAC + "\nmod native_menu;", MK + "\nmod native_menu;"),
        ("    " + MAC + "\n    native_menu: Option<native_menu::NativeMenu>,", "    " + MK + "\n    native_menu: Option<native_menu::NativeMenu>,"),
        ("    " + MAC + "\n    menus_in_window: bool,", "    " + MK + "\n    menus_in_window: bool,"),
        ("            " + MAC + "\n            {\n                if self.native_menu.is_none() && !self.menus_in_window {",
         "            " + MK + "\n            {\n                if self.native_menu.is_none() && !self.menus_in_window {"),
        ("            " + MAC + "\n            app.set_native_menus(vars.no_native_menu != Switch::On);",
         "            " + MK + "\n            app.set_native_menus(vars.no_native_menu != Switch::On);"),
        ("                " + MAC + "\n                native_menu: None,\n                " + MAC + "\n                menus_in_window:",
         "                " + MK + "\n                native_menu: None,\n                " + MK + "\n                menus_in_window:"),
    ])
    edit("crates/ui-egui/src/lib.rs", [
        ("            in_window_menu: !cfg!(target_os = \"macos\"),", "            in_window_menu: !cfg!(any(target_os = \"macos\", target_os = \"korin\")),"),
        ("            native_menus: cfg!(target_os = \"macos\"),", "            native_menus: cfg!(any(target_os = \"macos\", target_os = \"korin\")),"),
    ])
if name == "soundcraft":
    korin_dep("apps/soundcraft/Cargo.toml", 'muda = { version = "0.21.2", default-features = false }')
    edit("apps/soundcraft/src/main.rs", [
        (MAC + "\nmod native_menu;", MK + "\nmod native_menu;"),
        (MAC + " Option<native_menu::NativeMenu>", MK + " Option<native_menu::NativeMenu>"),
        ("        " + MAC + "\n        if let Some(menu) = &mut self.3 {", "        " + MK + "\n        if let Some(menu) = &mut self.3 {"),
        ("            " + MAC + "\n            let menu = match native_menu::NativeMenu::new(", "            " + MK + "\n            let menu = match native_menu::NativeMenu::new("),
        ("                env,\n                " + MAC + "\n                menu,", "                env,\n                " + MK + "\n                menu,"),
    ])
    # Masquer, Masquer les autres : AppKit, sans objet sur Korin
    edit("apps/soundcraft/src/native_menu.rs", [
        ("use objc2::MainThreadMarker;\nuse objc2_app_kit::NSApplication;\n",
         MAC + "\nuse objc2::MainThreadMarker;\n" + MAC + "\nuse objc2_app_kit::NSApplication;\n"),
        ("        app_menu.append(&MenuItem::with_id(\"hide\", \"Hide SoundCraft\", true, None))?;\n"
         "        app_menu.append(&MenuItem::with_id(\"hide_others\", \"Hide Others\", true, None))?;\n",
         "        " + MAC + "\n        app_menu.append(&MenuItem::with_id(\"hide\", \"Hide SoundCraft\", true, None))?;\n"
         "        " + MAC + "\n        app_menu.append(&MenuItem::with_id(\"hide_others\", \"Hide Others\", true, None))?;\n"),
        ("            if (event.id == \"hide\" || event.id == \"hide_others\")\n                && let Some(mtm) = MainThreadMarker::new()\n            {\n"
         "                let ns = NSApplication::sharedApplication(mtm);\n                if event.id == \"hide\" { ns.hide(None) } else { ns.hideOtherApplications(None) }\n"
         "            } else if",
         "            if hide_app(&event.id) {\n            } else if"),
    ])
    with open(os.path.join(app, "apps/soundcraft/src/native_menu.rs"), "a", encoding="utf-8", newline="\n") as f:
        f.write("\n/// Masquer, Masquer les autres (macOS ; tools/craft/korin_app.py) -> true s'il s'agissait de l'une d'elles.\n"
                + MAC + "\nfn hide_app(id: &muda::MenuId) -> bool {\n"
                "    if *id != \"hide\" && *id != \"hide_others\" {\n        return false;\n    }\n"
                "    if let Some(mtm) = MainThreadMarker::new() {\n        let ns = NSApplication::sharedApplication(mtm);\n"
                "        if *id == \"hide\" { ns.hide(None) } else { ns.hideOtherApplications(None) }\n    }\n    true\n}\n\n"
                "#[cfg(target_os = \"korin\")]\nfn hide_app(_id: &muda::MenuId) -> bool {\n    false\n}\n")
if name == "lightcraft":
    korin_dep("apps/lightcraft/Cargo.toml", 'muda = { workspace = true }')
    edit("apps/lightcraft/src/main.rs", [
        (MAC + "\nmod native_menu;", MK + "\nmod native_menu;"),
        (MAC + " Option<native_menu::NativeMenu>", MK + " Option<native_menu::NativeMenu>"),
        ("        " + MAC + "\n        if let Some(m) = self.3.as_mut()", "        " + MK + "\n        if let Some(m) = self.3.as_mut()"),
        ("                " + MAC + "\n                let menu =\n", "                " + MK + "\n                let menu =\n"),
        ("                    window_textures,\n                    " + MAC + "\n                    menu,", "                    window_textures,\n                    " + MK + "\n                    menu,"),
    ])
if name == "filmcraft":
    korin_dep("apps/filmcraft/Cargo.toml", 'muda = { version = "0.21", default-features = false }')
    edit("apps/filmcraft/src/main.rs", [
        (MAC + "\nmod native_menu;", MK + "\nmod native_menu;"),
        ("        " + MAC + "\n        {\n            let (rx, update, text_menu) = native_menu::install(&app, cc.egui_ctx.clone());\n"
         "            app.hooks.pick_color = Some(native_color::picker(text_menu));\n",
         "        " + MK + "\n        {\n            let (rx, update, text_menu) = native_menu::install(&app, cc.egui_ctx.clone());\n"
         "            // (le sélecteur de couleur de macOS ; tools/craft/korin_app.py)\n"
         "            " + MAC + "\n            {\n                app.hooks.pick_color = Some(native_color::picker(text_menu));\n            }\n"
         "            #[cfg(target_os = \"korin\")]\n            let _ = text_menu;\n"),
    ])
if name == "effectcraft":
    korin_dep("apps/effectcraft/Cargo.toml", 'muda = { version = "0.21", default-features = false }\negui-korin = { path = "%s/src/lib/egui-korin" }' % root)
    edit("apps/effectcraft/src/main.rs", [
        (MAC + "\nmod native_menu;", MK + "\nmod native_menu;"),
        ("                app.hooks.clipboard_text = Some(Box::new(native_menu::clipboard_text));\n            }\n",
         "                app.hooks.clipboard_text = Some(Box::new(native_menu::clipboard_text));\n            }\n"
         "            // Korin : le presse-papiers de Korin pour Coller depuis la barre (tools/craft/korin_app.py)\n"
         "            #[cfg(target_os = \"korin\")]\n            {\n                app.hooks.clipboard_text = Some(Box::new(egui_korin::clipboard_get));\n            }\n"),
        ("                " + MAC + "\n                menu: native_menu::NativeBar::new(&cc.egui_ctx),\n                " + MAC + "\n                menu_sync: Default::default(),",
         "                " + MK + "\n                menu: native_menu::NativeBar::new(&cc.egui_ctx),\n                " + MK + "\n                menu_sync: Default::default(),"),
        ("    " + MAC + "\n    menu: native_menu::NativeBar,\n    " + MAC + "\n    menu_sync:",
         "    " + MK + "\n    menu: native_menu::NativeBar,\n    " + MK + "\n    menu_sync:"),
        ("        " + MAC + "\n        self.menu_sync.before(", "        " + MK + "\n        self.menu_sync.before("),
        ("        " + MAC + "\n        self.menu_sync.after(", "        " + MK + "\n        self.menu_sync.after("),
    ])
    edit("apps/effectcraft/src/native_menu.rs", [
        ("pub fn app_action(id: &str) -> bool {", MAC + "\npub fn app_action(id: &str) -> bool {"),
        ("pub fn clipboard_text() -> Option<String> {", MAC + "\npub fn clipboard_text() -> Option<String> {"),
    ])
if name == "craft-anything":
    korin_dep("crates/ui-egui/Cargo.toml", 'muda = { version = "0.21.2", default-features = false }')
    edit("crates/ui-egui/src/lib.rs", [
        (MAC + "\npub mod menu;", MK + "\npub mod menu;"),
        ("    " + MAC + "\n    pub menu_bar: Option<menu::MenuBar>,", "    " + MK + "\n    pub menu_bar: Option<menu::MenuBar>,"),
        ("            " + MAC + "\n            menu_bar: None,", "            " + MK + "\n            menu_bar: None,"),
        ("        " + MAC + "\n        {\n            self.menu_bar = menu::MenuBar::install(ctx);\n        }\n        #[cfg(not(target_os = \"macos\"))]\n        let _ = ctx;",
         "        " + MK + "\n        {\n            self.menu_bar = menu::MenuBar::install(ctx);\n        }\n        #[cfg(not(any(target_os = \"macos\", target_os = \"korin\")))]\n        let _ = ctx;"),
        ("        " + MAC + "\n        menu::update(self, &ctx);", "        " + MK + "\n        menu::update(self, &ctx);"),
    ])
if name == "vectorcraft":
    korin_dep("apps/vectorcraft/Cargo.toml", 'muda = { version = "=0.21.2", default-features = false }')
    edit("apps/vectorcraft/src/main.rs", [
        (MAC + "\nmod mac_menu;", MK + "\nmod mac_menu;"),
        ("                #[cfg(not(target_os = \"macos\"))]\n                {\n                    let _ = in_window_menus;\n                    single_instance::set_ui(&cc.egui_ctx);\n                }",
         "                // Korin : la barre de menus de Korin (tools/craft/korin_app.py)\n"
         "                #[cfg(target_os = \"korin\")]\n                if !in_window_menus {\n                    app.services.native_menu = mac_menu::install(&cc.egui_ctx, &app);\n                }\n"
         "                #[cfg(not(target_os = \"macos\"))]\n                {\n                    let _ = in_window_menus;\n                    single_instance::set_ui(&cc.egui_ctx);\n                }"),
    ])
    edit("apps/vectorcraft/src/mac_menu.rs", [
        ("use block2::RcBlock;\n", MAC + "\nuse block2::RcBlock;\n"),
        ("use objc2::MainThreadMarker;\nuse objc2_app_kit::{NSApplication, NSEventModifierFlags, NSEventType, NSMenuDidBeginTrackingNotification, NSMenuDidEndTrackingNotification};\nuse objc2_foundation::{NSNotification, NSNotificationCenter};\n",
         MAC + "\nuse objc2::MainThreadMarker;\n" + MAC + "\nuse objc2_app_kit::{NSApplication, NSEventModifierFlags, NSEventType, NSMenuDidBeginTrackingNotification, NSMenuDidEndTrackingNotification};\n"
         + MAC + "\nuse objc2_foundation::{NSNotification, NSNotificationCenter};\n"),
        ("fn observe_tracking(ctx: &egui::Context) {", "/// Korin : pas de suivi des menus (la barre de Korin est un autre programme ; tools/craft/korin_app.py)\n"
         "#[cfg(target_os = \"korin\")]\nfn observe_tracking(_ctx: &egui::Context) {}\n\n" + MAC + "\nfn observe_tracking(ctx: &egui::Context) {"),
        ("fn cancel_tracking() {\n    if let Some(mtm)", "fn cancel_tracking() {\n    " + MAC + "\n    if let Some(mtm)"),
        ("            let event = MainThreadMarker::new().and_then(|mtm| NSApplication::sharedApplication(mtm).currentEvent());\n"
         "            let key = event.as_ref().is_some_and(|ev| ev.r#type() == NSEventType::KeyDown);\n"
         "            let flags = event.as_ref().map_or(NSEventModifierFlags::empty(), |ev| ev.modifierFlags());\n"
         "            let mods = [NSEventModifierFlags::Command, NSEventModifierFlags::Control, NSEventModifierFlags::Option, NSEventModifierFlags::Shift]\n"
         "                .map(|m| flags.contains(m));\n",
         "            " + MAC + "\n            let (key, mods) = {\n"
         "            let event = MainThreadMarker::new().and_then(|mtm| NSApplication::sharedApplication(mtm).currentEvent());\n"
         "            let key = event.as_ref().is_some_and(|ev| ev.r#type() == NSEventType::KeyDown);\n"
         "            let flags = event.as_ref().map_or(NSEventModifierFlags::empty(), |ev| ev.modifierFlags());\n"
         "            let mods = [NSEventModifierFlags::Command, NSEventModifierFlags::Control, NSEventModifierFlags::Option, NSEventModifierFlags::Shift]\n"
         "                .map(|m| flags.contains(m));\n            (key, mods)\n            };\n"
         "            // Korin : un choix dans la barre (ses raccourcis y sont déjà pris) est un clic\n"
         "            #[cfg(target_os = \"korin\")]\n            let (key, mods) = (false, [false; 4]);\n"),
        ("                if let Some(mtm) = MainThreadMarker::new() {\n                    let app = NSApplication::sharedApplication(mtm);\n"
         "                    if command == native_menu::HIDE { app.hide(None) } else { app.hideOtherApplications(None) }\n                }\n                continue;",
         "                " + MAC + "\n                if let Some(mtm) = MainThreadMarker::new() {\n                    let app = NSApplication::sharedApplication(mtm);\n"
         "                    if command == native_menu::HIDE { app.hide(None) } else { app.hideOtherApplications(None) }\n                }\n"
         "                let _ = command;\n                continue;"),
    ])
# ---- la barre de titre sur Korin (docs/CRAFT.md) : pas de boutons de fenêtre à soi (Elegant dessine les siens sur le
# contenu, en haut à droite : FRAME_CAP_W 102 px + marge), les contrôles de droite à KORIN_RIGHT px du bord, le titre à
# gauche, à côté de l'icône et des premiers contrôles
KR = 'if cfg!(target_os = "korin") { 112.0 } else { %s }'
if name == "deckcraft":
    edit("crates/ui-egui/src/ribbon/title_bar.rs", [
        ("pos2(rect.max.x - 10.0, rect.max.y)", "pos2(rect.max.x - %s, rect.max.y)" % (KR % "10.0")),
        ("        crate::qat::show(app, &mut child);\n", "        crate::qat::show(app, &mut child);\n        let qat_end = child.min_rect().right();\n"),
        ("        ui.painter().text(rect.center(), Align2::CENTER_CENTER, title, theme::font(13.0), t.text);",
         "        // Korin : le titre à gauche, après la barre d'accès rapide (tools/craft/korin_app.py)\n"
         "        if cfg!(target_os = \"korin\") {\n"
         "            ui.painter().text(pos2(qat_end + 14.0, rect.center().y), Align2::LEFT_CENTER, title, theme::font(13.0), t.text);\n"
         "        } else {\n"
         "            ui.painter().text(rect.center(), Align2::CENTER_CENTER, title, theme::font(13.0), t.text);\n"
         "        }"),
    ])
if name == "gridcraft":
    edit("apps/gridcraft/src/main.rs", [
        ("app.window_controls = cfg!(not(any(target_os = \"macos\", target_os = \"windows\"))) && std::env::var_os",
         "app.window_controls = cfg!(not(any(target_os = \"macos\", target_os = \"windows\", target_os = \"korin\"))) && std::env::var_os"),
    ])
    edit("crates/ui-egui/src/ribbon/title_bar.rs", [
        ("                    ui.add_space(3.0 * CONTROL_W);\n                }\n                ui.add_space(8.0);",
         "                    ui.add_space(3.0 * CONTROL_W);\n                }\n                ui.add_space(%s);" % (KR % "8.0")),
        ("            let max_w = title_max_width(rect.center().x, left_end, right.inner);\n            paint_title(ui, rect.center(), &title, dirty, max_w, t.text);",
         "            if cfg!(target_os = \"korin\") {\n"
         "                // Korin : le titre à gauche, après les contrôles de gauche (tools/craft/korin_app.py)\n"
         "                let max_w = (right.inner - left_end - 2.0 * TITLE_GAP).max(0.0);\n"
         "                let (galley, dot) = title_galleys(ui.painter(), &title, dirty, max_w, t.text);\n"
         "                let top = rect.center().y - galley.size().y / 2.0;\n"
         "                let x = left_end + TITLE_GAP;\n"
         "                let galley_w = galley.size().x;\n"
         "                ui.painter().galley(pos2(x, top), galley, t.text);\n"
         "                if let Some(dot) = dot {\n"
         "                    ui.painter().galley(pos2(x + galley_w, top), dot, t.text);\n"
         "                }\n"
         "            } else {\n"
         "                let max_w = title_max_width(rect.center().x, left_end, right.inner);\n"
         "                paint_title(ui, rect.center(), &title, dirty, max_w, t.text);\n"
         "            }"),
    ])
if name == "pdfcraft":
    edit("crates/ui-egui/src/chrome.rs", [("        (false, _) => (8, 10),", "        (false, _) => (8, if cfg!(target_os = \"korin\") { 112 } else { 10 }),")])
if name == "designcraft":
    edit("crates/ui-egui/src/chrome/app_bar.rs", [
        ("            right: if app.custom_titlebar { 0 } else { 10 },",
         "            right: if app.custom_titlebar { 0 } else if cfg!(target_os = \"korin\") { 112 } else { 10 },"),
        ("            let cx = full.center().x;\n            if cx - w / 2.0 > menus_end + 12.0 && cx + w / 2.0 < tools_start - 12.0 {\n"
         "                ui.painter().galley(egui::pos2(cx - w / 2.0, full.center().y - galley.size().y / 2.0), galley, t.text);",
         "            let cx = full.center().x;\n"
         "            // Korin : le titre à gauche, après l'icône et les menus (tools/craft/korin_app.py)\n"
         "            let (tx, fits) = if cfg!(target_os = \"korin\") {\n"
         "                (menus_end + 12.0, menus_end + 12.0 + w < tools_start - 12.0)\n"
         "            } else {\n"
         "                (cx - w / 2.0, cx - w / 2.0 > menus_end + 12.0 && cx + w / 2.0 < tools_start - 12.0)\n"
         "            };\n"
         "            if fits {\n"
         "                ui.painter().galley(egui::pos2(tx, full.center().y - galley.size().y / 2.0), galley, t.text);"),
    ])
if name == "lightcraft":
    edit("crates/ui-egui/src/panels/topbar.rs", [
        ("inner_margin(egui::Margin { left, right: 12, top: 0, bottom: 0 })",
         "inner_margin(egui::Margin { left, right: if cfg!(target_os = \"korin\") { 112 } else { 12 }, top: 0, bottom: 0 })"),
    ])
if name == "figcraft":
    edit("crates/ui-egui/src/tabs.rs", [
        ("let buttons_w = if app.ui.title_bar == TitleBar::Custom { window_buttons(app, ui, rect) } else { 0.0 };",
         "let buttons_w = if app.ui.title_bar == TitleBar::Custom { window_buttons(app, ui, rect) } else if cfg!(target_os = \"korin\") { 104.0 } else { 0.0 };"),
    ])
if name == "vectorcraft":
    edit("crates/ui-egui/src/chrome.rs", [
        ("    let right_margin = if captions_right > 0.0 { 0 } else { 14 };",
         "    let right_margin = if captions_right > 0.0 { 0 } else if cfg!(target_os = \"korin\") { 112 } else { 14 };"),
        # Korin : la marque de l'app reste (pas de barre de titre du système)
        ("    app.desktop_frame && !app.custom_titlebar && !cfg!(any(target_os = \"macos\", target_arch = \"wasm32\"))",
         "    app.desktop_frame && !app.custom_titlebar && !cfg!(any(target_os = \"macos\", target_arch = \"wasm32\", target_os = \"korin\"))"),
        ("                let title_rect = egui::Rect::from_center_size(full.center(), title.size());",
         "                // Korin : le titre à gauche, après les menus (tools/craft/korin_app.py)\n"
         "                let title_rect = if cfg!(target_os = \"korin\") {\n"
         "                    egui::Rect::from_min_size(egui::pos2(menus_end + 10.0, full.center().y - title.size().y / 2.0), title.size())\n"
         "                } else {\n"
         "                    egui::Rect::from_center_size(full.center(), title.size())\n"
         "                };"),
    ])
if name == "texturecraft":
    edit("crates/ui-egui/src/titlebar.rs", [
        ("pos2(rect.right() - ends(right.len()) - PAD, rect.bottom())",
         "pos2(rect.right() - ends(right.len()) - PAD - if cfg!(target_os = \"korin\") { 104.0 } else { 0.0 }, rect.bottom())"),
    ])
if name == "filmcraft":
    edit("crates/ui-egui/src/header.rs", [
        ("    let mut rx = rect.max.x - 14.0;", "    let mut rx = rect.max.x - %s;" % (KR.replace("112.0", "114.0") % "14.0")),
        ("    let qx = rect.max.x - 14.0 - 4.0 * 38.0;", "    let qx = rect.max.x - (%s) - 4.0 * 38.0;" % (KR.replace("112.0", "114.0") % "14.0")),
        ("            let center = if rect.center().x - half >= left && rect.center().x + half <= right { rect.center().x } else { (left + right) / 2.0 };",
         "            // Korin : le titre à gauche, après les menus (tools/craft/korin_app.py)\n"
         "            let center = if cfg!(target_os = \"korin\") {\n"
         "                left + half\n"
         "            } else if rect.center().x - half >= left && rect.center().x + half <= right {\n"
         "                rect.center().x\n"
         "            } else {\n"
         "                (left + right) / 2.0\n"
         "            };"),
    ])
if name == "effectcraft":
    edit("crates/ui-egui/src/header.rs", [
        ("    let rx = community_buttons(app, ui, p, rect.max.x - 10.0, cy);",
         "    let rx = community_buttons(app, ui, p, rect.max.x - %s, cy);" % (KR.replace("112.0", "110.0") % "10.0")),
    ])
if name == "cadcraft":
    edit("crates/ui-egui/src/chrome.rs", [
        ("            let available = (row.right() - 12.0 - x).max(0.0);",
         "            let available = (row.right() - (%s) - x).max(0.0);" % (KR % "12.0")),
    ])
if name == "soundcraft":
    # le menu dans la barre de Korin : la barre d'outils monte en haut ; sa fin défile au-delà des boutons d'Elegant.
    # Sans menu natif, le titre de la barre de menus (à droite) s'en écarte aussi
    edit("crates/ui-egui/src/toolbar.rs", [
        ("                output_meter(app, ui);\n            });",
         "                output_meter(app, ui);\n                ui.add_space(if cfg!(target_os = \"korin\") { 104.0 } else { 0.0 });\n            });"),
    ])
    edit("crates/ui-egui/src/menus.rs", [
        ("                ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {\n                    ui.label(egui::RichText::new(title).color(t.text_dim));",
         "                ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {\n"
         "                    ui.add_space(if cfg!(target_os = \"korin\") { 104.0 } else { 0.0 });\n"
         "                    ui.label(egui::RichText::new(title).color(t.text_dim));"),
    ])
if name == "materialcraft":
    # le nom de l'app, à droite de la barre d'outils (sous la barre de menus) : à l'écart des boutons d'Elegant
    edit("crates/ui-egui/src/toolbar.rs", [
        ("            ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {\n                ui.label(RichText::new(\"MaterialCraft\")",
         "            ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {\n"
         "                ui.add_space(if cfg!(target_os = \"korin\") { 104.0 } else { 0.0 });\n"
         "                ui.label(RichText::new(\"MaterialCraft\")"),
    ])
if name == "craft-anything":
    # Korin : le cadre, le bord, le redimensionnement et les boutons de fenêtre sont ceux d'Elegant
    edit("crates/ui-egui/src/titlebar.rs", [
        ("pub fn edge_at(ctx: &egui::Context) -> Option<ResizeDirection> {\n    if NATIVE_FRAME || maximized(ctx) {",
         "pub fn edge_at(ctx: &egui::Context) -> Option<ResizeDirection> {\n    if NATIVE_FRAME || cfg!(target_os = \"korin\") || maximized(ctx) {"),
        ("pub fn window_edge(ctx: &egui::Context) {\n    if NATIVE_FRAME || maximized(ctx) {",
         "pub fn window_edge(ctx: &egui::Context) {\n    if NATIVE_FRAME || cfg!(target_os = \"korin\") || maximized(ctx) {"),
        ("            ui.spacing_mut().item_spacing.x = 0.0;\n            if !NATIVE_FRAME {",
         "            ui.spacing_mut().item_spacing.x = 0.0;\n            if !NATIVE_FRAME && !cfg!(target_os = \"korin\") {"),
        ("            ui.add_space(if NATIVE_FRAME { 12.0 } else { 4.0 } * s);",
         "            ui.add_space(if NATIVE_FRAME { 12.0 } else if cfg!(target_os = \"korin\") { 104.0 } else { 4.0 } * s);"),
    ])
print("korin_app : %s retouchée" % name)
