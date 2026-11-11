#!/usr/bin/env python3
# tools/rg/difftest.py -- essais differentiels de rg (docs/RG.md) : le vrai ripgrep (14.1) et notre rg
# construit pour le PC (tools/rg/build.sh -> build/rg/host/rg) sur les memes arborescences, avec les memes
# arguments ; la sortie standard et le code de sortie doivent etre identiques octet pour octet.
#   python3 -I tools/rg/difftest.py [--ref /usr/bin/rg] [--rg build/rg/host/rg] [-v] [-k MOT]
# ripgrep est lance avec --sort path (un seul fil, ordre des noms : celui de notre rg) sauf si le cas trie
# deja ; dans --json, les durees (elapsed) sont masquees. HOME et XDG_CONFIG_HOME pointent vers un dossier
# vide (pas d'exclusions globales de l'utilisateur), sauf dans les cas « global ».
import argparse, os, re, shutil, subprocess, sys, tempfile, time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))


def w(path, data, mtime=None):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if isinstance(data, str):
        data = data.encode("utf-8")
    with open(path, "wb") as f:
        f.write(data)
    if mtime is not None:
        os.utime(path, (mtime, mtime))


def fixtures2(base):
    G = os.path.join(base, "gi")
    os.makedirs(os.path.join(G, ".git"))
    w(os.path.join(G, ".gitignore"),
      "*.LOG\n!keep.*\nsub/deep/\n**/gen/**\n/top.txt\nmid/*.o\na/**/b.txt\n*.c~\n\\!bang.txt\n#nope.txt\n \n"
      "{x,y}.brace\nk[0-9].txt\nsub/**/*.tmp\n.hid/\n")
    for k in ["top.txt", "sub/top.txt", "a.LOG", "keep.LOG", "sub/deep/x.c", "sub/deep2/x.c", "gen/q.c", "z/gen/r.c",
              "mid/m.o", "mid/sub/m.o", "a/b.txt", "a/x/y/b.txt", "q.c~", "!bang.txt", "#nope.txt", "x.brace", "z.brace",
              "k1.txt", "k12.txt", "sub/u/v.tmp", "sub/w.tmp", ".hid/h.txt", "top.c", "b.c", "sub/x/x.c", "sub/y/z/x.c",
              "UP.C", "sub/.gitignore", "sub/deep3/.ignore"]:
        if k.endswith(".gitignore"):
            w(os.path.join(G, k), "/x/\n!/deep/\n*.tmp\n")
        elif k.endswith(".ignore"):
            w(os.path.join(G, k), "*\n!*.keep\n")
        else:
            w(os.path.join(G, k), "hello %s\n" % k)
    w(os.path.join(G, "sub", "deep3", "a.keep"), "hello keep\n")
    w(os.path.join(G, "sub", "deep3", "a.other"), "hello other\n")
    B = os.path.join(base, "bb")
    line = b"hello " + b"x" * 57 + b"\n"          # 64 octets
    def mk(name, data):
        w(os.path.join(B, name), data)
    mk("b0", b"\x00hello\n")
    mk("b1", line * 1023 + b"hello" + b"\x00" + b"\n" + line)          # NUL juste avant 64 Kio
    mk("b2", line * 1024 + b"\x00\n" + line)                          # NUL a 64 Kio pile
    mk("b3", line * 1024 + b"hello\n" + b"\x00\n")                    # NUL apres 64 Kio
    mk("b4", line * 3000 + b"zz\x00\n" + line)                         # loin
    mk("b5", line * 10)
    mk("b6", b"hello" + b"y" * 70000 + b"\x00\nhello\n")               # premiere ligne de plus de 64 Kio
    mk("b7", line * 2000)
    mk("b8", b"a\x00b\nhello\n")
    mk("b9", line * 1100 + b"\x00")
    mk("b10", b"hello\n" * 5)


