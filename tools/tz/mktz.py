# Fait skel/sd0/res/tz.txt : chaque fuseau de zone.tab (IANA) et sa regle
# actuelle au format POSIX (la derniere ligne d'un fichier TZif v2+), lue par
# netd (heure locale) et Setup (liste des fuseaux).
# Usage (WSL, ou tout systeme avec /usr/share/zoneinfo) :
#   python3 tools/tz/mktz.py [/usr/share/zoneinfo] [sortie]
import os, sys

zi = sys.argv[1] if len(sys.argv) > 1 else "/usr/share/zoneinfo"
out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(__file__), "..", "..", "skel", "sd0", "res", "tz.txt")

def rule(name):
    data = open(os.path.join(zi, name), "rb").read()
    if data[:4] != b"TZif" or data[4:5] < b"2":
        raise SystemExit("%s : pas de TZif v2" % name)
    return data.rstrip(b"\n").rsplit(b"\n", 1)[1].decode("ascii")

names = set()
for l in open(os.path.join(zi, "zone.tab"), encoding="utf-8"):
    if l.startswith("#") or not l.strip():
        continue
    names.add(l.split("\t")[2].strip())
version = open(os.path.join(zi, "tzdata.zi"), encoding="utf-8").readline().split()[-1]
lines = ["# Fuseaux horaires : nom IANA, regle POSIX (tzdata %s, tools/tz/mktz.py)" % version, "UTC UTC0"]
for n in sorted(names):
    r = rule(n)
    if not r or " " in r:
        raise SystemExit("%s : regle inattendue %r" % (n, r))
    lines.append("%s %s" % (n, r))
with open(out, "w", encoding="ascii", newline="\n") as f:
    f.write("\n".join(lines) + "\n")
print("%d fuseaux -> %s" % (len(lines) - 1, out))
