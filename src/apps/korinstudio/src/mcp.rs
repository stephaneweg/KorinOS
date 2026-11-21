//! Serveur MCP de KorinStudio : les agents de code (Claude Code, claude-acp, Mistral Vibe) agissent dans l'IDE.
//!
//! Transport « Streamable HTTP » (spec MCP 2025-06-18), réduit : POST /mcp, une requête par connexion, réponses
//! JSON (pas de flux SSE). Chaque `tools/call` part vers l'IDE comme un `ToolCall` sur le canal rendu par `start` ;
//! le fil de la connexion attend la réponse. Mode pont (`korinstudio --mcp-bridge <url> <token>`) : un serveur MCP
//! stdio qui relaie vers le serveur HTTP, pour les agents qui ne parlent que stdio.
//!
//! Uniquement std, serde_json et egui : des E/S bloquantes et des fils, pour tourner aussi sur Korin OS.

#![allow(dead_code)]

use std::collections::hash_map::RandomState;
use std::hash::{BuildHasher, Hasher};
use std::io::{self, BufRead, BufReader, Read, Write};
use std::net::{Shutdown, TcpListener, TcpStream};
use std::sync::atomic::{AtomicBool, AtomicU64, Ordering};
use std::sync::mpsc::{self, Receiver, RecvTimeoutError, Sender};
use std::sync::{Arc, Mutex, OnceLock};
use std::time::{Duration, SystemTime, UNIX_EPOCH};

use serde_json::{Map, Value, json};

/// Port d'usage (Forge 7311, TerraCube 7312, Jet 7313, KorinStudio 7314).
pub const DEFAULT_PORT: u16 = 7314;
/// Versions du protocole acceptées ; la première est celle par défaut.
pub const PROTOCOL_VERSIONS: [&str; 3] = ["2025-06-18", "2025-03-26", "2024-11-05"];

const MAX_BODY: usize = 64 * 1024 * 1024;
const MAX_LINE: u64 = 16 * 1024;
const MAX_HEADERS: usize = 100;
const SOCKET_TIMEOUT: Duration = Duration::from_secs(30);
const CALL_TIMEOUT: Duration = Duration::from_secs(30 * 60);
/// Le pont attend un peu plus longtemps que le serveur, qui répond « timeout » lui-même.
const BRIDGE_READ_TIMEOUT: Duration = Duration::from_secs(35 * 60);

const INSTRUCTIONS: &str = "You are connected to KorinStudio, the IDE the user is looking at right now. \
Everything you do through these tools happens in the IDE window and is visible to the user. \
Call get_workspace first to learn which project is open. Prefer these tools over your own file and shell tools to: \
create projects (list_project_templates, then create_project), open the files the user should see (open_file), \
read and edit files (read_file, write_file, replace_in_file: they see unsaved editor content and keep the editor in sync), \
build and run the project (build, run, run_command) and read compiler errors and warnings (get_diagnostics). \
Relative paths are relative to the open project root.";

// ---------------------------------------------------------------------------------------------------------------
// Interface publique

/// Un outil offert aux agents.
#[derive(Clone, Debug)]
pub struct ToolDef {
    pub name: &'static str,
    pub description: &'static str,
    pub input_schema: Value,
    pub read_only: bool,
}

/// Un morceau du résultat d'un outil.
#[derive(Clone, Debug, PartialEq)]
pub enum Content {
    Text(String),
    Image { data_base64: String, mime: String },
}

/// Résultat d'un outil, rendu par l'IDE.
#[derive(Clone, Debug)]
pub struct ToolResult {
    pub content: Vec<Content>,
    pub is_error: bool,
}

impl ToolResult {
    pub fn text(s: impl Into<String>) -> Self {
        ToolResult { content: vec![Content::Text(s.into())], is_error: false }
    }

    pub fn error(s: impl Into<String>) -> Self {
        ToolResult { content: vec![Content::Text(s.into())], is_error: true }
    }

    fn to_json(&self) -> Value {
        let content: Vec<Value> = self
            .content
            .iter()
            .map(|c| match c {
                Content::Text(t) => json!({"type": "text", "text": t}),
                Content::Image { data_base64, mime } => {
                    json!({"type": "image", "data": data_base64, "mimeType": mime})
                }
            })
            .collect();
        json!({"content": content, "isError": self.is_error})
    }
}

/// Un appel d'outil à exécuter par l'IDE (dans son fil d'interface), qui répond sur `reply`.
#[derive(Debug)]
pub struct ToolCall {
    pub name: String,
    pub args: Value,
    pub reply: Sender<ToolResult>,
}

impl ToolCall {
    /// Répond à l'appel (sans erreur si l'agent est parti).
    pub fn respond(self, result: ToolResult) {
        let _ = self.reply.send(result);
    }
}

/// Le serveur lancé : son port et le jeton à présenter (`Authorization: Bearer <token>`).
#[derive(Clone, Debug)]
pub struct McpServer {
    pub port: u16,
    pub token: String,
}

impl McpServer {
    pub fn url(&self) -> String {
        format!("http://127.0.0.1:{}/mcp", self.port)
    }
}

/// Lance le fil du serveur sur 127.0.0.1:7314 (un port libre si 7314 est pris).
pub fn start(ctx: egui::Context) -> io::Result<(McpServer, Receiver<ToolCall>)> {
    let listener = TcpListener::bind(("127.0.0.1", DEFAULT_PORT))
        .or_else(|_| TcpListener::bind(("127.0.0.1", 0)))?;
    let port = listener.local_addr()?.port();
    let token = random_hex32();
    let (tx, rx) = mpsc::channel();
    let shared = Arc::new(Shared { token: token.clone(), tx, ctx });
    std::thread::Builder::new()
        .name("mcp-accept".into())
        .spawn(move || accept_loop(listener, shared))?;
    Ok((McpServer { port, token }, rx))
}

/// La liste des outils.
pub fn tool_defs() -> Vec<ToolDef> {
    defs().to_vec()
}

/// Mode pont : serveur MCP stdio relayé vers le serveur HTTP de l'IDE. Rend le code de sortie du process.
pub fn run_bridge(url: &str, token: &str) -> i32 {
    let stdin = io::stdin();
    bridge_loop(url, token, stdin.lock(), io::stdout())
}

/// Boucle du pont : un message JSON-RPC par ligne sur `input`, une réponse par ligne sur `output`.
/// Les requêtes partent chacune dans un fil (un `ping` passe pendant un long `build`) ; `initialize` et les
/// notifications sont relayés dans l'ordre.
pub fn bridge_loop(url: &str, token: &str, input: impl BufRead, output: impl Write + Send) -> i32 {
    let target = match Target::parse(url) {
        Ok(t) => t,
        Err(e) => {
            eprintln!("korinstudio --mcp-bridge: {e}");
            return 2;
        }
    };
    let state = BridgeState {
        target,
        token: token.to_string(),
        url: url.to_string(),
        session: Mutex::new(None),
        protocol: Mutex::new(None),
        output: Mutex::new(output),
        broken: AtomicBool::new(false),
    };
    let mut code = 0;
    std::thread::scope(|scope| {
        for line in input.lines() {
            let line = match line {
                Ok(l) => l,
                Err(e) => {
                    eprintln!("korinstudio --mcp-bridge: stdin: {e}");
                    code = 1;
                    break;
                }
            };
            if state.broken.load(Ordering::SeqCst) {
                break;
            }
            let line = line.trim().to_string();
            if line.is_empty() {
                continue;
            }
            let parsed: Option<Value> = serde_json::from_str(&line).ok();
            let ids = request_ids(parsed.as_ref());
            let is_init = is_initialize(parsed.as_ref());
            if ids.is_empty() || is_init {
                state.relay(&line, parsed.is_none(), &ids, is_init);
            } else {
                let st = &state;
                scope.spawn(move || st.relay(&line, false, &ids, false));
            }
        }
    });
    if state.broken.load(Ordering::SeqCst) { 1 } else { code }
}

// ---------------------------------------------------------------------------------------------------------------
// Outils

fn defs() -> &'static [ToolDef] {
    static DEFS: OnceLock<Vec<ToolDef>> = OnceLock::new();
    DEFS.get_or_init(build_defs)
}

/// Schéma d'objet : (nom, schéma) et la liste des champs obligatoires.
fn object(props: &[(&str, Value)], required: &[&str]) -> Value {
    let mut map = Map::new();
    for (k, v) in props {
        map.insert((*k).to_string(), v.clone());
    }
    let mut schema = json!({"type": "object", "properties": Value::Object(map)});
    if !required.is_empty() {
        schema["required"] = json!(required);
    }
    schema
}

fn string(desc: &str) -> Value {
    json!({"type": "string", "description": desc})
}

fn integer(desc: &str, min: i64) -> Value {
    json!({"type": "integer", "description": desc, "minimum": min})
}