def fixtures(base):
    """Les arborescences d'essai : proj (un depot git), outer/inner (exclusions des parents), plain (sans git)."""
    p = os.path.join(base, "proj")
    t0 = 1700000000
    os.makedirs(os.path.join(p, ".git", "info"))
    w(os.path.join(p, ".git", "HEAD"), "ref: refs/heads/main\nhello from git\n")
    w(os.path.join(p, ".git", "info", "exclude"), "excluded_by_info.txt\n")
    w(os.path.join(p, ".gitignore"),
      "# commentaire\n*.log\n!keep.log\n/build\nnode_modules/\ndocs/**/*.tmp\n\\#hash.txt\ntrailing\\ \n"
      "sub/*.gen\n*.bak   \n/anchored.txt\nlogs/\n!logs/\nonly_dir/\n[ab].ign\nx?.q\n**/deep/**\n")
    w(os.path.join(p, ".ignore"), "ignored_by_dot.txt\n")
    w(os.path.join(p, ".rgignore"), "!ignored_by_dot.txt\nrgignored.md\n")
    files = {
        "src/main.rs": "fn main() {\n    println!(\"hello world\");\n    let x = foo_bar(1);\n}\n\nfn foo_bar(n: i32) -> i32 {\n    n + 1 // TODO: hello\n}\n",
        "src/lib.rs": "pub mod util;\n// Hello there\npub fn hello() -> &'static str { \"hello\" }\n",
        "src/util/mod.rs": "pub fn helper() {}\n// TODO later\n",
        "src/app.ts": "export function greet(name: string) {\n  return `Hello ${name}`;\n}\nconst hello = 1;\n",
        "src/view.tsx": "export const View = () => <div>hello</div>;\n",
        "src/index.js": "const x = require('x');\nfunction hello() { return 'hello'; }\nmodule.exports = { hello };\n",
        "src/mod.py": "def hello():\n    return 'hello'\n\nclass Hello:\n    pass\n",
        "src/c/a.c": "#include <stdio.h>\nint main(void) { printf(\"hello\\n\"); return 0; }\n",
        "src/c/a.h": "#define HELLO 1\n",
        "Makefile": "all:\n\techo hello\n",
        "README.md": "# Projet\n\nhello world, Hello World, HELLO.\n\nSee docs.\n",
        "docs/guide.md": "Guide\nhello in docs\n",
        "docs/a/b/c.tmp": "hello tmp\n",
        "docs/keep.tmp": "hello keep tmp\n",
        "app.log": "hello log\n",
        "keep.log": "hello keep log\n",
        "build/out.txt": "hello build\n",
        "sub/build/out.txt": "hello sub build\n",
        "node_modules/m/index.js": "hello nm\n",
        "sub/node_modules/n.js": "hello nm2\n",
        "sub/x.gen": "hello gen\n",
        "sub/important.gen": "hello important gen\n",
        "sub/local.txt": "hello local\n",
        "sub/other.txt": "hello other\n",
        "sub/.gitignore": "local.txt\n!important.gen\n",
        "#hash.txt": "hello hash\n",
        "trailing ": "hello trailing space\n",
        "x.bak": "hello bak\n",
        "anchored.txt": "hello anchored\n",
        "sub/anchored.txt": "hello sub anchored\n",
        "logs/a.txt": "hello logs\n",
        "only_dir": "hello only_dir is a file\n",
        "a.ign": "hello a.ign\n",
        "c.ign": "hello c.ign\n",
        "x1.q": "hello x1.q\n",
        "x12.q": "hello x12.q\n",
        "z/deep/q.txt": "hello deep\n",
        "ignored_by_dot.txt": "hello dot\n",
        "rgignored.md": "hello rgignored\n",
        "excluded_by_info.txt": "hello excluded\n",
        ".hidden.txt": "hello hidden\n",
        ".config/settings.json": "{\"hello\": true}\n",
        "unicode.txt": "café hello\nnaïve Été\n日本語 hello 日本\nemoji 😀 hello\nΣίσυφος σίσυφος\nstraße STRASSE\n",
        "crlf.txt": "line one\r\nhello crlf\r\nlast\r\n",
        "nonl.txt": "first\nhello no newline",
        "empty.txt": "",
        "ctx.txt": "".join("line %d%s\n" % (i, " hello" if i in (3, 5, 12, 20, 21, 30) else "") for i in range(1, 36)),
        "long.txt": ("x" * 600) + " hello\nshort hello\n" + ("y" * 499) + "\n" + ("h" * 500) + "\n" + ("hello " * 120) + "\n",
        "words.txt": "foo foobar barfoo foo_bar foo-bar (foo) foo.\nfoofoo\n-foo- x\nfoo\n",
        "regex.txt": "a.b a+b a*b\n(x) [y] {z}\nfoo123 bar456\ntab\there\nend$\n^start\n",
        "multi.txt": "start block\nmiddle\nend block\nother\nstart again\nend again\n",
        "nested/a/b/c/d/e/f.txt": "hello deep nest\n",
        "nested/a/b/g.txt": "hello g\n",
        "bin/small.bin": b"abc\x00hello\n",
        "bin/text.dat": b"hello not binary\n",
        "bin/late.bin": b"hello line\n" + b"abcdefghij\n" * 9000 + b"hello2\n" + b"z\x00z\n" + b"hello3\n",
        "bin/early.bin": b"hello line\n" + b"abcdefghij\n" * 2000 + b"z\x00z\n" + b"hello3\n",
        "bin/latin1.txt": b"caf\xe9 hello\nna\xefve\n",
        "big/many.txt": "".join("row %d %s\n" % (i, "hello" if i % 97 == 0 else "pad" * (i % 13)) for i in range(20000)),
        "dup/a.txt": "same hello\n",
        "dup/b.txt": "same hello\n",
        "Space Dir/file name.txt": "hello spaces\n",
        "UPPER.TXT": "HELLO UPPER\n",
    }
    for i, (k, v) in enumerate(sorted(files.items())):
        w(os.path.join(p, k), v, t0 + i * 10)
    # outer/inner : un depot dont la racine est au-dessus du dossier cherche
    o = os.path.join(base, "outer")
    os.makedirs(os.path.join(o, ".git"))
    w(os.path.join(o, ".gitignore"), "parent_ignored.txt\n/inner/rooted.txt\n*.skip\n")
    w(os.path.join(o, "inner", "parent_ignored.txt"), "hello parent\n")
    w(os.path.join(o, "inner", "rooted.txt"), "hello rooted\n")
    w(os.path.join(o, "inner", "x.skip"), "hello skip\n")
    w(os.path.join(o, "inner", "kept.txt"), "hello kept\n")
    w(os.path.join(o, "inner", "deeper", "parent_ignored.txt"), "hello parent deeper\n")
    # plain : sans .git, le .gitignore ne compte pas (require_git) ; .ignore oui
    q = os.path.join(base, "plain")
    w(os.path.join(q, ".gitignore"), "*.txt\n")
    w(os.path.join(q, ".ignore"), "b.md\n")
    w(os.path.join(q, "a.txt"), "hello plain\n")
    w(os.path.join(q, "a.md"), "hello md\n")
    w(os.path.join(q, "b.md"), "hello b md\n")
    # glob : les motifs -g et les arborescences de Claude Code
    g = os.path.join(base, "globs")
    # (dates toutes differentes, dans un ordre qui n'est pas celui des noms : --sort=modified ; a date egale,
    # ripgrep garde l'ordre du systeme de fichiers)
    for i, k in enumerate(["a.ts", "b.js", "c.tsx", "src/x.ts", "src/y/z.ts", "src/y/z.js", "lib/q.ts", "test/t.spec.ts",
                           "test/t.ts", "deep/a/b/c/d.ts", ".dot/e.ts", "README", "x.TS", "a,b.txt", "brace{x}.txt", "q[1].txt"]):
        w(os.path.join(g, k), "hello %s\n" % k, t0 + ((i * 7) % 16) * 13)
    return p


