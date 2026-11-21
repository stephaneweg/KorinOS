// Le projet ouvert : son dossier, son genre (Cargo, Make, app de Korin, C#, FreeBASIC, Python), ses commandes de
// construction et d'exécution selon le système, l'arbre des fichiers, les modèles de nouveaux projets, les réglages.

use serde::{Deserialize, Serialize};
use std::collections::{HashMap, HashSet};
use std::path::{Path, PathBuf};
use std::time::{Duration, Instant};

pub const KORIN: bool = cfg!(target_os = "korin");

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum ProjectKind {
    Cargo,
    KorinApp,
    CSharp,
    Make,
    FreeBasic,
    Python,
    Folder,
}

impl ProjectKind {
    pub fn label(self) -> &'static str {
        match self {
            ProjectKind::Cargo => "Rust (Cargo)",
            ProjectKind::KorinApp => "App Korin",
            ProjectKind::CSharp => "C# (.csproj)",
            ProjectKind::Make => "Makefile",
            ProjectKind::FreeBasic => "FreeBASIC",
            ProjectKind::Python => "Python",
            ProjectKind::Folder => "Dossier",
        }
    }

    pub fn detect(root: &Path) -> ProjectKind {
        let has = |n: &str| root.join(n).is_file();
        let any_ext = |e: &str| {
            std::fs::read_dir(root).map(|rd| rd.flatten().any(|d| d.path().extension().is_some_and(|x| x.eq_ignore_ascii_case(e)))).unwrap_or(false)
        };
        if has("Cargo.toml") {
            ProjectKind::Cargo
        } else if any_ext("csproj") {
            ProjectKind::CSharp
        } else if has("app.txt") {
            ProjectKind::KorinApp
        } else if has("Makefile") || has("makefile") {
            ProjectKind::Make
        } else if any_ext("bas") {
            ProjectKind::FreeBasic
        } else if has("main.py") || any_ext("py") {
            ProjectKind::Python
        } else {
            ProjectKind::Folder
        }
    }
}

/// Une commande à lancer (programme, arguments, dossier).
#[derive(Clone, Debug)]
pub struct Cmd {
    pub program: String,
    pub args: Vec<String>,
    pub cwd: PathBuf,
    pub label: String,
}

impl Cmd {
    fn new(label: &str, cwd: &Path, argv: &[&str]) -> Cmd {
        Cmd { program: argv[0].to_string(), args: argv[1..].iter().map(|s| s.to_string()).collect(), cwd: cwd.to_path_buf(), label: label.to_string() }
    }
    pub fn display(&self) -> String {
        let mut s = self.program.clone();
        for a in &self.args {
            s.push(' ');
            if a.contains(' ') { s.push_str(&format!("\"{a}\"")) } else { s.push_str(a) }
        }
        s
    }
    /// Une ligne de commande pour le shell du système.
    pub fn shell(label: &str, cwd: &Path, line: &str) -> Cmd {
        if cfg!(windows) {
            Cmd::new(label, cwd, &["cmd", "/C", line])
        } else if KORIN {
            Cmd::new(label, cwd, &["bash", "-c", line])
        } else {
            Cmd::new(label, cwd, &["sh", "-c", line])
        }
    }
}

/// Le dossier du projet d'un fichier : le premier parent qui a un Cargo.toml, un .csproj, un app.txt, un Makefile,
/// un .git (sinon le dossier du fichier).
pub fn find_project_root(file: &Path) -> PathBuf {
    let start = file.parent().unwrap_or(file);
    let mut d = Some(start);
    while let Some(p) = d {
        let has = |n: &str| p.join(n).exists();
        let csproj = std::fs::read_dir(p).map(|rd| rd.flatten().any(|e| e.path().extension().is_some_and(|x| x == "csproj"))).unwrap_or(false);
        if has("Cargo.toml") || has("app.txt") || has("Makefile") || has(".git") || csproj {
            return p.to_path_buf();
        }
        d = p.parent();
    }
    start.to_path_buf()
}

pub fn project_name(root: &Path) -> String {
    root.file_name().map(|s| s.to_string_lossy().to_string()).unwrap_or_else(|| "projet".into())
}

fn files_with_ext(root: &Path, ext: &str) -> Vec<String> {
    let mut v: Vec<String> = std::fs::read_dir(root)
        .map(|rd| {
            rd.flatten()
                .filter(|d| d.path().extension().is_some_and(|x| x.eq_ignore_ascii_case(ext)))
                .map(|d| d.file_name().to_string_lossy().to_string())
                .collect()
        })
        .unwrap_or_default();
    v.sort();
    v
}

