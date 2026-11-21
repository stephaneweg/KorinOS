//! Coloration syntaxique de KorinStudio : des lexeurs écrits à la main, sans regex ni dépendance.
//!
//! `highlight` rend des intervalles d'octets triés, sans chevauchement, sur des frontières de caractères ;
//! les trous sont du `Tok::Text`. Chaque lexeur avance toujours (temps linéaire) et ne panique jamais.

// Module de bibliothèque dans un binaire : tout ce qui est public n'y sert pas forcément.
#![allow(dead_code)]

use std::ops::Range;
use std::path::Path;

/// Un intervalle coloré.
pub type Span = (Range<usize>, Tok);

#[derive(Clone, Copy, PartialEq, Eq, Debug, Hash)]
pub enum Lang {
    Rust,
    C,
    Cpp,
    CSharp,
    AsmGnu,
    AsmFasm,
    FreeBasic,
    Python,
    Toml,
    Json,
    Markdown,
    Shell,
    Make,
    Xml,
    Ini,
    Plain,
}

#[derive(Clone, Copy, PartialEq, Eq, Debug, Hash)]
pub enum Tok {
    Text,
    Keyword,
    Type,
    Function,
    String,
    Char,
    Number,
    Comment,
    DocComment,
    Preproc,
    Macro,
    Operator,
    Punct,
    Attribute,
    Label,
    Register,
    Constant,
    Lifetime,
    Escape,
    Heading,
    Link,
    Emphasis,
    Tag,
    AttrName,
}

const ALL_LANGS: [Lang; 16] = [
    Lang::Rust,
    Lang::C,
    Lang::Cpp,
    Lang::CSharp,
    Lang::AsmGnu,
    Lang::AsmFasm,
    Lang::FreeBasic,
    Lang::Python,
    Lang::Toml,
    Lang::Json,
    Lang::Markdown,
    Lang::Shell,
    Lang::Make,
    Lang::Xml,
    Lang::Ini,
    Lang::Plain,
];

impl Lang {
    /// Le langage d'un fichier, par son nom ou son extension.
    pub fn from_path(p: &Path) -> Lang {
        let name = p.file_name().and_then(|n| n.to_str()).unwrap_or("");
        let lower = name.to_ascii_lowercase();
        match lower.as_str() {
            "makefile" | "gnumakefile" | "bsdmakefile" => return Lang::Make,
            "app.txt" => return Lang::Ini,
            "cargo.lock" => return Lang::Toml,
            ".bashrc" | ".bash_profile" | ".profile" | ".zshrc" | ".bash_aliases" => return Lang::Shell,
            ".editorconfig" | ".gitconfig" | ".gitmodules" => return Lang::Ini,
            _ => {}
        }
        let ext = match lower.rsplit_once('.') {
            Some((stem, e)) if !stem.is_empty() => e,
            _ => return Lang::Plain,
        };
        match ext {
            "rs" => Lang::Rust,
            "c" | "h" => Lang::C,
            "cc" | "cpp" | "cxx" | "c++" | "hpp" | "hh" | "hxx" | "h++" | "ipp" | "tpp" | "inl" => Lang::Cpp,
            "cs" | "csx" => Lang::CSharp,
            "s" | "sx" => Lang::AsmGnu,
            "asm" | "inc" | "fasm" | "ash" | "abi" => Lang::AsmFasm,
            "bas" | "bi" => Lang::FreeBasic,
            "py" | "pyw" | "pyi" => Lang::Python,
            "toml" => Lang::Toml,
            "json" | "jsonc" | "code-workspace" | "geojson" => Lang::Json,
            "md" | "markdown" | "mdown" | "mkd" => Lang::Markdown,
            "sh" | "bash" | "zsh" | "ksh" => Lang::Shell,
            "mk" | "mak" => Lang::Make,
            "xml" | "csproj" | "fsproj" | "vbproj" | "props" | "targets" | "axaml" | "xaml" | "svg" | "resx"
            | "plist" | "xsd" | "xsl" | "xslt" | "html" | "htm" | "xhtml" | "nuspec" => Lang::Xml,
            "ini" | "cfg" | "conf" | "properties" | "desktop" => Lang::Ini,
            _ => Lang::Plain,
        }
    }

    /// Le langage d'un nom court (bloc ``` de Markdown, menu) : "rust", "c++", "py", "bash"…
    pub fn from_name(name: &str) -> Option<Lang> {
        let n = name.trim().trim_start_matches(['{', '.']).to_ascii_lowercase();
        Some(match n.as_str() {
            "rust" | "rs" => Lang::Rust,
            "c" | "h" => Lang::C,
            "cpp" | "c++" | "cxx" | "hpp" | "cc" => Lang::Cpp,
            "cs" | "csharp" | "c#" => Lang::CSharp,
            "s" | "gas" | "asm-gnu" | "x86asm" => Lang::AsmGnu,
            "asm" | "fasm" | "fasmarm" | "nasm" | "inc" => Lang::AsmFasm,
            "basic" | "freebasic" | "fb" | "bas" | "qbasic" | "vb" => Lang::FreeBasic,
            "python" | "py" | "python3" => Lang::Python,
            "toml" => Lang::Toml,
            "json" | "jsonc" | "json5" => Lang::Json,
            "md" | "markdown" => Lang::Markdown,
            "sh" | "bash" | "shell" | "zsh" | "console" | "shellsession" => Lang::Shell,
            "make" | "makefile" | "mk" => Lang::Make,
            "xml" | "html" | "svg" | "axaml" | "xaml" | "csproj" => Lang::Xml,
            "ini" | "cfg" | "conf" => Lang::Ini,
            "text" | "txt" | "plain" | "plaintext" => Lang::Plain,
            _ => return None,
        })
    }

    /// Le nom affiché dans la barre d'état.
    pub fn name(self) -> &'static str {
        match self {
            Lang::Rust => "Rust",
            Lang::C => "C",
            Lang::Cpp => "C++",
            Lang::CSharp => "C#",
            Lang::AsmGnu => "Assembleur (GNU)",
            Lang::AsmFasm => "Assembleur (FASM)",
            Lang::FreeBasic => "FreeBASIC",
            Lang::Python => "Python",
            Lang::Toml => "TOML",
            Lang::Json => "JSON",
            Lang::Markdown => "Markdown",
            Lang::Shell => "Shell",
            Lang::Make => "Makefile",
            Lang::Xml => "XML",
            Lang::Ini => "INI",
            Lang::Plain => "Texte brut",
        }
    }

    pub fn all() -> &'static [Lang] {
        &ALL_LANGS
    }

    /// Le début d'un commentaire de ligne (pour « commenter la sélection »).
    pub fn line_comment(self) -> Option<&'static str> {
        match self {
            Lang::Rust | Lang::C | Lang::Cpp | Lang::CSharp => Some("//"),
            Lang::AsmGnu | Lang::Python | Lang::Toml | Lang::Shell | Lang::Make | Lang::Ini => Some("#"),
            Lang::AsmFasm => Some(";"),
            Lang::FreeBasic => Some("'"),
            Lang::Json | Lang::Markdown | Lang::Xml | Lang::Plain => None,
        }
    }

    /// Les délimiteurs d'un commentaire de bloc, s'il y en a.
    pub fn block_comment(self) -> Option<(&'static str, &'static str)> {
        match self {
            Lang::Rust | Lang::C | Lang::Cpp | Lang::CSharp | Lang::AsmGnu | Lang::Json => Some(("/*", "*/")),
            Lang::FreeBasic => Some(("/'", "'/")),
            Lang::Markdown | Lang::Xml => Some(("<!--", "-->")),
            _ => None,
        }
    }
}

/// Les intervalles colorés de `text` (octets, triés, sans chevauchement ; les trous sont du `Tok::Text`).
pub fn highlight(lang: Lang, text: &str) -> Vec<Span> {
    let mut out = Vec::new();
    if lang == Lang::Plain || text.is_empty() {
        return out;
    }
    {
        let mut lx = Lx { s: text.as_bytes(), i: 0, out: &mut out, depth: 0 };
        lx.run(lang);
    }
    sanitize(text, &mut out);
    out
}

/// Dernier filet de sécurité : ordre, bornes et frontières de caractères garantis.
fn sanitize(text: &str, v: &mut Vec<Span>) {
    let len = text.len();
    let mut w = 0;
    let mut last = 0usize;
    for r in 0..v.len() {
        let (rg, t) = v[r].clone();
        let mut a = rg.start.max(last);
        let mut b = rg.end.min(len);
        while a < len && !text.is_char_boundary(a) {
            a += 1;
        }
        while b < len && !text.is_char_boundary(b) {
            b += 1;
        }
        if a >= b || t == Tok::Text {
            continue;
        }
        if w > 0 && v[w - 1].0.end == a && v[w - 1].1 == t {
            v[w - 1].0.end = b;
        } else {
            v[w] = (a..b, t);
            w += 1;
        }
        last = b;
    }
    v.truncate(w);
}

// Profondeur des sous-lexeurs (trous de chaînes interpolées, blocs de code du Markdown).
const MAX_DEPTH: u8 = 3;

// ---------------------------------------------------------------------------------------------
// Listes de mots (triées octet par octet : recherche dichotomique ; un test le vérifie)

const RUST_KW: &[&str] = &[
    "Self", "as", "async", "await", "break", "const", "continue", "crate", "dyn", "else", "enum", "extern", "false",
    "fn", "for", "if", "impl", "in", "let", "loop", "match", "mod", "move", "mut", "pub", "ref", "return", "self",
    "static", "struct", "super", "trait", "true", "type", "union", "unsafe", "use", "where", "while",
];
const RUST_PRIM: &[&str] = &[
    "bool", "char", "f128", "f16", "f32", "f64", "i128", "i16", "i32", "i64", "i8", "isize", "str", "u128", "u16",
    "u32", "u64", "u8", "usize",
];
const C_KW: &[&str] = &[
    "_Alignas",
    "_Alignof",
    "_Atomic",
    "_Generic",
    "_Noreturn",
    "_Static_assert",
    "_Thread_local",
    "__asm__",
    "__attribute__",
    "__extension__",
    "__inline__",
    "__typeof__",
    "__volatile__",
    "alignas",
    "alignof",
    "auto",
    "break",
    "case",
    "const",
    "constexpr",
    "continue",
    "default",
    "do",
    "else",
    "enum",
    "extern",
    "for",
    "goto",
    "if",
    "inline",
    "register",
    "restrict",
    "return",
    "sizeof",
    "static",
    "static_assert",
    "struct",
    "switch",
    "thread_local",
    "typedef",
    "typeof",
    "union",
    "volatile",
    "while",
];
const CPP_KW: &[&str] = &[
    "and",
    "and_eq",
    "asm",
    "bitand",
    "bitor",
    "catch",
    "class",
    "co_await",
    "co_return",
    "co_yield",
    "compl",
    "concept",
    "const_cast",
    "consteval",
    "constinit",
    "decltype",
    "delete",
    "dynamic_cast",
    "explicit",
    "export",
    "final",
    "friend",
    "mutable",
    "namespace",
    "new",
    "noexcept",
    "not",
    "not_eq",
    "operator",
    "or",
    "or_eq",
    "override",
    "private",
    "protected",
    "public",
    "reinterpret_cast",
    "requires",
    "static_cast",
    "template",
    "this",
    "throw",
    "try",
    "typeid",
    "typename",
    "using",
    "virtual",
    "xor",
    "xor_eq",
];
const C_TYPES: &[&str] = &[
    "FILE", "_Bool", "_Complex", "bool", "char", "char16_t", "char32_t", "char8_t", "double", "float", "i16", "i32",
    "i64", "i8", "int", "long", "short", "signed", "u16", "u32", "u64", "u8", "unsigned", "void", "wchar_t",
];
const CPP_STD: &[&str] = &[
    "basic_string",
    "exception",
    "ifstream",
    "initializer_list",
    "istream",
    "nullptr_t",
    "ofstream",
    "optional",
    "ostream",
    "shared_ptr",
    "string",
    "string_view",
    "stringstream",
    "unique_ptr",
    "unordered_map",
    "unordered_set",
    "variant",
    "vector",
    "weak_ptr",
    "wstring",
];
// Types de std ambigus : seulement après `::`.
const CPP_STD_QUAL: &[&str] = &[
    "array", "atomic", "deque", "function", "list", "map", "multimap", "mutex", "pair", "queue", "set", "span",
    "stack", "thread", "tuple",
];
const CS_TYPES: &[&str] = &[
    "bool", "byte", "char", "decimal", "double", "dynamic", "float", "int", "long", "nint", "nuint", "object", "sbyte",
    "short", "string", "uint", "ulong", "ushort", "void",
];
const CS_KW: &[&str] = &[
    "abstract",
    "add",
    "alias",
    "and",
    "as",
    "ascending",
    "async",
    "await",
    "base",
    "break",
    "by",
    "case",
    "catch",
    "checked",
    "class",
    "const",
    "continue",
    "default",
    "delegate",
    "descending",
    "do",
    "else",
    "enum",
    "equals",
    "event",
    "explicit",
    "extern",
    "file",
    "finally",
    "fixed",
    "for",
    "foreach",
    "from",
    "get",
    "global",
    "goto",
    "group",
    "if",
    "implicit",
    "in",
    "init",
    "interface",
    "internal",
    "into",
    "is",
    "join",
    "let",
    "lock",
    "managed",
    "nameof",
    "namespace",
    "new",
    "not",
    "notnull",
    "on",
    "operator",
    "or",
    "orderby",
    "out",
    "override",
    "params",
    "partial",
    "private",
    "protected",
    "public",
    "readonly",
    "record",
    "ref",
    "remove",
    "required",
    "return",
    "scoped",
    "sealed",
    "select",
    "set",
    "sizeof",
    "stackalloc",
    "static",
    "struct",
    "switch",
    "this",
    "throw",
    "try",
    "typeof",
    "unchecked",
    "unmanaged",
    "unsafe",
    "using",
    "var",
    "virtual",
    "volatile",
    "when",
    "where",
    "while",
    "with",
    "yield",
];
const PY_KW: &[&str] = &[
    "and", "as", "assert", "async", "await", "break", "case", "class", "cls", "continue", "def", "del", "elif", "else",
    "except", "finally", "for", "from", "global", "if", "import", "in", "is", "lambda", "match", "nonlocal", "not",
    "or", "pass", "raise", "return", "self", "try", "while", "with", "yield",
];
const PY_TYPES: &[&str] = &[
    "BaseException",
    "Exception",
    "bool",
    "bytearray",
    "bytes",
    "complex",
    "dict",
    "float",
    "frozenset",
    "int",
    "list",
    "object",
    "set",
    "str",
    "tuple",
    "type",
];
const FB_TYPES: &[&str] = &[
    "any", "boolean", "byte", "double", "integer", "long", "longint", "object", "pointer", "ptr", "short", "single",
    "string", "ubyte", "uinteger", "ulong", "ulongint", "ushort", "wstring", "zstring",
];
const FB_KW: &[&str] = &[
    "abstract",
    "alias",
    "and",
    "andalso",
    "as",
    "asm",
    "byref",
    "byval",
    "call",
    "case",
    "cast",
    "cdecl",
    "class",
    "cls",
    "color",
    "const",
    "constructor",
    "continue",
    "data",
    "declare",
    "delete",
    "destructor",
    "dim",
    "do",
    "else",
    "elseif",
    "end",
    "enum",
    "eqv",
    "erase",
    "exit",
    "explicit",
    "export",
    "extends",
    "extern",
    "field",
    "for",
    "function",
    "get",
    "gosub",
    "goto",
    "if",
    "iif",
    "imp",
    "implements",
    "import",
    "input",
    "is",
    "let",
    "lib",
    "locate",
    "loop",
    "mod",
    "namespace",
    "new",
    "next",
    "not",
    "operator",
    "option",
    "or",
    "orelse",
    "overload",
    "override",
    "pascal",
    "peek",
    "poke",
    "preserve",
    "print",
    "private",
    "property",
    "protected",
    "public",
    "put",
    "randomize",
    "read",
    "redim",
    "restore",
    "return",
    "scope",
    "screen",
    "select",
    "shared",
    "shl",
    "shr",
    "sizeof",
    "sleep",
    "static",
    "stdcall",
    "step",
    "sub",
    "then",
    "this",
    "to",
    "type",
    "typeof",
    "union",
    "until",
    "using",
    "var",
    "virtual",
    "wend",
    "while",
    "with",
    "xor",
];
const SH_KW: &[&str] = &[
    "!", "[[", "]]", "case", "coproc", "declare", "do", "done", "elif", "else", "esac", "export", "fi", "for",
    "function", "if", "in", "local", "readonly", "select", "then", "time", "typeset", "until", "while", "{", "}",
];
const SH_BUILTINS: &[&str] = &[
    ".", "alias", "bg", "break", "builtin", "cd", "command", "continue", "echo", "eval", "exec", "exit", "false", "fg",
    "getopts", "hash", "jobs", "kill", "let", "mapfile", "popd", "printf", "pushd", "pwd", "read", "return", "set",
    "shift", "shopt", "source", "test", "trap", "true", "type", "ulimit", "umask", "unalias", "unset", "wait",
];
const MAKE_DIR: &[&str] = &[
    "-include", "define", "else", "endef", "endif", "export", "ifdef", "ifeq", "ifndef", "ifneq", "include",
    "override", "private", "sinclude", "undefine", "unexport", "vpath",
];
const ASM_SIZE: &[&str] = &[
    "byte", "dword", "flat", "fword", "offset", "oword", "ptr", "qword", "tbyte", "word", "xmmword", "ymmword",
    "zmmword",
];
const FASM_DIR: &[&str] = &[
    "align",
    "as",
    "assert",
    "at",
    "break",
    "code16",
    "code32",
    "code64",
    "common",
    "coprocessor",
    "define",
    "defined",
    "discardable",
    "display",
    "else",
    "end",
    "endp",
    "ends",
    "entry",
    "eq",
    "eqtype",
    "equ",
    "err",
    "executable",
    "extrn",
    "fix",
    "format",
    "forward",
    "from",
    "heap",
    "if",
    "in",
    "include",
    "irp",
    "irps",
    "irpv",
    "label",
    "load",
    "local",
    "macro",
    "match",
    "notpageable",
    "org",
    "postpone",
    "proc",
    "processor",
    "public",
    "purge",
    "readable",
    "repeat",
    "rept",
    "restore",
    "restruc",
    "reverse",
    "rva",
    "section",
    "segment",
    "stack",
    "store",
    "struc",
    "struct",
    "thumb",
    "times",
    "use16",
    "use32",
    "use64",
    "used",
    "virtual",
    "while",
    "writable",
    "writeable",
];
const FASM_DATA: &[&str] =
    &["db", "dd", "df", "dh", "dp", "dq", "dt", "du", "dw", "file", "rb", "rd", "rf", "rh", "rp", "rq", "rt", "rw"];