fn boolean(desc: &str) -> Value {
    json!({"type": "boolean", "description": desc})
}

const PATH_DESC: &str = "File path, absolute or relative to the open project root.";

fn build_defs() -> Vec<ToolDef> {
    vec![
        ToolDef {
            name: "get_workspace",
            description: "Get the current state of the KorinStudio IDE: the open project folder (absolute path) and \
its kind (language and build system), the open editor tabs, the active file with the cursor line and column and the \
selected text, and the files with unsaved changes. Call this first to know what the user is working on.",
            input_schema: object(&[], &[]),
            read_only: true,
        },
        ToolDef {
            name: "list_project_templates",
            description: "List the project templates that create_project accepts: for each one its id (for example \
\"rust\", \"c\", \"cpp\", \"korin-app-c\", \"csharp\", \"freebasic\", \"asm-gnu\", \"python\"), language, build \
system and a short description.",
            input_schema: object(&[], &[]),
            read_only: true,
        },
        ToolDef {
            name: "create_project",
            description: "Create a new project from a template and open it in the IDE, so the user sees it right \
away. Returns the absolute path of the new project folder. Fails if that folder already exists. Call \
list_project_templates first to get a valid template id.",
            input_schema: object(
                &[
                    (
                        "template",
                        string("Template id from list_project_templates, for example \"rust\", \"c\", \"cpp\", \
\"korin-app-c\", \"csharp\", \"freebasic\", \"asm-gnu\" or \"python\"."),
                    ),
                    (
                        "name",
                        string("Project name, also used as the name of the new folder. Use letters, digits, '-' \
or '_'."),
                    ),
                    (
                        "location",
                        string("Parent folder in which the project folder is created (absolute path). Default: \
the user's projects folder."),
                    ),
                ],
                &["template", "name"],
            ),
            read_only: false,
        },
        ToolDef {
            name: "open_project",
            description: "Open an existing folder as the current project of the IDE (it replaces the project shown \
in the IDE window). Returns the project's absolute path and detected kind.",
            input_schema: object(&[("path", string("Absolute path of the project folder to open."))], &["path"]),
            read_only: false,
        },
        ToolDef {
            name: "open_file",
            description: "Open a file in an editor tab, or focus its tab if it is already open, so the user sees \
it; optionally move the cursor to a line and column and scroll there. Use it to show the user the code you are \
talking about.",
            input_schema: object(
                &[
                    ("path", string(PATH_DESC)),
                    ("line", integer("1-based line to put the cursor on and scroll to.", 1)),
                    ("column", integer("1-based column on that line (default 1).", 1)),
                ],
                &["path"],
            ),
            read_only: false,
        },
        ToolDef {
            name: "read_file",
            description: "Read a text file as the IDE sees it: if the file is open in an editor with unsaved \
changes, the editor content is returned, not the disk content. Each line is prefixed with its 1-based line number \
and a tab; that prefix is not part of the file. Use start_line and end_line to read part of a large file.",
            input_schema: object(
                &[
                    ("path", string(PATH_DESC)),
                    ("start_line", integer("First line to return, 1-based (default 1).", 1)),
                    ("end_line", integer("Last line to return, inclusive (default: the end of the file).", 1)),
                ],
                &["path"],
            ),
            read_only: true,
        },
        ToolDef {
            name: "write_file",
            description: "Create a file or overwrite it with the given full content (missing parent folders are \
created). The file is saved to disk, its editor tab is updated (or opened) and shown to the user. For a small \
change to an existing file, prefer replace_in_file.",
            input_schema: object(
                &[("path", string(PATH_DESC)), ("content", string("The complete new content of the file."))],
                &["path", "content"],
            ),
            read_only: false,
        },
        ToolDef {
            name: "replace_in_file",
            description: "Replace an exact piece of text in a file. old_text must match the current content \
exactly, whitespace and indentation included (never include the line-number prefixes that read_file adds). Fails \
if old_text is not found, or if it is found several times and replace_all is not true: then add surrounding lines \
to make it unique. Works on the editor content if the file is open; the result is saved to disk and shown to the \
user.",
            input_schema: object(
                &[
                    ("path", string(PATH_DESC)),
                    ("old_text", string("The exact text to replace (must not be empty).")),
                    ("new_text", string("The replacement text (may be empty to delete old_text).")),
                    ("replace_all", boolean("Replace every occurrence of old_text instead of exactly one (default false).")),
                ],
                &["path", "old_text", "new_text"],
            ),
            read_only: false,
        },
        ToolDef {
            name: "list_files",
            description: "List the files and folders of the project, or of one of its folders, as an indented \
tree down to the given depth. Build output and hidden folders (such as .git or target) may be summarized or \
skipped.",
            input_schema: object(
                &[
                    (
                        "path",
                        string("Folder to list, absolute or relative to the project root (default: the project \
root)."),
                    ),
                    ("depth", integer("How many folder levels to descend (default 3).", 1)),
                ],
                &[],
            ),
            read_only: true,
        },
        ToolDef {
            name: "search",
            description: "Search the text of the project's files, like grep, and return the matching lines as \
path:line:column: text. Unsaved editor content is searched for open files.",
            input_schema: object(
                &[
                    ("query", string("The text to look for; a regular expression if regex is true.")),
                    ("regex", boolean("Treat query as a regular expression (default false: plain text).")),
                    (
                        "path",
                        string("File or folder to search in, absolute or relative to the project root (default: \
the project root)."),
                    ),
                    ("case_sensitive", boolean("Match case exactly (default false).")),
                    ("max_results", integer("Maximum number of matching lines to return (default 100).", 1)),
                ],
                &["query"],
            ),
            read_only: true,
        },
        ToolDef {
            name: "get_diagnostics",
            description: "Get the errors and warnings of the last build: file, line, column, severity (error, \
warning or note) and message. Run build first if the code changed since the last build.",
            input_schema: object(
                &[(
                    "path",
                    string("Only return the diagnostics of this file, absolute or relative to the project root \
(default: all files)."),
                )],
                &[],
            ),
            read_only: true,
        },
        ToolDef {
            name: "build",
            description: "Build the open project with its build system (cargo, make, dotnet, ...) and wait until \
it finishes (at most 10 minutes). The user sees the build output in the IDE. Returns whether the build succeeded, \
the end of its output and the diagnostics (errors and warnings with file, line and message).",
            input_schema: object(&[], &[]),
            read_only: false,
        },
        ToolDef {
            name: "run",
            description: "Build the project if needed, then run it in the IDE (the user sees its output). Waits at \
most timeout_s seconds and returns the output produced so far, and the exit code if the program has ended. A \
program still running after the timeout (a GUI app, a server) keeps running in the IDE.",
            input_schema: object(
                &[
                    (
                        "args",
                        json!({"type": "array", "items": {"type": "string"},
                               "description": "Command-line arguments passed to the program."}),
                    ),
                    ("timeout_s", integer("Seconds to wait for output before returning (default 30).", 1)),
                ],
                &[],
            ),
            read_only: false,
        },
        ToolDef {
            name: "run_command",
            description: "Run a shell command in the IDE's terminal panel, where the user sees it, and wait for it \
to finish (at most timeout_s seconds). Returns the exit code and the output (stdout and stderr). Use it for git, \
package managers, tests or scripts; use build and run for the project itself.",
            input_schema: object(
                &[
                    ("command", string("The command line, run by the system shell.")),
                    (
                        "cwd",
                        string("Working directory, absolute or relative to the project root (default: the project \
root)."),
                    ),
                    ("timeout_s", integer("Maximum number of seconds to wait for the command (default 120).", 1)),
                ],
                &["command"],
            ),
            read_only: false,
        },
        ToolDef {
            name: "approve",
            description: "Used by the IDE itself for permission prompts (Claude Code's --permission-prompt-tool): \
it asks the user whether a tool may run. Do not call it yourself. Returns a JSON text, either \
{\"behavior\":\"allow\",\"updatedInput\":{...}} or {\"behavior\":\"deny\",\"message\":\"...\"}.",
            input_schema: object(
                &[
                    ("tool_name", string("Name of the tool that asks for permission.")),
                    (
                        "input",
                        json!({"type": "object", "description": "The input the tool would be called with."}),
                    ),
                    ("tool_use_id", string("Identifier of this tool use, if known.")),
                ],
                &["tool_name", "input"],
            ),
            read_only: false,
        },
    ]
}

/// « get_workspace » → « Get workspace ».
fn title_of(name: &str) -> String {
    let s = name.replace('_', " ");
    let mut c = s.chars();
    match c.next() {
        Some(f) => f.to_uppercase().chain(c).collect(),
        None => String::new(),
    }
}

fn tools_list_result() -> Value {
    let tools: Vec<Value> = defs()
        .iter()
        .map(|d| {
            let title = title_of(d.name);
            json!({
                "name": d.name,
                "title": title,
                "description": d.description,
                "inputSchema": d.input_schema,
                "annotations": {"readOnlyHint": d.read_only, "title": title},
            })
        })
        .collect();
    json!({"tools": tools})
}