def claude_cases(P):
    """Les appels de Claude Code 2.1 (docs/RG.md, inventaire)."""
    vcs = []
    for d in [".git", ".svn", ".hg", ".bzr", ".jj", ".sl"]:
        vcs += ["--glob", "!" + d]
    grep = ["--hidden"] + vcs + ["--max-columns", "500"]
    c = []
    for pat, extra in [("hello", []), ("Hello", ["-i"]), ("fn\\s+\\w+", []), ("TODO", []), ("nomatch_zzz", []),
                       ("hello", ["--type", "rust"]), ("hello", ["--type", "js"]), ("hello", ["--type", "py"]),
                       ("hello", ["--glob", "*.rs"]), ("hello", ["--glob", "*.{ts,tsx}"]),
                       ("hello", ["--glob", "src/**/*.rs"]), ("hello", ["--glob", "**/*.md"]),
                       ("-foo", ["-e"]), ("hello", ["--iglob", "**/readme*"])]:
        # le motif qui commence par « - » : Claude Code passe -e
        if extra == ["-e"]:
            c.append(("grep-l -e", P, grep + ["-l", "--null", "-e", pat, P]))
            continue
        c.append(("grep-l %s %s" % (pat, extra), P, grep + extra + ["-l", "--null", pat, P]))
        c.append(("grep-c %s %s" % (pat, extra), P, grep + extra + ["-c", "-H", "--null", pat, P]))
        c.append(("grep-json %s %s" % (pat, extra), P, grep + extra + ["--json", "-n", pat, P]))
    for extra in [["-C", "2"], ["-B", "1"], ["-A", "1"], ["-A", "3", "-B", "2"], ["-o"], ["-i", "-o"],
                  ["-U", "--multiline-dotall"], ["-C", "0"]]:
        c.append(("grep-json-ctx %s" % extra, P, grep + ["--json", "-n"] + extra[:] + ["hello", P]))
    c.append(("grep-json multiline", P, grep + ["-U", "--multiline-dotall", "--json", "-n", "start.*?end", P]))
    c.append(("grep-json multiline2", P, grep + ["-U", "--multiline-dotall", "--json", "-n", "block\\nmiddle", P]))
    c.append(("grep-l multiline", P, grep + ["-U", "--multiline-dotall", "-l", "--null", "block\\nmiddle", P]))
    c.append(("grep-c multiline", P, grep + ["-U", "--multiline-dotall", "-c", "-H", "--null", "start.*?end", P]))
    # cible : un fichier, un sous-dossier
    c.append(("grep-json file", P, grep + ["--json", "-n", "hello", os.path.join(P, "ctx.txt")]))
    c.append(("grep-c file", P, grep + ["-c", "-H", "--null", "hello", os.path.join(P, "ctx.txt")]))
    c.append(("grep-l subdir", P, grep + ["-l", "--null", "hello", os.path.join(P, "src")]))
    c.append(("grep-json binfile", P, grep + ["--json", "-n", "hello", os.path.join(P, "bin", "early.bin")]))
    # Glob (outil) : --files --null --glob X --sort=modified --no-ignore --hidden
    for g in ["**/*.ts", "*.ts", "src/**/*.ts", "/src/**/*.ts", "**/*.{ts,tsx}", "**/README", "**/*", "deep/**",
              "**/t.*.ts", "*.TS"]:
        G = os.path.join(os.path.dirname(P), "globs")
        c.append(("glob %s" % g, G, ["--files", "--null", "--glob", g, "--sort=modified", "--no-ignore", "--hidden", G]))
    c.append(("glob proj", P, ["--files", "--null", "--glob", "**/*.rs", "--sort=modified", "--no-ignore", "--hidden", P]))
    # index des fichiers (@-mentions)
    fi = ["--files", "--follow", "--hidden"] + [x for d in [".git", ".svn", ".hg", ".bzr", ".jj", ".sl"] for x in ["--glob", "!%s/" % d]]
    c.append(("fileindex", P, fi + [P]))
    c.append(("fileindex no-vcs", P, fi + ["--no-ignore-vcs", P]))
    c.append(("files md", P, ["--files", "--hidden", "--follow", "--no-ignore", "--glob", "*.md", P]))
    c.append(("files depth4", P, ["--files", "--hidden", "--no-ignore", "--max-depth", "4", "--glob", "*.txt", P]))
    c.append(("files count", P, ["--files", "--hidden", "--glob", "!node_modules", P]))
    c.append(("files st", P, ["--files", "--hidden", "--max-depth", "4", "-g", "!.git", "-g", "!node_modules", "-g", "*.json", P]))
    c.append(("models", P, ["--no-config", "--line-buffered", "--only-matching", "--with-filename", "--null",
                            "--no-line-number", "--no-heading", "--max-count", "20", "--max-filesize", "1M", "--max-depth", "8",
                            "-g", "!node_modules", "-g", "!site-packages", "-g", "!dist-packages", "-g", "!vendor",
                            "-g", "!third_party", "-g", "!*.lock", "-e", "[A-Za-z0-9._]*(?:hello|foo_bar)[A-Za-z0-9@_-]*", P]))
    c.append(("buckets", P, ["-o", "-H", "--no-line-number", "--no-messages", "--no-heading", "--color=never", "--null",
                             "--hidden", "-g", "!.git", "-g", "!node_modules", "-g", "*.toml", "-g", "*.yaml", "-g", "*.yml",
                             "-g", "*.json", "-g", "*.cfg", "--max-filesize", "4M", "-e", "[a-z0-9.+-]?(hello|true)", P]))
    c.append(("sandbox scan", P, ["--files", "--hidden", "--no-ignore", "--no-config", "--line-buffered", "--max-depth", "3",
                                  "--iglob", "**/.git/hooks/**", "--iglob", "**/.git/HEAD", "-g", "!**/node_modules/**", P]))
    return c