const FASM_SIZE: &[&str] = &[
    "byte", "dqword", "dword", "far", "fword", "hword", "near", "ptr", "pword", "qqword", "qword", "short", "tbyte",
    "tword", "word", "xword", "yword", "zword",
];
const X86_REGS: &[&str] = &[
    "ah", "al", "ax", "bh", "bl", "bp", "bpl", "bx", "ch", "cl", "cs", "cx", "dh", "di", "dil", "dl", "ds", "dx",
    "eax", "ebp", "ebx", "ecx", "edi", "edx", "eip", "es", "esi", "esp", "fs", "gs", "ip", "rax", "rbp", "rbx", "rcx",
    "rdi", "rdx", "rip", "rsi", "rsp", "si", "sil", "sp", "spl", "ss", "st",
];
const ARM_REGS: &[&str] = &[
    "apsr", "cpsr", "elr", "fp", "fpcr", "fpscr", "fpsr", "ip", "lr", "nzcv", "pc", "sb", "sl", "sp", "spsr", "wsp",
    "wzr", "xzr",
];

#[inline]
fn kw(list: &[&str], w: &[u8]) -> bool {
    list.binary_search_by(|k| k.as_bytes().cmp(w)).is_ok()
}

/// `w` en minuscules dans `buf` (None si trop long pour être un mot-clé).
#[inline]
fn lower<'b>(w: &[u8], buf: &'b mut [u8; 32]) -> &'b [u8] {
    if w.len() > buf.len() {
        return &[];
    }
    for (d, c) in buf.iter_mut().zip(w) {
        *d = c.to_ascii_lowercase();
    }
    &buf[..w.len()]
}

#[inline]
fn is_id_start(c: u8) -> bool {
    c.is_ascii_alphabetic() || c == b'_' || c >= 0x80
}
#[inline]
fn is_id(c: u8) -> bool {
    c.is_ascii_alphanumeric() || c == b'_' || c >= 0x80
}

/// TOUT_EN_CAPITALES (au moins deux caractères, une lettre, pas de minuscule).
fn is_caps(w: &[u8]) -> bool {
    w.len() >= 2
        && w.iter().any(|c| c.is_ascii_uppercase())
        && w.iter().all(|&c| c.is_ascii_uppercase() || c.is_ascii_digit() || c == b'_')
}

/// Un nombre décimal court en tête de `w`, et le reste.
fn split_num(w: &[u8]) -> Option<(u32, &[u8])> {
    let n = w.iter().take_while(|c| c.is_ascii_digit()).count();
    if n == 0 || n > 2 || (n == 2 && w[0] == b'0') {
        return None;
    }
    let v = w[..n].iter().fold(0u32, |a, &c| a * 10 + (c - b'0') as u32);
    Some((v, &w[n..]))
}

/// `w` = préfixe + nombre ≤ max (+ suffixe parmi `suf`).
fn reg_num(w: &[u8], prefix: &[u8], max: u32, suf: &[&[u8]]) -> bool {
    if !w.starts_with(prefix) {
        return false;
    }
    match split_num(&w[prefix.len()..]) {
        Some((v, rest)) => v <= max && (rest.is_empty() || suf.contains(&rest)),
        None => false,
    }
}

/// Registre x86 (mot en minuscules).
fn is_x86_reg(w: &[u8]) -> bool {
    if kw(X86_REGS, w) {
        return true;
    }
    if let Some(r) = w.strip_prefix(b"r")
        && let Some((v, rest)) = split_num(r)
    {
        return (8..=15).contains(&v) && matches!(rest, b"" | b"b" | b"w" | b"d" | b"l");
    }
    reg_num(w, b"xmm", 31, &[])
        || reg_num(w, b"ymm", 31, &[])
        || reg_num(w, b"zmm", 31, &[])
        || reg_num(w, b"mm", 7, &[])
        || reg_num(w, b"st", 7, &[])
        || reg_num(w, b"k", 7, &[])
        || reg_num(w, b"cr", 15, &[])
        || reg_num(w, b"dr", 15, &[])
}