/// Vérifie les champs obligatoires et les types simples, pour que l'IDE reçoive des arguments sûrs.
fn check_args(def: &ToolDef, args: &Map<String, Value>) -> Result<(), String> {
    let schema = &def.input_schema;
    if let Some(req) = schema.get("required").and_then(Value::as_array) {
        for r in req.iter().filter_map(Value::as_str) {
            if args.get(r).is_none_or(Value::is_null) {
                return Err(format!("Invalid arguments for {}: '{r}' is required.", def.name));
            }
        }
    }
    let Some(props) = schema.get("properties").and_then(Value::as_object) else { return Ok(()) };
    for (k, v) in args {
        let Some(ty) = props.get(k).and_then(|p| p.get("type")).and_then(Value::as_str) else { continue };
        if v.is_null() {
            continue; // absent, en pratique
        }
        let ok = match ty {
            "string" => v.is_string(),
            "integer" => v.is_i64() || v.is_u64() || v.as_f64().is_some_and(|f| f.fract() == 0.0),
            "number" => v.is_number(),
            "boolean" => v.is_boolean(),
            "object" => v.is_object(),
            "array" => match v.as_array() {
                Some(items) => {
                    let item_ty = props[k].get("items").and_then(|i| i.get("type")).and_then(Value::as_str);
                    item_ty != Some("string") || items.iter().all(Value::is_string)
                }
                None => false,
            },
            _ => true,
        };
        if !ok {
            let want = if ty == "array" { "an array of strings" } else { ty };
            return Err(format!("Invalid arguments for {}: '{k}' must be {want}.", def.name));
        }
    }
    Ok(())
}

// ---------------------------------------------------------------------------------------------------------------
// Aléa sans crate : SplitMix64 sur un mélange d'horloge, de pid, de compteur, d'adresse et de RandomState