def edge_cases(base, P):
    c = []
    O = os.path.join(base, "outer")
    Q = os.path.join(base, "plain")
    simple = [
        ["hello"], ["hello", "."], ["-n", "hello"], ["-N", "hello"], ["--column", "hello"], ["-c", "hello"],
        ["--count-matches", "hello"], ["-l", "hello"], ["--files-without-match", "hello"], ["-o", "hello"],
        ["-i", "HELLO"], ["-S", "hello"], ["-S", "Hello"], ["-s", "Hello"], ["-F", "a.b"], ["-F", "(x)"],
        ["a.b"], ["-w", "foo"], ["-x", "foo"], ["-v", "line"], ["-v", "-c", "line"], ["-e", "foo", "-e", "bar"],
        ["-n", "-A", "2", "hello"], ["-n", "-B", "2", "hello"], ["-n", "-C", "3", "hello"], ["-C", "1", "-m", "1", "hello"],
        ["-m", "2", "hello"], ["-m", "1", "-A", "2", "hello"], ["--heading", "-n", "hello"], ["--no-heading", "hello"],
        ["-H", "hello", "ctx.txt"], ["--no-filename", "hello"], ["--null", "hello"], ["-0", "-l", "hello"],
        ["--max-columns", "500", "hello"], ["--max-columns", "10", "-n", "hello"], ["-M", "20", "--max-columns-preview", "hello"],
        ["--max-columns", "500", "-o", "hello"], ["--max-columns", "5", "--column", "hello"],
        ["--files"], ["--files", "--hidden"], ["--files", "--no-ignore"], ["--files", "-u"], ["--files", "-uu"],
        ["--files", "--no-ignore-vcs"], ["--files", "--no-ignore-dot"], ["--files", "-g", "*.rs"], ["--files", "-g", "!*.rs"],
        ["--files", "-g", "src/**"], ["--files", "-g", "!src/"], ["--files", "-g", "!src"], ["--files", "-t", "rust"],
        ["--files", "-T", "rust"], ["--files", "-t", "md", "-t", "py"], ["--files", "--max-depth", "1"],
        ["--files", "--max-depth", "0"], ["--files", "-d", "2", "--hidden"], ["--files", "--null"],
        ["--files", "--sort", "path"], ["--files", "--sortr", "path"], ["--files", "--sort", "modified"],
        ["--files", "--sortr", "modified"], ["--files", "-g", "*.{md,py}", "-g", "!README*"],
        ["--files", "--iglob", "*.TXT"], ["--files", "-g", "*.TXT", "--glob-case-insensitive"],
        ["--files", "--type-add", "foo:*.foo", "-t", "foo"], ["--files", "-t", "all"],
        ["--files", "--max-filesize", "100"], ["--files", "--max-filesize", "1K"],
        ["hello", "src", "docs"], ["hello", "src/main.rs"], ["hello", "src/main.rs", "src/lib.rs"],
        ["hello", "nonexistent"], ["--json", "hello"], ["--json", "-C", "1", "hello", "ctx.txt"], ["--json", "nomatch_zz"],
        ["--json", "-v", "line", "ctx.txt"], ["--json", "-U", "start.*\\n.*middle", "multi.txt"],
        ["-U", "-n", "block\\nmiddle"], ["-U", "-c", "end"], ["-U", "-o", "start.*\\nmiddle"],
        ["--vimgrep", "hello"], ["-b", "hello"], ["--trim", "hello", "src"], ["-o", "-b", "hello"],
        ["--context-separator", "##", "-C", "1", "hello", "ctx.txt"], ["--no-context-separator", "-A", "1", "hello", "ctx.txt"],
        ["--field-match-separator", "=", "-n", "hello", "ctx.txt"], ["-c", "--include-zero", "hello", "src"],
        ["-q", "hello"], ["-q", "nomatch_zz"], ["-l", "-m", "1", "hello"],
        ["-x", ""], ["", "empty.txt"], ["-c", ""], ["^$"], ["-n", "$", "nonl.txt"], ["-o", "x*", "words.txt"],
        ["-o", "\\b"], ["--json", "b*", "words.txt"], ["--json", "-o", "hello", "unicode.txt"],
        ["-i", "été", "unicode.txt"], ["-i", "σίσυφος", "unicode.txt"], ["-i", "STRASSE", "unicode.txt"],
        ["\\w+", "unicode.txt"], ["-o", "\\w+", "unicode.txt"], ["--column", "hello", "unicode.txt"],
        ["crlf$", "crlf.txt"], ["crlf\\r$", "crlf.txt"], ["--json", "hello", "crlf.txt"],
        ["hello", "bin"], ["-c", "hello", "bin"], ["-l", "hello", "bin"], ["--files-without-match", "hello", "bin"],
        ["-a", "hello", "bin"], ["--binary", "hello", "bin"], ["--json", "hello", "bin"],
        ["hello", "bin/small.bin"], ["hello", "bin/early.bin", "bin/late.bin"], ["-c", "hello", "bin/early.bin", "bin/late.bin"],
        ["-o", "hello", "bin/early.bin"], ["--json", "hello", "bin/late.bin"],
        ["hello", "bin/latin1.txt"], ["--json", "caf", "bin/latin1.txt"],
        ["-w", "-o", "foo", "words.txt"], ["-x", "foo", "words.txt"], ["-w", "-c", "foo", "words.txt"],
        ["-F", "-x", "foo", "words.txt"], ["-n", "row 9[0-9]*00", "big"], ["-c", "hello", "big"],
        ["--sort", "path", "-l", "same"], ["--sortr", "path", "-l", "same"], ["-t", "rust", "-c", "fn"],
        ["-g", "*.md", "-i", "hello"], ["-g", "!*.md", "-c", "hello"], ["-T", "md", "-l", "hello"],
        ["-g", ".*", "--files"], ["-g", "*.txt", "--files"], ["--hidden", "-g", "!.git", "-l", "hello"],
        ["--hidden", "-l", "hello"], ["-L", "-l", "hello"], ["-j", "1", "-c", "hello"],
        ["--color", "never", "hello", "src"], ["--color=auto", "hello", "src"],
        ["-e", "a(", "x"], ["-g", "a[", "x"], ["-t", "nosuchtype", "x"], ["--bogus"], ["-A", "x", "y"],
        ["-f", "pats.txt"], ["-f", "pats.txt", "-c"], ["-F", "-f", "pats.txt", "-o"],
        ["-n", "hello", "Space Dir"], ["-l", "hello", "./src"], ["-l", "hello", "src/"], ["-l", "hello", "./"],
        ["--no-ignore-parent", "--files"], ["--files", "--no-ignore", "--hidden", "-g", "!.git"],
        ["--stats", "-q", "hello"],
    ]
    w(os.path.join(P, "pats.txt"), "foo_bar\nTODO\n(x)\n")
    for a in simple:
        c.append((" ".join(a), P, a))
    for a in [["--files"], ["hello"], ["-c", "hello"], ["--files", "--no-ignore-parent"], ["--files", "deeper"]]:
        c.append(("outer " + " ".join(a), os.path.join(O, "inner"), a))
    c.append(("outer abs", O, ["--files", os.path.join(O, "inner")]))
    for a in [["--files"], ["hello"], ["--files", "--no-require-git"]]:
        c.append(("plain " + " ".join(a), Q, a))
    c.append(("global", P, ["--files", "--hidden", "-g", "!.git"], "global"))
    # gitignore : formes rares
    G = os.path.join(base, "gi")
    for a in [["--files"], ["--files", "--hidden"], ["--files", "sub"], ["--files", "./sub"], ["--files", "sub/deep"],
              ["--files", "-g", "*.c"], ["--files", "-g", "!*.c", "--no-ignore"], ["--files", "--ignore-file-case-insensitive"],
              ["--files", "-g", "**/k?.txt"], ["--files", "-g", "[ab]*"], ["--files", "-g", "*[!a-c].txt"],
              ["--files", "-g", "sub/**"], ["--files", "-g", "**/deep/**"], ["--files", "-g", "/top.c"],
              ["--files", "-g", "{top,b}.c"], ["--files", "-g", "sub/*/x.c"], ["--files", "-g", "!sub/", "-g", "*.c"],
              ["--files", "-g", "*.c", "-g", "!b.c"], ["--files", "-g", "!b.c", "-g", "*.c"], ["--files", "--iglob", "*.C"],
              ["hello", "keep.LOG"], ["hello", "-g", "*.txt", "keep.LOG"], ["--files", ".hid"], ["--files", "--max-depth", "2"]]:
        c.append(("gi " + " ".join(a), G, a))
    # entree standard
    c.append(("stdin hello", P, ["hello"], None, b"a hello\nb\nhello c\n"))
    c.append(("stdin -", P, ["-n", "hello", "-"], None, b"a hello\nb\nhello c\n"))
    c.append(("stdin json", P, ["--json", "hello"], None, b"a hello\nb\x00x\nhello c\n"))
    c.append(("stdin bin", P, ["hello"], None, b"a hello\nb\x00x\nhello c\n"))
    c.append(("stdin -c bin", P, ["-c", "hello"], None, b"a hello\nb\x00x\nhello c\n"))
    c.append(("stdin + file", P, ["hello", "-", "ctx.txt"], None, b"x hello\n"))
    c.append(("stdin files", P, ["--files"], None, b"x hello\n"))
    # binaire : NUL aux frontieres des tampons de 64 Kio
    for a in [["-c", "hello", "bb"], ["hello", "bb"], ["--json", "hello", "bb"], ["-l", "hello", "bb"], ["-n", "-A", "2", "hello", "bb"],
              ["-U", "-c", "hello", "bb"], ["--files-without-match", "hello", "bb"], ["-m", "1", "--json", "hello", "bb"],
              ["-c", "hello", "bb/b0", "bb/b1", "bb/b2", "bb/b3", "bb/b4", "bb/b5", "bb/b6", "bb/b7", "bb/b8", "bb/b9", "bb/b10"],
              ["hello", "bb/b0", "bb/b1", "bb/b2", "bb/b3", "bb/b4", "bb/b5", "bb/b6", "bb/b7", "bb/b8", "bb/b9", "bb/b10"],
              ["hello", "bb/b3"], ["-c", "hello", "bb/b3"], ["--json", "hello", "bb/b6"], ["-a", "-c", "hello", "bb"]]:
        c.append(("bb " + " ".join(a), base, a))
    # contexte, -m, -v
    for a in [["-n", "-C", "2", "hello", "ctx.txt", "nonl.txt"], ["--heading", "-n", "-C", "1", "hello", "ctx.txt", "src/lib.rs"],
              ["-n", "-A", "8", "hello", "ctx.txt"], ["-n", "-B", "8", "hello", "ctx.txt"], ["-n", "-m", "2", "-A", "3", "hello", "ctx.txt"],
              ["-n", "-m", "1", "-A", "10", "hello", "ctx.txt"], ["-n", "-v", "-C", "1", "line [1-3]", "ctx.txt"],
              ["--json", "-m", "2", "-A", "3", "hello", "ctx.txt"], ["--json", "-v", "-m", "3", "line", "ctx.txt"],
              ["-c", "-m", "3", "hello", "ctx.txt"], ["-n", "-C", "1", "hello", "big/many.txt"], ["--json", "-C", "3", "hello", "big"],
              ["-n", "-U", "-C", "1", "line 3.*\\n.*line 5", "ctx.txt"], ["-U", "-v", "-c", "hello\\nline", "ctx.txt"],
              ["--json", "-U", "hello\\nline 2", "ctx.txt"], ["--json", "-U", "o", "multi.txt"], ["-U", "-c", "o", "multi.txt"],
              ["--count-matches", "-U", "o", "multi.txt"], ["-o", "-U", "(?s)start.*?end", "multi.txt"],
              ["-y", "x"], ["-n", "y", "long.txt", "--max-columns", "500"], ["-n", "h", "long.txt", "--max-columns", "500"],
              ["-o", "--max-columns", "3", "hello", "ctx.txt"], ["--max-columns", "8", "-C", "1", "hello", "ctx.txt"],
              ["-o", "hello", "crlf.txt"], ["--json", "-o", "crlf", "crlf.txt"], ["-w", "été", "unicode.txt"],
              ["-S", "\\Whello"], ["-S", "\\S+ Hello"], ["-c", "-i", "ß", "unicode.txt"], ["-i", "-o", "σ", "unicode.txt"],
              ["hello", "src/main.rs", "-g", "*.py"], ["hello", ".hidden.txt"], ["--files", "src/../docs"],
              ["--files", "."], ["--files", "src", "src"], ["-l", "", "empty.txt", "nonl.txt"], ["-c", "^", "empty.txt", "nonl.txt"],
              ["--json", "", "nonl.txt"], ["-o", "$", "nonl.txt"], ["-n", "--column", "-o", "o", "words.txt"],
              ["--vimgrep", "foo", "words.txt"], ["--null", "-c", "foo", "words.txt", "regex.txt"],
              ["-F", "-e", "a.b", "-e", "a+b", "-o", "regex.txt"], ["-x", "-F", "end$", "regex.txt"], ["\\$$", "regex.txt"],
              ["--json", "-e", "foo", "-e", "o", "words.txt"], ["-w", "foo|bar", "words.txt"], ["-c", "(?i)HELLO"],
              ["--files", "-t", "c"], ["--type-list"], ["--files", "--type-clear", "rust", "--type-add", "rust:*.c", "-t", "rust"],
              ["--files", "--type-add", "mine:include:rust,py", "-t", "mine"]]:
        c.append(("ctx " + " ".join(a), P, a))
    # expressions courantes (Rust regex de ripgrep / PCRE2 ici)
    R = os.path.join(base, "re")
    w(os.path.join(R, "code.ts"), "import { a } from './a';\nexport interface Foo {}\nclass Bar extends Baz {\n  async run(x: number): Promise<void> {\n"
      "    await fetch('http://x/y?z=1').then(r => r.json());\n    // TODO: fix FIXME later\n    const s = \"quoted \\\"str\\\"\";\n"
      "    if (x >= 10 && x <= 20) { return; }\n  }\n}\nfunction  foo_bar1 ( ) {}\nconst re = /a+b*/g;\n\tindented\twith tabs\n"
      "αβγ Ελληνικά 123 4567\nmixed CamelCase snake_case kebab-case\nemail me@example.com 2024-01-15\n")
    for pat in ["\\bfoo\\b", "(?i)todo", "[[:alpha:]]+_case", "\\d{2,}", "TODO|FIXME", "^\\s*import", "class\\s+\\w+",
                "function\\s*\\w*\\s*\\(", "\\.then\\(", "\"[^\"]*\"", "\\p{Greek}+", "(?P<n>Bar)", "[^\\x00-\\x7F]+",
                "interface \\w+ \\{\\}", "a\\{", "\\x41|\\x61", "x >= \\d+", "\\w+@\\w+\\.com", "\\d{4}-\\d{2}-\\d{2}",
                "^\\t", "\\s+$", "[A-Z][a-z]+[A-Z]", "(?m)^}$", "async\\s+\\w+\\(.*\\)", "\\$", "=>", "[{}]",
                "(?x) foo _ bar", "\\Bcase", "^$", "x\\b", "\\S+\\.json\\(\\)", "(\\w)\\1"]:
        for extra in [[], ["-o"], ["--json"], ["-c"], ["-i"], ["-w"]]:
            c.append(("re %s %s" % (pat, extra), R, extra + ["-e", pat]))
    return c


