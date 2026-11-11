#!/usr/bin/env python3
# tools/rg/cardtest.py -- essais de rg sur la carte, dans QEMU (docs/RG.md, « Essais sur la carte »).
#   1. prepare une arborescence d'essai dans un dossier temporaire : src/ (les sources de Korin OS, copiees
#      du depot), repo/ (un depot git cree par le git du PC : .gitignore, .git), bin/ (fichiers binaires) ;
#   2. y lance le rg du PC (build/rg/host/rg, identique a ripgrep 14.1 : tools/rg/difftest.py) avec les
#      appels de Claude Code : les sorties attendues ;
#   3. copie le tout dans build/sdroot/t/rg avec un script bash qui relance chaque appel sur la carte et
#      compare (diff) a la sortie attendue ;
#   4. (--boot) ajoute la ligne du script a build/sdroot/etc/startup.txt, refait build/sd.img, demarre
#      QEMU (tools/qemu_drive.py) et lit build/uart.log.
# A lancer apres tools/rg/build.sh et tools/ci/carte_linux.py :
#   ARMPI_QEMU=/opt/qemu/bin/qemu-system-aarch64 python3 -I tools/rg/cardtest.py --boot [--wait 600]
# (startup.txt est regenere par carte_linux.py : ne pas le commiter.)
import argparse, os, re, shutil, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
HOSTRG = os.path.join(ROOT, "build", "rg", "host", "rg")
CARD = "SD0:/t/rg"
REF = os.environ.get("RG_REF", "/usr/bin/rg")


def stage(base):
    t = os.path.join(base, "rg")
    shutil.copytree(os.path.join(ROOT, "src"), os.path.join(t, "src"),
                    ignore=shutil.ignore_patterns("extra"))
    r = os.path.join(t, "repo")
    shutil.copytree(os.path.join(ROOT, "tools", "rg"), os.path.join(r, "rg"), ignore=shutil.ignore_patterns("__pycache__"))
    shutil.copytree(os.path.join(ROOT, "tools", "pkg"), os.path.join(r, "pkg"), ignore=shutil.ignore_patterns("__pycache__"))
    shutil.copytree(os.path.join(ROOT, "tools", "posix"), os.path.join(r, "posix"))
    with open(os.path.join(r, ".gitignore"), "w") as f:
        f.write("*.ini\n!versions.ini\n/posix/*.sh\n")
    os.makedirs(os.path.join(r, "build"))
    with open(os.path.join(r, "build", "ignored.txt"), "w") as f:
        f.write("hello build\n")
    with open(os.path.join(r, ".ignore"), "w") as f:
        f.write("build/\n")
    env = dict(os.environ, GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@t", GIT_COMMITTER_NAME="t", GIT_COMMITTER_EMAIL="t@t",
               HOME=base)
    subprocess.run(["git", "init", "-q", "-b", "main", r], check=True, env=env)
    subprocess.run(["git", "-C", r, "add", "-A"], check=True, env=env)
    subprocess.run(["git", "-C", r, "-c", "commit.gpgsign=false", "commit", "-q", "-m", "essai"], check=True, env=env)
    b = os.path.join(t, "bin")
    os.makedirs(b)
    line = b"hello " + b"x" * 57 + b"\n"
    for k, v in {"nul0": b"\x00hello\n", "late": line * 1100 + b"\x00\n" + line, "text": line * 3}.items():
        with open(os.path.join(b, k), "wb") as f:
            f.write(v)
    return t


def cases():
    vcs = []
    for d in [".git", ".svn", ".hg", ".bzr", ".jj", ".sl"]:
        vcs += ["--glob", "!" + d]
    g = ["--hidden"] + vcs + ["--max-columns", "500"]
    A = "@ABS@"          # remplace par le chemin absolu de l'arborescence (PC : le dossier temporaire ; carte : SD0:/t/rg)
    c = [
        ("version", ["--version"], "first"),
        ("l-src", g + ["-l", "--null", "SYS_VM_MAP", "src"], ""),
        ("l-src-i", g + ["-i", "-l", "--null", "framebuffer", "src"], ""),
        ("c-src", g + ["-c", "-H", "--null", "mov\\s+x0,\\s*x1", "src"], ""),
        ("json-src", g + ["--json", "-n", "SYS_VM_MAP", "src"], "json"),
        ("json-ctx", g + ["--json", "-n", "-C", "2", "E_NOSYS", "src/lib/posix"], "json"),
        ("json-o", g + ["--json", "-n", "-o", "-i", "copyright \\(c\\) \\d+", "src/lib/posix"], "json"),
        ("glob-inc", g + ["-l", "--null", "--glob", "*.inc", "SYS_", "src"], ""),
        ("type-c", g + ["-l", "--null", "--type", "c", "mmap", "src"], ""),
        ("multiline", g + ["-U", "--multiline-dotall", "--json", "-n", "static int\\s+\\w+\\s*\\(void\\)\\s*\\{", "src/lib/posix"], "json"),
        ("files-glob", ["--files", "--null", "--glob", "**/*.c", "--sort=modified", "--no-ignore", "--hidden", "src"], "bytes"),
        ("files-index", ["--files", "--follow", "--hidden", "--glob", "!.git/", "repo"], ""),
        ("files-index-novcs", ["--files", "--follow", "--hidden", "--glob", "!.git/", "--no-ignore-vcs", "repo"], ""),
        ("repo-l", g + ["-l", "--null", "hello", "repo"], ""),
        ("repo-json", g + ["--json", "-n", "-i", "pcre2", "repo"], "json"),
        ("repo-abs", g + ["-l", "--null", "hello", A + "/repo"], "abs"),
        ("repo-abs-json", g + ["--json", "-n", "fixtures", A + "/repo/rg"], "json abs"),
        ("files-abs", ["--files", "--hidden", "--glob", "!.git", A + "/repo"], "abs"),
        ("bin", ["-c", "hello", "bin"], ""),
        ("bin-json", ["--json", "hello", "bin"], "json"),
        ("bin-explicit", ["hello", "bin/nul0", "bin/late"], ""),
        ("std-n", ["-n", "-C", "1", "SYS_VM_MAP", "src/lib/posix/mman.c"], ""),
        ("count-all", ["-c", "e", "src"], "bytes"),
        ("files-all", ["--files", "src"], "bytes"),
        ("nomatch", ["zzzz_nomatch_qq", "src"], ""),
        ("badre", ["a(", "src"], ""),
    ]
    return c