fn splitmix64(state: &mut u64) -> u64 {
    *state = state.wrapping_add(0x9E37_79B9_7F4A_7C15);
    let mut z = *state;
    z = (z ^ (z >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
    z = (z ^ (z >> 27)).wrapping_mul(0x94D0_49BB_1331_11EB);
    z ^ (z >> 31)
}

fn random_hex32() -> String {
    static COUNTER: AtomicU64 = AtomicU64::new(0);
    let local = 0u8;
    let nanos = SystemTime::now().duration_since(UNIX_EPOCH).map(|d| d.as_nanos()).unwrap_or(0);
    let count = COUNTER.fetch_add(1, Ordering::SeqCst);
    let addr = &local as *const u8 as usize as u64;
    let mut h = RandomState::new().build_hasher();
    h.write_u128(nanos);
    h.write_u32(std::process::id());
    h.write_u64(count);
    h.write_u64(addr);
    h.write(format!("{:?}", std::thread::current().id()).as_bytes());
    let mut s = h.finish() ^ (nanos as u64).rotate_left(29) ^ addr.rotate_left(47) ^ count;
    let a = splitmix64(&mut s);
    let b = splitmix64(&mut s) ^ ((nanos >> 64) as u64) ^ u64::from(std::process::id()).rotate_left(13);
    format!("{a:016x}{:016x}", splitmix64(&mut (b ^ a)))
}

/// Comparaison en temps constant (le jeton).
fn ct_eq(a: &str, b: &str) -> bool {
    let (a, b) = (a.as_bytes(), b.as_bytes());
    if a.len() != b.len() {
        return false;
    }
    a.iter().zip(b).fold(0u8, |acc, (x, y)| acc | (x ^ y)) == 0
}

// ---------------------------------------------------------------------------------------------------------------
// HTTP : lecture commune au serveur et au pont

struct Head {
    start: String,
    /// Noms en minuscules.
    headers: Vec<(String, String)>,
}

impl Head {
    fn get(&self, name: &str) -> Option<&str> {
        self.headers.iter().find(|(k, _)| k == name).map(|(_, v)| v.as_str())
    }
}

fn invalid(msg: &str) -> io::Error {
    io::Error::new(io::ErrorKind::InvalidData, msg.to_string())
}

/// Une ligne sans son CR LF ; None à la fin du flux. Erreur si la ligne dépasse `max`.
fn read_line_limited<R: BufRead>(r: &mut R, max: u64) -> io::Result<Option<String>> {
    let mut buf = Vec::new();
    let n = r.by_ref().take(max).read_until(b'\n', &mut buf)?;
    if n == 0 {
        return Ok(None);
    }
    if buf.last() != Some(&b'\n') && n as u64 >= max {
        return Err(invalid("line too long"));
    }
    while matches!(buf.last(), Some(b'\n' | b'\r')) {
        buf.pop();
    }
    Ok(Some(String::from_utf8_lossy(&buf).into_owned()))
}

/// Ligne de départ et en-têtes ; None si la connexion se ferme avant.
fn read_head<R: BufRead>(r: &mut R) -> io::Result<Option<Head>> {
    let start = loop {
        match read_line_limited(r, MAX_LINE)? {
            None => return Ok(None),
            Some(l) if l.trim().is_empty() => continue,
            Some(l) => break l,
        }
    };
    let mut headers: Vec<(String, String)> = Vec::new();
    loop {
        let line = read_line_limited(r, MAX_LINE)?
            .ok_or_else(|| io::Error::new(io::ErrorKind::UnexpectedEof, "truncated headers"))?;
        if line.is_empty() {
            break;
        }
        if line.starts_with([' ', '\t']) {
            // Suite d'un en-tête replié (obs-fold).
            if let Some(last) = headers.last_mut() {
                last.1.push(' ');
                last.1.push_str(line.trim());
            }
            continue;
        }
        if headers.len() >= MAX_HEADERS {
            return Err(invalid("too many headers"));
        }
        if let Some((k, v)) = line.split_once(':') {
            headers.push((k.trim().to_ascii_lowercase(), v.trim().to_string()));
        }
    }
    Ok(Some(Head { start, headers }))
}

/// Le corps d'un message HTTP : par morceaux, de longueur connue, jusqu'à la fermeture, ou vide.
enum BodyReader<R> {
    Chunked { inner: R, left: u64, done: bool },
    Fixed(io::Take<R>),
    ToEof(R),
    Empty,
}

impl<R: BufRead> Read for BodyReader<R> {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        match self {
            BodyReader::Empty => Ok(0),
            BodyReader::Fixed(t) => t.read(buf),
            BodyReader::ToEof(r) => r.read(buf),
            BodyReader::Chunked { inner, left, done } => {
                if *done || buf.is_empty() {
                    return Ok(0);
                }
                if *left == 0 {
                    let line = read_line_limited(inner, MAX_LINE)?
                        .ok_or_else(|| io::Error::new(io::ErrorKind::UnexpectedEof, "truncated chunk"))?;
                    let size = line.split(';').next().unwrap_or("").trim();
                    let size = u64::from_str_radix(size, 16).map_err(|_| invalid("bad chunk size"))?;
                    if size == 0 {
                        // Fin : en-têtes de queue éventuels, puis ligne vide.
                        while let Some(l) = read_line_limited(inner, MAX_LINE)? {
                            if l.is_empty() {
                                break;
                            }
                        }
                        *done = true;
                        return Ok(0);
                    }
                    *left = size;
                }
                let max = (*left).min(buf.len() as u64) as usize;
                let n = inner.read(&mut buf[..max])?;
                if n == 0 {
                    return Err(io::Error::new(io::ErrorKind::UnexpectedEof, "truncated chunk"));
                }
                *left -= n as u64;
                if *left == 0 {
                    match read_line_limited(inner, MAX_LINE)? {
                        Some(l) if !l.is_empty() => return Err(invalid("missing CRLF after chunk")),
                        _ => {}
                    }
                }
                Ok(n)
            }
        }
    }
}

/// Choisit la lecture du corps selon les en-têtes ; `to_eof` : sans longueur, lire jusqu'à la fermeture (réponses).
fn body_reader<R: BufRead>(r: R, head: &Head, to_eof: bool) -> io::Result<BodyReader<R>> {
    if let Some(te) = head.get("transfer-encoding") {
        let te = te.to_ascii_lowercase();
        if te.split(',').any(|t| t.trim() == "chunked") {
            return Ok(BodyReader::Chunked { inner: r, left: 0, done: false });
        }
        if te.trim() != "identity" {
            return Err(io::Error::new(io::ErrorKind::Unsupported, "unsupported transfer-encoding"));
        }
    }
    if let Some(cl) = head.get("content-length") {
        let n: u64 = cl.trim().parse().map_err(|_| invalid("bad content-length"))?;
        return Ok(BodyReader::Fixed(r.take(n)));
    }
    Ok(if to_eof { BodyReader::ToEof(r) } else { BodyReader::Empty })
}

// ---------------------------------------------------------------------------------------------------------------
// Serveur

struct Shared {
    token: String,
    tx: Sender<ToolCall>,
    ctx: egui::Context,
}

struct Response {
    status: u16,
    headers: Vec<(&'static str, String)>,
    body: Vec<u8>,
}

impl Response {
    fn json(status: u16, v: &Value) -> Self {
        let body = serde_json::to_vec(v).unwrap_or_default();
        Response { status, headers: vec![("Content-Type", "application/json".into())], body }
    }

    /// Erreur HTTP, avec un corps JSON-RPC lisible par le client.
    fn error(status: u16, msg: &str) -> Self {
        Response::json(status, &rpc_error(Value::Null, -32000, msg))
    }

    fn empty(status: u16) -> Self {
        Response { status, headers: Vec::new(), body: Vec::new() }
    }
}

fn reason(status: u16) -> &'static str {
    match status {
        100 => "Continue",
        200 => "OK",
        202 => "Accepted",
        400 => "Bad Request",
        401 => "Unauthorized",
        403 => "Forbidden",
        404 => "Not Found",
        405 => "Method Not Allowed",
        413 => "Payload Too Large",
        431 => "Request Header Fields Too Large",
        500 => "Internal Server Error",
        501 => "Not Implemented",
        _ => "Unknown",
    }
}

fn write_response<W: Write>(w: &mut W, resp: &Response) -> io::Result<()> {
    let mut head = format!("HTTP/1.1 {} {}\r\n", resp.status, reason(resp.status));
    for (k, v) in &resp.headers {
        head.push_str(&format!("{k}: {v}\r\n"));
    }
    head.push_str(&format!("Content-Length: {}\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n", resp.body.len()));
    w.write_all(head.as_bytes())?;
    w.write_all(&resp.body)?;
    w.flush()
}

fn accept_loop(listener: TcpListener, shared: Arc<Shared>) {
    for conn in listener.incoming() {
        match conn {
            Ok(stream) => {
                let sh = Arc::clone(&shared);
                let _ = std::thread::Builder::new()
                    .name("mcp-conn".into())
                    .spawn(move || {
                        let _ = handle_connection(stream, &sh);
                    });
            }
            // Plus de descripteurs, etc. : ne pas boucler à vide.
            Err(_) => std::thread::sleep(Duration::from_millis(50)),
        }
    }
}

fn handle_connection(stream: TcpStream, sh: &Shared) -> io::Result<()> {
    stream.set_read_timeout(Some(SOCKET_TIMEOUT))?;
    stream.set_write_timeout(Some(SOCKET_TIMEOUT))?;
    let _ = stream.set_nodelay(true);
    let mut writer = stream.try_clone()?;
    let mut reader = BufReader::new(stream);
    let resp = match read_head(&mut reader) {
        Ok(Some(head)) => route(&head, &mut reader, &mut writer, sh),
        Ok(None) => return Ok(()),
        Err(e) if matches!(e.kind(), io::ErrorKind::TimedOut | io::ErrorKind::WouldBlock) => return Ok(()),
        Err(e) if e.to_string().contains("too many") || e.to_string().contains("too long") => {
            Response::error(431, "Request header fields too large")
        }
        Err(_) => Response::error(400, "Bad request"),
    };
    write_response(&mut writer, &resp)?;
    // Fermer proprement : vider ce que le client envoie encore, sinon un RST peut avaler la réponse.
    let _ = writer.shutdown(Shutdown::Write);
    let _ = writer.set_read_timeout(Some(Duration::from_millis(500)));
    let _ = io::copy(&mut reader.take(4 * 1024 * 1024), &mut io::sink());
    Ok(())
}

/// Origines permises : absente (vu avant), « null », ou http://127.0.0.1, localhost, [::1] avec ou sans port.
fn origin_allowed(origin: &str) -> bool {
    let o = origin.trim().to_ascii_lowercase();
    if o == "null" {
        return true;
    }
    let Some(rest) = o.strip_prefix("http://") else { return false };
    let host = rest.strip_suffix('/').unwrap_or(rest);
    ["127.0.0.1", "localhost", "[::1]"].iter().any(|h| {
        host == *h
            || host
                .strip_prefix(h)
                .and_then(|p| p.strip_prefix(':'))
                .is_some_and(|port| !port.is_empty() && port.bytes().all(|b| b.is_ascii_digit()))
    })
}

fn auth_ok(head: &Head, token: &str) -> bool {
    let Some(v) = head.get("authorization") else { return false };
    let Some((scheme, cred)) = v.trim().split_once(' ') else { return false };
    scheme.eq_ignore_ascii_case("bearer") && ct_eq(cred.trim(), token)
}

fn route<R: BufRead, W: Write>(head: &Head, reader: &mut R, writer: &mut W, sh: &Shared) -> Response {
    let mut parts = head.start.split_whitespace();
    let method = parts.next().unwrap_or("");
    let target = parts.next().unwrap_or("");
    // Forme absolue tolérée : http://hôte:port/mcp.
    let target = match target.strip_prefix("http://") {
        Some(rest) => rest.find('/').map_or("/", |i| &rest[i..]),
        None => target,
    };
    let path = target.split(['?', '#']).next().unwrap_or("");
    if path != "/mcp" && path != "/mcp/" {
        return Response::error(404, "Not found: the MCP endpoint is /mcp");
    }
    if let Some(origin) = head.get("origin")
        && !origin_allowed(origin) {
            return Response::error(403, "Forbidden origin");
        }
    if !auth_ok(head, &sh.token) {
        let mut r = Response::error(401, "Unauthorized: missing or wrong bearer token");
        r.headers.push(("WWW-Authenticate", "Bearer".into()));
        return r;
    }
    match method {
        "POST" => {}
        "DELETE" => return Response::empty(200),
        _ => {
            let mut r = Response::error(405, "Method not allowed: use POST (no SSE stream is offered)");
            r.headers.push(("Allow", "POST, DELETE".into()));
            return r;
        }
    }
    if let Some(cl) = head.get("content-length").and_then(|v| v.trim().parse::<u64>().ok())
        && cl > MAX_BODY as u64 {
            return Response::error(413, "Request body too large");
        }
    if head.get("expect").is_some_and(|e| e.eq_ignore_ascii_case("100-continue")) {
        let _ = writer.write_all(b"HTTP/1.1 100 Continue\r\n\r\n").and_then(|_| writer.flush());
    }
    let mut body = Vec::new();
    let read = body_reader(&mut *reader, head, false)
        .and_then(|b| b.take(MAX_BODY as u64 + 1).read_to_end(&mut body));
    match read {
        Ok(_) if body.len() > MAX_BODY => return Response::error(413, "Request body too large"),
        Ok(_) => {}
        Err(e) if e.kind() == io::ErrorKind::Unsupported => {
            return Response::error(501, "Unsupported transfer-encoding");
        }
        Err(_) => return Response::error(400, "Bad request body"),
    }
    if let Some(cl) = head.get("content-length").and_then(|v| v.trim().parse::<usize>().ok())
        && head.get("transfer-encoding").is_none() && body.len() < cl {
            return Response::error(400, "Truncated request body");
        }
    handle_post(&body, sh)
}

fn rpc_error(id: Value, code: i64, msg: impl Into<String>) -> Value {
    json!({"jsonrpc": "2.0", "id": id, "error": {"code": code, "message": msg.into()}})
}

fn handle_post(body: &[u8], sh: &Shared) -> Response {
    let msg: Value = match serde_json::from_slice(body) {
        Ok(v) => v,
        Err(e) => return Response::json(400, &rpc_error(Value::Null, -32700, format!("Parse error: {e}"))),
    };
    let mut session = None;
    let out = match msg {
        Value::Array(items) if items.is_empty() => {
            return Response::json(400, &rpc_error(Value::Null, -32600, "Invalid Request: empty batch"));
        }
        Value::Array(items) => {
            let replies: Vec<Value> =
                items.into_iter().filter_map(|m| handle_message(m, sh, &mut session)).collect();
            if replies.is_empty() { None } else { Some(Value::Array(replies)) }
        }
        m => handle_message(m, sh, &mut session),
    };
    match out {
        None => Response::empty(202),
        Some(v) => {
            let mut r = Response::json(200, &v);
            if let Some(s) = session {
                r.headers.push(("Mcp-Session-Id", s));
            }
            r
        }
    }
}

/// Traite un message ; None pour une notification ou une réponse du client.
fn handle_message(msg: Value, sh: &Shared, session: &mut Option<String>) -> Option<Value> {
    let Value::Object(obj) = msg else {
        return Some(rpc_error(Value::Null, -32600, "Invalid Request: expected an object"));
    };
    let id = obj.get("id").cloned();
    let Some(method) = obj.get("method").and_then(Value::as_str) else {
        if id.is_some() && (obj.contains_key("result") || obj.contains_key("error")) {
            return None; // réponse du client à une requête du serveur : on n'en fait pas
        }
        return Some(rpc_error(id.unwrap_or(Value::Null), -32600, "Invalid Request: missing method"));
    };
    let id = id?; // notification : rien à rendre
    if !(id.is_string() || id.is_number() || id.is_null()) {
        return Some(rpc_error(Value::Null, -32600, "Invalid Request: bad id"));
    }
    let params = obj.get("params").cloned().unwrap_or(Value::Null);
    let result = match method {
        "initialize" => {
            *session = Some(random_hex32());
            Ok(initialize_result(&params))
        }
        "ping" => Ok(json!({})),
        "tools/list" => Ok(tools_list_result()),
        "tools/call" => tools_call(&params, sh).map(|r| r.to_json()),
        _ => Err((-32601, format!("Method not found: {method}"))),
    };
    Some(match result {
        Ok(r) => json!({"jsonrpc": "2.0", "id": id, "result": r}),
        Err((code, m)) => rpc_error(id, code, m),
    })
}

fn initialize_result(params: &Value) -> Value {
    let requested = params.get("protocolVersion").and_then(Value::as_str);
    let version = requested.filter(|v| PROTOCOL_VERSIONS.contains(v)).unwrap_or(PROTOCOL_VERSIONS[0]);
    json!({
        "protocolVersion": version,
        "capabilities": {"tools": {"listChanged": false}},
        "serverInfo": {"name": "korinstudio", "title": "KorinStudio", "version": env!("CARGO_PKG_VERSION")},
        "instructions": INSTRUCTIONS,
    })
}

fn tools_call(params: &Value, sh: &Shared) -> Result<ToolResult, (i64, String)> {
    let Some(name) = params.get("name").and_then(Value::as_str) else {
        return Err((-32602, "Invalid params: missing tool name".into()));
    };
    let args = match params.get("arguments") {
        None | Some(Value::Null) => Map::new(),
        Some(Value::Object(m)) => m.clone(),
        Some(_) => return Ok(ToolResult::error("Invalid arguments: 'arguments' must be an object.")),
    };
    let Some(def) = defs().iter().find(|d| d.name == name) else {
        return Ok(ToolResult::error(format!("Unknown tool: {name}. Call tools/list to see the available tools.")));
    };
    if let Err(e) = check_args(def, &args) {
        return Ok(ToolResult::error(e));
    }
    Ok(forward(sh, name, Value::Object(args)))
}

/// Envoie l'appel à l'IDE, le réveille, attend sa réponse.
fn forward(sh: &Shared, name: &str, args: Value) -> ToolResult {
    let (reply, wait) = mpsc::channel();
    if sh.tx.send(ToolCall { name: name.to_string(), args, reply }).is_err() {
        return ToolResult::error("KorinStudio is not accepting tool calls (the IDE is closing).");
    }
    sh.ctx.request_repaint();
    match wait.recv_timeout(CALL_TIMEOUT) {
        Ok(r) => r,
        Err(RecvTimeoutError::Timeout) => ToolResult::error("timeout"),
        Err(RecvTimeoutError::Disconnected) => ToolResult::error("The IDE dropped the request without answering."),
    }
}

// ---------------------------------------------------------------------------------------------------------------
// Pont stdio → HTTP

struct Target {
    /// Pour connect : sans crochets.
    host: String,
    port: u16,
    path: String,
    /// Pour l'en-tête Host.
    authority: String,
}

impl Target {
    fn parse(url: &str) -> Result<Target, String> {
        let rest = url
            .strip_prefix("http://")
            .ok_or_else(|| format!("unsupported URL (only http:// is supported): {url}"))?;
        let (authority, path) = match rest.find('/') {
            Some(i) => (&rest[..i], &rest[i..]),
            None => (rest, "/"),
        };
        let (host, port) = if let Some(h) = authority.strip_prefix('[') {
            let (h, after) = h.split_once(']').ok_or_else(|| format!("bad host in URL: {url}"))?;
            (h, after.strip_prefix(':'))
        } else {
            match authority.rsplit_once(':') {
                Some((h, p)) => (h, Some(p)),
                None => (authority, None),
            }
        };
        let port = match port {
            Some(p) => p.parse::<u16>().map_err(|_| format!("bad port in URL: {url}"))?,
            None => 80,
        };
        if host.is_empty() {
            return Err(format!("missing host in URL: {url}"));
        }
        Ok(Target { host: host.to_string(), port, path: path.to_string(), authority: authority.to_string() })
    }
}

struct PostReply {
    status: u16,
    session: Option<String>,
    messages: Vec<Value>,
    /// Début du corps quand ce n'est pas du JSON (pour les erreurs).
    raw: String,
}

struct BridgeState<W> {
    target: Target,
    token: String,
    url: String,
    session: Mutex<Option<String>>,
    protocol: Mutex<Option<String>>,
    output: Mutex<W>,
    broken: AtomicBool,
}

fn lock<T>(m: &Mutex<T>) -> std::sync::MutexGuard<'_, T> {
    m.lock().unwrap_or_else(|e| e.into_inner())
}

