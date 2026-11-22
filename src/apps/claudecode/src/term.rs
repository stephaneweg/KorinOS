//! L'onglet « Terminal » du volet de droite : un bash dans le dossier de la conversation, sans terminal (pas de pty :
//! les commandes et leur sortie, pas les programmes plein écran). Chaque commande est suivie d'une ligne repère qui
//! dit sa fin et son code ; la sortie et les erreurs arrivent par deux fils. Pour les programmes interactifs, le
//! Terminal de Korin s'ouvre dans le même dossier.

use std::io::{BufRead, BufReader, Write};
use std::process::{Child, ChildStdin, Command, Stdio};
use std::sync::mpsc::{Receiver, Sender, channel};

/// La ligne repère qui suit chaque commande (`echo` de bash) : « <repère> <code> ».
const MARK: &str = "\u{1e}KORIN_FIN";
const MAX_LINES: usize = 3000;

/// Une ligne de la sortie : le texte, et sa nature.
#[derive(Clone, Debug, PartialEq)]
pub enum Line {
    /// La commande tapée.
    Cmd(String),
    Out(String),
    Err(String),
    /// La fin d'une commande : son code.
    End(i32),
    Note(String),
}

pub struct Term {
    pub cwd: String,
    child: Option<Child>,
    stdin: Option<ChildStdin>,
    rx: Receiver<Line>,
    tx: Sender<Line>,
    pub lines: Vec<Line>,
    /// Une commande tourne (pas encore de repère).
    pub running: bool,
    pub input: String,
    pub history: Vec<String>,
    pub hist_pos: Option<usize>,
}

impl Term {
    pub fn new(cwd: &str) -> Term {
        let (tx, rx) = channel();
        Term { cwd: cwd.to_owned(), child: None, stdin: None, rx, tx, lines: Vec::new(), running: false, input: String::new(), history: Vec::new(), hist_pos: None }
    }

    fn start(&mut self) -> std::io::Result<()> {
        let mut cmd = Command::new(bash());
        cmd.current_dir(&self.cwd).stdin(Stdio::piped()).stdout(Stdio::piped()).stderr(Stdio::piped());
        // (comme le lanceur de la CLI : les commandes d'unixtools devant celles de /bin)
        if std::path::Path::new("/devtools/bin/busybox").exists() {
            let path = std::env::var("PATH").unwrap_or_else(|_| "/bin:/sbin".into());
            if !path.split(':').any(|p| p == "/devtools/bin") {
                cmd.env("PATH", format!("/devtools/bin:{path}"));
            }
        }
        cmd.env("TERM", "dumb");
        let mut child = cmd.spawn()?;
        if let Some(o) = child.stdout.take() {
            reader(o, self.tx.clone(), false);
        }
        if let Some(e) = child.stderr.take() {
            reader(e, self.tx.clone(), true);
        }
        self.stdin = child.stdin.take();
        self.child = Some(child);
        Ok(())
    }

    /// Lance une commande (bash démarre au premier besoin).
    pub fn run(&mut self, command: &str) {
        let command = command.trim_end();
        if command.trim().is_empty() {
            return;
        }
        if self.history.last().map(String::as_str) != Some(command) {
            self.history.push(command.to_owned());
        }
        self.hist_pos = None;
        self.push(Line::Cmd(command.to_owned()));
        if self.child.is_none()
            && let Err(e) = self.start()
        {
            self.push(Line::Note(format!("bash ne démarre pas : {e}")));
            return;
        }
        let script = format!("{command}\necho \"{MARK} $?\"\n");
        let ok = self.stdin.as_mut().is_some_and(|w| w.write_all(script.as_bytes()).and_then(|_| w.flush()).is_ok());
        if ok {
            self.running = true;
        } else {
            self.push(Line::Note("bash s'est arrêté : relancé à la commande suivante.".into()));
            self.stop();
        }
    }

    /// Arrête bash (et la commande qui tourne) ; le suivant démarre à la prochaine commande, dans le même dossier.
    pub fn stop(&mut self) {
        self.stdin = None;
        if let Some(mut c) = self.child.take() {
            let _ = c.kill();
            let _ = c.wait();
        }
        if self.running {
            self.running = false;
            self.push(Line::Note("Arrêté.".into()));
        }
    }