/// Ce qui construit le projet (None : rien à construire).
pub fn build_cmd(kind: ProjectKind, root: &Path) -> Result<Option<Cmd>, String> {
    let name = project_name(root);
    let r = root.to_string_lossy().to_string();
    Ok(match kind {
        ProjectKind::Cargo => Some(Cmd::new("cargo build", root, &["cargo", "build"])),
        ProjectKind::Make => Some(Cmd::new("make", root, &["make"])),
        ProjectKind::KorinApp => {
            if KORIN {
                Some(Cmd::new("pibuild", root, &["pibuild", "app", &r, &name]))
            } else {
                return Err("une app de Korin se construit dans Korin (pibuild) : ouvrez ce projet dans KorinStudio sous Korin".into());
            }
        }
        ProjectKind::CSharp => {
            if KORIN {
                Some(Cmd::new("pibuild", root, &["pibuild", &r, &name]))
            } else {
                Some(Cmd::new("dotnet build", root, &["dotnet", "build"]))
            }
        }
        ProjectKind::FreeBasic => {
            let mut argv = vec!["fbc".to_string(), "-O".into(), "2".into()];
            let bas = files_with_ext(root, "bas");
            if bas.iter().any(|b| b == "main.bas") {
                argv.push("main.bas".into());
                argv.extend(bas.into_iter().filter(|b| b != "main.bas"));
            } else {
                argv.extend(bas);
            }
            argv.push("-x".into());
            argv.push(format!("build/{name}{}", if cfg!(windows) { ".exe" } else { "" }));
            let _ = std::fs::create_dir_all(root.join("build"));
            let refs: Vec<&str> = argv.iter().map(|s| s.as_str()).collect();
            Some(Cmd::new("fbc", root, &refs))
        }
        ProjectKind::Python => {
            let main = if root.join("main.py").is_file() { "main.py".to_string() } else { files_with_ext(root, "py").into_iter().next().unwrap_or_else(|| "main.py".into()) };
            Some(Cmd::new("py_compile", root, &[python(), "-m", "py_compile", &main]))
        }
        ProjectKind::Folder => None,
    })
}

fn python() -> &'static str {
    if cfg!(windows) { "python" } else { "python3" }
}

/// Ce qui lance le projet (après sa construction).
pub fn run_cmd(kind: ProjectKind, root: &Path, extra: &[String]) -> Result<Cmd, String> {
    let name = project_name(root);
    let mut c = match kind {
        ProjectKind::Cargo => Cmd::new("cargo run", root, &["cargo", "run", "-q"]),
        ProjectKind::Make => Cmd::new("make run", root, &["make", "run"]),
        ProjectKind::KorinApp => {
            if !KORIN {
                return Err("une app de Korin ne tourne que dans Korin".into());
            }
            Cmd::new(&name, root, &[&format!("/apps/{name}.app/main")])
        }
        ProjectKind::CSharp => {
            if KORIN {
                let app = format!("/apps/{name}.app/main");
                if Path::new(&app).is_file() { Cmd::new(&name, root, &[&app]) } else { Cmd::new(&name, root, &[&name]) }
            } else {
                Cmd::new("dotnet run", root, &["dotnet", "run"])
            }
        }
        ProjectKind::FreeBasic => {
            let exe = root.join("build").join(format!("{name}{}", if cfg!(windows) { ".exe" } else { "" }));
            Cmd::new(&name, root, &[&exe.to_string_lossy()])
        }
        ProjectKind::Python => {
            let main = if root.join("main.py").is_file() { "main.py".to_string() } else { files_with_ext(root, "py").into_iter().next().unwrap_or_else(|| "main.py".into()) };
            Cmd::new(&main, root, &[python(), "-u", &main])
        }
        ProjectKind::Folder => return Err("ce dossier n'a pas de projet reconnu (Cargo.toml, Makefile, app.txt, .csproj, .bas, .py)".into()),
    };
    if kind == ProjectKind::Cargo && !extra.is_empty() {
        c.args.push("--".into());
    }
    c.args.extend(extra.iter().cloned());
    Ok(c)
}