def norm(b):
    b = re.sub(rb'"elapsed":\{"secs":\d+,"nanos":\d+,"human":"[0-9.]+s"\}', b'"elapsed":{}', b)
    b = re.sub(rb'"elapsed_total":\{"human":"[0-9.]+s","nanos":\d+,"secs":\d+\}', b'"elapsed_total":{}', b)
    b = re.sub(rb'"elapsed":\{"human":"[0-9.]+s","nanos":\d+,"secs":\d+\}', b'"elapsed":{}', b)
    b = re.sub(rb'\n[0-9.]+ seconds spent searching\n[0-9.]+ seconds\n$', b'\n(durees)\n', b)
    return b


def run(cmd, cwd, env, data=None):
    if data is None:
        r = subprocess.run(cmd, cwd=cwd, env=env, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    else:
        r = subprocess.run(cmd, cwd=cwd, env=env, input=data, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return r.returncode, r.stdout, r.stderr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ref", default="/usr/bin/rg")
    ap.add_argument("--rg", default=os.path.join(ROOT, "build", "rg", "host", "rg"))
    ap.add_argument("-v", action="store_true")
    ap.add_argument("-k", default=None, help="seulement les cas dont le nom contient ce mot")
    ap.add_argument("--keep", action="store_true")
    a = ap.parse_args()
    base = tempfile.mkdtemp(prefix="rgdiff-")
    try:
        P = fixtures(base)
        fixtures2(base)
        home = os.path.join(base, "home")
        os.makedirs(home)
        ghome = os.path.join(base, "ghome")
        w(os.path.join(ghome, ".config", "git", "ignore"), "*.md\n.config/\n")
        env = {"PATH": os.environ.get("PATH", "/usr/bin:/bin"), "HOME": home, "LC_ALL": "C"}
        cases = claude_cases(P) + edge_cases(base, P)
        ok = bad = 0
        t_ref = t_rg = 0.0
        for case in cases:
            name, cwd, args = case[0], case[1], case[2]
            if a.k and a.k not in name:
                continue
            e = dict(env)
            if len(case) > 3 and case[3] == "global":
                e["HOME"] = ghome
            sorting = any(x.startswith("--sort") for x in args)
            refargs = ([] if sorting else ["--sort", "path"]) + args
            # (ripgrep lit ses options avant « -e » ; --sort en tete ne change pas le sens)
            t = time.time()
            data = case[4] if len(case) > 4 else None
            rc1, o1, e1 = run([a.ref] + refargs, cwd, e, data)
            t_ref += time.time() - t
            t = time.time()
            rc2, o2, e2 = run([a.rg] + args, cwd, e, data)
            t_rg += time.time() - t
            o1n, o2n = norm(o1), norm(o2)
            if args and args[0] in ("--version",):
                o1n, o2n = o1n.split(b"\n")[0], o2n.split(b"\n")[0]
            if rc1 == rc2 and o1n == o2n:
                ok += 1
                if a.v:
                    print("ok   %s" % name)
            else:
                bad += 1
                print("DIFF %s  (cwd %s)\n     args: %s\n     exit ripgrep %d, rg %d" % (name, os.path.relpath(cwd, base), args, rc1, rc2))
                l1, l2 = o1n.split(b"\n"), o2n.split(b"\n")
                shown = 0
                for i in range(max(len(l1), len(l2))):
                    x = l1[i] if i < len(l1) else b"<fin>"
                    y = l2[i] if i < len(l2) else b"<fin>"
                    if x != y:
                        print("     ligne %d\n       ripgrep: %r\n       rg:      %r" % (i + 1, x[:300], y[:300]))
                        shown += 1
                        if shown >= 3:
                            break
                if e1 != e2 and a.v:
                    print("     stderr ripgrep: %r\n     stderr rg:      %r" % (e1[:300], e2[:300]))
        print("rg difftest : %d identiques, %d differents (sur %d) ; ripgrep %.2f s, rg %.2f s" % (ok, bad, ok + bad, t_ref, t_rg))
        return 0 if bad == 0 else 1
    finally:
        if a.keep:
            print("arborescences gardees : %s" % base)
        else:
            shutil.rmtree(base, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
