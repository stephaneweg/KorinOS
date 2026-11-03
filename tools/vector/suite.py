"""suite.py -- la suite de tests de resvg (github.com/linebender/resvg-test-suite, MIT) contre Vector, sous QEMU.

  python -I tools/vector/suite.py prep SUITE TAG      carte build/try/TAG.img : tests/ en /rt, la liste, startup.txt
  python -I tools/vector/suite.py run TAG SECONDES VNC   lance QEMU, attend
  python -I tools/vector/suite.py get SUITE TAG        rend les images calculees dans build/try/TAG-out
  python -I tools/vector/suite.py cmp SUITE TAG [motif]  compare, par categorie ; ecrit build/try/TAG-fails.txt

Un cas passe si moins de 1 % des pixels different de plus de 32 (sur 255, composante la plus ecartee) une fois poses sur du
blanc. Le texte depend des polices : la carte n'a que DejaVu, la suite attend Noto ; il est compte a part.
"""
import os
import subprocess
import sys

import numpy as np
from PIL import Image


def wsl(path):
    p = os.path.abspath(path).replace("\\", "/")
    return "/mnt/%s%s" % (p[0].lower(), p[2:])


def sh(cmd):
    return subprocess.run(["wsl", "-d", "Ubuntu", "--", "bash", "-c", cmd], capture_output=True, text=True)


def offset(img):
    import struct
    with open(img, "rb") as f:
        mbr = f.read(512)
    return struct.unpack("<I", mbr[446 + 8:446 + 12])[0] * 512


def cases(suite):
    out = []
    root = os.path.join(suite, "tests")
    for d, _, fs in os.walk(root):
        for f in fs:
            if f.endswith(".svg"):
                out.append(os.path.relpath(os.path.join(d, f), root).replace("\\", "/"))
    return sorted(out)


def prep(suite, tag):
    img = "build/try/%s.img" % tag
    subprocess.check_call([sys.executable, "-I", "tools/office/tryimg.py", "new", tag])
    subprocess.check_call([sys.executable, "-I", "tools/office/tryimg.py", "put", tag, "build/office/sdroot/apps/vector.app", "/apps/vector.app"])
    os.makedirs("build/try/%s-list" % tag, exist_ok=True)
    with open("build/try/%s-list/list.txt" % tag, "w", newline="\n") as f:
        for c in cases(suite):
            with Image.open(os.path.join(suite, "tests", c[:-4] + ".png")) as im:
                f.write("SD0:/rt/%s\t%d %d\n" % (c, im.width, im.height))
    with open("build/try/%s-list/startup.txt" % tag, "w", newline="\n") as f:
        f.write("SD0:/apps/vector.app/main --delay 30 --batch SD0:/rt/list.txt SD0:/rt/report.txt\n")
    off = offset(img)
    sh("export MTOOLS_SKIP_CHECK=1; mmd -i '%s@@%d' ::/home/fonts; mcopy -i '%s@@%d' -n -o -Q '%s'/*.ttf ::/home/fonts/" % (wsl(img), off, wsl(img), off, wsl(os.path.join(suite, "fonts"))))
    i = "'%s@@%d'" % (wsl(img), off)
    # (sans prechargements : vfs reste libre pendant l'essai)
    with open("build/try/%s-list/preload.txt" % tag, "w") as f:
        pass
    sh("export MTOOLS_SKIP_CHECK=1; mcopy -i %s -n -o '%s' ::/etc/preload.txt" % (i, wsl("build/try/%s-list/preload.txt" % tag)))
    sh("export MTOOLS_SKIP_CHECK=1; mmd -i %s ::/resources; mcopy -i %s -s -n -o -Q '%s'/* ::/resources/" % (i, i, wsl(os.path.join(suite, "resources"))))
    r = sh("export MTOOLS_SKIP_CHECK=1; mmd -i %s ::/rt; mcopy -i %s -s -n -o -Q '%s'/* ::/rt/ && mcopy -i %s -n -o '%s' ::/rt/list.txt && mcopy -i %s -n -o '%s' ::/etc/startup.txt && echo copied"
           % (i, i, wsl(os.path.join(suite, "tests")), i, wsl("build/try/%s-list/list.txt" % tag), i, wsl("build/try/%s-list/startup.txt" % tag)))
    print(r.stdout[-200:], r.stderr[-300:])