    /// Les lignes arrivées -> true s'il y en a eu.
    pub fn poll(&mut self) -> bool {
        let got: Vec<Line> = self.rx.try_iter().collect();
        let any = !got.is_empty();
        for l in got {
            if let Line::End(_) = l {
                self.running = false;
            }
            self.push(l);
        }
        if let Some(c) = self.child.as_mut()
            && c.try_wait().ok().flatten().is_some()
        {
            self.child = None;
            self.stdin = None;
            if self.running {
                self.running = false;
                self.push(Line::Note("bash a fini.".into()));
            }
        }
        any
    }

    fn push(&mut self, l: Line) {
        self.lines.push(l);
        if self.lines.len() > MAX_LINES {
            let n = self.lines.len() - MAX_LINES;
            self.lines.drain(..n);
        }
    }

    /// Le texte de la sortie (pour la copier).
    pub fn text(&self) -> String {
        let mut s = String::new();
        for l in &self.lines {
            match l {
                Line::Cmd(c) => s.push_str(&format!("$ {c}\n")),
                Line::Out(o) | Line::Err(o) | Line::Note(o) => {
                    s.push_str(o);
                    s.push('\n');
                }
                Line::End(_) => {}
            }
        }
        s
    }

    /// L'historique : la commande précédente (up) ou suivante dans le champ.
    pub fn recall(&mut self, up: bool) {
        if self.history.is_empty() {
            return;
        }
        let n = self.history.len();
        let pos = match (self.hist_pos, up) {
            (None, true) => n - 1,
            (None, false) => return,
            (Some(p), true) => p.saturating_sub(1),
            (Some(p), false) if p + 1 < n => p + 1,
            (Some(_), false) => {
                self.hist_pos = None;
                self.input.clear();
                return;
            }
        };
        self.hist_pos = Some(pos);
        self.input = self.history[pos].clone();
    }
}

impl Drop for Term {
    fn drop(&mut self) {
        self.stdin = None;
        if let Some(mut c) = self.child.take() {
            let _ = c.kill();
            let _ = c.wait();
        }
    }
}

fn bash() -> &'static str {
    if std::path::Path::new("/bin/bash").exists() { "/bin/bash" } else { "bash" }
}

/// Une ligne de sortie -> Line (le repère : la fin et son code).
pub fn classify(line: &str, err: bool) -> Line {
    if let Some(rest) = line.strip_prefix(MARK) {
        return Line::End(rest.trim().parse().unwrap_or(0));
    }
    let clean = strip_ansi(line);
    if err { Line::Err(clean) } else { Line::Out(clean) }
}

fn reader<R: std::io::Read + Send + 'static>(r: R, tx: Sender<Line>, err: bool) {
    std::thread::spawn(move || {
        let mut br = BufReader::new(r);
        let mut buf = Vec::new();
        loop {
            buf.clear();
            match br.read_until(b'\n', &mut buf) {
                Ok(0) | Err(_) => break,
                Ok(_) => {
                    let s = String::from_utf8_lossy(&buf);
                    let s = s.trim_end_matches(['\n', '\r']);
                    if tx.send(classify(s, err)).is_err() {
                        return;
                    }
                }
            }
        }
    });
}

/// Les séquences de couleur d'un terminal, retirées.
pub fn strip_ansi(s: &str) -> String {
    let mut out = String::new();
    let mut it = s.chars().peekable();
    while let Some(c) = it.next() {
        if c == '\u{1b}' {
            if it.peek() == Some(&'[') {
                it.next();
                for d in it.by_ref() {
                    if d.is_ascii_alphabetic() {
                        break;
                    }
                }
            }
            continue;
        }
        out.push(c);
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn marks_and_colors() {
        assert_eq!(classify(&format!("{MARK} 2"), false), Line::End(2));
        assert_eq!(classify("\u{1b}[31merreur\u{1b}[0m", true), Line::Err("erreur".into()));
    }

    /// Un vrai bash (la machine de construction) : deux commandes, leur sortie et leur code.
    #[cfg(unix)]
    #[test]
    fn runs_commands() {
        let mut t = Term::new("/");
        t.run("echo bonjour; echo oups >&2");
        t.run("false");
        let t0 = std::time::Instant::now();
        while t0.elapsed().as_secs() < 5 && t.lines.iter().filter(|l| matches!(l, Line::End(_))).count() < 2 {
            t.poll();
            std::thread::sleep(std::time::Duration::from_millis(20));
        }
        assert!(t.lines.contains(&Line::Out("bonjour".into())));
        assert!(t.lines.contains(&Line::Err("oups".into())));
        assert!(t.lines.contains(&Line::End(1)));
        assert!(!t.running);
        t.recall(true);
        assert_eq!(t.input, "false");
    }
}
