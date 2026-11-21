// Les tâches : construire, lancer, les commandes du terminal. Un process par tâche, sa sortie lue par deux fils
// (stdout, stderr) ligne à ligne ; les messages d'erreur des compilateurs deviennent des diagnostics cliquables.

use crate::workspace::Cmd;
use regex::Regex;
use std::io::{BufRead, BufReader, Read, Write};
use std::path::{Path, PathBuf};
use std::process::{Child, ChildStdin, Command, Stdio};
use std::sync::{Arc, Mutex, OnceLock};
use std::time::Instant;

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Severity {
    Error,
    Warning,
    Info,
}

#[derive(Clone, Debug)]
pub struct Diagnostic {
    pub path: PathBuf,
    pub line: usize,
    pub column: usize,
    pub severity: Severity,
    pub message: String,
    pub source: String,
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum TaskKind {
    Build,
    Run,
    Shell,
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum LineKind {
    Out,
    Err,
    Info,
}

pub struct Output {
    pub lines: Vec<(LineKind, String)>,
    pub exit: Option<Option<i32>>, // Some(None) : fini sans code (tué)
    pub partial: [String; 2],
}

pub struct Task {
    pub id: u64,
    pub kind: TaskKind,
    pub title: String,
    pub cmd: Cmd,
    pub started: Instant,
    pub ended: Option<Instant>,
    pub out: Arc<Mutex<Output>>,
    child: Option<Arc<Mutex<Child>>>,
    stdin: Option<ChildStdin>,
    pub diagnostics_taken: bool,
}

/// Retire les séquences d'échappement ANSI (couleurs de cargo, gcc…).
pub fn strip_ansi(s: &str) -> String {
    let mut out = String::with_capacity(s.len());
    let mut it = s.chars().peekable();
    while let Some(c) = it.next() {
        if c == '\x1b' {
            if it.peek() == Some(&'[') {
                it.next();
                while let Some(&n) = it.peek() {
                    it.next();
                    if ('@'..='~').contains(&n) {
                        break;
                    }
                }
            } else if it.peek() == Some(&']') {
                // OSC : jusqu'à BEL ou ESC \
                while let Some(n) = it.next() {
                    if n == '\x07' || n == '\x1b' {
                        break;
                    }
                }
            }
            continue;
        }
        if c == '\r' {
            continue;
        }
        out.push(c);
    }
    out
}

fn reader<R: Read + Send + 'static>(r: R, out: Arc<Mutex<Output>>, kind: LineKind, ctx: egui::Context) {
    std::thread::spawn(move || {
        let mut br = BufReader::new(r);
        let mut buf = Vec::new();
        loop {
            buf.clear();
            match br.read_until(b'\n', &mut buf) {
                Ok(0) | Err(_) => break,
                Ok(_) => {
                    let line = strip_ansi(String::from_utf8_lossy(&buf).trim_end_matches('\n'));
                    let mut o = out.lock().unwrap();
                    if o.lines.len() > 50_000 {
                        o.lines.drain(..10_000);
                    }
                    o.lines.push((kind, line));
                    drop(o);
                    ctx.request_repaint();
                }
            }
        }
    });
}

/// Le chemin d'un programme : tel quel s'il a un dossier, sinon cherché dans le PATH (et, dans Korin, les dossiers
/// des outils de développement) ; un script `#!` est lancé par son interprète (Korin n'exécute pas toujours un script).
pub fn resolve_program(program: &str) -> (String, Vec<String>) {
    let path = if program.contains('/') || program.contains('\\') {
        Some(PathBuf::from(program))
    } else {
        which(program)
    };
    if let Some(p) = &path {
        if crate::workspace::KORIN {
            if let Ok(mut f) = std::fs::File::open(p) {
                let mut head = [0u8; 128];
                let n = f.read(&mut head).unwrap_or(0);
                if n > 2 && &head[..2] == b"#!" {
                    let line = String::from_utf8_lossy(&head[2..n]);
                    let line = line.lines().next().unwrap_or("").trim().to_string();
                    let mut parts = line.split_whitespace();
                    if let Some(interp) = parts.next() {
                        let mut pre: Vec<String> = parts.map(|s| s.to_string()).collect();
                        let interp = if interp.ends_with("/env") && !pre.is_empty() { pre.remove(0) } else { interp.to_string() };
                        pre.push(p.to_string_lossy().to_string());
                        return (interp, pre);
                    }
                }
            }
        }
        return (p.to_string_lossy().to_string(), vec![]);
    }
    (program.to_string(), vec![])
}

pub fn which(program: &str) -> Option<PathBuf> {
    let mut dirs: Vec<PathBuf> = std::env::var_os("PATH").map(|p| std::env::split_paths(&p).collect()).unwrap_or_default();
    if crate::workspace::KORIN {
        for d in ["/bin", "/devtools/gcc/bin", "/devtools/rust/bin", "/devtools/dotnet/bin", "/devtools/python/bin", "/python/bin"] {
            dirs.push(PathBuf::from(d));
        }
    }
    let exts: &[&str] = if cfg!(windows) { &["", ".exe", ".cmd", ".bat"] } else { &[""] };
    for d in dirs {
        for e in exts {
            let p = d.join(format!("{program}{e}"));
            if p.is_file() {
                return Some(p);
            }
        }
    }
    None
}

impl Task {
    pub fn start(id: u64, kind: TaskKind, title: String, cmd: Cmd, ctx: &egui::Context) -> Task {
        let out = Arc::new(Mutex::new(Output { lines: Vec::new(), exit: None, partial: Default::default() }));
        out.lock().unwrap().lines.push((LineKind::Info, format!("> {}   ({})", cmd.display(), cmd.cwd.display())));
        let (prog, mut pre) = resolve_program(&cmd.program);
        pre.extend(cmd.args.iter().cloned());
        let mut c = Command::new(&prog);
        c.args(&pre).current_dir(&cmd.cwd).stdin(Stdio::piped()).stdout(Stdio::piped()).stderr(Stdio::piped());
        c.env("CARGO_TERM_COLOR", "never").env("TERM", "dumb").env("KORINSTUDIO", "1");
        let mut task = Task { id, kind, title, cmd, started: Instant::now(), ended: None, out: out.clone(), child: None, stdin: None, diagnostics_taken: false };
        match c.spawn() {
            Ok(mut child) => {
                if let Some(o) = child.stdout.take() {
                    reader(o, out.clone(), LineKind::Out, ctx.clone());
                }
                if let Some(e) = child.stderr.take() {
                    reader(e, out.clone(), LineKind::Err, ctx.clone());
                }
                task.stdin = child.stdin.take();
                let child = Arc::new(Mutex::new(child));
                task.child = Some(child.clone());
                let ctx = ctx.clone();
                // l'attente de la fin, sans tenir le verrou longtemps
                std::thread::spawn(move || loop {
                    let st = child.lock().unwrap().try_wait();
                    match st {
                        Ok(Some(s)) => {
                            // laisser les lecteurs finir
                            std::thread::sleep(std::time::Duration::from_millis(80));
                            out.lock().unwrap().exit = Some(s.code());
                            ctx.request_repaint();
                            break;
                        }
                        Ok(None) => std::thread::sleep(std::time::Duration::from_millis(50)),
                        Err(_) => {
                            out.lock().unwrap().exit = Some(None);
                            ctx.request_repaint();
                            break;
                        }
                    }
                });
            }
            Err(e) => {
                let mut o = out.lock().unwrap();
                o.lines.push((LineKind::Err, format!("impossible de lancer {prog} : {e}")));
                o.exit = Some(Some(127));
            }
        }
        task
    }

