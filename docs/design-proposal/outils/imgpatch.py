"""Ecrit des fichiers dans une image FAT32 d'ARMPI (copie privee).
imgpatch.py img /etc/startup.txt=fichier_ou_texte ...  ; un fichier existant
garde ses clusters (4 Ko) : le contenu tient dans la chaine, ou la chaine s'allonge."""
import struct, sys
class Fat:
    def __init__(s, path):
        s.f = open(path, "r+b")
        s.f.seek(446); e = s.f.read(16)
        s.base = struct.unpack("<I", e[8:12])[0] * 512
        s.f.seek(s.base); b = s.f.read(512)
        s.spc = b[13]; s.res = struct.unpack("<H", b[14:16])[0]; s.nfat = b[16]
        s.fatsz = struct.unpack("<I", b[36:40])[0]; s.root = struct.unpack("<I", b[44:48])[0]
        s.data = s.base + (s.res + s.nfat * s.fatsz) * 512
        s.cs = s.spc * 512
    def cl(s, n): return s.data + (n - 2) * s.cs
    def fat(s, n, v=None):
        o = s.base + s.res * 512 + n * 4
        if v is None:
            s.f.seek(o); return struct.unpack("<I", s.f.read(4))[0] & 0x0FFFFFFF
        for i in range(s.nfat):
            s.f.seek(o + i * s.fatsz * 512); s.f.write(struct.pack("<I", v))
    def chain(s, n):
        r = []
        while 2 <= n < 0x0FFFFFF8:
            r.append(n); n = s.fat(n)
        return r
    def entries(s, cl):
        for c in s.chain(cl):
            s.f.seek(s.cl(c)); d = s.f.read(s.cs)
            lfn = ""
            for i in range(0, s.cs, 32):
                e = d[i:i+32]
                if e[0] == 0: return
                if e[0] == 0xE5: lfn = ""; continue
                if e[11] == 0x0F:
                    t = e[1:11] + e[14:26] + e[28:32]
                    lfn = t.decode("utf-16le", "ignore").split("\0")[0].rstrip("￿") + lfn
                    continue
                sn = e[:8].decode("latin1").strip()
                ex = e[8:11].decode("latin1").strip()
                nm = lfn or (sn + ("." + ex if ex else ""))
                lfn = ""
                yield nm, s.cl(c) + i, e
    def find(s, path):
        cl = s.root; pos = None; e = None
        for p in [x for x in path.split("/") if x]:
            for nm, o, en in s.entries(cl):
                if nm.lower() == p.lower():
                    pos, e = o, en
                    cl = struct.unpack("<H", en[20:22])[0] << 16 | struct.unpack("<H", en[26:28])[0]
                    break
            else: raise SystemExit("absent: " + path)
        return pos, e, cl
    def free(s, start=2):
        n = start
        while s.fat(n) != 0: n += 1
        return n
    def write(s, path, data):
        pos, e, cl = s.find(path)
        ch = s.chain(cl)
        need = max(1, (len(data) + s.cs - 1) // s.cs)
        while len(ch) < need:
            n = s.free(ch[-1]); s.fat(ch[-1], n); s.fat(n, 0x0FFFFFFF); ch.append(n)
        for i in range(need):
            s.f.seek(s.cl(ch[i])); s.f.write(data[i*s.cs:(i+1)*s.cs].ljust(s.cs, b"\0"))
        s.f.seek(pos + 28); s.f.write(struct.pack("<I", len(data)))
    def rename(s, old, new83):
        """renomme un fichier (efface ses entrees LFN) ; new83 = 11 octets."""
        pos, e, cl = s.find(old)
        # entrees LFN juste avant
        p = pos - 32
        while True:
            s.f.seek(p); x = s.f.read(32)
            if x[11] != 0x0F or x[0] == 0xE5: break
            s.f.seek(p); s.f.write(b"\xE5"); p -= 32
        s.f.seek(pos); s.f.write(new83.encode())
if __name__ == "__main__":
    fs = Fat(sys.argv[1])
    for a in sys.argv[2:]:
        if a.startswith("ren:"):
            o, n = a[4:].split("=")
            fs.rename(o, n.ljust(11)[:11] if "." not in n else n.split(".")[0].ljust(8) + n.split(".")[1].ljust(3))
            continue
        p, v = a.split("=", 1)
        fs.write(p, v.replace("\n", "\n").encode())