def expected(t, cs):
    out = {}
    for name, args, mode in cs:
        a = [x.replace("@ABS@", t) for x in args]
        # la reference : ripgrep 14.1 lui-meme (un fil, ordre des noms), sinon notre rg du PC
        ref = [REF, "--sort", "path"] if os.path.exists(REF) and not any(x.startswith("--sort") for x in a) else [REF]
        if not os.path.exists(REF):
            ref = [HOSTRG]
        r = subprocess.run(ref + a, cwd=t, stdin=subprocess.DEVNULL, capture_output=True,
                           env={"PATH": "/usr/bin:/bin", "HOME": t})
        o = r.stdout
        if "abs" in mode:
            o = o.replace(t.encode(), CARD.encode())
        out[name] = (r.returncode, o)
    return out


def script(cs, exp):
    L = ["# rgtest.sh -- essais de rg sur la carte (tools/rg/cardtest.py) : chaque appel, sa duree, compare a la",
         "# sortie de rg sur le PC (sorties attendues dans exp/)",
         "cd %s || exit 1" % CARD,
         "TIMEFORMAT='%R'",
         "mkdir -p RAM0:/tmp",
         "echo \"RGTEST debut $(SD0:/bin/rg --version | head -1)\"",
         "ok=0; bad=0"]
    for name, args, mode in cs:
        a = " ".join("'" + x.replace("@ABS@", CARD).replace("'", "'\\''") + "'" for x in args)
        rc = exp[name][0]
        L.append("t=$( { time SD0:/bin/rg %s > RAM0:/tmp/rgo 2> RAM0:/tmp/rge; echo $? > RAM0:/tmp/rgc; } 2>&1 )" % a)
        L.append("c=$(cat RAM0:/tmp/rgc); n=$(wc -c < RAM0:/tmp/rgo)")
        if mode == "first":
            L.append("if [ \"$(head -1 RAM0:/tmp/rgo)\" = \"$(head -1 exp/%s.out)\" ]; then r=OK; else r=DIFF; fi" % name)
        elif mode == "bytes":
            L.append("if [ \"$n\" = \"$(wc -c < exp/%s.out)\" ] && [ \"$c\" = %d ]; then r=OK; else r=DIFF; fi" % (name, rc))
        elif "json" in mode:
            # (les durees de --json changent : on les masque des deux cotes)
            L.append("sed -E 's/\"(elapsed|elapsed_total)\":\\{[^}]*\\}/\"\\1\":{}/g%s' RAM0:/tmp/rgo > RAM0:/tmp/rgo2"
                     % ("; s/\"bytes_printed\":[0-9]+/\"bytes_printed\":0/g" if "abs" in mode else ""))
            L.append("if diff exp/%s.out RAM0:/tmp/rgo2 > /dev/null && [ \"$c\" = %d ]; then r=OK; else r=DIFF; fi" % (name, rc))
        else:
            L.append("if diff exp/%s.out RAM0:/tmp/rgo > /dev/null && [ \"$c\" = %d ]; then r=OK; else r=DIFF; fi" % (name, rc))
        L.append("if [ $r = OK ]; then ok=$((ok+1)); else bad=$((bad+1)); fi")
        L.append("echo \"RGTEST $r %s code=$c octets=$n duree=${t}s $(head -c 200 RAM0:/tmp/rge)\"" % name)
    # l'entree standard, le chemin implicite, RAM0:
    L.append("r=$(printf 'a hello\\nb\\n' | SD0:/bin/rg -n hello); echo \"RGTEST stdin [$r]\"")
    L.append("cd repo && r=$(SD0:/bin/rg -l hello | head -3 | tr '\\n' ' '); echo \"RGTEST implicite [$r]\"; cd ..")
    L.append("mkdir -p RAM0:/tmp/rgr/sub && echo 'hello ram' > RAM0:/tmp/rgr/sub/a.txt && echo 'x.txt' > RAM0:/tmp/rgr/.ignore"
             " && echo 'hello x' > RAM0:/tmp/rgr/x.txt")
    L.append("r=$(SD0:/bin/rg -l hello RAM0:/tmp/rgr | tr '\\n' ' '); echo \"RGTEST ram [$r]\"")
    L.append("r=$(cd RAM0:/tmp/rgr && SD0:/bin/rg --files); echo \"RGTEST ram-implicite [$r]\"")
    # le JIT de PCRE2 : la ligne de --version, et la meme recherche sans lui
    L.append("echo \"RGTEST jit [$(SD0:/bin/rg --version | grep JIT)]\"")
    L.append("t=$( { time SD0:/bin/rg -c -e 'mov\\s+x0,\\s*x1' src > /dev/null; } 2>&1 ); echo \"RGTEST avec-jit duree=${t}s\"")
    L.append("t=$( { time RG_NO_JIT=1 SD0:/bin/rg -c -e 'mov\\s+x0,\\s*x1' src > /dev/null; } 2>&1 ); echo \"RGTEST sans-jit duree=${t}s\"")
    L.append("n=$(SD0:/bin/rg --files src/lib src/kernel src/svc | wc -l); t=$( { time SD0:/bin/rg --json -n -i 'syscall' src/lib src/kernel src/svc > /dev/null; } 2>&1 ); echo \"RGTEST quelques-centaines fichiers=$n duree=${t}s\"")
    L.append("echo \"RGTEST fin ok=$ok diff=$bad\"")
    return "\n".join(L) + "\n"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--boot", action="store_true")
    ap.add_argument("--wait", type=int, default=480)
    a = ap.parse_args()
    base = tempfile.mkdtemp(prefix="rgcard-")
    t = stage(base)
    cs = cases()
    exp = expected(t, cs)
    os.makedirs(os.path.join(t, "exp"))
    for name, (rc, o) in exp.items():
        mode = [m for n, _, m in cs if n == name][0]
        if "json" in mode:
            o = re.sub(rb'"(elapsed|elapsed_total)":\{[^}]*\}', rb'"\1":{}', o)
        if "json" in mode and "abs" in mode:
            # (les chemins absolus n'ont pas la meme longueur sur le PC et sur la carte)
            o = re.sub(rb'"bytes_printed":[0-9]+', rb'"bytes_printed":0', o)
        with open(os.path.join(t, "exp", name + ".out"), "wb") as f:
            f.write(o)
    with open(os.path.join(t, "rgtest.sh"), "w") as f:
        f.write(script(cs, exp))
    dst = os.path.join(ROOT, "build", "sdroot", "t", "rg")
    shutil.rmtree(dst, ignore_errors=True)
    shutil.copytree(t, dst, symlinks=False)
    shutil.rmtree(base, ignore_errors=True)
    nfiles = sum(len(f) for _, _, f in os.walk(os.path.join(dst, "src")))
    print("build/sdroot/t/rg : %d fichiers dans src/, %d appels" % (nfiles, len(cs)))
    if not a.boot:
        return 0
    st = os.path.join(ROOT, "build", "sdroot", "etc", "startup.txt")
    lines = open(st, encoding="utf-8").read().splitlines()
    lines = [l for l in lines if "rgtest.sh" not in l] + ["SD0:/bin/bash %s/rgtest.sh" % CARD]
    open(st, "w", encoding="utf-8").write("\n".join(lines) + "\n")
    subprocess.run([sys.executable, "-I", os.path.join(ROOT, "tools", "mkdisk.py"), os.path.join(ROOT, "build", "sd.img"),
                    "--fat32", os.path.join(ROOT, "build", "sdroot"), "--exfat", os.path.join(ROOT, "build", "exfat_root")],
                   check=True, stdout=subprocess.DEVNULL, cwd=ROOT)
    scr = os.path.join(ROOT, "build", "rgtest-qemu.txt")
    open(scr, "w").write("wait %d\n" % a.wait)
    mods = ",".join("build/modules/%s.mod" % m for m in ["init", "list", "appkit", "sd", "vfs", "fatfs"])
    subprocess.run([sys.executable, "-I", os.path.join(ROOT, "tools", "qemu_drive.py"), scr, "--modules", mods], cwd=ROOT)
    log = open(os.path.join(ROOT, "build", "uart.log"), encoding="utf-8", errors="replace").read()
    for l in log.splitlines():
        if "RGTEST" in l:
            print(l[l.index("RGTEST"):])
    return 0


if __name__ == "__main__":
    sys.exit(main())
