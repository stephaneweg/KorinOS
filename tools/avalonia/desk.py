"""desk : un essai sous QEMU depuis le terminal du bureau, clavier AZERTY (docs/AVALONIA.md).
    python -I tools/avalonia/desk.py nom "commande" [etape...]
Une etape : shot:30 (attendre 30 s, capture build/avalonia/t/nom-N.png), wait:10, click:x,y, type:texte, key:ret,
drag:x1,y1,x2,y2, wheel:x,y,n, until:mot (attend ce mot dans /tmp/av.log), run:commande du PC, log (relit /tmp/av.log).
"""
import subprocess
import sys

KEYS = {"a": "q", "q": "a", "z": "w", "w": "z", "m": "semicolon", "/": "shift-dot", ".": "shift-comma", "-": "6", "_": "8",
        " ": "spc", ":": "dot", ",": "m", ";": "comma", "!": "slash"}


def typed(text):
    out = []
    for c in text:
        if c.isdigit():
            out.append("key shift-" + c)
        elif c.isupper():
            k = KEYS.get(c.lower(), c.lower())
            out.append("key shift-" + k)
        else:
            out.append("key " + KEYS.get(c, c))
    return out


def main():
    name, cmd = sys.argv[1], sys.argv[2]
    t = "build/avalonia/t/"
    script = ["wait 30", "click 460 740", "wait 8"] + typed(cmd) + ["key ret"]
    n = 0
    for s in sys.argv[3:]:
        op, _, arg = s.partition(":")
        if op == "shot":
            n += 1
            script += ["wait " + arg, "shot %s%s-%d.png" % (t, name, n)]
        elif op == "wait":
            script.append("wait " + arg)
        elif op == "click":
            script.append("click " + arg.replace(",", " "))
        elif op == "dclick":
            script.append("dclick " + arg.replace(",", " "))
        elif op == "drag":
            script.append("drag " + arg.replace(",", " "))
        elif op == "wheel":
            script.append("wheel " + arg.replace(",", " "))
        elif op == "type":
            script += typed(arg)
        elif op == "key":
            script.append("key " + arg)
        elif op == "until":
            script.append("run python -I tools/avalonia/waitlog.py " + arg)
        elif op == "run":
            script.append("run " + arg)
        elif op == "log":
            script.append('run python -I tools/armpi_remote.py telnet 127.0.0.1:2371 "cat RAM0:/tmp/av.log"')
    open(t + name + ".txt", "w", encoding="utf-8").write("\n".join(script) + "\n")
    r = subprocess.run([sys.executable, "-I", "tools/qemu_drive.py", t + name + ".txt", "--port", "4471", "--vnc", "71",
                        "--sd", "build/avalonia/sd.img", "--uart", t + name + ".uart",
                        "--fwd", "2371:23,2171:21,2100:2100,2101:2101,2102:2102,2103:2103"], capture_output=True, text=True, errors="replace")
    print(r.stdout[-3000:])
    print(r.stderr[-1500:])


main()
