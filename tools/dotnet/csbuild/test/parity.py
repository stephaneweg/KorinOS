"""parity : compare notre lecteur de .csproj au vrai MSBuild, sur le PC (docs/DOTNET.md).
    python -I tools/dotnet/csbuild/test/parity.py dossier-de-sortie-de-csbuild projet.csproj...
Chaque projet est lu par `dotnet msbuild -getProperty -getItem` et par `csbuild --print` : OutputType, AssemblyName,
TargetFramework, la liste des Compile, leurs Link et les Folder doivent etre les memes (hors fichiers que le SDK genere
dans obj ; LinkBase n'est applique par le SDK qu'a la construction, il n'est donc pas compare ici).
"""
import json
import os
import subprocess
import sys

csbuild = os.path.join(sys.argv[1], "csbuild.dll")
fail = 0
for proj in sys.argv[2:]:
    d = os.path.dirname(os.path.abspath(proj))
    env = dict(os.environ, AVALONIA_TELEMETRY_OPTOUT="1", DOTNET_NOLOGO="1")
    r = subprocess.run(["dotnet", "msbuild", proj, "-p:Configuration=Release", "-getProperty:OutputType", "-getProperty:AssemblyName",
                        "-getProperty:TargetFramework", "-getItem:Compile", "-getItem:Folder"], capture_output=True, text=True, env=env)
    j = json.loads(r.stdout)
    want = {k: j["Properties"].get(k, "") for k in ("OutputType", "AssemblyName", "TargetFramework")}
    wc, wl, wf = set(), {}, set()
    for it in j["Items"].get("Compile", []):
        full = os.path.normpath(it["FullPath"])
        rel = os.path.relpath(full, d).replace("\\", "/")
        if not rel.startswith("obj/"):
            wc.add(rel)
            if it.get("Link"):
                wl[rel] = it["Link"].replace("\\", "/")
    for it in j["Items"].get("Folder", []):
        wf.add(it["Identity"].replace("\\", "/").rstrip("/"))
    r = subprocess.run(["dotnet", csbuild, "--print", proj], capture_output=True, text=True)
    got, gc, warn, gl, gf = {}, set(), [], {}, set()
    for line in r.stdout.splitlines():
        k, _, v = line.partition("=")
        if k == "Compile":
            gc.add(v)
        elif k == "Link":
            a, _, b = v.partition("|")
            gl[a] = b
        elif k == "Folder":
            gf.add(v)
        elif k == "Warning":
            warn.append(v)
        elif k in want:
            got[k] = v
    linked = {k: v for k, v in gl.items() if k in wl}
    ok = got == want and gc == wc and linked == wl and gf == wf
    print(("ok   " if ok else "ECART"), proj, len(wc), "sources", ("; " + "; ".join(warn)) if warn else "")
    if not ok:
        fail += 1
        print("  msbuild", want, sorted(wc - gc), wl, wf)
        print("  csbuild", got, sorted(gc - wc), gl, gf)
sys.exit(1 if fail else 0)