    pub fn running(&self) -> bool {
        self.out.lock().unwrap().exit.is_none()
    }

    pub fn exit(&self) -> Option<Option<i32>> {
        self.out.lock().unwrap().exit
    }

    pub fn success(&self) -> bool {
        matches!(self.exit(), Some(Some(0)))
    }

    pub fn kill(&mut self) {
        if let Some(c) = &self.child {
            let _ = c.lock().unwrap().kill();
        }
    }

    pub fn write_stdin(&mut self, s: &str) {
        if let Some(i) = &mut self.stdin {
            let _ = i.write_all(s.as_bytes());
            let _ = i.flush();
        }
        self.out.lock().unwrap().lines.push((LineKind::Info, s.trim_end().to_string()));
    }

    pub fn text(&self) -> String {
        let o = self.out.lock().unwrap();
        let mut s = String::new();
        for (_, l) in &o.lines {
            s.push_str(l);
            s.push('\n');
        }
        s
    }

    pub fn tail(&self, max_lines: usize) -> String {
        let o = self.out.lock().unwrap();
        let start = o.lines.len().saturating_sub(max_lines);
        let mut s = String::new();
        if start > 0 {
            s.push_str(&format!("[… {start} lignes avant]\n"));
        }
        for (_, l) in &o.lines[start..] {
            s.push_str(l);
            s.push('\n');
        }
        s
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Les diagnostics

struct Rx {
    gcc: Regex,
    rust_head: Regex,
    rust_loc: Regex,
    fbc: Regex,
    csc: Regex,
    py_file: Regex,
    fasm_loc: Regex,
}

fn rx() -> &'static Rx {
    static R: OnceLock<Rx> = OnceLock::new();
    R.get_or_init(|| Rx {
        // gcc, g++, as, ld, make : fichier:ligne[:colonne]: niveau: message
        gcc: Regex::new(r"^([^:\s][^:]*?):(\d+)(?::(\d+))?:\s*(fatal error|error|Error|warning|Warning|note|Note|Fatal error):\s*(.*)$").unwrap(),
        rust_head: Regex::new(r"^(error|warning)(?:\[(E\d+)\])?:\s*(.*)$").unwrap(),
        rust_loc: Regex::new(r"^\s*-->\s*(.+?):(\d+):(\d+)\s*$").unwrap(),
        // fbc : fichier(ligne) error 41: message
        fbc: Regex::new(r"^(.+?)\((\d+)\)\s+(error|warning)\s*\d*\s*(?:\([^)]*\))?:\s*(.*)$").unwrap(),
        // csc, csbuild, dotnet : fichier(ligne,colonne): error CS0103: message
        csc: Regex::new(r"^\s*(.+?)\((\d+),(\d+)\):\s*(error|warning)\s+(\w+):\s*(.*?)(?:\s+\[.*\])?$").unwrap(),
        py_file: Regex::new(r#"^\s*File "(.+?)", line (\d+)"#).unwrap(),
        // fasm : fichier [ligne]:
        fasm_loc: Regex::new(r"^(.+?) \[(\d+)\]:\s*$").unwrap(),
    })
}

fn sev(s: &str) -> Severity {
    let s = s.to_ascii_lowercase();
    if s.contains("error") {
        Severity::Error
    } else if s.contains("warning") {
        Severity::Warning
    } else {
        Severity::Info
    }
}

/// Les diagnostics d'une sortie de compilation ; les chemins relatifs le sont au dossier `cwd`.
pub fn parse_diagnostics(text: &str, cwd: &Path) -> Vec<Diagnostic> {
    let r = rx();
    let abs = |p: &str| -> PathBuf {
        let pb = PathBuf::from(p.trim());
        if pb.is_absolute() { pb } else { cwd.join(pb) }
    };
    let mut out: Vec<Diagnostic> = Vec::new();
    let lines: Vec<&str> = text.lines().collect();
    let mut i = 0;
    while i < lines.len() {
        let l = lines[i];
        if let Some(m) = r.rust_head.captures(l) {
            // rustc : le lieu est sur une des lignes suivantes (« --> »)
            let msg = m[3].to_string();
            if msg.starts_with("aborting due to") || msg.contains("generated") && msg.contains("warning") || msg.starts_with("could not compile") {
                i += 1;
                continue;
            }
            for j in i + 1..(i + 4).min(lines.len()) {
                if let Some(loc) = r.rust_loc.captures(lines[j]) {
                    let code = m.get(2).map(|c| format!("[{}] ", c.as_str())).unwrap_or_default();
                    out.push(Diagnostic {
                        path: abs(&loc[1]),
                        line: loc[2].parse().unwrap_or(1),
                        column: loc[3].parse().unwrap_or(1),
                        severity: sev(&m[1]),
                        message: format!("{code}{msg}"),
                        source: "rustc".into(),
                    });
                    break;
                }
            }
            i += 1;
            continue;
        }
        if let Some(m) = r.csc.captures(l) {
            out.push(Diagnostic { path: abs(&m[1]), line: m[2].parse().unwrap_or(1), column: m[3].parse().unwrap_or(1), severity: sev(&m[4]), message: format!("{} {}", &m[5], &m[6]), source: "csc".into() });
        } else if let Some(m) = r.fbc.captures(l) {
            out.push(Diagnostic { path: abs(&m[1]), line: m[2].parse().unwrap_or(1), column: 1, severity: sev(&m[3]), message: m[4].to_string(), source: "fbc".into() });
        } else if let Some(m) = r.gcc.captures(l) {
            let p = &m[1];
            if !p.starts_with("make") && !p.starts_with("In file included") && !p.starts_with("collect2") {
                let s = sev(&m[4]);
                if s != Severity::Info || !out.is_empty() {
                    out.push(Diagnostic {
                        path: abs(p),
                        line: m[2].parse().unwrap_or(1),
                        column: m.get(3).and_then(|c| c.as_str().parse().ok()).unwrap_or(1),
                        severity: s,
                        message: m[5].to_string(),
                        source: "gcc".into(),
                    });
                }
            }
        } else if let Some(m) = r.py_file.captures(l) {
            // la dernière ligne « Erreur: message » du traceback
            let mut msg = String::new();
            for k in i + 1..lines.len() {
                let t = lines[k];
                if !t.starts_with(' ') && t.contains(':') {
                    msg = t.to_string();
                    break;
                }
                if r.py_file.is_match(t) {
                    break;
                }
            }
            if !msg.is_empty() {
                out.push(Diagnostic { path: abs(&m[1]), line: m[2].parse().unwrap_or(1), column: 1, severity: Severity::Error, message: msg, source: "python".into() });
            }
        } else if let Some(m) = r.fasm_loc.captures(l) {
            for k in i + 1..(i + 4).min(lines.len()) {
                if let Some(e) = lines[k].strip_prefix("error: ") {
                    out.push(Diagnostic { path: abs(&m[1]), line: m[2].parse().unwrap_or(1), column: 1, severity: Severity::Error, message: e.to_string(), source: "fasm".into() });
                    break;
                }
            }
        }
        i += 1;
    }
    // sans doublons (gcc répète parfois)
    let mut seen = std::collections::HashSet::new();
    out.retain(|d| seen.insert((d.path.clone(), d.line, d.column, d.message.clone())));
    out
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn parses() {
        let cwd = Path::new("/p");
        let t = "main.c:3:5: error: 'x' undeclared (first use in this function)\nmain.c:4:1: warning: unused\n\
error[E0425]: cannot find value `y` in this scope\n --> src/main.rs:2:20\n  |\n\
main.bas(3) error 41: Variable not declared, y in 'print y'\n\
Program.cs(5,9): error CS0103: The name 'z' does not exist in the current context [/p/a.csproj]\n\
  File \"main.py\", line 7, in <module>\nNameError: name 'q' is not defined\n\
main.s:6: Error: no such instruction: `movv rax,1'\n";
        let d = parse_diagnostics(t, cwd);
        assert_eq!(d.len(), 7, "{d:#?}");
        assert_eq!(d[0].path, PathBuf::from("/p/main.c"));
        assert_eq!(d[2].line, 2);
        assert_eq!(d[2].column, 20);
        assert_eq!(d[3].source, "fbc");
        assert_eq!(d[4].column, 9);
        assert_eq!(d[5].message, "NameError: name 'q' is not defined");
        assert_eq!(d[6].severity, Severity::Error);
    }
}