impl<W: Write> BridgeState<W> {
    /// Relaie une ligne et écrit les réponses ; `ids` : les requêtes qui attendent une réponse.
    fn relay(&self, line: &str, malformed: bool, ids: &[Value], is_init: bool) {
        let session = lock(&self.session).clone();
        let protocol = lock(&self.protocol).clone();
        let reply = http_post(&self.target, &self.token, session.as_deref(), protocol.as_deref(), line.as_bytes(), ids);
        let expects = malformed || !ids.is_empty();
        let mut out: Vec<Value> = Vec::new();
        match reply {
            Ok(r) => {
                if let Some(s) = r.session {
                    *lock(&self.session) = Some(s);
                }
                if is_init {
                    let v = r.messages.iter().find_map(|m| m.pointer("/result/protocolVersion")?.as_str());
                    if let Some(v) = v {
                        *lock(&self.protocol) = Some(v.to_string());
                    }
                }
                // Chaque requête doit avoir sa réponse, sinon l'agent l'attendrait sans fin.
                let answered = |id: &Value| {
                    r.messages.iter().any(|m| m.get("method").is_none() && m.get("id") == Some(id))
                };
                let missing: Vec<Value> = ids.iter().filter(|id| !answered(id)).cloned().collect();
                out = r.messages.clone();
                if !missing.is_empty() {
                    let detail = r
                        .messages
                        .iter()
                        .find_map(|m| m.pointer("/error/message")?.as_str().map(str::to_string))
                        .unwrap_or_else(|| r.raw.trim().to_string());
                    let msg = format!("KorinStudio MCP server answered HTTP {}: {detail}", r.status);
                    // Les erreurs sans id (401, 400…) sont remplacées par une erreur par requête.
                    out.retain(|m| !(m.get("method").is_none() && m.get("id").is_none_or(Value::is_null)));
                    out.extend(error_replies(&missing, &msg));
                } else if malformed && out.is_empty() {
                    let msg = format!("KorinStudio MCP server answered HTTP {}: {}", r.status, r.raw.trim());
                    out = error_replies(ids, &msg);
                }
            }
            Err(e) => {
                if expects {
                    out = error_replies(ids, &format!("Cannot reach KorinStudio at {}: {e}", self.url));
                } else {
                    eprintln!("korinstudio --mcp-bridge: {e}");
                }
            }
        }
        if out.is_empty() {
            return;
        }
        let mut w = lock(&self.output);
        for m in out {
            let line = serde_json::to_string(&m).unwrap_or_default();
            if w.write_all(line.as_bytes()).and_then(|_| w.write_all(b"\n")).and_then(|_| w.flush()).is_err() {
                self.broken.store(true, Ordering::SeqCst);
                return;
            }
        }
    }
}

fn error_replies(ids: &[Value], msg: &str) -> Vec<Value> {
    if ids.is_empty() {
        return vec![rpc_error(Value::Null, -32603, msg)];
    }
    ids.iter().map(|id| rpc_error(id.clone(), -32603, msg)).collect()
}

/// Ids des requêtes (avec method et id) d'un message ou d'un lot.
fn request_ids(v: Option<&Value>) -> Vec<Value> {
    let one = |m: &Value| -> Option<Value> {
        m.get("method")?;
        m.get("id").cloned()
    };
    match v {
        Some(Value::Array(items)) => items.iter().filter_map(one).collect(),
        Some(m) => one(m).into_iter().collect(),
        None => Vec::new(),
    }
}

fn is_initialize(v: Option<&Value>) -> bool {
    let one = |m: &Value| m.get("method").and_then(Value::as_str) == Some("initialize");
    match v {
        Some(Value::Array(items)) => items.iter().any(one),
        Some(m) => one(m),
        None => false,
    }
}