/// Registre ARM (AArch32 et AArch64, mot en minuscules ; `v0.16b` accepté).
fn is_arm_reg(w: &[u8]) -> bool {
    let w = match w.iter().position(|&c| c == b'.') {
        Some(p) if p > 0 => &w[..p],
        _ => w,
    };
    kw(ARM_REGS, w)
        || reg_num(w, b"r", 30, &[])
        || reg_num(w, b"x", 30, &[])
        || reg_num(w, b"w", 30, &[])
        || reg_num(w, b"v", 31, &[])
        || reg_num(w, b"q", 31, &[])
        || reg_num(w, b"d", 31, &[])
        || reg_num(w, b"s", 31, &[])
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum Fl {
    Rust,
    C,
    Cpp,
    CSharp,
}

/// Ce que devient le prochain identificateur (après `fn`, `struct`, `class`, `macro`…).
#[derive(Clone, Copy, PartialEq, Eq)]
enum Next {
    None,
    Func,
    Type,
}

#[derive(Clone, Copy, PartialEq, Eq)]
enum PpCmt {
    C,
    Basic,
}

// ---------------------------------------------------------------------------------------------
// Le lexeur : un curseur sur les octets (tronqués à la fin de la zone) et la sortie partagée.

struct Lx<'a, 'o> {
    s: &'a [u8],
    i: usize,
    out: &'o mut Vec<Span>,
    depth: u8,
}

/// Garde-fou : une itération qui n'avance pas fait avancer d'un caractère.
macro_rules! guard {
    ($self:ident, $last:ident) => {
        if $self.i == $last {
            $self.bump();
        }
        $last = $self.i;
    };
}

impl<'a, 'o> Lx<'a, 'o> {
    fn run(&mut self, lang: Lang) {
        match lang {
            Lang::Rust => self.clike(Fl::Rust),
            Lang::C => self.clike(Fl::C),
            Lang::Cpp => self.clike(Fl::Cpp),
            Lang::CSharp => self.clike(Fl::CSharp),
            Lang::AsmGnu => self.asm_gnu(),
            Lang::AsmFasm => self.fasm(),
            Lang::FreeBasic => self.fbasic(),
            Lang::Python => self.python(),
            Lang::Toml => self.toml(),
            Lang::Json => self.json(),
            Lang::Markdown => self.markdown(),
            Lang::Shell => self.shell(),
            Lang::Make => self.make(),
            Lang::Xml => self.xml(),
            Lang::Ini => self.ini(),
            Lang::Plain => {}
        }
    }

    /// Colore `a..b` avec un autre lexeur (ou `fallback` si trop profond ou texte brut).
    fn sub(&mut self, lang: Lang, a: usize, b: usize, fallback: Tok) {
        let b = b.min(self.s.len());
        if a >= b {
            return;
        }
        if self.depth >= MAX_DEPTH || lang == Lang::Plain {
            self.push(a, b, fallback);
            return;
        }
        let mut lx = Lx { s: &self.s[..b], i: a, out: &mut *self.out, depth: self.depth + 1 };
        lx.run(lang);
    }

    #[inline]
    fn push(&mut self, a: usize, b: usize, t: Tok) {
        if t == Tok::Text {
            return;
        }
        let b = b.min(self.s.len());
        let a = match self.out.last() {
            Some(l) if l.0.end > a => l.0.end,
            _ => a,
        };
        if a >= b {
            return;
        }
        if let Some(l) = self.out.last_mut()
            && l.0.end == a
            && l.1 == t
        {
            l.0.end = b;
            return;
        }
        self.out.push((a..b, t));
    }

    #[inline]
    fn at(&self, j: usize) -> u8 {
        if j < self.s.len() { self.s[j] } else { 0 }
    }
    #[inline]
    fn cur(&self) -> u8 {
        self.at(self.i)
    }
    #[inline]
    fn nx(&self, k: usize) -> u8 {
        self.at(self.i + k)
    }
    #[inline]
    fn len(&self) -> usize {
        self.s.len()
    }
    #[inline]
    fn starts(&self, j: usize, pat: &[u8]) -> bool {
        j <= self.s.len() && self.s[j..].starts_with(pat)
    }
    /// Longueur du caractère UTF-8 qui commence en `j`.
    #[inline]
    fn char_len(&self, j: usize) -> usize {
        if j >= self.s.len() || self.s[j] < 0x80 {
            return 1;
        }
        let mut k = j + 1;
        while k < self.s.len() && (self.s[k] & 0xC0) == 0x80 {
            k += 1;
        }
        k - j
    }
    #[inline]
    fn bump(&mut self) {
        self.i = (self.i + self.char_len(self.i)).min(self.s.len());
    }
    /// Position du prochain '\n' (ou la fin).
    #[inline]
    fn eol(&self, j: usize) -> usize {
        let j = j.min(self.s.len());
        match self.s[j..].iter().position(|&c| c == b'\n') {
            Some(p) => j + p,
            None => self.s.len(),
        }
    }
    #[inline]
    fn ident_end(&self, j: usize) -> usize {
        let mut k = j;
        while k < self.s.len() && is_id(self.s[k]) {
            k += 1;
        }
        k
    }
    #[inline]
    fn hspace(&self, j: usize) -> usize {
        let mut k = j;
        while k < self.s.len() && matches!(self.s[k], b' ' | b'\t') {
            k += 1;
        }
        k
    }
    /// Prochain octet non blanc de la ligne.
    #[inline]
    fn next_sig(&self, j: usize) -> u8 {
        self.at(self.hspace(j))
    }
    /// Première occurrence de `pat` à partir de `from` (position absolue).
    fn find(&self, from: usize, pat: &[u8]) -> Option<usize> {
        let s = self.s;
        let f = pat[0];
        let mut k = from;
        while k + pat.len() <= s.len() {
            k += s[k..].iter().position(|&c| c == f)?;
            if s[k..].starts_with(pat) {
                return Some(k);
            }
            k += 1;
        }
        None
    }

    // ----- chaînes et commentaires communs

    /// Longueur d'une séquence d'échappement `\…` en `j`.
    fn esc_len(&self, j: usize) -> usize {
        let k0 = j + 1;
        if k0 >= self.len() {
            return 1;
        }
        let c = self.s[k0];
        let mut k = k0 + self.char_len(k0);
        let hex = |mut k: usize, n: usize| {
            let e = k + n;
            while k < e && self.at(k).is_ascii_hexdigit() {
                k += 1;
            }
            k
        };
        match c {
            b'x' => k = hex(k, 2),
            b'u' => {
                if self.at(k) == b'{' {
                    k = hex(k + 1, 8);
                    if self.at(k) == b'}' {
                        k += 1;
                    }
                } else {
                    k = hex(k, 4);
                }
            }
            b'U' => k = hex(k, 8),
            b'0'..=b'7' => {
                let e = k + 2;
                while k < e && matches!(self.at(k), b'0'..=b'7') {
                    k += 1;
                }
            }
            b'\r' if self.at(k) == b'\n' => k += 1,
            _ => {}
        }
        k.min(self.len()) - j
    }

    /// Fin d'une chaîne (après le guillemet fermant) ; `j` suit le guillemet ouvrant.
    fn skip_str(&self, j: usize, q: u8, esc: bool, multi: bool, doubled: bool) -> usize {
        let s = self.s;
        let mut k = j;
        while k < s.len() {
            let c = s[k];
            if c == q {
                if doubled && self.at(k + 1) == q {
                    k += 2;
                    continue;
                }
                return k + 1;
            }
            if c == b'\\' && esc {
                k += self.esc_len(k);
                continue;
            }
            if c == b'\n' && !multi {
                return k;
            }
            k += 1;
        }
        s.len()
    }

    /// Pousse la chaîne `a..b` en `tok`, ses échappements `\…` en `Tok::Escape`.
    fn str_pieces(&mut self, a: usize, b: usize, tok: Tok) {
        let mut seg = a;
        let mut k = a;
        while k < b {
            if self.s[k] == b'\\' {
                let l = self.esc_len(k).min(b - k);
                self.push(seg, k, tok);
                self.push(k, k + l, Tok::Escape);
                k += l;
                seg = k;
            } else {
                k += 1;
            }
        }
        self.push(seg, b, tok);
    }

    /// Chaîne simple de `start` ; self.i est sur le guillemet ouvrant.
    fn plain_str(&mut self, start: usize, tok: Tok, esc: bool, multi: bool, doubled: bool) {
        let q = self.cur();
        let e = self.skip_str(self.i + 1, q, esc, multi, doubled);
        if esc {
            self.str_pieces(start, e, tok);
        } else {
            self.push(start, e, tok);
        }
        self.i = e;
    }

    fn line_comment(&mut self, tok: Tok) {
        let a = self.i;
        self.i = self.eol(a);
        self.push(a, self.i, tok);
    }

    fn block_comment(&mut self, open: &[u8], close: &[u8], nest: bool, tok: Tok) {
        let a = self.i;
        let s = self.s;
        let mut k = a + open.len();
        let mut depth = 1u32;
        while k < s.len() {
            let c = s[k];
            if c == close[0] && s[k..].starts_with(close) {
                k += close.len();
                depth -= 1;
                if depth == 0 {
                    break;
                }
            } else if nest && c == open[0] && s[k..].starts_with(open) {
                k += open.len();
                depth += 1;
            } else {
                k += 1;
            }
        }
        self.i = k.min(s.len());
        self.push(a, self.i, tok);
    }

    /// Fin d'un nombre (chiffres, `_`, suffixes, point décimal, exposant signé).
    fn number_end(&self, j: usize, quote_sep: bool) -> usize {
        let s = self.s;
        let mut k = j;
        let hex = self.at(k) == b'0' && matches!(self.at(k + 1), b'x' | b'X');
        if hex {
            k += 2;
        }
        if self.at(k) == b'.' {
            k += 1;
        }
        while k < s.len() {
            let c = s[k];
            let more = c.is_ascii_alphanumeric()
                || c == b'_'
                || (c == b'.' && self.at(k + 1).is_ascii_digit())
                || ((c == b'+' || c == b'-')
                    && k > j
                    && (if hex { matches!(s[k - 1], b'p' | b'P') } else { matches!(s[k - 1], b'e' | b'E') })
                    && self.at(k + 1).is_ascii_digit())
                || (c == b'\'' && quote_sep && self.at(k + 1).is_ascii_alphanumeric());
            if !more {
                break;
            }
            k += 1;
        }
        k
    }

    /// Opérateurs et ponctuation ; rend false pour un autre caractère (qu'on saute).
    fn op_punct(&mut self) {
        let a = self.i;
        let tok = match self.cur() {
            b'+' | b'-' | b'*' | b'/' | b'%' | b'=' | b'<' | b'>' | b'!' | b'&' | b'|' | b'^' | b'~' | b'?' => {
                Tok::Operator
            }
            b'(' | b')' | b'[' | b']' | b'{' | b'}' | b',' | b';' | b':' | b'.' | b'@' | b'#' | b'$' | b'\\' | b'`' => {
                Tok::Punct
            }
            _ => {
                self.bump();
                return;
            }
        };
        self.push(a, a + 1, tok);
        self.i = a + 1;
    }

    /// Directive de préprocesseur (#…) jusqu'à la fin de la ligne logique ; chaînes et <en-tête> à part.
    fn preproc(&mut self, cmt: PpCmt) {
        let s = self.s;
        let a = self.i;
        let mut k = self.hspace(a + 1);
        let we = self.ident_end(k);
        let mut buf = [0u8; 32];
        let w = lower(&s[k..we], &mut buf);
        let inc = matches!(w, b"include" | b"include_next" | b"import" | b"inclib" | b"libpath");
        k = we;
        let mut run = a;
        while k < s.len() {
            let c = s[k];
            if c == b'\n' {
                break;
            }
            if c == b'\\' && cmt == PpCmt::C && (self.at(k + 1) == b'\n' || self.at(k + 1) == b'\r') {
                k += if self.at(k + 1) == b'\r' && self.at(k + 2) == b'\n' { 3 } else { 2 };
                continue;
            }
            if cmt == PpCmt::C && c == b'/' && (self.at(k + 1) == b'/' || self.at(k + 1) == b'*') {
                self.push(run, k, Tok::Preproc);
                self.i = k;
                if self.at(k + 1) == b'/' {
                    self.line_comment(Tok::Comment);
                    return;
                }
                self.block_comment(b"/*", b"*/", false, Tok::Comment);
                k = self.i;
                run = k;
                continue;
            }
            if cmt == PpCmt::Basic && c == b'\'' {
                self.push(run, k, Tok::Preproc);
                self.i = k;
                self.line_comment(Tok::Comment);
                return;
            }
            if c == b'"' {
                self.push(run, k, Tok::Preproc);
                let e = self.skip_str(k + 1, b'"', cmt == PpCmt::C, false, cmt == PpCmt::Basic);
                self.push(k, e, Tok::String);
                k = e;
                run = k;
                continue;
            }
            if c == b'<' && inc {
                let le = self.eol(k);
                if let Some(p) = s[k..le].iter().position(|&c| c == b'>') {
                    self.push(run, k, Tok::Preproc);
                    self.push(k, k + p + 1, Tok::String);
                    k += p + 1;
                    run = k;
                    continue;
                }
            }
            k += 1;
        }
        self.push(run, k, Tok::Preproc);
        self.i = k;
    }

    /// Attribut entre crochets (#[…] de Rust, […] de C#), chaînes à part. `multi` : sur plusieurs lignes
    /// (jusqu'au crochet fermant ou à une ligne vide) ; sinon seulement s'il se ferme sur la ligne.
    fn attr_region(&mut self, multi: bool) -> bool {
        let s = self.s;
        let a = self.i;
        let mut j = a;
        while j < s.len() && s[j] != b'[' {
            j += 1;
        }
        let mut depth = 0usize;
        let mut end = None;
        while j < s.len() {
            match s[j] {
                b'[' => depth += 1,
                b']' => {
                    depth = depth.saturating_sub(1);
                    if depth == 0 {
                        end = Some(j + 1);
                        break;
                    }
                }
                b'"' => {
                    j = self.skip_str(j + 1, b'"', true, multi, false);
                    continue;
                }
                b'\n' => {
                    let n = self.at(j + 1);
                    if !multi || n == b'\n' || (n == b'\r' && self.at(j + 2) == b'\n') {
                        break;
                    }
                }
                _ => {}
            }
            j += 1;
        }
        let end = match end {
            Some(e) => e,
            None if multi => j.min(s.len()),
            None => return false,
        };
        let mut run = a;
        let mut k = a;
        while k < end {
            if s[k] == b'"' {
                self.push(run, k, Tok::Attribute);
                let e = self.skip_str(k + 1, b'"', true, multi, false).min(end);
                self.str_pieces(k, e, Tok::String);
                k = e;
                run = k;
            } else {
                k += 1;
            }
        }
        self.push(run, end, Tok::Attribute);
        self.i = end;
        true
    }

    /// Chaîne interpolée ($"…" de C#, f"…" de Python) : self.i suit le guillemet ouvrant.
    #[allow(clippy::too_many_arguments)]
    fn interp(&mut self, start: usize, lang: Lang, q: u8, triple: bool, esc: bool, multi: bool, doubled: bool) {
        let mut seg = start;
        let mut last = usize::MAX;
        while self.i < self.len() {
            guard!(self, last);
            if self.i >= self.len() {
                break;
            }
            let c = self.cur();
            if c == q {
                if triple {
                    if self.nx(1) == q && self.nx(2) == q {
                        self.i += 3;
                        break;
                    }
                    self.i += 1;
                    continue;
                }
                if doubled && self.nx(1) == q {
                    self.push(seg, self.i, Tok::String);
                    self.push(self.i, self.i + 2, Tok::Escape);
                    self.i += 2;
                    seg = self.i;
                    continue;
                }
                self.i += 1;
                break;
            }
            if c == b'\\' {
                if esc {
                    self.push(seg, self.i, Tok::String);
                    let l = self.esc_len(self.i);
                    self.push(self.i, self.i + l, Tok::Escape);
                    self.i += l;
                    seg = self.i;
                } else {
                    self.i += 1;
                    if self.i < self.len() && self.cur() != b'\n' {
                        self.bump();
                    }
                }
                continue;
            }
            if c == b'\n' && !multi {
                break;
            }
            if (c == b'{' || c == b'}') && self.nx(1) == c {
                self.push(seg, self.i, Tok::String);
                self.push(self.i, self.i + 2, Tok::Escape);
                self.i += 2;
                seg = self.i;
                continue;
            }
            if c == b'{' {
                self.push(seg, self.i, Tok::String);
                self.hole(lang, q, multi);
                seg = self.i;
                continue;
            }
            self.i += 1;
        }
        self.push(seg, self.i, Tok::String);
    }

    /// Un trou {expr[:format]} d'une chaîne interpolée ; self.i sur '{'.
    fn hole(&mut self, lang: Lang, q: u8, multi: bool) {
        let s = self.s;
        let a = self.i;
        self.push(a, a + 1, Tok::Punct);
        let code = a + 1;
        let mut k = code;
        let mut depth = 0u32;
        while k < s.len() {
            let c = s[k];
            match c {
                b'(' | b'[' | b'{' => depth += 1,
                b')' | b']' => depth = depth.saturating_sub(1),
                b'}' => {
                    if depth == 0 {
                        break;
                    }
                    depth -= 1;
                }
                b':' if depth == 0 && self.at(k + 1) != b':' && self.at(k + 1) != b'=' && s[k - 1] != b':' => break,
                b'!' if depth == 0 && lang == Lang::Python && self.at(k + 1) != b'=' => break,
                b'"' | b'\'' => {
                    if c == q && !multi {
                        break;
                    }
                    k = self.skip_str(k + 1, c, true, false, false);
                    continue;
                }
                b'\n' if !multi => break,
                _ => {}
            }
            k += 1;
        }
        self.sub(lang, code, k, Tok::Text);
        self.i = k.min(s.len());
        if matches!(self.cur(), b':' | b'!') {
            let f = self.i;
            let mut depth = 0u32;
            while self.i < s.len() {
                let c = self.cur();
                if c == b'{' {
                    depth += 1;
                } else if c == b'}' {
                    if depth == 0 {
                        break;
                    }
                    depth -= 1;
                } else if (c == b'\n' && !multi) || c == q {
                    break;
                }
                self.i += 1;
            }
            self.push(f, self.i, Tok::String);
        }
        if self.cur() == b'}' {
            self.push(self.i, self.i + 1, Tok::Punct);
            self.i += 1;
        }
    }

    // ----- Rust, C, C++, C#

    fn clike(&mut self, fl: Fl) {
        let mut ls = true;
        let mut next = Next::None;
        let mut last = usize::MAX;
        while self.i < self.len() {
            guard!(self, last);
            if self.i >= self.len() {
                break;
            }
            let start = self.i;
            let c = self.cur();
            match c {
                b'\n' => {
                    self.i += 1;
                    ls = true;
                    continue;
                }
                b' ' | b'\t' | b'\r' | 0x0b | 0x0c => {
                    self.i += 1;
                    continue;
                }
                _ => {}
            }
            let line_start = ls;
            ls = false;
            let pending = std::mem::replace(&mut next, Next::None);
            let c1 = self.nx(1);
            if c == b'/' && c1 == b'/' {
                let (c2, c3) = (self.nx(2), self.nx(3));
                let doc = (c2 == b'/' && c3 != b'/') || (c2 == b'!' && fl != Fl::CSharp);
                self.line_comment(if doc { Tok::DocComment } else { Tok::Comment });
                continue;
            }
            if c == b'/' && c1 == b'*' {
                let (c2, c3) = (self.nx(2), self.nx(3));
                let doc = (c2 == b'*' && c3 != b'*' && c3 != b'/') || (c2 == b'!' && fl != Fl::CSharp);
                let tok = if doc { Tok::DocComment } else { Tok::Comment };
                self.block_comment(b"/*", b"*/", fl == Fl::Rust, tok);
                continue;
            }
            if c == b'#' {
                if fl == Fl::Rust {
                    if (c1 == b'[' || (c1 == b'!' && self.nx(2) == b'[')) && self.attr_region(true) {
                        continue;
                    }
                } else if line_start {
                    self.preproc(PpCmt::C);
                    continue;
                }
            }
            if c == b'[' && fl == Fl::CSharp && line_start && self.attr_region(false) {
                continue;
            }
            if c == b'"' {
                if fl == Fl::CSharp && c1 == b'"' && self.nx(2) == b'"' {
                    self.cs_raw(start);
                } else {
                    self.plain_str(start, Tok::String, true, fl == Fl::Rust, false);
                }
                continue;
            }
            if c == b'\'' {
                if fl == Fl::Rust {
                    self.rust_quote(start);
                } else {
                    self.plain_str(start, Tok::Char, true, false, false);
                }
                continue;
            }
            if fl == Fl::CSharp && (c == b'@' || c == b'$') {
                let mut k = start;
                let (mut verb, mut dollars) = (false, 0);
                while k < self.len() && k - start < 6 && matches!(self.s[k], b'@' | b'$') {
                    if self.s[k] == b'@' {
                        verb = true;
                    } else {
                        dollars += 1;
                    }
                    k += 1;
                }
                if self.at(k) == b'"' {
                    self.i = k;
                    if self.at(k + 1) == b'"' && self.at(k + 2) == b'"' {
                        self.cs_raw(start);
                    } else if dollars > 0 {
                        self.i = k + 1;
                        self.interp(start, Lang::CSharp, b'"', false, !verb, verb, verb);
                    } else {
                        self.plain_str(start, Tok::String, false, true, true);
                    }
                    continue;
                }
                if c == b'@' && is_id_start(c1) {
                    self.i = self.ident_end(start + 1);
                    continue;
                }
            }
            if c.is_ascii_digit() || (c == b'.' && c1.is_ascii_digit()) {
                let e = self.number_end(start, fl == Fl::Cpp);
                self.push(start, e, Tok::Number);
                self.i = e;
                continue;
            }
            if is_id_start(c) {
                let e = self.ident_end(start);
                if self.string_prefix(fl, start, e) {
                    continue;
                }
                let (tok, end) = self.classify(fl, start, e, pending, &mut next);
                self.push(start, end, tok);
                self.i = end;
                continue;
            }
            self.op_punct();
        }
    }

    /// Préfixes de chaînes collés à un identificateur (r#"…"#, b"…", L"…", u8R"(…)"…) ; true si traité.
    fn string_prefix(&mut self, fl: Fl, start: usize, e: usize) -> bool {
        let w = &self.s[start..e];
        let q = self.at(e);
        match fl {
            Fl::Rust => {
                if matches!(w, b"r" | b"br" | b"cr") && (q == b'"' || q == b'#') {
                    let mut k = e;
                    while self.at(k) == b'#' && k - e < 255 {
                        k += 1;
                    }
                    if self.at(k) == b'"' {
                        self.i = e;
                        self.rust_raw(start);
                        return true;
                    }
                    if w == b"r" && q == b'#' && is_id_start(self.at(e + 1)) {
                        self.i = self.ident_end(e + 1);
                        return true;
                    }
                }
                if matches!(w, b"b" | b"c") && q == b'"' {
                    self.i = e;
                    self.plain_str(start, Tok::String, true, true, false);
                    return true;
                }
                if w == b"b" && q == b'\'' {
                    self.i = e;
                    self.rust_quote(start);
                    return true;
                }
            }
            Fl::C | Fl::Cpp => {
                if matches!(w, b"L" | b"u" | b"U" | b"u8") && (q == b'"' || q == b'\'') {
                    self.i = e;
                    self.plain_str(start, if q == b'"' { Tok::String } else { Tok::Char }, true, false, false);
                    return true;
                }
                if fl == Fl::Cpp && matches!(w, b"R" | b"LR" | b"uR" | b"UR" | b"u8R") && q == b'"' {
                    self.i = e + 1;
                    self.cpp_raw(start);
                    return true;
                }
            }
            Fl::CSharp => {}
        }
        false
    }

    /// La couleur d'un identificateur `a..e` et sa fin (le `!` d'une macro Rust en fait partie).
    fn classify(&self, fl: Fl, a: usize, e: usize, pending: Next, next: &mut Next) -> (Tok, usize) {
        let s = self.s;
        let w = &s[a..e];
        let call = self.next_sig(e) == b'(';
        let after_colons = a >= 2 && &s[a - 2..a] == b"::";
        let after_dot = a >= 1 && s[a - 1] == b'.';
        let upper = w[0].is_ascii_uppercase();
        let named = match pending {
            Next::Func => Some(Tok::Function),
            Next::Type => Some(Tok::Type),
            Next::None => None,
        };
        let tok = match fl {
            Fl::Rust => {
                if kw(RUST_KW, w) {
                    match w {
                        b"fn" => *next = Next::Func,
                        b"struct" | b"enum" | b"trait" | b"union" | b"type" => *next = Next::Type,
                        _ => {}
                    }
                    return match w {
                        b"true" | b"false" => (Tok::Constant, e),
                        b"Self" => (Tok::Type, e),
                        _ => (Tok::Keyword, e),
                    };
                }
                if self.at(e) == b'!' && self.at(e + 1) != b'=' {
                    return (Tok::Macro, e + 1);
                }
                if kw(RUST_PRIM, w) {
                    Tok::Type
                } else if let Some(t) = named {
                    t
                } else if call && !(upper && !is_caps(w)) {
                    Tok::Function
                } else if is_caps(w) {
                    Tok::Constant
                } else if upper {
                    Tok::Type
                } else {
                    Tok::Text
                }
            }
            Fl::C | Fl::Cpp => {
                let cpp = fl == Fl::Cpp;
                if matches!(w, b"true" | b"false" | b"NULL" | b"nullptr") {
                    Tok::Constant
                } else if kw(C_KW, w) || (cpp && kw(CPP_KW, w)) {
                    if matches!(w, b"struct" | b"union" | b"enum" | b"class" | b"namespace") {
                        *next = Next::Type;
                    }
                    Tok::Keyword
                } else if kw(C_TYPES, w)
                    || (w.len() > 2 && w.ends_with(b"_t"))
                    || (cpp && (kw(CPP_STD, w) || (after_colons && kw(CPP_STD_QUAL, w))))
                {
                    Tok::Type
                } else if let Some(t) = named {
                    t
                } else if call {
                    Tok::Function
                } else if is_caps(w) {
                    Tok::Constant
                } else if cpp && w.len() == 1 && upper {
                    Tok::Type
                } else {
                    Tok::Text
                }
            }
            Fl::CSharp => {
                if kw(CS_TYPES, w) {
                    Tok::Type
                } else if matches!(w, b"true" | b"false" | b"null") {
                    Tok::Constant
                } else if kw(CS_KW, w) {
                    if matches!(w, b"class" | b"struct" | b"interface" | b"enum" | b"record" | b"delegate") {
                        *next = Next::Type;
                    }
                    Tok::Keyword
                } else if let Some(t) = named {
                    t
                } else if call {
                    Tok::Function
                } else if is_caps(w) {
                    Tok::Constant
                } else if upper && !after_dot {
                    Tok::Type
                } else {
                    Tok::Text
                }
            }
        };
        (tok, e)
    }

    /// `'` en Rust : caractère ('x', '\n', b'x') ou durée de vie ('a) ; self.i sur la quote.
    fn rust_quote(&mut self, start: usize) {
        let a = self.i;
        let c1 = self.at(a + 1);
        if c1 == b'\\' {
            let e = self.skip_str(a + 1, b'\'', true, false, false);
            self.str_pieces(start, e, Tok::Char);
            self.i = e;
            return;
        }
        if a + 1 >= self.len() || c1 == b'\n' {
            self.push(a, a + 1, Tok::Punct);
            self.i = a + 1;
            return;
        }
        let cl = self.char_len(a + 1);
        if self.at(a + 1 + cl) == b'\'' {
            self.push(start, a + 2 + cl, Tok::Char);
            self.i = a + 2 + cl;
            return;
        }
        if is_id_start(c1) && start == a {
            let e = self.ident_end(a + 1);
            self.push(a, e, Tok::Lifetime);
            self.i = e;
            return;
        }
        self.push(start, a + 1, Tok::Punct);
        self.i = a + 1;
    }

    /// Chaîne brute r#"…"# ; self.i sur le premier '#' ou le '"'.
    fn rust_raw(&mut self, start: usize) {
        let s = self.s;
        let mut n = 0;
        while self.cur() == b'#' {
            n += 1;
            self.i += 1;
        }
        let mut k = self.i + 1;
        while k < s.len() {
            if s[k] == b'"' && (1..=n).all(|m| self.at(k + m) == b'#') {
                k += 1 + n;
                break;
            }
            k += 1;
        }
        self.i = k.min(s.len());
        self.push(start, self.i, Tok::String);
    }

    /// Chaîne brute C++ R"délim(…)délim" ; self.i suit le '"'.
    fn cpp_raw(&mut self, start: usize) {
        let s = self.s;
        let ds = self.i;
        let mut k = ds;
        while k < s.len() && k - ds < 16 && !matches!(s[k], b'(' | b' ' | b')' | b'\\' | b'\n' | b'"') {
            k += 1;
        }
        if self.at(k) != b'(' {
            let e = self.skip_str(ds, b'"', true, false, false);
            self.str_pieces(start, e, Tok::String);
            self.i = e;
            return;
        }
        let delim = &s[ds..k];
        k += 1;
        while k < s.len() {
            if s[k] == b')' && s[k + 1..].starts_with(delim) && self.at(k + 1 + delim.len()) == b'"' {
                k += 2 + delim.len();
                break;
            }
            k += 1;
        }
        self.i = k.min(s.len());
        self.push(start, self.i, Tok::String);
    }

    /// Chaîne brute C# """…""" (guillemets ≥ 3) ; self.i sur le premier guillemet.
    fn cs_raw(&mut self, start: usize) {
        let mut n = 0;
        while self.cur() == b'"' {
            n += 1;
            self.i += 1;
        }
        while self.i < self.len() {
            if self.cur() == b'"' {
                let mut m = 0;
                while self.cur() == b'"' {
                    m += 1;
                    self.i += 1;
                }
                if m >= n {
                    break;
                }
            } else {
                self.i += 1;
            }
        }
        self.push(start, self.i, Tok::String);
    }

    // ----- assembleur GNU (x86-64 AT&T ou Intel, AArch64)

    fn asm_gnu(&mut self) {
        let mut ls = true;
        let mut stmt = true;
        let mut last = usize::MAX;
        while self.i < self.len() {
            guard!(self, last);
            if self.i >= self.len() {
                break;
            }
            let a = self.i;
            let c = self.cur();
            match c {
                b'\n' => {
                    self.i += 1;
                    ls = true;
                    stmt = true;
                    continue;
                }
                b' ' | b'\t' | b'\r' => {
                    self.i += 1;
                    continue;
                }
                _ => {}
            }
            let line_start = ls;
            ls = false;
            let c1 = self.nx(1);
            if c == b'#' {
                if c1.is_ascii_digit() || (c1 == b'-' && self.nx(2).is_ascii_digit()) {
                    // immédiat AArch64 (#4)
                    let e = self.number_end(a + if c1 == b'-' { 2 } else { 1 }, false);
                    self.push(a, e, Tok::Number);
                    self.i = e;
                    stmt = false;
                    continue;
                }
                if line_start {
                    let k = self.hspace(a + 1);
                    let w = &self.s[k..self.ident_end(k)];
                    if matches!(
                        w,
                        b"include"
                            | b"define"
                            | b"if"
                            | b"ifdef"
                            | b"ifndef"
                            | b"else"
                            | b"elif"
                            | b"endif"
                            | b"undef"
                            | b"error"
                            | b"pragma"
                            | b"line"
                    ) {
                        self.preproc(PpCmt::C);
                        continue;
                    }
                }
                self.line_comment(Tok::Comment);
                continue;
            }
            if c == b'/' && c1 == b'/' {
                self.line_comment(Tok::Comment);
                continue;
            }
            if c == b'/' && c1 == b'*' {
                self.block_comment(b"/*", b"*/", false, Tok::Comment);
                continue;
            }
            if c == b';' {
                self.push(a, a + 1, Tok::Punct);
                self.i += 1;
                stmt = true;
                continue;
            }
            if c == b'"' {
                self.plain_str(a, Tok::String, true, false, false);
                stmt = false;
                continue;
            }
            if c == b'\'' {
                let mut e = if c1 == b'\\' { a + 1 + self.esc_len(a + 1) } else { a + 1 + self.char_len(a + 1) };
                if self.at(e) == b'\'' {
                    e += 1;
                }
                self.push(a, e, Tok::Char);
                self.i = e.min(self.len());
                stmt = false;
                continue;
            }
            if c == b'%' && is_id_start(c1) {
                let e = self.ident_end(a + 1);
                self.push(a, e, Tok::Register);
                self.i = e;
                continue;
            }
            if c == b'$' {
                if c1.is_ascii_digit() || (c1 == b'-' && self.nx(2).is_ascii_digit()) {
                    let e = self.number_end(a + if c1 == b'-' { 2 } else { 1 }, false);
                    self.push(a, e, Tok::Number);
                    self.i = e;
                } else {
                    self.push(a, a + 1, Tok::Operator);
                    self.i += 1;
                }
                continue;
            }
            if c.is_ascii_digit() {
                let e = self.number_end(a, false);
                let w = &self.s[a..e];
                if stmt && self.at(e) == b':' {
                    self.push(a, e + 1, Tok::Label);
                    self.i = e + 1;
                    continue;
                }
                let local = w.len() >= 2
                    && matches!(w[w.len() - 1], b'f' | b'b')
                    && w[..w.len() - 1].iter().all(|c| c.is_ascii_digit());
                self.push(a, e, if local { Tok::Label } else { Tok::Number });
                self.i = e;
                stmt = false;
                continue;
            }
            if (c == b'\\' || c == b'@') && is_id_start(c1) {
                let e = self.ident_end(a + 1);
                self.push(a, e, if c == b'\\' { Tok::Macro } else { Tok::Constant });
                self.i = e;
                continue;
            }
            if is_id_start(c) || (c == b'.' && is_id_start(c1)) {
                let mut e = a + 1;
                while e < self.len() && (is_id(self.s[e]) || matches!(self.s[e], b'.' | b'$')) {
                    e += 1;
                }
                if self.at(e) == b':' && self.at(e + 1) != b':' {
                    self.push(a, e + 1, Tok::Label);
                    self.i = e + 1;
                    continue;
                }
                let mut buf = [0u8; 32];
                let lw = lower(&self.s[a..e], &mut buf);
                let tok = if c == b'.' {
                    stmt = false;
                    Tok::Preproc
                } else if stmt {
                    let n = self.hspace(e);
                    stmt = matches!(
                        lw,
                        b"rep" | b"repe" | b"repz" | b"repne" | b"repnz" | b"lock" | b"data16" | b"addr32"
                    );
                    if self.at(n) == b'=' && self.at(n + 1) != b'=' { Tok::Constant } else { Tok::Keyword }
                } else if is_x86_reg(lw) || is_arm_reg(lw) {
                    Tok::Register
                } else if kw(ASM_SIZE, lw) {
                    Tok::Type
                } else {
                    Tok::Text
                };
                self.push(a, e, tok);
                self.i = e;
                continue;
            }
            if c == b'.' && c1.is_ascii_digit() {
                let e = self.number_end(a, false);
                self.push(a, e, Tok::Number);
                self.i = e;
                continue;
            }
            self.op_punct();
        }
    }

    // ----- FASM / FASMARM

    fn fasm(&mut self) {
        let mut stmt = true;
        let mut next = Next::None;
        let mut last = usize::MAX;
        let fid = |c: u8| is_id(c) || matches!(c, b'.' | b'?' | b'@' | b'$');
        while self.i < self.len() {
            guard!(self, last);
            if self.i >= self.len() {
                break;
            }
            let a = self.i;
            let c = self.cur();
            match c {
                b'\n' => {
                    self.i += 1;
                    stmt = true;
                    continue;
                }
                b' ' | b'\t' | b'\r' => {
                    self.i += 1;
                    continue;
                }
                _ => {}
            }
            let pending = std::mem::replace(&mut next, Next::None);
            let c1 = self.nx(1);
            match c {
                b';' => {
                    self.line_comment(Tok::Comment);
                    continue;
                }
                b'"' | b'\'' => {
                    self.plain_str(a, Tok::String, false, false, true);
                    stmt = false;
                    continue;
                }
                b'#' => {
                    if c1.is_ascii_digit() || (c1 == b'-' && self.nx(2).is_ascii_digit()) {
                        let e = self.number_end(a + if c1 == b'-' { 2 } else { 1 }, false);
                        self.push(a, e, Tok::Number);
                        self.i = e;
                    } else {
                        self.push(a, a + 1, Tok::Operator);
                        self.i += 1;
                    }
                    continue;
                }
                b'$' => {
                    if c1.is_ascii_hexdigit() {
                        let e = self.number_end(a + 1, false);
                        self.push(a, e, Tok::Number);
                        self.i = e;
                    } else {
                        self.push(a, a + 1, Tok::Constant);
                        self.i += 1;
                    }
                    continue;
                }
                b'{' | b'}' => {
                    self.push(a, a + 1, Tok::Punct);
                    self.i += 1;
                    stmt = true;
                    continue;
                }
                _ => {}
            }
            if c.is_ascii_digit() || (c == b'.' && c1.is_ascii_digit()) {
                let e = self.number_end(a, false);
                self.push(a, e, Tok::Number);
                self.i = e;
                stmt = false;
                continue;
            }
            if is_id_start(c) || matches!(c, b'.' | b'?' | b'@') {
                let mut e = a + 1;
                while e < self.len() && fid(self.s[e]) {
                    e += 1;
                }
                if self.at(e) == b':' && self.at(e + 1) != b'=' {
                    self.push(a, e + 1, Tok::Label);
                    self.i = e + 1;
                    continue;
                }
                let mut buf = [0u8; 32];
                let lw = lower(&self.s[a..e], &mut buf);
                let tok = if matches!(lw, b"@b" | b"@f" | b"@r" | b"@@") {
                    Tok::Label
                } else if pending == Next::Func {
                    Tok::Function
                } else if kw(FASM_DIR, lw) {
                    if matches!(lw, b"macro" | b"struc" | b"proc" | b"struct") {
                        next = Next::Func;
                    }
                    Tok::Preproc
                } else if kw(FASM_DATA, lw) || kw(FASM_SIZE, lw) {
                    Tok::Type
                } else if is_x86_reg(lw) || is_arm_reg(lw) {
                    Tok::Register
                } else if stmt {
                    let n = self.hspace(e);
                    let mut ne = n;
                    while ne < self.len() && fid(self.s[ne]) {
                        ne += 1;
                    }
                    let mut b2 = [0u8; 32];
                    let nw = lower(&self.s[n..ne], &mut b2);
                    if (self.at(n) == b'=' && self.at(n + 1) != b'=') || nw == b"equ" {
                        Tok::Constant
                    } else if !nw.is_empty() && kw(FASM_DATA, nw) {
                        Tok::Label
                    } else {
                        Tok::Keyword
                    }
                } else if is_caps(&self.s[a..e]) {
                    Tok::Constant
                } else {
                    Tok::Text
                };
                stmt = false;
                self.push(a, e, tok);
                self.i = e;
                continue;
            }
            self.op_punct();
        }
    }

    // ----- FreeBASIC

    fn fbasic(&mut self) {
        let mut ls = true;
        let mut stmt = true;
        let mut next = Next::None;
        let mut end_exit = false;
        let mut last = usize::MAX;
        while self.i < self.len() {
            guard!(self, last);
            if self.i >= self.len() {
                break;
            }
            let a = self.i;
            let c = self.cur();
            match c {
                b'\n' => {
                    self.i += 1;
                    ls = true;
                    stmt = true;
                    continue;
                }
                b' ' | b'\t' | b'\r' => {
                    self.i += 1;
                    continue;
                }
                _ => {}
            }
            let line_start = ls;
            ls = false;
            let pending = std::mem::replace(&mut next, Next::None);
            let was_end = std::mem::replace(&mut end_exit, false);
            let c1 = self.nx(1);
            if c == b'\'' {
                self.line_comment(Tok::Comment);
                continue;
            }
            if c == b'/' && c1 == b'\'' {
                self.block_comment(b"/'", b"'/", true, Tok::Comment);
                continue;
            }
            if c == b'#' && line_start {
                self.preproc(PpCmt::Basic);
                continue;
            }
            if c == b'"' {
                self.plain_str(a, Tok::String, false, false, true);
                stmt = false;
                continue;
            }
            if (c == b'!' || c == b'$') && c1 == b'"' {
                self.i = a + 1;
                self.plain_str(a, Tok::String, c == b'!', false, c == b'$');
                stmt = false;
                continue;
            }
            if c == b'&' && matches!(c1 | 0x20, b'h' | b'o' | b'b') && self.nx(2).is_ascii_alphanumeric() {
                let mut e = a + 2;
                while self.at(e).is_ascii_alphanumeric() {
                    e += 1;
                }
                self.push(a, e, Tok::Number);
                self.i = e;
                continue;
            }
            if c.is_ascii_digit() || (c == b'.' && c1.is_ascii_digit()) {
                let mut e = self.number_end(a, false);
                if matches!(self.at(e), b'%' | b'!' | b'#') && !is_id(self.at(e + 1)) {
                    e += 1;
                }
                self.push(a, e, Tok::Number);
                self.i = e;
                stmt = false;
                continue;
            }
            if c == b':' {
                self.push(a, a + 1, Tok::Punct);
                self.i += 1;
                stmt = true;
                continue;
            }
            if is_id_start(c) {
                let mut e = self.ident_end(a);
                // suffixe de type : Left$, n%, x!, d#
                if matches!(self.at(e), b'$' | b'%' | b'!' | b'#') && self.at(e + 1) != b'"' {
                    e += 1;
                }
                let mut buf = [0u8; 32];
                let lw = lower(&self.s[a..e], &mut buf);
                if stmt && lw == b"rem" {
                    self.line_comment(Tok::Comment);
                    continue;
                }
                if line_start && self.at(e) == b':' && self.at(e + 1) != b'=' && !kw(FB_KW, lw) {
                    self.push(a, e + 1, Tok::Label);
                    self.i = e + 1;
                    continue;
                }
                let tok = if kw(FB_TYPES, lw) {
                    Tok::Type
                } else if matches!(lw, b"true" | b"false") {
                    Tok::Constant
                } else if kw(FB_KW, lw) {
                    if !was_end {
                        if matches!(
                            lw,
                            b"sub" | b"function" | b"property" | b"constructor" | b"destructor" | b"operator"
                        ) {
                            next = Next::Func;
                        } else if matches!(lw, b"type" | b"enum" | b"union" | b"class" | b"namespace") {
                            next = Next::Type;
                        }
                    }
                    end_exit = matches!(lw, b"end" | b"exit");
                    Tok::Keyword
                } else if pending == Next::Func {
                    Tok::Function
                } else if pending == Next::Type {
                    Tok::Type
                } else if self.next_sig(e) == b'(' {
                    Tok::Function
                } else if is_caps(&self.s[a..e]) {
                    Tok::Constant
                } else if stmt && {
                    // appel sans parenthèses en tête d'instruction : « TcSetBlock x, y »
                    let n = self.next_sig(e);
                    matches!(n, 0 | b'\n' | b'\r' | b'\'' | b'"') || is_id_start(n) || n.is_ascii_digit()
                } {
                    Tok::Function
                } else {
                    Tok::Text
                };
                stmt = false;
                self.push(a, e, tok);
                self.i = e;
                continue;
            }
            self.op_punct();
        }
    }

    // ----- Python

    fn python(&mut self) {
        let mut ls = true;
        let mut next = Next::None;
        let mut last = usize::MAX;
        while self.i < self.len() {
            guard!(self, last);
            if self.i >= self.len() {
                break;
            }
            let a = self.i;
            let c = self.cur();
            match c {
                b'\n' => {
                    self.i += 1;
                    ls = true;
                    continue;
                }
                b' ' | b'\t' | b'\r' | 0x0c => {
                    self.i += 1;
                    continue;
                }
                _ => {}
            }
            let line_start = ls;
            ls = false;
            let pending = std::mem::replace(&mut next, Next::None);
            let c1 = self.nx(1);
            if c == b'#' {
                self.line_comment(Tok::Comment);
                continue;
            }
            if c == b'@' && line_start && is_id_start(c1) {
                let mut e = a + 1;
                while is_id(self.at(e)) || (self.at(e) == b'.' && is_id_start(self.at(e + 1))) {
                    e += 1;
                }
                self.push(a, e, Tok::Attribute);
                self.i = e;
                continue;
            }
            if c == b'"' || c == b'\'' {
                self.py_string(a, a, false, false, line_start);
                continue;
            }
            if is_id_start(c) {
                let e = self.ident_end(a);
                let q = self.at(e);
                if e - a <= 2 && (q == b'"' || q == b'\'') {
                    let mut buf = [0u8; 32];
                    let p = lower(&self.s[a..e], &mut buf);
                    if matches!(p, b"r" | b"b" | b"u" | b"f" | b"rb" | b"br" | b"fr" | b"rf") {
                        let raw = p.contains(&b'r');
                        let fmt = p.contains(&b'f');
                        self.py_string(a, e, raw, fmt, false);
                        continue;
                    }
                }
                let w = &self.s[a..e];
                let tok = if matches!(w, b"True" | b"False" | b"None") {
                    Tok::Constant
                } else if kw(PY_KW, w) {
                    match w {
                        b"def" => next = Next::Func,
                        b"class" => next = Next::Type,
                        _ => {}
                    }
                    Tok::Keyword
                } else if kw(PY_TYPES, w) {
                    Tok::Type
                } else if pending == Next::Func {
                    Tok::Function
                } else if pending == Next::Type {
                    Tok::Type
                } else if self.next_sig(e) == b'(' {
                    Tok::Function
                } else if is_caps(w) {
                    Tok::Constant
                } else if w[0].is_ascii_uppercase() {
                    Tok::Type
                } else {
                    Tok::Text
                };
                self.push(a, e, tok);
                self.i = e;
                continue;
            }
            if c.is_ascii_digit() || (c == b'.' && c1.is_ascii_digit()) {
                let e = self.number_end(a, false);
                self.push(a, e, Tok::Number);
                self.i = e;
                continue;
            }
            self.op_punct();
        }
    }

    /// Chaîne Python ; `qpos` sur le guillemet (après un préfixe éventuel).
    fn py_string(&mut self, start: usize, qpos: usize, raw: bool, fmt: bool, doc_pos: bool) {
        let q = self.at(qpos);
        let triple = self.at(qpos + 1) == q && self.at(qpos + 2) == q;
        self.i = qpos + if triple { 3 } else { 1 };
        if fmt {
            self.interp(start, Lang::Python, q, triple, !raw, triple, false);
            return;
        }
        let s = self.s;
        let mut k = self.i;
        let mut e = s.len();
        while k < s.len() {
            let c = s[k];
            if c == b'\\' {
                k += 1;
                if k < s.len() {
                    k += self.char_len(k);
                }
                continue;
            }
            if c == q {
                if !triple {
                    e = k + 1;
                    break;
                }
                if self.at(k + 1) == q && self.at(k + 2) == q {
                    e = k + 3;
                    break;
                }
            }
            if c == b'\n' && !triple {
                e = k;
                break;
            }
            k += 1;
        }
        let e = e.min(s.len());
        let tok = if triple && doc_pos { Tok::DocComment } else { Tok::String };
        if raw || tok == Tok::DocComment {
            self.push(start, e, tok);
        } else {
            self.str_pieces(start, e, tok);
        }
        self.i = e;
    }

    // ----- TOML

    fn toml(&mut self) {
        let mut stack: Vec<u8> = Vec::new();
        let mut key = true;
        let mut ls = true;
        let mut last = usize::MAX;
        while self.i < self.len() {
            guard!(self, last);
            if self.i >= self.len() {
                break;
            }
            let a = self.i;
            let c = self.cur();
            match c {
                b'\n' => {
                    self.i += 1;
                    ls = true;
                    if stack.is_empty() {
                        key = true;
                    }
                    continue;
                }
                b' ' | b'\t' | b'\r' => {
                    self.i += 1;
                    continue;
                }
                _ => {}
            }
            let line_start = ls;
            ls = false;
            if c == b'#' {
                self.line_comment(Tok::Comment);
                continue;
            }
            if line_start && stack.is_empty() && c == b'[' {
                let le = self.eol(a);
                let close: &[u8] = if self.nx(1) == b'[' { b"]]" } else { b"]" };
                let e = match self.find(a + 1, close) {
                    Some(p) if p < le => p + close.len(),
                    _ => le,
                };
                self.push(a, e, Tok::Type);
                self.i = e;
                key = false;
                continue;
            }
            if key {
                if c.is_ascii_alphanumeric() || c == b'_' || c == b'-' {
                    let mut e = a;
                    while self.at(e).is_ascii_alphanumeric() || matches!(self.at(e), b'_' | b'-') {
                        e += 1;
                    }
                    self.push(a, e, Tok::AttrName);
                    self.i = e;
                    continue;
                }
                match c {
                    b'"' | b'\'' => {
                        let e = self.skip_str(a + 1, c, c == b'"', false, false);
                        self.push(a, e, Tok::AttrName);
                        self.i = e;
                        continue;
                    }
                    b'.' => {
                        self.push(a, a + 1, Tok::Punct);
                        self.i += 1;
                        continue;
                    }
                    b'=' => {
                        self.push(a, a + 1, Tok::Operator);
                        self.i += 1;
                        key = false;
                        continue;
                    }
                    _ => key = false,
                }
            }
            match c {
                b'"' | b'\'' => {
                    if self.nx(1) == c && self.nx(2) == c {
                        let pat: &[u8] = if c == b'"' { b"\"\"\"" } else { b"'''" };
                        let mut k = a + 3;
                        let e = loop {
                            if k >= self.len() {
                                break self.len();
                            }
                            if self.s[k] == b'\\' && c == b'"' {
                                k += self.esc_len(k);
                                continue;
                            }
                            if self.starts(k, pat) {
                                break k + 3;
                            }
                            k += 1;
                        };
                        if c == b'"' {
                            self.str_pieces(a, e, Tok::String);
                        } else {
                            self.push(a, e, Tok::String);
                        }
                        self.i = e;
                    } else {
                        self.plain_str(a, Tok::String, c == b'"', false, false);
                    }
                }
                b'[' | b'{' => {
                    stack.push(c);
                    key = c == b'{';
                    self.push(a, a + 1, Tok::Punct);
                    self.i += 1;
                }
                b']' | b'}' => {
                    stack.pop();
                    self.push(a, a + 1, Tok::Punct);
                    self.i += 1;
                }
                b',' => {
                    if stack.last() == Some(&b'{') {
                        key = true;
                    }
                    self.push(a, a + 1, Tok::Punct);
                    self.i += 1;
                }
                _ if c.is_ascii_digit()
                    || ((c == b'+' || c == b'-') && matches!(self.nx(1), b'0'..=b'9' | b'i' | b'n')) =>
                {
                    let mut e = a + 1;
                    while self.at(e).is_ascii_alphanumeric() || matches!(self.at(e), b'_' | b'.' | b':' | b'+' | b'-') {
                        e += 1;
                    }
                    self.push(a, e, Tok::Number);
                    self.i = e;
                }
                _ if is_id_start(c) => {
                    let e = self.ident_end(a);
                    let tok = match &self.s[a..e] {
                        b"true" | b"false" => Tok::Constant,
                        b"inf" | b"nan" => Tok::Number,
                        _ => Tok::Text,
                    };
                    self.push(a, e, tok);
                    self.i = e;
                }
                _ => self.op_punct(),
            }
        }
    }

    // ----- JSON (et JSONC : commentaires acceptés)

    fn json(&mut self) {
        let mut last = usize::MAX;
        while self.i < self.len() {
            guard!(self, last);
            if self.i >= self.len() {
                break;
            }
            let a = self.i;
            let c = self.cur();
            match c {
                b' ' | b'\t' | b'\r' | b'\n' => self.i += 1,
                b'"' => {
                    let e = self.skip_str(a + 1, b'"', true, false, false);
                    let mut k = e;
                    while matches!(self.at(k), b' ' | b'\t' | b'\r' | b'\n') {
                        k += 1;
                    }
                    if self.at(k) == b':' {
                        self.push(a, e, Tok::AttrName);
                    } else {
                        self.str_pieces(a, e, Tok::String);
                    }
                    self.i = e;
                }
                b'/' if self.nx(1) == b'/' => self.line_comment(Tok::Comment),
                b'/' if self.nx(1) == b'*' => self.block_comment(b"/*", b"*/", false, Tok::Comment),
                b'-' | b'0'..=b'9' => {
                    let e = self.number_end(if c == b'-' { a + 1 } else { a }, false).max(a + 1);
                    self.push(a, e, Tok::Number);
                    self.i = e;
                }
                b'{' | b'}' | b'[' | b']' | b':' | b',' => {
                    self.push(a, a + 1, Tok::Punct);
                    self.i += 1;
                }
                _ if is_id_start(c) => {
                    let e = self.ident_end(a);
                    let tok = match &self.s[a..e] {
                        b"true" | b"false" | b"null" => Tok::Constant,
                        _ => Tok::Text,
                    };
                    self.push(a, e, tok);
                    self.i = e;
                }
                _ => self.bump(),
            }
        }
    }

    // ----- Markdown

    fn markdown(&mut self) {
        let s = self.s;
        let len = s.len();
        let mut i = self.i;
        let mut prev_para = false;
        while i < len {
            let ls = i;
            let le = self.eol(i);
            let nl = (le + 1).min(len);
            let mut p = ls;
            while p < le && s[p] == b' ' && p - ls < 4 {
                p += 1;
            }
            let ind = p - ls;
            let t = &s[p..le];
            let tt = trim_end(t);
            if tt.is_empty() {
                prev_para = false;
                i = nl;
                continue;
            }
            let c0 = t[0];
            // bloc de code ``` ou ~~~
            if ind <= 3 && (c0 == b'`' || c0 == b'~') {
                let n = t.iter().take_while(|&&c| c == c0).count();
                let info = &t[n..];
                if n >= 3 && !(c0 == b'`' && info.contains(&b'`')) {
                    self.push(p, p + n, Tok::Punct);
                    let ia = p + n + info.iter().take_while(|c| c.is_ascii_whitespace()).count();
                    let ie = p + n + trim_end(info).len();
                    self.push(ia, ie, Tok::Keyword);
                    let word_end =
                        s[ia..ie.max(ia)].iter().position(|&c| !(is_id(c) || matches!(c, b'+' | b'#' | b'-')));
                    let word = &s[ia..word_end.map_or(ie.max(ia), |w| ia + w)];
                    let lang = std::str::from_utf8(word).ok().and_then(Lang::from_name);
                    let cs = nl;
                    let mut k = nl;
                    let mut close = None;
                    while k < len {
                        let le2 = self.eol(k);
                        let mut q = k;
                        while q < le2 && s[q] == b' ' && q - k < 4 {
                            q += 1;
                        }
                        let m = s[q..le2].iter().take_while(|&&c| c == c0).count();
                        if q - k <= 3 && m >= n && trim_end(&s[q + m..le2]).is_empty() {
                            close = Some((k, q, q + m, le2));
                            break;
                        }
                        k = le2 + 1;
                    }
                    let ce = close.map_or(len, |c| c.0);
                    match lang {
                        Some(l) if l != Lang::Plain => self.sub(l, cs, ce, Tok::String),
                        _ => self.push(cs, ce, Tok::String),
                    }
                    match close {
                        Some((_, q0, q1, le2)) => {
                            self.push(q0, q1, Tok::Punct);
                            i = (le2 + 1).min(len);
                        }
                        None => i = len,
                    }
                    prev_para = false;
                    continue;
                }
            }
            // titre #
            if ind <= 3 && c0 == b'#' {
                let h = t.iter().take_while(|&&c| c == b'#').count();
                if h <= 6 && (h == t.len() || matches!(t[h], b' ' | b'\t' | b'\r')) {
                    self.push(p, le, Tok::Heading);
                    prev_para = false;
                    i = nl;
                    continue;
                }
            }
            // soulignement (titre setext) ou filet
            if ind <= 3 && prev_para && (c0 == b'=' || c0 == b'-') && tt.iter().all(|&c| c == c0) {
                self.push(p, le, Tok::Heading);
                prev_para = false;
                i = nl;
                continue;
            }
            if ind <= 3 && matches!(c0, b'*' | b'-' | b'_') {
                let n = tt.iter().filter(|&&c| c == c0).count();
                if n >= 3 && tt.iter().all(|&c| c == c0 || c == b' ' || c == b'\t') {
                    self.push(p, le, Tok::Punct);
                    prev_para = false;
                    i = nl;
                    continue;
                }
            }
            // commentaire HTML, éventuellement sur plusieurs lignes
            if t.starts_with(b"<!--") {
                let end = self.find(p + 4, b"-->").map_or(len, |e| e + 3);
                self.push(p, end, Tok::Comment);
                let le2 = self.eol(end);
                self.md_inline(end, le2);
                i = (le2 + 1).min(len);
                prev_para = false;
                continue;
            }
            // définition de lien [x]: url
            if c0 == b'[' && self.find(p, b"]:").is_some_and(|e| e < le) {
                self.push(p, le, Tok::Link);
                i = nl;
                continue;
            }
            // citations et puces
            let mut k = p;
            while self.at(k) == b'>' && k < le {
                self.push(k, k + 1, Tok::Punct);
                k = self.hspace(k + 1);
            }
            let m = self.at(k);
            if matches!(m, b'-' | b'*' | b'+') && matches!(self.at(k + 1), b' ' | b'\t') && k + 1 < le {
                self.push(k, k + 1, Tok::Punct);
                k = self.hspace(k + 1);
            } else if m.is_ascii_digit() {
                let mut d = k;
                while self.at(d).is_ascii_digit() && d - k < 9 {
                    d += 1;
                }
                if matches!(self.at(d), b'.' | b')') && matches!(self.at(d + 1), b' ' | b'\t') && d + 1 < le {
                    self.push(k, d + 1, Tok::Punct);
                    k = self.hspace(d + 1);
                }
            }
            if self.at(k) == b'['
                && matches!(self.at(k + 1), b' ' | b'x' | b'X')
                && self.at(k + 2) == b']'
                && k + 3 <= le
            {
                self.push(k, k + 3, Tok::Punct);
                k += 3;
            }
            self.md_inline(k.min(le), le);
            prev_para = true;
            i = nl;
        }
        self.i = len;
    }

    /// Le texte d'une ligne de Markdown : code, emphase, liens, balises, échappements.
    /// Chaque recherche de fermeture est mémorisée par sorte : temps linéaire même sur une ligne hostile.
    fn md_inline(&mut self, a: usize, e: usize) {
        let s = self.s;
        let e = e.min(s.len());
        // (départ de la recherche, résultat) par sorte ; usize::MAX = jamais cherché / rien trouvé
        let mut memo = [(usize::MAX, usize::MAX); 24];
        let mut find = |kind: usize, from: usize, pred: &dyn Fn(usize) -> bool| -> Option<usize> {
            let (f0, r0) = memo[kind];
            if f0 != usize::MAX && from >= f0 {
                if r0 == usize::MAX {
                    return None;
                }
                if r0 >= from {
                    return Some(r0);
                }
            }
            let mut j = from;
            while j < e {
                if pred(j) {
                    memo[kind] = (from, j);
                    return Some(j);
                }
                j += 1;
            }
            memo[kind] = (from, usize::MAX);
            None
        };
        let at = |j: usize| if j < e { s[j] } else { 0 };
        let run = |j: usize, c: u8| {
            let mut k = j;
            while k < e && s[k] == c {
                k += 1;
            }
            k - j
        };
        let mut k = a;
        while k < e {
            let c = s[k];
            match c {
                b'\\' if at(k + 1).is_ascii_punctuation() => {
                    self.push(k, k + 2, Tok::Escape);
                    k += 2;
                }
                b'`' => {
                    let n = run(k, b'`');
                    if n <= 8 {
                        let pred = |j: usize| s[j] == b'`' && s[j - 1] != b'`' && run(j, b'`') == n;
                        if let Some(j) = find(n - 1, k + n, &pred) {
                            self.push(k, j + n, Tok::String);
                            k = j + n;
                            continue;
                        }
                    }
                    k += n;
                }
                b'*' | b'_' | b'~' => {
                    let r = run(k, c);
                    let n = if c == b'~' { 2 } else { r.min(3) };
                    let flank = k + r < e && !at(k + r).is_ascii_whitespace();
                    let word_ok = c != b'_' || k == 0 || !is_id(s[k - 1]);
                    if (c == b'~' && r != 2) || !flank || !word_ok {
                        k += r;
                        continue;
                    }
                    let kind = 8 + match c {
                        b'*' => n - 1,
                        b'_' => 3 + n - 1,
                        _ => 6,
                    };
                    let pred = |j: usize| {
                        j + n <= e
                            && s[j..j + n].iter().all(|&x| x == c)
                            && !s[j - 1].is_ascii_whitespace()
                            && (c != b'_' || !is_id(at(j + n)))
                    };
                    match find(kind, k + r, &pred) {
                        Some(j) => {
                            self.push(k, j + n, Tok::Emphasis);
                            k = j + n;
                        }
                        None => k += r,
                    }
                }
                b'[' | b'!' if c == b'[' || at(k + 1) == b'[' => {
                    let b = if c == b'!' { k + 1 } else { k };
                    let close = find(15, b + 1, &|j| s[j] == b']');
                    let mut done = false;
                    if let Some(r) = close {
                        let o = at(r + 1);
                        if o == b'(' || o == b'[' {
                            let cc = if o == b'(' { b')' } else { b']' };
                            let kind = if o == b'(' { 16 } else { 17 };
                            if let Some(p) = find(kind, r + 2, &|j| s[j] == cc) {
                                self.push(k, p + 1, Tok::Link);
                                k = p + 1;
                                done = true;
                            }
                        }
                    }
                    if !done {
                        k = b + 1;
                    }
                }
                b'<' => {
                    let n1 = at(k + 1);
                    if n1 == b'!' && s[k..e].starts_with(b"<!--") {
                        let pred = |j: usize| s[j..e].starts_with(b"-->");
                        if let Some(j) = find(18, k + 4, &pred) {
                            self.push(k, j + 3, Tok::Comment);
                            k = j + 3;
                            continue;
                        }
                    } else if (n1.is_ascii_alphabetic() || n1 == b'/')
                        && let Some(j) = find(19, k + 1, &|j| s[j] == b'>')
                    {
                        let inner = &s[k + 1..j];
                        let link = !inner.contains(&b' ') && (inner.contains(&b':') || inner.contains(&b'@'));
                        self.push(k, j + 1, if link { Tok::Link } else { Tok::Tag });
                        k = j + 1;
                        continue;
                    }
                    k += 1;
                }
                b'h' if (k == a || !is_id(s[k - 1]))
                    && (s[k..e].starts_with(b"http://") || s[k..e].starts_with(b"https://")) =>
                {
                    let mut j = k;
                    while j < e && !s[j].is_ascii_whitespace() && !matches!(s[j], b')' | b'>' | b']' | b'"') {
                        j += 1;
                    }
                    self.push(k, j, Tok::Link);
                    k = j;
                }
                _ => k += self.char_len(k),
            }
        }
    }

    // ----- Shell

    fn shell(&mut self) {
        let mut cmd = true;
        let mut fn_name = false;
        let mut docs: Vec<(usize, usize, bool)> = Vec::new();
        let mut last = usize::MAX;
        while self.i < self.len() {
            guard!(self, last);
            if self.i >= self.len() {
                break;
            }
            let s = self.s;
            let a = self.i;
            let c = s[a];
            let c1 = self.nx(1);
            match c {
                b'\n' => {
                    self.i += 1;
                    if !docs.is_empty() {
                        self.heredoc_bodies(&mut docs);
                    }
                    cmd = true;
                    continue;
                }
                b' ' | b'\t' | b'\r' => {
                    self.i += 1;
                    continue;
                }
                _ => {}
            }
            let word_start = a == 0 || matches!(s[a - 1], b' ' | b'\t' | b'\n' | b';' | b'&' | b'|' | b'(' | b')');
            match c {
                b'#' if word_start => {
                    self.line_comment(if a == 0 && c1 == b'!' { Tok::Preproc } else { Tok::Comment });
                    continue;
                }
                b'\\' => {
                    let l = if a + 1 < s.len() { 1 + self.char_len(a + 1) } else { 1 };
                    self.push(a, a + l, Tok::Escape);
                    self.i = (a + l).min(s.len());
                    continue;
                }
                b'\'' => {
                    let e = self.skip_str(a + 1, b'\'', false, true, false);
                    self.push(a, e, Tok::String);
                    self.i = e;
                    cmd = false;
                    continue;
                }
                b'"' => {
                    self.sh_dq(a);
                    cmd = false;
                    continue;
                }
                b'$' => {
                    if self.sh_dollar(false) {
                        cmd = true;
                    }
                    continue;
                }
                b'`' => {
                    self.push(a, a + 1, Tok::Punct);
                    self.i += 1;
                    cmd = true;
                    continue;
                }
                b'<' if c1 == b'<' && self.nx(2) != b'<' => {
                    let mut k = a + 2;
                    let tabs = self.at(k) == b'-';
                    if tabs {
                        k += 1;
                    }
                    self.push(a, k, Tok::Operator);
                    k = self.hspace(k);
                    let q = self.at(k);
                    if q == b'\'' || q == b'"' {
                        let le = self.eol(k);
                        let d1 = s[k + 1..le].iter().position(|&x| x == q).map_or(le, |p| k + 1 + p);
                        self.push(k, (d1 + 1).min(le), Tok::String);
                        if d1 > k + 1 {
                            docs.push((k + 1, d1, tabs));
                        }
                        self.i = (d1 + 1).min(le).max(k + 1);
                    } else {
                        let mut d1 = k;
                        while d1 < s.len() && (is_id(s[d1]) || matches!(s[d1], b'-' | b'.')) {
                            d1 += 1;
                        }
                        if d1 > k {
                            self.push(k, d1, Tok::Label);
                            docs.push((k, d1, tabs));
                        }
                        self.i = d1.max(a + 2);
                    }
                    continue;
                }
                b'|' | b'&' | b';' | b'<' | b'>' => {
                    self.push(a, a + 1, Tok::Operator);
                    self.i += 1;
                    cmd = c != b'<' && c != b'>';
                    continue;
                }
                b'(' | b')' => {
                    self.push(a, a + 1, Tok::Punct);
                    self.i += 1;
                    cmd = c == b'(';
                    continue;
                }
                _ => {}
            }
            // affectation NOM=valeur
            if cmd && is_id_start(c) && c < 0x80 {
                let ie = self.ident_end(a);
                let op = if self.at(ie) == b'=' {
                    1
                } else if self.at(ie) == b'+' && self.at(ie + 1) == b'=' {
                    2
                } else {
                    0
                };
                if op > 0 {
                    self.push(a, ie, Tok::Constant);
                    self.push(ie, ie + op, Tok::Operator);
                    self.i = ie + op;
                    continue;
                }
            }
            let mut e = a;
            while e < s.len()
                && !matches!(
                    s[e],
                    b' ' | b'\t'
                        | b'\r'
                        | b'\n'
                        | b'\''
                        | b'"'
                        | b'`'
                        | b'$'
                        | b'\\'
                        | b'|'
                        | b'&'
                        | b';'
                        | b'('
                        | b')'
                        | b'<'
                        | b'>'
                )
            {
                e += 1;
            }
            if e == a {
                self.bump();
                continue;
            }
            let w = &s[a..e];
            let tok = if fn_name {
                fn_name = false;
                Tok::Function
            } else if cmd && kw(SH_KW, w) {
                fn_name = w == b"function";
                cmd = matches!(
                    w,
                    b"!" | b"do" | b"elif" | b"else" | b"if" | b"then" | b"time" | b"until" | b"while" | b"{"
                );
                Tok::Keyword
            } else if w == b"in" {
                cmd = false;
                Tok::Keyword
            } else if cmd && kw(SH_BUILTINS, w) {
                cmd = false;
                Tok::Function
            } else if w.iter().all(|c| c.is_ascii_digit()) {
                cmd = false;
                Tok::Number
            } else if cmd && self.at(e) == b'(' && self.at(e + 1) == b')' {
                Tok::Function
            } else {
                cmd = false;
                Tok::Text
            };
            self.push(a, e, tok);
            self.i = e;
        }
    }

    /// Chaîne "…" du shell, avec ses $VARIABLES ; self.i sur le guillemet.
    fn sh_dq(&mut self, start: usize) {
        self.i += 1;
        let mut seg = start;
        while self.i < self.len() {
            let c = self.cur();
            if c == b'"' {
                self.i += 1;
                break;
            }
            if c == b'\\' {
                self.push(seg, self.i, Tok::String);
                let l = if self.i + 1 < self.len() { 1 + self.char_len(self.i + 1) } else { 1 };
                self.push(self.i, self.i + l, Tok::Escape);
                self.i = (self.i + l).min(self.len());
                seg = self.i;
                continue;
            }
            if c == b'$' {
                let n = self.nx(1);
                if is_id_start(n)
                    || n.is_ascii_digit()
                    || matches!(n, b'{' | b'(' | b'@' | b'*' | b'#' | b'?' | b'$' | b'!' | b'-')
                {
                    self.push(seg, self.i, Tok::String);
                    self.sh_dollar(true);
                    seg = self.i;
                    continue;
                }
            }
            self.i += 1;
        }
        self.push(seg, self.i, Tok::String);
    }

    /// `$…` : variable, ${…}, $(…) ; rend true si une commande commence ensuite ($( hors chaîne).
    fn sh_dollar(&mut self, in_str: bool) -> bool {
        let s = self.s;
        let a = self.i;
        let c1 = self.at(a + 1);
        let matching = |open: u8, close: u8| {
            let mut depth = 0usize;
            let mut k = a + 1;
            while k < s.len() {
                if s[k] == open {
                    depth += 1;
                } else if s[k] == close {
                    depth -= 1;
                    if depth == 0 {
                        return k + 1;
                    }
                } else if s[k] == b'\\' {
                    k += 1;
                }
                k += 1;
            }
            s.len()
        };
        if c1 == b'{' {
            let e = matching(b'{', b'}');
            self.push(a, e, Tok::Constant);
            self.i = e;
        } else if c1 == b'(' {
            if in_str {
                let e = matching(b'(', b')');
                self.push(a, e, Tok::Constant);
                self.i = e;
            } else {
                let l = if self.at(a + 2) == b'(' { 3 } else { 2 };
                self.push(a, a + l, Tok::Punct);
                self.i = a + l;
                return true;
            }
        } else if is_id_start(c1) && c1 < 0x80 {
            let e = self.ident_end(a + 1);
            self.push(a, e, Tok::Constant);
            self.i = e;
        } else if c1.is_ascii_digit() || matches!(c1, b'@' | b'*' | b'#' | b'?' | b'$' | b'!' | b'-') {
            self.push(a, a + 2, Tok::Constant);
            self.i = a + 2;
        } else if c1 == b'\'' && !in_str {
            let e = self.skip_str(a + 2, b'\'', true, true, false);
            self.str_pieces(a, e, Tok::String);
            self.i = e;
        } else if c1 == b'"' && !in_str {
            self.push(a, a + 1, Tok::String);
            self.i = a + 1;
            self.sh_dq(a + 1);
        } else {
            self.push(a, a + 1, Tok::Punct);
            self.i = a + 1;
        }
        false
    }

    /// Corps des documents en ligne (<<EOF) en attente ; self.i au début de la ligne qui suit.
    fn heredoc_bodies(&mut self, docs: &mut Vec<(usize, usize, bool)>) {
        let s = self.s;
        for (d0, d1, tabs) in docs.drain(..) {
            let delim = &s[d0..d1];
            let body = self.i;
            loop {
                if self.i >= s.len() {
                    self.push(body, s.len(), Tok::String);
                    break;
                }
                let le = self.eol(self.i);
                let mut k = self.i;
                if tabs {
                    while self.at(k) == b'\t' {
                        k += 1;
                    }
                }
                let line = &s[k.min(le)..le];
                let line = line.strip_suffix(b"\r").unwrap_or(line);
                if line == delim {
                    self.push(body, self.i, Tok::String);
                    self.push(k, le, Tok::Label);
                    self.i = (le + 1).min(s.len());
                    break;
                }
                self.i = (le + 1).min(s.len());
            }
        }
    }

    // ----- Makefile

    fn make(&mut self) {
        let s = self.s;
        let len = s.len();
        let mut i = self.i;
        let mut cont = 0u8; // ligne précédente continuée : 1 recette, 2 commentaire, 3 autre
        while i < len {
            let ls = i;
            let le = self.eol(i);
            let nl = (le + 1).min(len);
            let body = trim_end(&s[ls..le]);
            let ends_bs = body.last() == Some(&b'\\');
            let kind = if cont == 2 {
                self.push(ls, le, Tok::Comment);
                2
            } else if cont == 1 || s.get(ls) == Some(&b'\t') {
                self.mk_text(ls, le, true);
                1
            } else {
                let p = self.hspace(ls);
                if p >= le {
                    0
                } else if s[p] == b'#' {
                    self.push(p, le, Tok::Comment);
                    2
                } else if cont == 3 {
                    self.mk_text(ls, le, false);
                    3
                } else {
                    self.mk_line(p, le);
                    3
                }
            };
            cont = if ends_bs { kind } else { 0 };
            i = nl;
        }
        self.i = len;
    }

    /// Une ligne de Makefile hors recette : directive, affectation ou règle.
    fn mk_line(&mut self, p: usize, le: usize) {
        let s = self.s;
        let mut we = p;
        while we < le && !s[we].is_ascii_whitespace() {
            we += 1;
        }
        if kw(MAKE_DIR, &s[p..we]) {
            self.push(p, we, Tok::Keyword);
            self.mk_text(we, le, false);
            return;
        }
        let mut k = p;
        let mut depth = 0usize;
        let mut op = None;
        while k < le {
            let c = s[k];
            if c == b'$' && matches!(self.at(k + 1), b'(' | b'{') {
                depth += 1;
                k += 2;
                continue;
            }
            if depth > 0 {
                match c {
                    b')' | b'}' => depth -= 1,
                    b'(' | b'{' => depth += 1,
                    _ => {}
                }
                k += 1;
                continue;
            }
            match c {
                b'#' => break,
                b'=' => {
                    op = Some((k, k + 1, true));
                    break;
                }
                b':' => {
                    op = Some(if self.at(k + 1) == b'=' {
                        (k, k + 2, true)
                    } else if self.at(k + 1) == b':' && self.at(k + 2) == b'=' {
                        (k, k + 3, true)
                    } else if self.at(k + 1) == b':' {
                        (k, k + 2, false)
                    } else {
                        (k, k + 1, false)
                    });
                    break;
                }
                b'?' | b'+' | b'!' if self.at(k + 1) == b'=' => {
                    op = Some((k, k + 2, true));
                    break;
                }
                _ => {}
            }
            k += 1;
        }
        match op {
            Some((o0, o1, true)) => {
                let name_end = p + trim_end(&s[p..o0]).len();
                self.push(p, name_end, Tok::Constant);
                self.push(o0, o1, Tok::Operator);
                self.mk_text(o1, le, false);
            }
            Some((o0, o1, false)) => {
                let mut k = p;
                while k < o0 {
                    if s[k] == b'$' {
                        k = self.mk_var(k, o0);
                    } else if s[k].is_ascii_whitespace() {
                        k += 1;
                    } else {
                        let a = k;
                        while k < o0 && !s[k].is_ascii_whitespace() && s[k] != b'$' {
                            k += 1;
                        }
                        self.push(a, k, Tok::Function);
                    }
                }
                self.push(o0, o1, Tok::Operator);
                match s[o1..le].iter().position(|&c| c == b';') {
                    Some(sc) => {
                        self.mk_text(o1, o1 + sc, false);
                        self.push(o1 + sc, o1 + sc + 1, Tok::Operator);
                        self.mk_text(o1 + sc + 1, le, true);
                    }
                    None => self.mk_text(o1, le, false),
                }
            }
            None => self.mk_text(p, le, false),
        }
    }

    /// `$(…)`, `${…}`, `$x` en `j` ; rend la fin.
    fn mk_var(&mut self, j: usize, e: usize) -> usize {
        let s = self.s;
        let o = self.at(j + 1);
        if j + 1 >= e {
            return e;
        }
        if o == b'$' {
            self.push(j, j + 2, Tok::Escape);
            return j + 2;
        }
        if o == b'(' || o == b'{' {
            let close = if o == b'(' { b')' } else { b'}' };
            let mut depth = 0usize;
            let mut k = j + 1;
            while k < e {
                if s[k] == o {
                    depth += 1;
                } else if s[k] == close {
                    depth -= 1;
                    if depth == 0 {
                        k += 1;
                        break;
                    }
                }
                k += 1;
            }
            self.push(j, k, Tok::Constant);
            return k;
        }
        let l = 1 + self.char_len(j + 1);
        self.push(j, j + l, Tok::Constant);
        (j + l).min(e)
    }

    /// Texte d'une ligne de Makefile (valeur, prérequis ou recette).
    fn mk_text(&mut self, a: usize, e: usize, recipe: bool) {
        let s = self.s;
        let mut k = a;
        if recipe {
            k = self.hspace(k);
            while k < e && matches!(s[k], b'@' | b'-' | b'+') {
                self.push(k, k + 1, Tok::Operator);
                k += 1;
            }
        }
        while k < e {
            let c = s[k];
            match c {
                b'$' => k = self.mk_var(k, e),
                b'#' if !recipe || k == a || s[k - 1].is_ascii_whitespace() => {
                    self.push(k, e, Tok::Comment);
                    k = e;
                }
                b'\\' if k + 1 < e => {
                    let l = 1 + self.char_len(k + 1);
                    self.push(k, k + l, Tok::Escape);
                    k += l;
                }
                b'"' | b'\'' if recipe => {
                    let mut j = k + 1;
                    while j < e && s[j] != c {
                        j += if s[j] == b'\\' && c == b'"' { 2 } else { 1 };
                    }
                    let j = (j + 1).min(e);
                    self.push(k, j, Tok::String);
                    k = j;
                }
                _ => k += self.char_len(k),
            }
        }
    }

    // ----- XML (et HTML, AXAML, csproj, SVG)

    fn xml(&mut self) {
        let mut last = usize::MAX;
        while self.i < self.len() {
            guard!(self, last);
            if self.i >= self.len() {
                break;
            }
            let a = self.i;
            let c = self.cur();
            if c == b'<' {
                let n1 = self.nx(1);
                let region = |me: &Self, close: &[u8]| me.find(a + 2, close).map_or(me.len(), |p| p + close.len());
                if self.starts(a, b"<!--") {
                    let e = region(self, b"-->");
                    self.push(a, e, Tok::Comment);
                    self.i = e;
                } else if self.starts(a, b"<![CDATA[") {
                    let e = region(self, b"]]>");
                    self.push(a, e, Tok::String);
                    self.i = e;
                } else if n1 == b'?' {
                    let e = region(self, b"?>");
                    self.push(a, e, Tok::Preproc);
                    self.i = e;
                } else if n1 == b'!' {
                    let e = region(self, b">");
                    self.push(a, e, Tok::Preproc);
                    self.i = e;
                } else if n1 == b'/' || is_id_start(n1) {
                    self.xml_tag();
                } else {
                    self.i += 1;
                }
                continue;
            }
            if c == b'&' {
                let mut k = a + 1;
                while k < self.len() && k - a < 40 && (self.s[k].is_ascii_alphanumeric() || self.s[k] == b'#') {
                    k += 1;
                }
                if self.at(k) == b';' && k > a + 1 {
                    self.push(a, k + 1, Tok::Escape);
                    self.i = k + 1;
                } else {
                    self.i += 1;
                }
                continue;
            }
            // texte : jusqu'au prochain '<' ou '&'
            let rest = &self.s[a..];
            self.i = a + rest.iter().position(|&c| c == b'<' || c == b'&').unwrap_or(rest.len()).max(1);
        }
    }

    /// Une balise <nom attr="valeur" …> ; self.i sur '<'.
    fn xml_tag(&mut self) {
        let s = self.s;
        let a = self.i;
        let p = if self.at(a + 1) == b'/' { 2 } else { 1 };
        self.push(a, a + p, Tok::Punct);
        let name = |k: usize| is_id(s[k]) || matches!(s[k], b'-' | b'.' | b':');
        let mut k = a + p;
        let n0 = k;
        while k < s.len() && name(k) {
            k += 1;
        }
        self.push(n0, k, Tok::Tag);
        while k < s.len() {
            let c = s[k];
            if c.is_ascii_whitespace() {
                k += 1;
            } else if c == b'>' {
                self.push(k, k + 1, Tok::Punct);
                k += 1;
                break;
            } else if c == b'/' && self.at(k + 1) == b'>' {
                self.push(k, k + 2, Tok::Punct);
                k += 2;
                break;
            } else if c == b'<' {
                break;
            } else if is_id_start(c) {
                let n = k;
                while k < s.len() && name(k) {
                    k += 1;
                }
                self.push(n, k, Tok::AttrName);
            } else if c == b'=' {
                self.push(k, k + 1, Tok::Operator);
                k += 1;
            } else if c == b'"' || c == b'\'' {
                let e = s[k + 1..].iter().position(|&x| x == c).map_or(s.len(), |q| k + 2 + q);
                self.push(k, e, Tok::String);
                k = e;
            } else {
                k += self.char_len(k);
            }
        }
        self.i = k.min(s.len());
    }

    // ----- INI (et app.txt de Korin : pas de commentaire en fin de ligne)

    fn ini(&mut self) {
        let s = self.s;
        let len = s.len();
        let mut i = self.i;
        while i < len {
            let ls = i;
            let le = self.eol(i);
            let nl = (le + 1).min(len);
            let p = self.hspace(ls);
            if p < le {
                let c = s[p];
                if c == b';' || c == b'#' {
                    self.push(p, le, Tok::Comment);
                } else if c == b'[' {
                    let e = s[p..le].iter().position(|&x| x == b']').map_or(le, |q| p + q + 1);
                    self.push(p, e, Tok::Type);
                } else {
                    let line = &s[p..le];
                    let o = line.iter().position(|&x| x == b'=').or_else(|| line.iter().position(|&x| x == b':'));
                    if let Some(o) = o {
                        let o = p + o;
                        self.push(p, p + trim_end(&s[p..o]).len(), Tok::AttrName);
                        self.push(o, o + 1, Tok::Operator);
                        let v = self.hspace(o + 1);
                        let ve = v + trim_end(&s[v.min(le)..le]).len();
                        if v < ve {
                            let val = &s[v..ve];
                            let q = val[0];
                            let mut buf = [0u8; 32];
                            let lv = lower(val, &mut buf);
                            let tok = if val.len() >= 2 && (q == b'"' || q == b'\'') && val[val.len() - 1] == q {
                                Tok::String
                            } else if is_number_word(val) {
                                Tok::Number
                            } else if matches!(lv, b"true" | b"false" | b"yes" | b"no" | b"on" | b"off") {
                                Tok::Constant
                            } else {
                                Tok::Text
                            };
                            self.push(v, ve, tok);
                        }
                    }
                }
            }
            i = nl;
        }
        self.i = len;
    }
}

fn trim_end(t: &[u8]) -> &[u8] {
    let mut n = t.len();
    while n > 0 && t[n - 1].is_ascii_whitespace() {
        n -= 1;
    }
    &t[..n]
}

/// Un nombre seul (12, -3.5, 0x1F).
fn is_number_word(w: &[u8]) -> bool {
    let w = w.strip_prefix(b"-").or_else(|| w.strip_prefix(b"+")).unwrap_or(w);
    if let Some(h) = w.strip_prefix(b"0x").or_else(|| w.strip_prefix(b"0X")) {
        return !h.is_empty() && h.iter().all(|c| c.is_ascii_hexdigit());
    }
    !w.is_empty()
        && w[0].is_ascii_digit()
        && w.iter().all(|&c| c.is_ascii_digit() || c == b'.' || c == b'_')
        && w.iter().filter(|&&c| c == b'.').count() <= 1
}

// ---------------------------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;

    fn check(text: &str, spans: &[Span]) {
        let mut last = 0;
        for (r, _) in spans {
            assert!(r.start < r.end, "intervalle vide {r:?}");
            assert!(r.start >= last, "chevauchement ou désordre {r:?}");
            assert!(r.end <= text.len(), "hors du texte {r:?}");
            assert!(text.is_char_boundary(r.start) && text.is_char_boundary(r.end), "frontière {r:?}");
            last = r.end;
        }
    }

    fn hl(lang: Lang, text: &str) -> Vec<Span> {
        let v = highlight(lang, text);
        check(text, &v);
        v
    }

    /// La couleur du premier octet de la `nth` occurrence de `needle`.
    fn tok_nth(lang: Lang, text: &str, needle: &str, nth: usize) -> Tok {
        let spans = hl(lang, text);
        let pos = text.match_indices(needle).nth(nth).unwrap_or_else(|| panic!("{needle:?} absent")).0;
        spans.iter().find(|(r, _)| r.contains(&pos)).map_or(Tok::Text, |s| s.1)
    }
    fn tok(lang: Lang, text: &str, needle: &str) -> Tok {
        tok_nth(lang, text, needle, 0)
    }
    /// L'intervalle exact qui couvre `needle`.
    fn span_of(lang: Lang, text: &str, needle: &str) -> Option<(Range<usize>, Tok)> {
        let pos = text.find(needle).unwrap();
        hl(lang, text).into_iter().find(|(r, _)| r.contains(&pos))
    }

    #[test]
    fn lists_sorted() {
        let lists: &[(&str, &[&str])] = &[
            ("RUST_KW", RUST_KW),
            ("RUST_PRIM", RUST_PRIM),
            ("C_KW", C_KW),
            ("CPP_KW", CPP_KW),
            ("C_TYPES", C_TYPES),
            ("CPP_STD", CPP_STD),
            ("CPP_STD_QUAL", CPP_STD_QUAL),
            ("CS_TYPES", CS_TYPES),
            ("CS_KW", CS_KW),
            ("PY_KW", PY_KW),
            ("PY_TYPES", PY_TYPES),
            ("FB_TYPES", FB_TYPES),
            ("FB_KW", FB_KW),
            ("SH_KW", SH_KW),
            ("SH_BUILTINS", SH_BUILTINS),
            ("MAKE_DIR", MAKE_DIR),
            ("ASM_SIZE", ASM_SIZE),
            ("FASM_DIR", FASM_DIR),
            ("FASM_DATA", FASM_DATA),
            ("FASM_SIZE", FASM_SIZE),
            ("X86_REGS", X86_REGS),
            ("ARM_REGS", ARM_REGS),
        ];
        for (name, l) in lists {
            let mut sorted = l.to_vec();
            sorted.sort();
            sorted.dedup();
            assert_eq!(&sorted[..], *l, "{name} n'est pas trié ou a des doublons");
        }
    }

    #[test]
    fn from_path_and_names() {
        let p = |s: &str| Lang::from_path(Path::new(s));
        assert_eq!(p("src/main.rs"), Lang::Rust);
        assert_eq!(p("a/Makefile"), Lang::Make);
        assert_eq!(p("makefile"), Lang::Make);
        assert_eq!(p("rules.mk"), Lang::Make);
        assert_eq!(p("x.h"), Lang::C);
        assert_eq!(p("x.hpp"), Lang::Cpp);
        assert_eq!(p("x.cc"), Lang::Cpp);
        assert_eq!(p("Program.cs"), Lang::CSharp);
        assert_eq!(p("boot.S"), Lang::AsmGnu);
        assert_eq!(p("boot.s"), Lang::AsmGnu);
        assert_eq!(p("kernel.asm"), Lang::AsmFasm);
        assert_eq!(p("syscalls.inc"), Lang::AsmFasm);
        assert_eq!(p("hello.bas"), Lang::FreeBasic);
        assert_eq!(p("x.bi"), Lang::FreeBasic);
        assert_eq!(p("App.csproj"), Lang::Xml);
        assert_eq!(p("MainWindow.axaml"), Lang::Xml);
        assert_eq!(p("icon.svg"), Lang::Xml);
        assert_eq!(p("app.txt"), Lang::Ini);
        assert_eq!(p("notes.txt"), Lang::Plain);
        assert_eq!(p("x.conf"), Lang::Ini);
        assert_eq!(p("run.sh"), Lang::Shell);
        assert_eq!(p("Cargo.toml"), Lang::Toml);
        assert_eq!(p("package.json"), Lang::Json);
        assert_eq!(p("README.md"), Lang::Markdown);
        assert_eq!(p("x.py"), Lang::Python);
        assert_eq!(p("noext"), Lang::Plain);
        assert_eq!(p(".bashrc"), Lang::Shell);
        assert_eq!(Lang::all().len(), 16);
        assert_eq!(Lang::Cpp.name(), "C++");
        assert_eq!(Lang::Plain.name(), "Texte brut");
        assert_eq!(Lang::FreeBasic.line_comment(), Some("'"));
        assert_eq!(Lang::AsmFasm.line_comment(), Some(";"));
        assert_eq!(Lang::Json.line_comment(), None);
        assert_eq!(Lang::from_name("c++"), Some(Lang::Cpp));
        assert_eq!(Lang::from_name("nope"), None);
    }

    #[test]
    fn plain_is_empty() {
        assert!(highlight(Lang::Plain, "fn main() {}").is_empty());
        for &l in Lang::all() {
            assert!(highlight(l, "").is_empty());
        }
    }

    #[test]
    fn rust() {
        let src = r##"/// doc
//! inner
// plain
#[derive(Debug, Clone)]
pub struct Foo<'a> { x: &'a str, c: char }
fn main() {
    let s = r#"raw "quoted" str"#;
    let c = 'x'; let n = '\n'; let b = b'z';
    println!("hi {}\n", 0x1F_u32 + 1_000i64 + 0b1010 + 2.5e-3f64);
    /* outer /* nested */ still */ let after = MAX_SIZE;
    let v: Vec<u8> = Vec::new(); helper(1); r#type();
    'outer: loop { break 'outer; }
}"##;
        assert_eq!(tok(Lang::Rust, src, "/// doc"), Tok::DocComment);
        assert_eq!(tok(Lang::Rust, src, "//! inner"), Tok::DocComment);
        assert_eq!(tok(Lang::Rust, src, "// plain"), Tok::Comment);
        assert_eq!(span_of(Lang::Rust, src, "#[derive").unwrap().1, Tok::Attribute);
        assert_eq!(tok(Lang::Rust, src, "pub"), Tok::Keyword);
        assert_eq!(tok(Lang::Rust, src, "Foo"), Tok::Type);
        assert_eq!(tok(Lang::Rust, src, "'a>"), Tok::Lifetime);
        assert_eq!(tok(Lang::Rust, src, "str,"), Tok::Type);
        assert_eq!(tok(Lang::Rust, src, "main"), Tok::Function);
        let raw = span_of(Lang::Rust, src, "r#\"raw").unwrap();
        assert_eq!(&src[raw.0.clone()], r##"r#"raw "quoted" str"#"##);
        assert_eq!(raw.1, Tok::String);
        assert_eq!(tok(Lang::Rust, src, "'x'"), Tok::Char);
        assert_eq!(span_of(Lang::Rust, src, "'\\n'").unwrap().1, Tok::Char);
        assert_eq!(tok(Lang::Rust, src, "b'z'"), Tok::Char);
        assert_eq!(
            span_of(Lang::Rust, src, "println!").unwrap(),
            (src.find("println!").unwrap()..src.find("println!").unwrap() + 8, Tok::Macro)
        );
        assert_eq!(tok(Lang::Rust, src, "\\n\""), Tok::Escape);
        assert_eq!(span_of(Lang::Rust, src, "0x1F_u32").unwrap().0.len(), 8);
        assert_eq!(tok(Lang::Rust, src, "1_000i64"), Tok::Number);
        assert_eq!(span_of(Lang::Rust, src, "2.5e-3f64").unwrap().0.len(), 9);
        let c = span_of(Lang::Rust, src, "/* outer").unwrap();
        assert!(src[c.0.clone()].ends_with("still */"));
        assert_eq!(tok(Lang::Rust, src, "after"), Tok::Text);
        assert_eq!(tok(Lang::Rust, src, "MAX_SIZE"), Tok::Constant);
        assert_eq!(tok(Lang::Rust, src, "u8>"), Tok::Type);
        assert_eq!(tok(Lang::Rust, src, "new"), Tok::Function);
        assert_eq!(tok(Lang::Rust, src, "helper"), Tok::Function);
        assert_eq!(tok(Lang::Rust, src, "'outer:"), Tok::Lifetime);
    }

    #[test]
    fn c_cpp() {
        let src = "#include <stdio.h>\n#include \"korin.h\"\n#define MAX(a, b) ((a) > (b) ? (a) : (b)) // max\n\
                   /** doc */\nstatic uint32_t count = 0x10u;\nint main(void) {\n  size_t n = BUF_LEN;\n  \
                   printf(\"%d\\n\", n); char c = '\\'';\n  /* multi\n line */ return 0;\n}\n";
        assert_eq!(tok(Lang::C, src, "#include"), Tok::Preproc);
        assert_eq!(span_of(Lang::C, src, "<stdio.h>").unwrap(), (9..18, Tok::String));
        assert_eq!(tok(Lang::C, src, "\"korin.h\""), Tok::String);
        assert_eq!(tok(Lang::C, src, "MAX(a"), Tok::Preproc);
        assert_eq!(tok(Lang::C, src, "// max"), Tok::Comment);
        assert_eq!(tok(Lang::C, src, "/** doc"), Tok::DocComment);
        assert_eq!(tok(Lang::C, src, "static"), Tok::Keyword);
        assert_eq!(tok(Lang::C, src, "uint32_t"), Tok::Type);
        assert_eq!(tok(Lang::C, src, "0x10u"), Tok::Number);
        assert_eq!(tok(Lang::C, src, "int main"), Tok::Type);
        assert_eq!(tok(Lang::C, src, "main"), Tok::Function);
        assert_eq!(tok(Lang::C, src, "size_t"), Tok::Type);
        assert_eq!(tok(Lang::C, src, "BUF_LEN"), Tok::Constant);
        assert_eq!(tok(Lang::C, src, "printf"), Tok::Function);
        assert_eq!(tok(Lang::C, src, "\\n\""), Tok::Escape);
        assert_eq!(tok(Lang::C, src, "'\\''"), Tok::Char);
        assert_eq!(tok(Lang::C, src, " line */"), Tok::Comment);
        assert_eq!(tok(Lang::C, src, "return"), Tok::Keyword);
        let cpp = "namespace ks { class Widget; }\nstd::vector<std::string> v; auto s = R\"x(raw \" )\" here)x\";\n\
                   template<typename T> T id(T t) { return nullptr; } int big = 1'000'000; std::map<int,int> m;";
        assert_eq!(tok(Lang::Cpp, cpp, "namespace"), Tok::Keyword);
        assert_eq!(tok(Lang::Cpp, cpp, "ks"), Tok::Type);
        assert_eq!(tok(Lang::Cpp, cpp, "Widget"), Tok::Type);
        assert_eq!(tok(Lang::Cpp, cpp, "vector"), Tok::Type);
        assert_eq!(tok(Lang::Cpp, cpp, "string>"), Tok::Type);
        let raw = span_of(Lang::Cpp, cpp, "R\"x(").unwrap();
        assert_eq!(&cpp[raw.0], "R\"x(raw \" )\" here)x\"");
        assert_eq!(tok(Lang::Cpp, cpp, "template"), Tok::Keyword);
        assert_eq!(tok(Lang::Cpp, cpp, "nullptr"), Tok::Constant);
        assert_eq!(span_of(Lang::Cpp, cpp, "1'000'000").unwrap().0.len(), 9);
        assert_eq!(tok(Lang::Cpp, cpp, "map"), Tok::Type);
    }

    #[test]
    fn csharp() {
        let src = "using System;\n[Serializable(\"x\")]\npublic record Point(int X, int Y);\nclass App {\n  \
                   /// <summary>doc</summary>\n  async Task Run() {\n    var p = @\"C:\\dir\"\"q\"\" \n line\";\n    \
                   var s = $\"x={p.X,5:N2} {{lit}} {Name(\"a\")}\\n\";\n    await Task.Delay(10); string t = \"\"\"raw \" \"\"\";\n  \
                   }\n  public int Prop { get; set; }\n}\n#region R\n";
        assert_eq!(tok(Lang::CSharp, src, "using"), Tok::Keyword);
        assert_eq!(tok(Lang::CSharp, src, "System"), Tok::Type);
        assert_eq!(tok(Lang::CSharp, src, "[Serializable"), Tok::Attribute);
        assert_eq!(tok(Lang::CSharp, src, "\"x\")]"), Tok::String);
        assert_eq!(tok(Lang::CSharp, src, "record"), Tok::Keyword);
        assert_eq!(tok(Lang::CSharp, src, "Point"), Tok::Type);
        assert_eq!(tok(Lang::CSharp, src, "int X"), Tok::Type);
        assert_eq!(tok(Lang::CSharp, src, "/// <summary>"), Tok::DocComment);
        assert_eq!(tok(Lang::CSharp, src, "async"), Tok::Keyword);
        assert_eq!(tok(Lang::CSharp, src, "Run"), Tok::Function);
        assert_eq!(tok(Lang::CSharp, src, "var"), Tok::Keyword);
        let v = span_of(Lang::CSharp, src, "@\"C:").unwrap();
        assert_eq!(v.1, Tok::String);
        assert_eq!(tok(Lang::CSharp, src, " line\";"), Tok::String);
        assert_eq!(tok(Lang::CSharp, src, "$\"x="), Tok::String);
        assert_eq!(tok(Lang::CSharp, src, "{p.X"), Tok::Punct);
        assert_eq!(tok(Lang::CSharp, src, ":N2"), Tok::String);
        assert_eq!(tok(Lang::CSharp, src, "{{lit"), Tok::Escape);
        assert_eq!(tok(Lang::CSharp, src, "Name(\"a\")"), Tok::Function);
        assert_eq!(tok(Lang::CSharp, src, "\"a\""), Tok::String);
        assert_eq!(tok(Lang::CSharp, src, "await"), Tok::Keyword);
        assert_eq!(tok(Lang::CSharp, src, "Delay"), Tok::Function);
        let raw = span_of(Lang::CSharp, src, "\"\"\"raw").unwrap();
        assert_eq!(&src[raw.0], "\"\"\"raw \" \"\"\"");
        assert_eq!(tok(Lang::CSharp, src, "get"), Tok::Keyword);
        assert_eq!(tok(Lang::CSharp, src, "#region"), Tok::Preproc);
    }

    #[test]
    fn asm_gnu() {
        let src = ".section .text\n.globl _start\n_start:\n    movq $60, %rax   # exit\n    xor rdi, rdi // intel\n\
                   1:  jmp 1b\n    /* block\n */ lock incl (%rbx)\n    .L2: mov eax, DWORD PTR [rbp-4]\n";
        assert_eq!(tok(Lang::AsmGnu, src, ".section"), Tok::Preproc);
        assert_eq!(tok(Lang::AsmGnu, src, "_start:"), Tok::Label);
        assert_eq!(tok(Lang::AsmGnu, src, "movq"), Tok::Keyword);
        assert_eq!(tok(Lang::AsmGnu, src, "$60"), Tok::Number);
        assert_eq!(tok(Lang::AsmGnu, src, "%rax"), Tok::Register);
        assert_eq!(tok(Lang::AsmGnu, src, "# exit"), Tok::Comment);
        assert_eq!(tok(Lang::AsmGnu, src, "xor"), Tok::Keyword);
        assert_eq!(tok(Lang::AsmGnu, src, "rdi,"), Tok::Register);
        assert_eq!(tok(Lang::AsmGnu, src, "// intel"), Tok::Comment);
        assert_eq!(tok(Lang::AsmGnu, src, "1:"), Tok::Label);
        assert_eq!(tok(Lang::AsmGnu, src, "1b"), Tok::Label);
        assert_eq!(tok(Lang::AsmGnu, src, " */"), Tok::Comment);
        assert_eq!(tok(Lang::AsmGnu, src, "lock incl"), Tok::Keyword);
        assert_eq!(tok(Lang::AsmGnu, src, "incl"), Tok::Keyword);
        assert_eq!(tok(Lang::AsmGnu, src, ".L2:"), Tok::Label);
        assert_eq!(tok(Lang::AsmGnu, src, "PTR"), Tok::Type);
        assert_eq!(tok(Lang::AsmGnu, src, "eax,"), Tok::Register);
    }

    #[test]
    fn asm_fasm() {
        let src = "format ELF64 executable 3\ninclude 'macros.inc'\nFILES_APPKIT = 1\nif ~KIT_BUILD\nend if\n\
                   macro kit_fn name { dw name }\nstart:\n    mov x0, #42 ; commentaire\n    ldr w1, [sp, #-8]!\n    \
                   mov eax, 0FFh\n    sys SYS_OPEN\nmsg db 'Bonjour', 0\n.loop: dec ecx\n    jnz @b\n    \
                   mov rax, qword [rbx]\n    ld1 {v0.16b}, [x1]\n    mov r0, 1010b\n";
        assert_eq!(tok(Lang::AsmFasm, src, "format"), Tok::Preproc);
        assert_eq!(tok(Lang::AsmFasm, src, "ELF64"), Tok::Constant);
        assert_eq!(tok(Lang::AsmFasm, src, "executable"), Tok::Preproc);
        assert_eq!(tok(Lang::AsmFasm, src, "include"), Tok::Preproc);
        assert_eq!(tok(Lang::AsmFasm, src, "'macros.inc'"), Tok::String);
        assert_eq!(tok(Lang::AsmFasm, src, "FILES_APPKIT"), Tok::Constant);
        assert_eq!(tok(Lang::AsmFasm, src, "if ~"), Tok::Preproc);
        assert_eq!(tok(Lang::AsmFasm, src, "KIT_BUILD"), Tok::Constant);
        assert_eq!(tok(Lang::AsmFasm, src, "end if"), Tok::Preproc);
        assert_eq!(tok(Lang::AsmFasm, src, "macro kit"), Tok::Preproc);
        assert_eq!(tok(Lang::AsmFasm, src, "kit_fn"), Tok::Function);
        assert_eq!(tok(Lang::AsmFasm, src, "dw name"), Tok::Type);
        assert_eq!(tok(Lang::AsmFasm, src, "start:"), Tok::Label);
        assert_eq!(tok(Lang::AsmFasm, src, "mov x0"), Tok::Keyword);
        assert_eq!(tok(Lang::AsmFasm, src, "x0"), Tok::Register);
        assert_eq!(tok(Lang::AsmFasm, src, "#42"), Tok::Number);
        assert_eq!(tok(Lang::AsmFasm, src, "; commentaire"), Tok::Comment);
        assert_eq!(tok(Lang::AsmFasm, src, "w1"), Tok::Register);
        assert_eq!(tok(Lang::AsmFasm, src, "sp,"), Tok::Register);
        assert_eq!(tok(Lang::AsmFasm, src, "#-8"), Tok::Number);
        assert_eq!(tok(Lang::AsmFasm, src, "0FFh"), Tok::Number);
        assert_eq!(tok(Lang::AsmFasm, src, "eax"), Tok::Register);
        assert_eq!(tok(Lang::AsmFasm, src, "sys"), Tok::Keyword);
        assert_eq!(tok(Lang::AsmFasm, src, "SYS_OPEN"), Tok::Constant);
        assert_eq!(tok(Lang::AsmFasm, src, "msg"), Tok::Label);
        assert_eq!(tok(Lang::AsmFasm, src, "db 'B"), Tok::Type);
        assert_eq!(tok(Lang::AsmFasm, src, ".loop:"), Tok::Label);
        assert_eq!(tok(Lang::AsmFasm, src, "dec"), Tok::Keyword);
        assert_eq!(tok(Lang::AsmFasm, src, "@b"), Tok::Label);
        assert_eq!(tok(Lang::AsmFasm, src, "qword"), Tok::Type);
        assert_eq!(tok(Lang::AsmFasm, src, "v0.16b"), Tok::Register);
        assert_eq!(tok(Lang::AsmFasm, src, "r0,"), Tok::Register);
        assert_eq!(tok(Lang::AsmFasm, src, "1010b"), Tok::Number);
    }

    #[test]
    fn freebasic() {
        let src = "#include once \"fbgfx.bi\"\n' commentaire\n/' bloc /' imbriqué '/ fin '/\nDim As Integer x = &HFF\n\
                   REM vieux commentaire\nDeclare Function Carre(ByVal n As Double) As Double\nPrint \"dit \"\"oui\"\"\": x = 10%\n\
                   Sub Hello()\n  If x > 1 Then Print Chr$(65) Else Sleep\nEnd Sub\ndebut:\n  GoTo debut\nConst MAX_N = 3\n";
        assert_eq!(tok(Lang::FreeBasic, src, "#include"), Tok::Preproc);
        assert_eq!(tok(Lang::FreeBasic, src, "\"fbgfx.bi\""), Tok::String);
        assert_eq!(tok(Lang::FreeBasic, src, "' commentaire"), Tok::Comment);
        let b = span_of(Lang::FreeBasic, src, "/' bloc").unwrap();
        assert!(src[b.0].ends_with("fin '/"));
        assert_eq!(tok(Lang::FreeBasic, src, "Dim"), Tok::Keyword);
        assert_eq!(tok(Lang::FreeBasic, src, "Integer"), Tok::Type);
        assert_eq!(tok(Lang::FreeBasic, src, "&HFF"), Tok::Number);
        assert_eq!(tok(Lang::FreeBasic, src, "REM"), Tok::Comment);
        assert_eq!(tok(Lang::FreeBasic, src, "Declare"), Tok::Keyword);
        assert_eq!(tok(Lang::FreeBasic, src, "Carre"), Tok::Function);
        assert_eq!(tok(Lang::FreeBasic, src, "ByVal"), Tok::Keyword);
        assert_eq!(tok(Lang::FreeBasic, src, "Double"), Tok::Type);
        let st = span_of(Lang::FreeBasic, src, "\"dit").unwrap();
        assert_eq!(&src[st.0], "\"dit \"\"oui\"\"\"");
        assert_eq!(tok(Lang::FreeBasic, src, "10%"), Tok::Number);
        assert_eq!(tok(Lang::FreeBasic, src, "Hello"), Tok::Function);
        assert_eq!(tok(Lang::FreeBasic, src, "Chr$"), Tok::Function);
        assert_eq!(tok(Lang::FreeBasic, src, "Sleep"), Tok::Keyword);
        assert_eq!(tok(Lang::FreeBasic, src, "Sub\nde"), Tok::Keyword);
        assert_eq!(tok(Lang::FreeBasic, src, "debut:"), Tok::Label);
        assert_eq!(tok(Lang::FreeBasic, src, "MAX_N"), Tok::Constant);
    }

    #[test]
    fn python() {
        let src = "@dataclass\nclass Point(Base):\n    \"\"\"Docstring\n    multi\"\"\"\n    def norm(self) -> float:\n        \
                   s = '''tri\nple'''\n        f = f\"v={self.x!r:>10} {{x}} {len(d['k'])}\"\n        r = rb'\\d+'\n        \
                   return None if x else 0x1F + 1_000.5j  # fin\nprint(MAX)\n";
        assert_eq!(tok(Lang::Python, src, "@dataclass"), Tok::Attribute);
        assert_eq!(tok(Lang::Python, src, "class Point"), Tok::Keyword);
        assert_eq!(tok(Lang::Python, src, "Point"), Tok::Type);
        assert_eq!(tok(Lang::Python, src, "Base"), Tok::Type);
        assert_eq!(tok(Lang::Python, src, "    multi"), Tok::DocComment);
        assert_eq!(tok(Lang::Python, src, "norm"), Tok::Function);
        assert_eq!(tok(Lang::Python, src, "self)"), Tok::Keyword);
        assert_eq!(tok(Lang::Python, src, "float"), Tok::Type);
        assert_eq!(tok(Lang::Python, src, "ple'''"), Tok::String);
        assert_eq!(tok(Lang::Python, src, "f\"v="), Tok::String);
        assert_eq!(tok(Lang::Python, src, "self.x!r"), Tok::Keyword);
        assert_eq!(tok(Lang::Python, src, "!r:>10"), Tok::String);
        assert_eq!(tok(Lang::Python, src, "{{x"), Tok::Escape);
        assert_eq!(tok(Lang::Python, src, "len"), Tok::Function);
        assert_eq!(tok(Lang::Python, src, "'k'"), Tok::String);
        assert_eq!(tok(Lang::Python, src, "rb'"), Tok::String);
        assert_eq!(tok(Lang::Python, src, "None"), Tok::Constant);
        assert_eq!(tok(Lang::Python, src, "0x1F"), Tok::Number);
        assert_eq!(tok(Lang::Python, src, "1_000.5j"), Tok::Number);
        assert_eq!(tok(Lang::Python, src, "# fin"), Tok::Comment);
        assert_eq!(tok(Lang::Python, src, "print"), Tok::Function);
        assert_eq!(tok(Lang::Python, src, "MAX"), Tok::Constant);
    }

    #[test]
    fn toml_json() {
        let src = "# config\n[package]\nname = \"korin\\tstudio\"\nedition = 2024\n[[bin]]\n\"quoted key\" = 'lit'\n\
                   deps = { serde = { version = \"1\", features = [\"derive\"] }, ok = true }\n\
                   list = [\n  1,\n  2.5,\n]\nwhen = 1979-05-27T07:32:00Z\nml = \"\"\"\nmulti\n\"\"\"\n";
        assert_eq!(tok(Lang::Toml, src, "# config"), Tok::Comment);
        assert_eq!(span_of(Lang::Toml, src, "[package]").unwrap().1, Tok::Type);
        assert_eq!(tok(Lang::Toml, src, "name"), Tok::AttrName);
        assert_eq!(tok(Lang::Toml, src, "\"korin"), Tok::String);
        assert_eq!(tok(Lang::Toml, src, "\\t"), Tok::Escape);
        assert_eq!(tok(Lang::Toml, src, "2024"), Tok::Number);
        assert_eq!(tok(Lang::Toml, src, "[[bin]]"), Tok::Type);
        assert_eq!(tok(Lang::Toml, src, "\"quoted key\""), Tok::AttrName);
        assert_eq!(tok(Lang::Toml, src, "'lit'"), Tok::String);
        assert_eq!(tok(Lang::Toml, src, "serde"), Tok::AttrName);
        assert_eq!(tok(Lang::Toml, src, "version"), Tok::AttrName);
        assert_eq!(tok(Lang::Toml, src, "\"derive\""), Tok::String);
        assert_eq!(tok(Lang::Toml, src, "ok"), Tok::AttrName);
        assert_eq!(tok(Lang::Toml, src, "true"), Tok::Constant);
        assert_eq!(tok(Lang::Toml, src, "2.5"), Tok::Number);
        assert_eq!(span_of(Lang::Toml, src, "1979").unwrap().0.len(), 20);
        assert_eq!(tok(Lang::Toml, src, "multi"), Tok::String);

        let j = "{\n  // jsonc\n  \"name\": \"ks\\u00e9\",\n  \"n\" : -1.5e3, \"ok\": [true, null]\n}";
        assert_eq!(tok(Lang::Json, j, "// jsonc"), Tok::Comment);
        assert_eq!(tok(Lang::Json, j, "\"name\""), Tok::AttrName);
        assert_eq!(tok(Lang::Json, j, "\"ks"), Tok::String);
        assert_eq!(tok(Lang::Json, j, "\\u00e9"), Tok::Escape);
        assert_eq!(tok(Lang::Json, j, "\"n\""), Tok::AttrName);
        assert_eq!(span_of(Lang::Json, j, "-1.5e3").unwrap().0.len(), 6);
        assert_eq!(tok(Lang::Json, j, "null"), Tok::Constant);
        assert_eq!(tok(Lang::Json, j, "{"), Tok::Punct);
    }

    #[test]
    fn markdown() {
        let src = "# Titre\n\nDu **gras**, de l'*italique*, du `code`, un [lien](http://x.org) et snake_case_name.\n\
                   Sous-titre\n---\n\n- puce\n1. numéro\n> citation\n\n```rust\nfn main() { let x = 1; }\n```\n\n\
                   ```\nbrut\n```\n<!-- commentaire\nsur deux lignes -->\n~~~python\ndef f(): pass\n~~~\n";
        assert_eq!(tok(Lang::Markdown, src, "# Titre"), Tok::Heading);
        assert_eq!(span_of(Lang::Markdown, src, "**gras**").unwrap().1, Tok::Emphasis);
        assert_eq!(tok(Lang::Markdown, src, "*italique*"), Tok::Emphasis);
        assert_eq!(tok(Lang::Markdown, src, "l'"), Tok::Text);
        assert_eq!(tok(Lang::Markdown, src, "`code`"), Tok::String);
        assert_eq!(span_of(Lang::Markdown, src, "[lien]").unwrap().1, Tok::Link);
        assert_eq!(tok(Lang::Markdown, src, "_case"), Tok::Text);
        assert_eq!(tok(Lang::Markdown, src, "---"), Tok::Heading);
        assert_eq!(tok(Lang::Markdown, src, "- puce"), Tok::Punct);
        assert_eq!(tok(Lang::Markdown, src, "1. "), Tok::Punct);
        assert_eq!(tok(Lang::Markdown, src, "> "), Tok::Punct);
        assert_eq!(tok(Lang::Markdown, src, "```rust"), Tok::Punct);
        assert_eq!(tok(Lang::Markdown, src, "rust\n"), Tok::Keyword);
        assert_eq!(tok(Lang::Markdown, src, "fn main"), Tok::Keyword);
        assert_eq!(tok(Lang::Markdown, src, "main()"), Tok::Function);
        assert_eq!(tok(Lang::Markdown, src, "brut"), Tok::String);
        assert_eq!(tok(Lang::Markdown, src, "sur deux"), Tok::Comment);
        assert_eq!(tok(Lang::Markdown, src, "def"), Tok::Keyword);
        // bloc non fermé : jusqu'à la fin, sans paniquer
        assert_eq!(tok(Lang::Markdown, "```c\nint x;\n", "int"), Tok::Type);
    }

    #[test]
    fn shell_make() {
        let src = "#!/bin/bash\n# commentaire\nset -e\nNAME=\"monde\"\nif [ -n \"$NAME\" ]; then\n  echo \"Bonjour ${NAME:-x} $(date)\" 'lit $x'\n\
                   fi\nfor f in *.c; do gcc -c \"$f\"; done\ncat <<EOF\ncorps $x\nEOF\nfunction greet() { local a=$1; }\nx=$((1 + 2)) # fin\n";
        assert_eq!(tok(Lang::Shell, src, "#!/bin/bash"), Tok::Preproc);
        assert_eq!(tok(Lang::Shell, src, "# commentaire"), Tok::Comment);
        assert_eq!(tok(Lang::Shell, src, "set"), Tok::Function);
        assert_eq!(tok(Lang::Shell, src, "NAME="), Tok::Constant);
        assert_eq!(tok(Lang::Shell, src, "\"monde\""), Tok::String);
        assert_eq!(tok(Lang::Shell, src, "if"), Tok::Keyword);
        assert_eq!(tok(Lang::Shell, src, "$NAME\""), Tok::Constant);
        assert_eq!(tok(Lang::Shell, src, "then"), Tok::Keyword);
        assert_eq!(tok(Lang::Shell, src, "echo"), Tok::Function);
        assert_eq!(tok(Lang::Shell, src, "${NAME"), Tok::Constant);
        assert_eq!(tok(Lang::Shell, src, "$(date)"), Tok::Constant);
        assert_eq!(tok(Lang::Shell, src, "'lit $x'"), Tok::String);
        assert_eq!(tok(Lang::Shell, src, "fi"), Tok::Keyword);
        assert_eq!(tok(Lang::Shell, src, "in *"), Tok::Keyword);
        assert_eq!(tok(Lang::Shell, src, "gcc"), Tok::Text);
        assert_eq!(tok(Lang::Shell, src, "done"), Tok::Keyword);
        assert_eq!(tok(Lang::Shell, src, "corps"), Tok::String);
        assert_eq!(tok_nth(Lang::Shell, src, "EOF", 1), Tok::Label);
        assert_eq!(tok(Lang::Shell, src, "greet"), Tok::Function);
        assert_eq!(tok(Lang::Shell, src, "$1"), Tok::Constant);
        assert_eq!(tok(Lang::Shell, src, "$(("), Tok::Punct);
        assert_eq!(tok(Lang::Shell, src, "# fin"), Tok::Comment);

        let mk = "# Makefile\nCC ?= gcc\nCFLAGS := -O2 $(EXTRA)\n.PHONY: all clean\nall: $(OBJS) main.o\n\t@echo \"build $@\" # note\n\t$(CC) -o $@ $^\n\
                  ifeq ($(OS),Windows)\nendif\ninclude rules.mk\n";
        assert_eq!(tok(Lang::Make, mk, "# Makefile"), Tok::Comment);
        assert_eq!(tok(Lang::Make, mk, "CC ?="), Tok::Constant);
        assert_eq!(tok(Lang::Make, mk, "?="), Tok::Operator);
        assert_eq!(tok(Lang::Make, mk, "$(EXTRA)"), Tok::Constant);
        assert_eq!(tok(Lang::Make, mk, ".PHONY"), Tok::Function);
        assert_eq!(tok(Lang::Make, mk, "all:"), Tok::Function);
        assert_eq!(tok(Lang::Make, mk, "$(OBJS)"), Tok::Constant);
        assert_eq!(tok(Lang::Make, mk, "@echo"), Tok::Operator);
        assert_eq!(tok(Lang::Make, mk, "\"build"), Tok::String);
        assert_eq!(tok(Lang::Make, mk, "# note"), Tok::Comment);
        assert_eq!(tok(Lang::Make, mk, "$^"), Tok::Constant);
        assert_eq!(tok(Lang::Make, mk, "ifeq"), Tok::Keyword);
        assert_eq!(tok(Lang::Make, mk, "include"), Tok::Keyword);
    }

    #[test]
    fn xml_ini() {
        let src = "<?xml version=\"1.0\"?>\n<!-- commentaire\n-->\n<Project Sdk=\"Microsoft.NET.Sdk\">\n  <x:Name a='b'>t &amp; u</x:Name>\n  \
                   <![CDATA[ <brut> ]]>\n  <br/>\n</Project>\n";
        assert_eq!(tok(Lang::Xml, src, "<?xml"), Tok::Preproc);
        assert_eq!(tok(Lang::Xml, src, "-->"), Tok::Comment);
        assert_eq!(tok(Lang::Xml, src, "<Project"), Tok::Punct);
        assert_eq!(tok(Lang::Xml, src, "Project"), Tok::Tag);
        assert_eq!(tok(Lang::Xml, src, "Sdk"), Tok::AttrName);
        assert_eq!(tok(Lang::Xml, src, "\"Microsoft"), Tok::String);
        assert_eq!(tok(Lang::Xml, src, "x:Name"), Tok::Tag);
        assert_eq!(tok(Lang::Xml, src, "'b'"), Tok::String);
        assert_eq!(tok(Lang::Xml, src, "t &"), Tok::Text);
        assert_eq!(tok(Lang::Xml, src, "&amp;"), Tok::Escape);
        assert_eq!(tok(Lang::Xml, src, "<brut>"), Tok::String);
        assert_eq!(tok(Lang::Xml, src, "/>"), Tok::Punct);

        let ini = "# Manifeste\nname     = UIKit\n[section]\n; vieux\ncount = 42\nflag = true\nhelp = a ; b : c\npath = \"x y\"\n";
        assert_eq!(tok(Lang::Ini, ini, "# Manifeste"), Tok::Comment);
        assert_eq!(span_of(Lang::Ini, ini, "name").unwrap().0.len(), 4);
        assert_eq!(tok(Lang::Ini, ini, "name"), Tok::AttrName);
        assert_eq!(tok(Lang::Ini, ini, "= UIKit"), Tok::Operator);
        assert_eq!(tok(Lang::Ini, ini, "UIKit"), Tok::Text);
        assert_eq!(tok(Lang::Ini, ini, "[section]"), Tok::Type);
        assert_eq!(tok(Lang::Ini, ini, "; vieux"), Tok::Comment);
        assert_eq!(tok(Lang::Ini, ini, "42"), Tok::Number);
        assert_eq!(tok(Lang::Ini, ini, "true"), Tok::Constant);
        assert_eq!(tok(Lang::Ini, ini, "; b"), Tok::Text);
        assert_eq!(tok(Lang::Ini, ini, "\"x y\""), Tok::String);
    }

    #[test]
    fn unterminated_and_unicode() {
        let cases = [
            "/* jamais fermé",
            "\"chaîne ouverte",
            "r#\"brut",
            "'",
            "'é",
            "#[attr",
            "$\"{",
            "@\"",
            "\"\"\"",
            "f'{",
            "f\"{x!",
            "```",
            "<!--",
            "<a href=\"",
            "${",
            "<<EOF\n",
            "/'",
            "R\"x(",
            "é日本\u{1F600}",
            "'\u{1F600}'",
            "b'\u{e9}",
            "x = '\u{e9}",
            "\\",
            "$",
            "#",
            "&",
            "[",
            "`",
            "*",
            "_",
            "~~",
        ];
        for &l in Lang::all() {
            for c in cases {
                hl(l, c);
                hl(l, &format!("{c}{c}\n{c}"));
            }
        }
        assert_eq!(tok(Lang::Rust, "let é = 'é';", "'é'"), Tok::Char);
        assert_eq!(tok(Lang::C, "/* jamais fermé", "fermé"), Tok::Comment);
    }

    /// Générateur pseudo-aléatoire (xorshift) : pas de dépendance.
    struct Rng(u64);
    impl Rng {
        fn next(&mut self) -> u64 {
            self.0 ^= self.0 << 13;
            self.0 ^= self.0 >> 7;
            self.0 ^= self.0 << 17;
            self.0
        }
    }

    #[test]
    fn fuzz_invariants() {
        const FRAGS: &[&str] = &[
            "\"",
            "'",
            "`",
            "```",
            "~~~",
            "/*",
            "*/",
            "//",
            "///",
            "#",
            "#[",
            "]",
            "[",
            "{",
            "}",
            "(",
            ")",
            "r#\"",
            "\"#",
            "r\"",
            "b'",
            "$\"",
            "@\"",
            "$@\"",
            "\"\"\"",
            "'''",
            "f\"",
            "rb'",
            "\\",
            "\\n",
            "\n",
            "\r\n",
            " ",
            "\t",
            "é",
            "日本",
            "\u{1F600}",
            "a",
            "x0",
            "rax",
            "%eax",
            "$1",
            "${",
            "$(",
            "<<EOF\n",
            "EOF\n",
            "<!--",
            "-->",
            "<",
            ">",
            "/>",
            "&amp;",
            "&",
            "/'",
            "'/",
            "REM ",
            "**",
            "*",
            "_",
            "~~",
            "[a](b)",
            "fn",
            "let",
            "class",
            "def",
            "if",
            "0x",
            "1e+",
            "12",
            ".5",
            "'a",
            ":",
            "=",
            ";",
            ",",
            "!",
            "#include <",
            "R\"d(",
            ")d\"",
            "macro",
            "db",
            "@@:",
            "@b",
            "\t@echo",
            "$(X)",
            "key = ",
            "[[",
            "]]",
            "\\\n",
            "u8\"",
            "L'",
        ];
        let mut rng = Rng(0x9E37_79B9_7F4A_7C15);
        let iters = std::env::var("KS_FUZZ_ITERS").ok().and_then(|v| v.parse().ok()).unwrap_or(1500);
        for _ in 0..iters {
            let n = (rng.next() % 40) as usize;
            let mut t = String::new();
            for _ in 0..n {
                t.push_str(FRAGS[(rng.next() % FRAGS.len() as u64) as usize]);
            }
            for &l in Lang::all() {
                hl(l, &t);
            }
        }
        // octets au hasard, rendus valides en UTF-8
        for _ in 0..300 {
            let n = (rng.next() % 200) as usize;
            let bytes: Vec<u8> = (0..n).map(|_| (rng.next() % 256) as u8).collect();
            let t = String::from_utf8_lossy(&bytes);
            for &l in Lang::all() {
                hl(l, &t);
            }
        }
    }

    #[test]
    fn hostile_lines_stay_fast() {
        // motifs qui rendraient un lexeur naïf quadratique
        let n = 200_000;
        let hostile = [
            "[".repeat(n),
            "*a ".repeat(n / 3),
            "`".repeat(3) + &"` ".repeat(n / 2),
            "#[".repeat(n / 2),
            "$\"{".repeat(n / 3),
            "r#".repeat(n / 2) + "\"",
            "<a ".repeat(n / 3),
        ];
        for t in &hostile {
            for &l in Lang::all() {
                let t0 = std::time::Instant::now();
                hl(l, t);
                let ms = t0.elapsed().as_millis();
                assert!(ms < 2000, "{l:?} trop lent ({ms} ms) sur un motif hostile");
            }
        }
    }

    fn gen_c(size: usize) -> String {
        let chunk = r#"/* Gestion des tampons : allocation, copie et libération. */
#include <stdio.h>
#include "korin.h"
#define BUF_LEN 4096
static uint32_t counter = 0x1Fu; // compteur global
typedef struct { int x, y; char *name; } point_t;

/** Copie `n` octets. */
int copy_buf(char *dst, const char *src, size_t n) {
    for (size_t i = 0; i < n; i++) {
        dst[i] = src[i] ^ 'a';
    }
    if (n > BUF_LEN && dst != NULL) {
        printf("trop long : %zu\n", n);
        return -1;
    }
    double ratio = 1.5e-3 * (double)n;
    counter += (uint32_t)ratio;
    return 0;
}

"#;
        let mut s = String::with_capacity(size + chunk.len());
        while s.len() < size {
            s.push_str(chunk);
        }
        s
    }

    #[test]
    fn bench_1mb_c() {
        let src = gen_c(1 << 20);
        let _ = highlight(Lang::C, &src);
        let runs = 5;
        let t0 = std::time::Instant::now();
        let mut spans = 0;
        for _ in 0..runs {
            spans = highlight(Lang::C, &src).len();
        }
        let ms = t0.elapsed().as_secs_f64() * 1000.0 / runs as f64;
        eprintln!("1 Mo de C : {ms:.1} ms, {spans} intervalles");
        if !cfg!(debug_assertions) {
            assert!(ms < 50.0, "trop lent : {ms:.1} ms");
        }
    }
}
