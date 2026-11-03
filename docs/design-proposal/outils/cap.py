"""cap.py nom theme(g|n) LxH 'app1,app2' script.txt : image privee, boot, script d'operations."""
import sys, shutil, subprocess, os
sys.path.insert(0, ".")
import imgpatch
THEME = "theme = %s\n"
def run(name, theme, res, apps, script, port, extra=""):
    img = "build/sd_%s.img" % name
    shutil.copyfile("build/sd.img", img)
    fs = imgpatch.Fat(img)
    fs.write("/etc/options.txt", ("ecran=%s\nno_preload\n" % res).encode())
    fs.write("/etc/preload.txt", b"#\n")
    t = "Givre" if theme == "g" else "Onyx Noir"
    fs.write("/etc/theme.txt", (THEME % t).encode())
    st = "".join("SD0:/apps/%s.app/main\n" % a if "/" not in a else a + "\n" for a in apps.split(",") if a)
    fs.write("/etc/startup.txt", (st or "#\n").encode())
    if extra: exec(extra, {"fs": fs})
    fs.f.close()
    sc = "s_%s.txt" % name
    open(sc, "w").write(script)
    r = subprocess.run([sys.executable, "-I", "qemu_drive.py", sc, "--port", str(port), "--vnc", str(port - 4500),
        "--uart", "build/u_%s.log" % name, "--ecran", res, "--sd", img], capture_output=True, text=True, timeout=400)
    print(name, r.returncode, r.stdout[-200:], r.stderr[-300:])
    os.remove(img)
if __name__ == "__main__":
    run(sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4], open(sys.argv[5]).read(), int(sys.argv[6]))