/// Un POST HTTP/1.1 minimal ; lit du JSON ou un flux SSE (qu'il quitte dès qu'il a les réponses de `ids`).
fn http_post(
    t: &Target,
    token: &str,
    session: Option<&str>,
    protocol: Option<&str>,
    body: &[u8],
    ids: &[Value],
) -> io::Result<PostReply> {
    let stream = TcpStream::connect((t.host.as_str(), t.port))?;
    stream.set_read_timeout(Some(BRIDGE_READ_TIMEOUT))?;
    stream.set_write_timeout(Some(Duration::from_secs(60)))?;
    let _ = stream.set_nodelay(true);
    let mut req = format!(
        "POST {} HTTP/1.1\r\nHost: {}\r\nAuthorization: Bearer {token}\r\nContent-Type: application/json\r\n\
Accept: application/json, text/event-stream\r\nContent-Length: {}\r\nConnection: close\r\n",
        t.path,
        t.authority,
        body.len()
    );
    if let Some(s) = session {
        req.push_str(&format!("Mcp-Session-Id: {s}\r\n"));
    }
    if let Some(p) = protocol {
        req.push_str(&format!("MCP-Protocol-Version: {p}\r\n"));
    }
    req.push_str("\r\n");
    let mut w = stream.try_clone()?;
    w.write_all(req.as_bytes())?;
    w.write_all(body)?;
    w.flush()?;

    let mut reader = BufReader::new(stream);
    let (head, status) = loop {
        let head = read_head(&mut reader)?
            .ok_or_else(|| io::Error::new(io::ErrorKind::UnexpectedEof, "connection closed without a response"))?;
        let status: u16 = head
            .start
            .split_whitespace()
            .nth(1)
            .and_then(|s| s.parse().ok())
            .ok_or_else(|| invalid("bad HTTP status line"))?;
        if !(100..200).contains(&status) {
            break (head, status);
        }
    };
    let session = head.get("mcp-session-id").map(str::to_string);
    let ctype = head.get("content-type").unwrap_or("").to_ascii_lowercase();
    let mut reply = PostReply { status, session, messages: Vec::new(), raw: String::new() };
    if status == 204 || status == 304 {
        return Ok(reply);
    }
    let body = body_reader(&mut reader, &head, true)?;
    if ctype.starts_with("text/event-stream") {
        read_sse(BufReader::new(body), ids, &mut reply.messages)?;
    } else {
        let mut buf = Vec::new();
        body.take(MAX_BODY as u64).read_to_end(&mut buf)?;
        match serde_json::from_slice::<Value>(&buf) {
            Ok(Value::Array(items)) => reply.messages = items,
            Ok(v) => reply.messages.push(v),
            Err(_) => reply.raw = String::from_utf8_lossy(&buf[..buf.len().min(300)]).into_owned(),
        }
    }
    Ok(reply)
}

/// Lit les évènements SSE (`data:`) ; s'arrête quand toutes les réponses attendues sont là, ou à la fin du flux.
fn read_sse<R: BufRead>(mut r: R, ids: &[Value], messages: &mut Vec<Value>) -> io::Result<()> {
    let mut pending: Vec<Value> = ids.to_vec();
    let mut data = String::new();
    let mut dispatch = |data: &mut String, pending: &mut Vec<Value>| {
        if data.is_empty() {
            return;
        }
        if let Ok(v) = serde_json::from_str::<Value>(data) {
            let items = match v {
                Value::Array(a) => a,
                v => vec![v],
            };
            for m in items {
                if m.get("method").is_none()
                    && let Some(id) = m.get("id") {
                        pending.retain(|p| p != id);
                    }
                messages.push(m);
            }
        }
        data.clear();
    };
    while let Some(line) = read_line_limited(&mut r, MAX_BODY as u64)? {
        if line.is_empty() {
            dispatch(&mut data, &mut pending);
            if !ids.is_empty() && pending.is_empty() {
                return Ok(());
            }
            continue;
        }
        if line.starts_with(':') {
            continue; // commentaire
        }
        let (field, value) = match line.split_once(':') {
            Some((f, v)) => (f, v.strip_prefix(' ').unwrap_or(v)),
            None => (line.as_str(), ""),
        };
        if field == "data" {
            if !data.is_empty() {
                data.push('\n');
            }
            data.push_str(value);
        }
    }
    dispatch(&mut data, &mut pending);
    Ok(())
}

// ---------------------------------------------------------------------------------------------------------------
// Tests

#[cfg(test)]
mod tests {
    use super::*;
    use std::io::Cursor;

    /// Serveur de test : un fil répond aux appels (écho, image, lenteur).
    fn spawn_server() -> McpServer {
        let (server, rx) = start(egui::Context::default()).expect("start");
        std::thread::spawn(move || {
            for call in rx {
                let r = match call.name.as_str() {
                    "get_workspace" => ToolResult {
                        content: vec![
                            Content::Text("workspace".into()),
                            Content::Image { data_base64: "iVBORw0K".into(), mime: "image/png".into() },
                        ],
                        is_error: false,
                    },
                    "build" => {
                        std::thread::sleep(Duration::from_millis(300));
                        ToolResult::text("built")
                    }
                    "run" => {
                        drop(call.reply); // l'IDE lâche la requête
                        continue;
                    }
                    _ => ToolResult::text(format!("{}:{}", call.name, call.args)),
                };
                call.respond(r);
            }
        });
        server
    }

    struct Raw {
        status: u16,
        head: Head,
        body: Vec<u8>,
    }

    impl Raw {
        fn json(&self) -> Value {
            serde_json::from_slice(&self.body).expect("json body")
        }
    }

    fn raw(port: u16, req: &[u8]) -> Raw {
        let mut s = TcpStream::connect(("127.0.0.1", port)).unwrap();
        s.write_all(req).unwrap();
        let mut r = BufReader::new(s);
        let head = read_head(&mut r).unwrap().unwrap();
        let status = head.start.split_whitespace().nth(1).unwrap().parse().unwrap();
        let mut body = Vec::new();
        body_reader(&mut r, &head, true).unwrap().read_to_end(&mut body).unwrap();
        Raw { status, head, body }
    }

    fn post(sv: &McpServer, extra: &str, body: &str) -> Raw {
        let req = format!(
            "POST /mcp HTTP/1.1\r\nHost: 127.0.0.1\r\nAuthorization: Bearer {}\r\nContent-Type: application/json\r\n\
{extra}Content-Length: {}\r\n\r\n{body}",
            sv.token,
            body.len()
        );
        raw(sv.port, req.as_bytes())
    }

    #[test]
    fn token_format() {
        let a = random_hex32();
        let b = random_hex32();
        assert_eq!(a.len(), 32);
        assert!(a.bytes().all(|c| c.is_ascii_hexdigit()));
        assert_ne!(a, b);
    }

    #[test]
    fn url_and_port() {
        let sv = spawn_server();
        assert_eq!(sv.url(), format!("http://127.0.0.1:{}/mcp", sv.port));
        assert_eq!(sv.token.len(), 32);
    }