pub fn clean_cmd(kind: ProjectKind, root: &Path) -> Option<Cmd> {
    match kind {
        ProjectKind::Cargo => Some(Cmd::new("cargo clean", root, &["cargo", "clean"])),
        ProjectKind::Make => Some(Cmd::new("make clean", root, &["make", "clean"])),
        ProjectKind::CSharp if !KORIN => Some(Cmd::new("dotnet clean", root, &["dotnet", "clean"])),
        ProjectKind::FreeBasic => Some(Cmd::shell("nettoyer", root, if cfg!(windows) { "rmdir /S /Q build" } else { "rm -rf build" })),
        _ => None,
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Les modèles de projets

pub struct Template {
    pub id: &'static str,
    pub label: &'static str,
    pub description: &'static str,
    pub korin_only: bool,
}

pub const TEMPLATES: &[Template] = &[
    Template { id: "rust", label: "Rust", description: "Un programme Rust (Cargo) : cargo build, cargo run.", korin_only: false },
    Template { id: "c", label: "C", description: "Un programme C en ligne de commande, avec un Makefile (gcc).", korin_only: false },
    Template { id: "cpp", label: "C++", description: "Un programme C++ en ligne de commande, avec un Makefile (g++).", korin_only: false },
    Template { id: "korin-app-c", label: "App Korin (C)", description: "Une app graphique de Korin en C sur UIKit (fenêtre, bouton, message), construite par pibuild.", korin_only: true },
    Template { id: "csharp", label: "C#", description: "Un programme C# (.csproj) : csbuild dans Korin, dotnet ailleurs.", korin_only: false },
    Template { id: "freebasic", label: "FreeBASIC", description: "Un programme FreeBASIC (fbc), aussi pour le BASIC à la QBasic (-lang qb).", korin_only: false },
    Template { id: "asm-gnu", label: "Assembleur x86-64", description: "Un programme en assembleur x86-64 (GNU as, syntaxe Intel) lié par gcc, avec un Makefile.", korin_only: false },
    Template { id: "python", label: "Python", description: "Un script Python 3.", korin_only: false },
];

/// Les fichiers d'un modèle (chemin relatif, contenu) pour un projet nommé `name`.
pub fn template_files(id: &str, name: &str) -> Option<Vec<(String, String)>> {
    let crate_name: String = name.chars().map(|c| if c.is_ascii_alphanumeric() { c.to_ascii_lowercase() } else { '_' }).collect();
    let f = |p: &str, c: String| (p.to_string(), c);
    Some(match id {
        "rust" => vec![
            f("Cargo.toml", format!("[package]\nname = \"{crate_name}\"\nversion = \"0.1.0\"\nedition = \"2024\"\n\n[dependencies]\n")),
            f("src/main.rs", "fn main() {\n    println!(\"Bonjour depuis Rust !\");\n}\n".into()),
            f(".gitignore", "/target\n".into()),
        ],
        "c" => vec![
            f("Makefile", format!("CC ?= gcc\nCFLAGS ?= -O2 -Wall -Wextra\nOUT = build/{name}\n\n$(OUT): main.c\n\tmkdir -p build\n\t$(CC) $(CFLAGS) main.c -o $(OUT)\n\nrun: $(OUT)\n\t./$(OUT)\n\nclean:\n\trm -rf build\n\n.PHONY: run clean\n")),
            f("main.c", "#include <stdio.h>\n\nint main(void)\n{\n\tprintf(\"Bonjour depuis C !\\n\");\n\treturn 0;\n}\n".into()),
            f(".gitignore", "/build\n".into()),
        ],
        "cpp" => vec![
            f("Makefile", format!("CXX ?= g++\nCXXFLAGS ?= -O2 -Wall -Wextra -std=c++20\nOUT = build/{name}\n\n$(OUT): main.cpp\n\tmkdir -p build\n\t$(CXX) $(CXXFLAGS) main.cpp -o $(OUT)\n\nrun: $(OUT)\n\t./$(OUT)\n\nclean:\n\trm -rf build\n\n.PHONY: run clean\n")),
            f("main.cpp", "#include <iostream>\n\nint main()\n{\n\tstd::cout << \"Bonjour depuis C++ !\" << std::endl;\n\treturn 0;\n}\n".into()),
            f(".gitignore", "/build\n".into()),
        ],
        "korin-app-c" => vec![
            f("app.txt", format!("# Manifeste de l'app (clés : /apps/help.app/api/app_fr.md)\nname        = {name}\ncategory    = Développement\ndescription = Une app de Korin faite avec KorinStudio.\nkeywords    = exemple\nhelp        = Le bouton Bonjour affiche un message.\n")),
            f("main.c", include_str!("../templates/korin-app.c").into()),
        ],
        "csharp" => vec![
            f(&format!("{name}.csproj"), "<Project Sdk=\"Microsoft.NET.Sdk\">\n\n  <PropertyGroup>\n    <OutputType>Exe</OutputType>\n    <TargetFramework>net8.0</TargetFramework>\n    <Nullable>enable</Nullable>\n    <ImplicitUsings>enable</ImplicitUsings>\n  </PropertyGroup>\n\n</Project>\n".into()),
            f("Program.cs", "Console.WriteLine(\"Bonjour depuis C# !\");\n".into()),
            f(".gitignore", "bin/\nobj/\n".into()),
        ],
        "freebasic" => vec![
            f("main.bas", "' main.bas -- un programme FreeBASIC (fbc main.bas)\nDim nom As String = \"FreeBASIC\"\nPrint \"Bonjour depuis \"; nom; \" !\"\n".into()),
            f(".gitignore", "/build\n".into()),
        ],
        "asm-gnu" => vec![
            f("Makefile", format!("CC ?= gcc\nOUT = build/{name}\n\n$(OUT): main.s\n\tmkdir -p build\n\t$(CC) -no-pie main.s -o $(OUT)\n\nrun: $(OUT)\n\t./$(OUT)\n\nclean:\n\trm -rf build\n\n.PHONY: run clean\n")),
            f("main.s", "# main.s -- x86-64, syntaxe Intel, lié par gcc (la libc donne puts)\n\t.intel_syntax noprefix\n\t.text\n\t.globl main\nmain:\n\tsub rsp, 8\n\tlea rdi, [rip + message]\n\tcall puts\n\txor eax, eax\n\tadd rsp, 8\n\tret\n\n\t.section .rodata\nmessage:\n\t.asciz \"Bonjour depuis l'assembleur !\"\n\t.section .note.GNU-stack,\"\",@progbits\n".into()),
            f(".gitignore", "/build\n".into()),
        ],
        "python" => vec![f("main.py", "def main():\n    print(\"Bonjour depuis Python !\")\n\n\nif __name__ == \"__main__\":\n    main()\n".into())],
        _ => return None,
    })
}

/// Crée le projet `name` dans `parent` depuis le modèle `id` ; rend son dossier.
pub fn create_project(id: &str, name: &str, parent: &Path) -> Result<PathBuf, String> {
    let name = name.trim();
    if name.is_empty() || name.contains(['/', '\\', ':', '*', '?', '"', '<', '>', '|']) {
        return Err(format!("nom de projet invalide : « {name} »"));
    }
    let files = template_files(id, name).ok_or_else(|| format!("modèle inconnu : {id}"))?;
    let root = parent.join(name);
    if root.exists() && std::fs::read_dir(&root).map(|mut d| d.next().is_some()).unwrap_or(true) {
        return Err(format!("le dossier {} existe déjà et n'est pas vide", root.display()));
    }
    for (rel, content) in files {
        let p = root.join(&rel);
        if let Some(d) = p.parent() {
            std::fs::create_dir_all(d).map_err(|e| format!("{} : {e}", d.display()))?;
        }
        std::fs::write(&p, content).map_err(|e| format!("{} : {e}", p.display()))?;
    }
    Ok(root)
}

/// Le premier fichier à ouvrir dans un projet neuf.
pub fn main_file(root: &Path) -> Option<PathBuf> {
    ["src/main.rs", "main.c", "main.cpp", "Program.cs", "main.bas", "main.s", "main.py"].iter().map(|f| root.join(f)).find(|p| p.is_file())
}

// ---------------------------------------------------------------------------------------------------------------
// L'arbre des fichiers (lu à la demande, relu toutes les deux secondes pour les dossiers ouverts)

#[derive(Clone)]
pub struct Entry {
    pub name: String,
    pub path: PathBuf,
    pub is_dir: bool,
}

#[derive(Default)]
pub struct Tree {
    cache: HashMap<PathBuf, (Instant, Vec<Entry>)>,
    pub expanded: HashSet<PathBuf>,
}

pub fn hidden(name: &str) -> bool {
    name == ".git"
}

pub fn dimmed(name: &str) -> bool {
    matches!(name, "target" | "build" | "bin" | "obj" | "node_modules" | "__pycache__") || name.starts_with('.')
}

impl Tree {
    pub fn children(&mut self, dir: &Path) -> Vec<Entry> {
        let fresh = self.cache.get(dir).is_some_and(|(t, _)| t.elapsed() < Duration::from_secs(2));
        if !fresh {
            let mut v: Vec<Entry> = std::fs::read_dir(dir)
                .map(|rd| {
                    rd.flatten()
                        .filter_map(|d| {
                            let name = d.file_name().to_string_lossy().to_string();
                            if hidden(&name) {
                                return None;
                            }
                            let path = d.path();
                            let is_dir = path.is_dir();
                            Some(Entry { name, path, is_dir })
                        })
                        .collect()
                })
                .unwrap_or_default();
            v.sort_by(|a, b| b.is_dir.cmp(&a.is_dir).then_with(|| a.name.to_lowercase().cmp(&b.name.to_lowercase())));
            self.cache.insert(dir.to_path_buf(), (Instant::now(), v));
        }
        self.cache[dir].1.clone()
    }
    pub fn invalidate(&mut self) {
        self.cache.clear();
    }
    pub fn reveal(&mut self, root: &Path, file: &Path) {
        let mut d = file.parent();
        while let Some(p) = d {
            if !p.starts_with(root) {
                break;
            }
            self.expanded.insert(p.to_path_buf());
            d = p.parent();
        }
    }
}

/// Les fichiers du projet (pour Ctrl+P et la recherche), sans les dossiers de construction.
pub fn walk_files(root: &Path, limit: usize) -> Vec<PathBuf> {
    let mut out = Vec::new();
    let mut stack = vec![root.to_path_buf()];
    while let Some(d) = stack.pop() {
        let Ok(rd) = std::fs::read_dir(&d) else { continue };
        let mut entries: Vec<_> = rd.flatten().collect();
        entries.sort_by_key(|e| e.file_name());
        for e in entries {
            let name = e.file_name().to_string_lossy().to_string();
            if hidden(&name) || matches!(name.as_str(), "target" | "node_modules" | "obj" | "__pycache__" | ".vs") {
                continue;
            }
            let p = e.path();
            if p.is_dir() {
                stack.push(p);
            } else {
                out.push(p);
                if out.len() >= limit {
                    return out;
                }
            }
        }
    }
    out
}

pub fn rel(root: Option<&Path>, p: &Path) -> String {
    match root.and_then(|r| p.strip_prefix(r).ok()) {
        Some(r) if !r.as_os_str().is_empty() => r.to_string_lossy().replace('\\', "/"),
        _ => p.to_string_lossy().to_string(),
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Les réglages et les dossiers

#[derive(Clone, Copy, PartialEq, Eq, Debug, Serialize, Deserialize, Default)]
pub enum ThemeChoice {
    #[default]
    System,
    Dark,
    Light,
}

#[derive(Clone, Serialize, Deserialize)]
#[serde(default)]
pub struct Settings {
    pub recent: Vec<PathBuf>,
    pub last_project: Option<PathBuf>,
    pub theme: ThemeChoice,
    pub code_size: f32,
    pub tab_size: usize,
    pub autosave: bool,
    pub word_wrap: bool,
    pub agent: crate::agent::AgentKind,
    pub agent_modes: HashMap<String, String>,
    pub copilot_open: bool,
    pub sidebar_width: f32,
    pub copilot_width: f32,
}

impl Default for Settings {
    fn default() -> Self {
        Settings {
            recent: Vec::new(),
            last_project: None,
            theme: ThemeChoice::System,
            code_size: 13.5,
            tab_size: 4,
            autosave: false,
            word_wrap: false,
            agent: crate::agent::AgentKind::ClaudeCode,
            agent_modes: HashMap::new(),
            copilot_open: true,
            sidebar_width: 260.0,
            copilot_width: 380.0,
        }
    }
}

pub fn home_dir() -> PathBuf {
    std::env::var_os("HOME").or_else(|| std::env::var_os("USERPROFILE")).map(PathBuf::from).unwrap_or_else(|| PathBuf::from(if KORIN { "/home" } else { "." }))
}

pub fn config_dir() -> PathBuf {
    if cfg!(windows) {
        std::env::var_os("APPDATA").map(PathBuf::from).unwrap_or_else(home_dir).join("KorinStudio")
    } else {
        home_dir().join(".config").join("korinstudio")
    }
}

pub fn projects_dir() -> PathBuf {
    home_dir().join(if KORIN { "projets" } else { "Projets" })
}

impl Settings {
    pub fn load() -> Settings {
        std::fs::read(config_dir().join("settings.json")).ok().and_then(|b| serde_json::from_slice(&b).ok()).unwrap_or_default()
    }
    pub fn save(&self) {
        let d = config_dir();
        let _ = std::fs::create_dir_all(&d);
        if let Ok(s) = serde_json::to_vec_pretty(self) {
            let tmp = d.join("settings.json.tmp");
            if std::fs::write(&tmp, s).is_ok() {
                let _ = std::fs::rename(&tmp, d.join("settings.json"));
            }
        }
    }
    pub fn add_recent(&mut self, p: &Path) {
        self.recent.retain(|r| r != p);
        self.recent.insert(0, p.to_path_buf());
        self.recent.truncate(12);
        self.last_project = Some(p.to_path_buf());
    }
}