def run(tag, secs, vnc):
    with open("build/try/%s-run.txt" % tag, "w", newline="\n") as f:
        f.write("screen 800x600\nwait %d\nshot build/try/%s-end.png\n" % (int(secs), tag))
    subprocess.call([sys.executable, "-I", "tools/qemu_drive.py", "build/try/%s-run.txt" % tag, "--sd", "build/try/%s.img" % tag,
                     "--kernel", "build/kernel_sd.img", "--port", "4%s" % (500 + int(vnc)), "--vnc", vnc, "--uart", "build/try/%s.log" % tag, "--ecran", "800x600"])


def get(tag):
    img = "build/try/%s.img" % tag
    out = "build/try/%s-out" % tag
    os.makedirs(out, exist_ok=True)
    r = sh("export MTOOLS_SKIP_CHECK=1; mcopy -i '%s@@%d' -s -n -o -Q ::/rt/* '%s'/" % (wsl(img), offset(img), wsl(out)))
    print(r.stderr[-300:])


def load(path):
    a = np.asarray(Image.open(path).convert("RGBA"), dtype=np.float32)
    al = a[..., 3:] / 255
    return a[..., :3] * al + 255 * (1 - al)


def cmp_(suite, tag, pat):
    out = "build/try/%s-out" % tag
    report = {}
    try:
        for l in open(os.path.join(out, "report.txt"), errors="replace"):
            p = l.split(" ", 1)
            if len(p) == 2 and p[1].startswith("SD0:/rt/"):
                report[p[1].strip()[8:]] = p[0]
    except OSError:
        print("pas de report.txt")
    stats, fails = {}, []
    # (results.csv, colonne resvg : 0 = comportement indefini, pas de reference a juger)
    import csv
    undefined = set()
    with open(os.path.join(suite, "results.csv")) as f:
        for row in csv.reader(f):
            if row[0] != "title" and row[4] == "0":
                undefined.add(row[0])
    for c in cases(suite):
        if pat and pat not in c:
            continue
        if c in undefined:
            continue
        cat = "/".join(c.split("/")[:2])
        st = stats.setdefault(cat, [0, 0, 0])
        st[0] += 1
        got = os.path.join(out, c[:-4] + ".out")
        ref = os.path.join(suite, "tests", c[:-4] + ".png")
        if not os.path.exists(ref):
            st[0] -= 1
            continue
        if not os.path.exists(got):
            st[2] += 1
            fails.append((100.0, c, report.get(c, "none")))
            continue
        try:
            a, b = load(got), load(ref)
        except Exception as e:
            st[2] += 1
            fails.append((100.0, c, "image"))
            continue
        if a.shape != b.shape:
            b2 = np.asarray(Image.fromarray(np.uint8(b)).resize((a.shape[1], a.shape[0])), dtype=np.float32)
            b = b2
        d = np.abs(a - b).max(axis=2)
        pct = float((d > 32).mean() * 100)
        if pct < 1:
            st[1] += 1
        else:
            fails.append((pct, c, report.get(c, "?")))
    total = [0, 0]
    for k in sorted(stats):
        n, ok, crash = stats[k]
        total[0] += n
        total[1] += ok
        print("%-40s %4d/%4d%s" % (k, ok, n, "  sans image: %d" % crash if crash else ""))
    print("TOTAL %d/%d (%.1f %%)" % (total[1], total[0], 100.0 * total[1] / max(total[0], 1)))
    with open("build/try/%s-fails.txt" % tag, "w") as f:
        for p, c, s in sorted(fails, key=lambda x: x[1]):
            f.write("%6.1f %s %s\n" % (p, s, c))


if __name__ == "__main__":
    a = sys.argv[1:]
    if a[0] == "prep":
        prep(a[1], a[2])
    elif a[0] == "run":
        run(a[1], a[2], a[3])
    elif a[0] == "get":
        get(a[2] if len(a) > 2 else a[1])
    else:
        cmp_(a[1], a[2], a[3] if len(a) > 3 else "")