    #[test]
    fn initialize_and_session() {
        let sv = spawn_server();
        let r = post(&sv, "", r#"{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"t","version":"1"}}}"#);
        assert_eq!(r.status, 200);
        assert_eq!(r.head.get("content-type"), Some("application/json"));
        let sid = r.head.get("mcp-session-id").expect("session id");
        assert_eq!(sid.len(), 32);
        let v = r.json();
        assert_eq!(v["id"], 1);
        assert_eq!(v["result"]["protocolVersion"], "2025-03-26");
        assert_eq!(v["result"]["capabilities"]["tools"]["listChanged"], false);
        assert_eq!(v["result"]["serverInfo"]["name"], "korinstudio");
        assert!(v["result"]["instructions"].as_str().unwrap().contains("KorinStudio"));

        let r = post(&sv, "", r#"{"jsonrpc":"2.0","id":"a","method":"initialize","params":{"protocolVersion":"1999-01-01"}}"#);
        assert_eq!(r.json()["result"]["protocolVersion"], "2025-06-18");
        assert_eq!(r.json()["id"], "a");
    }

    #[test]
    fn tools_list_schemas() {
        let sv = spawn_server();
        let v = post(&sv, "", r#"{"jsonrpc":"2.0","id":2,"method":"tools/list"}"#).json();
        let tools = v["result"]["tools"].as_array().unwrap();
        assert_eq!(tools.len(), 15);
        let mut names: Vec<&str> = tools.iter().map(|t| t["name"].as_str().unwrap()).collect();
        names.sort();
        names.dedup();
        assert_eq!(names.len(), 15);
        for t in tools {
            let s = &t["inputSchema"];
            assert_eq!(s["type"], "object", "{}", t["name"]);
            let props = s["properties"].as_object().unwrap();
            for (k, p) in props {
                assert!(p["type"].is_string(), "{k}");
                assert!(p["description"].as_str().is_some_and(|d| !d.is_empty()), "{k}");
            }
            if let Some(req) = s.get("required") {
                for r in req.as_array().unwrap() {
                    assert!(props.contains_key(r.as_str().unwrap()), "{r}");
                }
            }
            assert!(t["description"].as_str().unwrap().len() > 30);
            assert!(t["annotations"]["readOnlyHint"].is_boolean());
            assert!(t["annotations"]["title"].is_string());
        }
        let ro: Vec<&str> =
            tools.iter().filter(|t| t["annotations"]["readOnlyHint"] == true).map(|t| t["name"].as_str().unwrap()).collect();
        assert_eq!(ro, ["get_workspace", "list_project_templates", "read_file", "list_files", "search", "get_diagnostics"]);
        assert_eq!(tool_defs().len(), 15);
    }

    #[test]
    fn tools_call_forwarded() {
        let sv = spawn_server();
        let v = post(&sv, "", r#"{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"open_file","arguments":{"path":"src/main.rs","line":4}}}"#).json();
        assert_eq!(v["result"]["isError"], false);
        let text = v["result"]["content"][0]["text"].as_str().unwrap();
        assert!(text.starts_with("open_file:"));
        assert!(text.contains("\"line\":4"));

        let v = post(&sv, "", r#"{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"get_workspace"}}"#).json();
        let c = &v["result"]["content"];
        assert_eq!(c[0], json!({"type":"text","text":"workspace"}));
        assert_eq!(c[1], json!({"type":"image","data":"iVBORw0K","mimeType":"image/png"}));
    }

    #[test]
    fn tools_call_errors() {
        let sv = spawn_server();
        let v = post(&sv, "", r#"{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"nope","arguments":{}}}"#).json();
        assert_eq!(v["result"]["isError"], true);
        assert!(v.get("error").is_none());
        // Champ obligatoire absent, mauvais type : refusés sans déranger l'IDE.
        let v = post(&sv, "", r#"{"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"open_file","arguments":{}}}"#).json();
        assert_eq!(v["result"]["isError"], true);
        assert!(v["result"]["content"][0]["text"].as_str().unwrap().contains("'path' is required"));
        let v = post(&sv, "", r#"{"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"open_file","arguments":{"path":"a","line":"x"}}}"#).json();
        assert_eq!(v["result"]["isError"], true);
        let v = post(&sv, "", r#"{"jsonrpc":"2.0","id":8,"method":"tools/call","params":{"name":"run","arguments":{"args":[1]}}}"#).json();
        assert_eq!(v["result"]["isError"], true);
        // L'IDE lâche la réponse.
        let v = post(&sv, "", r#"{"jsonrpc":"2.0","id":9,"method":"tools/call","params":{"name":"run","arguments":{}}}"#).json();
        assert_eq!(v["result"]["isError"], true);
        let v = post(&sv, "", r#"{"jsonrpc":"2.0","id":10,"method":"tools/call","params":{}}"#).json();
        assert_eq!(v["error"]["code"], -32602);
    }

    #[test]
    fn receiver_dropped() {
        let (sv, rx) = start(egui::Context::default()).unwrap();
        drop(rx);
        let v = post(&sv, "", r#"{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"build","arguments":{}}}"#).json();
        assert_eq!(v["result"]["isError"], true);
    }

    #[test]
    fn auth_and_origin() {
        let sv = spawn_server();
        let body = r#"{"jsonrpc":"2.0","id":1,"method":"ping"}"#;
        let req = format!("POST /mcp HTTP/1.1\r\nAuthorization: Bearer wrong\r\nContent-Length: {}\r\n\r\n{body}", body.len());
        let r = raw(sv.port, req.as_bytes());
        assert_eq!(r.status, 401);
        assert_eq!(r.head.get("www-authenticate"), Some("Bearer"));
        let req = format!("POST /mcp HTTP/1.1\r\nContent-Length: {}\r\n\r\n{body}", body.len());
        assert_eq!(raw(sv.port, req.as_bytes()).status, 401);
        // En-têtes insensibles à la casse.
        let req = format!("POST /mcp HTTP/1.1\r\nauthorization: bearer {}\r\ncontent-length: {}\r\n\r\n{body}", sv.token, body.len());
        assert_eq!(raw(sv.port, req.as_bytes()).json()["result"], json!({}));

        assert_eq!(post(&sv, "Origin: http://evil.example\r\n", body).status, 403);
        assert_eq!(post(&sv, "Origin: http://localhost.evil.example\r\n", body).status, 403);
        assert_eq!(post(&sv, "Origin: http://localhost:5173\r\n", body).status, 200);
        assert_eq!(post(&sv, "Origin: null\r\n", body).status, 200);
        assert_eq!(post(&sv, "Origin: http://127.0.0.1\r\n", body).status, 200);
    }

    #[test]
    fn origins() {
        assert!(origin_allowed("http://127.0.0.1:7314"));
        assert!(origin_allowed("http://[::1]:80"));
        assert!(origin_allowed("HTTP://LOCALHOST"));
        assert!(!origin_allowed("https://localhost"));
        assert!(!origin_allowed("http://127.0.0.1.nip.io"));
        assert!(!origin_allowed("http://localhost:"));
        assert!(!origin_allowed(""));
    }

    #[test]
    fn notifications_and_methods() {
        let sv = spawn_server();
        let r = post(&sv, "", r#"{"jsonrpc":"2.0","method":"notifications/initialized"}"#);
        assert_eq!(r.status, 202);
        assert!(r.body.is_empty());
        let r = post(&sv, "", r#"{"jsonrpc":"2.0","id":1,"method":"resources/list"}"#);
        assert_eq!(r.json()["error"]["code"], -32601);
        let r = post(&sv, "", r#"{"jsonrpc":"2.0","id":1,"method":"ping"}"#);
        assert_eq!(r.json()["result"], json!({}));
        let r = post(&sv, "", r#"{"jsonrpc":"2.0","id":1,"result":{}}"#);
        assert_eq!(r.status, 202);
        let r = post(&sv, "", r#"{"jsonrpc":"2.0","id":1,"method":"ping""#);
        assert_eq!(r.status, 400);
        assert_eq!(r.json()["error"]["code"], -32700);
        assert_eq!(r.json()["id"], Value::Null);
        let r = post(&sv, "", "42");
        assert_eq!(r.json()["error"]["code"], -32600);
    }

    #[test]
    fn batch() {
        let sv = spawn_server();
        let r = post(&sv, "", r#"[{"jsonrpc":"2.0","id":1,"method":"ping"},{"jsonrpc":"2.0","method":"notifications/x"},{"jsonrpc":"2.0","id":2,"method":"tools/list"}]"#);
        let v = r.json();
        let a = v.as_array().unwrap();
        assert_eq!(a.len(), 2);
        assert_eq!(a[0]["id"], 1);
        assert_eq!(a[1]["result"]["tools"].as_array().unwrap().len(), 15);
        let r = post(&sv, "", r#"[{"jsonrpc":"2.0","method":"notifications/x"}]"#);
        assert_eq!(r.status, 202);
        assert_eq!(post(&sv, "", "[]").json()["error"]["code"], -32600);
    }

    #[test]
    fn http_routing() {
        let sv = spawn_server();
        let t = &sv.token;
        let get = format!("GET /mcp HTTP/1.1\r\nAuthorization: Bearer {t}\r\nAccept: text/event-stream\r\n\r\n");
        let r = raw(sv.port, get.as_bytes());
        assert_eq!(r.status, 405);
        assert_eq!(r.head.get("allow"), Some("POST, DELETE"));
        let del = format!("DELETE /mcp HTTP/1.1\r\nAuthorization: Bearer {t}\r\nMcp-Session-Id: x\r\n\r\n");
        assert_eq!(raw(sv.port, del.as_bytes()).status, 200);
        let other = format!("POST /other HTTP/1.1\r\nAuthorization: Bearer {t}\r\nContent-Length: 0\r\n\r\n");
        assert_eq!(raw(sv.port, other.as_bytes()).status, 404);
        let q = format!("POST /mcp?x=1 HTTP/1.1\r\nAuthorization: Bearer {t}\r\nContent-Length: 2\r\n\r\n{{}}");
        assert_eq!(raw(sv.port, q.as_bytes()).json()["error"]["code"], -32600);
        let big = format!("POST /mcp HTTP/1.1\r\nAuthorization: Bearer {t}\r\nContent-Length: 999999999\r\n\r\n");
        assert_eq!(raw(sv.port, big.as_bytes()).status, 413);
        assert_eq!(raw(sv.port, b"garbage\r\n\r\n").status, 404);
        let r = raw(sv.port, b"POST /mcp HTTP/1.1\r\nX: \xff\xfe\r\n\r\n");
        assert_eq!(r.status, 401);
    }

    #[test]
    fn chunked_request() {
        let sv = spawn_server();
        let body = r#"{"jsonrpc":"2.0","id":11,"method":"tools/call","params":{"name":"read_file","arguments":{"path":"a.c"}}}"#;
        let (a, b) = body.split_at(20);
        let req = format!(
            "POST /mcp HTTP/1.1\r\nAuthorization: Bearer {}\r\nTransfer-Encoding: chunked\r\n\r\n{:x};ext=1\r\n{a}\r\n{:x}\r\n{b}\r\n0\r\nX-Trailer: 1\r\n\r\n",
            sv.token,
            a.len(),
            b.len()
        );
        let v = raw(sv.port, req.as_bytes()).json();
        assert_eq!(v["id"], 11);
        assert!(v["result"]["content"][0]["text"].as_str().unwrap().starts_with("read_file:"));
        // Expect: 100-continue.
        let req = format!("POST /mcp HTTP/1.1\r\nAuthorization: Bearer {}\r\nExpect: 100-continue\r\nContent-Length: {}\r\n\r\n{body}", sv.token, body.len());
        let mut s = TcpStream::connect(("127.0.0.1", sv.port)).unwrap();
        s.write_all(req.as_bytes()).unwrap();
        let mut all = String::new();
        s.read_to_string(&mut all).unwrap();
        assert!(all.starts_with("HTTP/1.1 100 Continue\r\n\r\nHTTP/1.1 200 OK"));
    }

    #[test]
    fn bridge_end_to_end() {
        let sv = spawn_server();
        let input = concat!(
            r#"{"jsonrpc":"2.0","id":0,"method":"initialize","params":{"protocolVersion":"2025-06-18"}}"#, "\n",
            r#"{"jsonrpc":"2.0","method":"notifications/initialized"}"#, "\n",
            "\n",
            r#"{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"build","arguments":{}}}"#, "\n",
            r#"{"jsonrpc":"2.0","id":2,"method":"tools/list"}"#, "\n",
            "{not json\n",
        );
        let mut out = Vec::new();
        let code = bridge_loop(&sv.url(), &sv.token, Cursor::new(input), &mut out);
        assert_eq!(code, 0);
        let text = String::from_utf8(out).unwrap();
        let lines: Vec<Value> = text.lines().map(|l| serde_json::from_str(l).unwrap()).collect();
        assert_eq!(lines.len(), 4, "{text}");
        assert_eq!(lines[0]["id"], 0);
        assert_eq!(lines[0]["result"]["serverInfo"]["name"], "korinstudio");
        // tools/list (rapide) passe avant build (lent) : les requêtes sont parallèles.
        let by_id = |id: i64| lines.iter().find(|l| l["id"] == id).unwrap();
        assert_eq!(by_id(1)["result"]["content"][0]["text"], "built");
        assert_eq!(by_id(2)["result"]["tools"].as_array().unwrap().len(), 15);
        assert!(lines.iter().any(|l| l["error"]["code"] == -32700));

        // Mauvais jeton : une erreur JSON-RPC par requête.
        let mut out = Vec::new();
        bridge_loop(&sv.url(), "bad", Cursor::new("{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"ping\"}\n"), &mut out);
        let v: Value = serde_json::from_slice(&out).unwrap();
        assert_eq!(v["id"], 7); // le 401 devient une erreur pour la requête 7
        assert!(v["error"]["message"].as_str().unwrap().contains("401"));
    }

    #[test]
    fn bridge_unreachable_and_bad_url() {
        let port = TcpListener::bind("127.0.0.1:0").unwrap().local_addr().unwrap().port();
        let mut out = Vec::new();
        let code = bridge_loop(&format!("http://127.0.0.1:{port}/mcp"), "t",
            Cursor::new("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"ping\"}\n{\"jsonrpc\":\"2.0\",\"method\":\"n\"}\n"), &mut out);
        assert_eq!(code, 0);
        let v: Value = serde_json::from_slice(&out).unwrap();
        assert_eq!(v["id"], 3);
        assert_eq!(v["error"]["code"], -32603);
        assert_eq!(bridge_loop("https://x/mcp", "t", Cursor::new(""), Vec::new()), 2);
    }

    /// Faux serveur SSE en morceaux : vérifie le jeton et la reprise de Mcp-Session-Id.
    #[test]
    fn bridge_sse_and_session() {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let port = listener.local_addr().unwrap().port();
        let seen = Arc::new(Mutex::new(Vec::<(Option<String>, Option<String>, Option<String>)>::new()));
        let seen2 = Arc::clone(&seen);
        std::thread::spawn(move || {
            for (i, conn) in listener.incoming().enumerate() {
                let s = conn.unwrap();
                let seen2 = Arc::clone(&seen2);
                std::thread::spawn(move || {
                let mut w = s.try_clone().unwrap();
                let mut r = BufReader::new(s);
                let head = read_head(&mut r).unwrap().unwrap();
                let mut body = Vec::new();
                body_reader(&mut r, &head, false).unwrap().read_to_end(&mut body).unwrap();
                let msg: Value = serde_json::from_slice(&body).unwrap();
                lock(&seen2).push((
                    head.get("authorization").map(str::to_string),
                    head.get("mcp-session-id").map(str::to_string),
                    head.get("mcp-protocol-version").map(str::to_string),
                ));
                let Some(id) = msg.get("id") else {
                    w.write_all(b"HTTP/1.1 202 Accepted\r\nContent-Length: 0\r\n\r\n").unwrap();
                    return;
                };
                let result = if i == 0 { json!({"protocolVersion":"2025-03-26"}) } else { json!({"n": i}) };
                let ev1 = "event: message\ndata: {\"jsonrpc\":\"2.0\",\"method\":\"notifications/progress\",\"params\":{}}\n\n".to_string();
                let resp = serde_json::to_string(&json!({"jsonrpc":"2.0","id":id,"result":result})).unwrap();
                // Une réponse coupée sur deux lignes data:, qui se recollent avec \n (JSON valide).
                let (a, b) = resp.split_at(resp.find(',').unwrap() + 1);
                let ev2 = format!(": comment\r\ndata: {a}\r\ndata:{b}\r\n\r\n");
                let mut out = String::from("HTTP/1.1 200 OK\r\nContent-Type: text/event-stream\r\nTransfer-Encoding: chunked\r\n");
                if i == 0 {
                    out.push_str("Mcp-Session-Id: sess-42\r\n");
                }
                out.push_str("\r\n");
                for chunk in [ev1, ev2] {
                    out.push_str(&format!("{:x}\r\n{chunk}\r\n", chunk.len()));
                }
                w.write_all(out.as_bytes()).unwrap();
                w.flush().unwrap();
                // Le flux reste ouvert : le pont doit partir de lui-même une fois la réponse reçue.
                std::thread::sleep(Duration::from_secs(2));
                let _ = w.write_all(b"0\r\n\r\n");
                });
            }
        });
        let input = concat!(
            "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{}}\n",
            "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}\n",
            "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}\n",
        );
        let t0 = std::time::Instant::now();
        let mut out = Vec::new();
        let code = bridge_loop(&format!("http://127.0.0.1:{port}/mcp"), "tok", Cursor::new(input), &mut out);
        assert_eq!(code, 0);
        assert!(t0.elapsed() < Duration::from_millis(1500), "bridge waited for the stream to end {:?} {}", t0.elapsed(), String::from_utf8_lossy(&out));
        let text = String::from_utf8(out).unwrap();
        let lines: Vec<Value> = text.lines().map(|l| serde_json::from_str(l).unwrap()).collect();
        assert_eq!(lines.len(), 4, "{text}");
        assert_eq!(lines[0]["method"], "notifications/progress");
        assert_eq!(lines[1]["id"], 1);
        assert_eq!(lines[3]["result"]["n"], 2);
        let seen = lock(&seen);
        assert_eq!(seen.len(), 3);
        assert!(seen.iter().all(|s| s.0.as_deref() == Some("Bearer tok")));
        assert_eq!(seen[0].1, None);
        assert_eq!(seen[1].1.as_deref(), Some("sess-42"));
        assert_eq!(seen[2].1.as_deref(), Some("sess-42"));
        assert_eq!(seen[2].2.as_deref(), Some("2025-03-26"));
    }

    #[test]
    fn line_and_chunk_limits() {
        let mut r = Cursor::new(vec![b'a'; 100]);
        assert!(read_line_limited(&mut r, 10).is_err());
        let head = Head { start: String::new(), headers: vec![("transfer-encoding".into(), "chunked".into())] };
        let mut body = Vec::new();
        assert!(body_reader(Cursor::new(&b"zz\r\nab"[..]), &head, false).unwrap().read_to_end(&mut body).is_err());
        let head = Head { start: String::new(), headers: vec![("transfer-encoding".into(), "gzip".into())] };
        assert!(body_reader(Cursor::new(&b""[..]), &head, false).is_err());
    }
}
