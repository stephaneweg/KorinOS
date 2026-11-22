"""jet-sdk.py K T PREFIX ROOT -- l'arbre du SDK de Jet (appelé par tools/webkit/jet-sdk.sh).

K : la construction du cloud (~/korin-webkit-x86_64) ; T : l'arbre à remplir ; PREFIX : où il s'installe dans Korin
(/devtools/jet-sdk) ; ROOT : le dépôt. Les chemins de K sont réécrits en PREFIX ; les liens symboliques (Korin n'en a
pas) deviennent des copies (fichiers et dossiers)."""
import os, re, shlex, shutil, subprocess, sys

K, T, PREFIX, ROOT = sys.argv[1:5]
W = os.path.join(K, "webcore")
TEXT = (".h", ".hxx", ".hpp", ".inc", ".def", ".specs", ".txt", ".in")


def copyfile(src, dst):
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    shutil.copy2(src, dst, follow_symlinks=True)


def copytree(src, dst, skip=(".o", ".a", ".gch", ".d")):
    os.makedirs(dst, exist_ok=True)
    subprocess.run(["rsync", "-a", "--exclude=*.o", "--exclude=*.a", "--exclude=*.gch", "--exclude=*.o.d",
                    "--exclude=.git", src.rstrip("/") + "/", dst + "/"], check=True)


def inside(p):
    return os.path.join(T, os.path.relpath(p, K))


# ---- 1. la liaison : les archives minces de WebKit et tous leurs objets, les objets de Jet, les autres bibliothèques
libs = sorted(f for f in os.listdir(os.path.join(W, "lib")) if f.endswith(".a"))
members = []
for l in libs:
    a = os.path.join(W, "lib", l)
    copyfile(a, inside(a))
    for m in subprocess.run(["ar", "t", a], capture_output=True, text=True, check=True).stdout.split():
        members.append(m)
for m in members:
    copyfile(m, inside(m))
for o in ["jet_main.o", "jet_engine_webkit.o", "jet_webapp.o", "jet_present.o", "webkit2_armpi.o", "gpucomp.o", "gpucomp_armpi.o"]:
    copyfile(os.path.join(W, o), os.path.join(T, "webcore", o))
copyfile(os.path.join(K, "uikit", "libuikit_c.a"), os.path.join(T, "uikit", "libuikit_c.a"))
copyfile(os.path.join(K, "av", "libonyxav.a"), os.path.join(T, "av", "libonyxav.a"))
ff = "/tmp/korin-ffmpeg-jet-x86_64"
for l in ["libavformat.a", "libavcodec.a", "libswscale.a", "libavutil.a"]:
    copyfile(os.path.join(ff, l), os.path.join(T, "ffmpeg", l))
copyfile(os.path.join(ROOT, "build", "pc", "mesa", "lib", "libglkit.a"), os.path.join(T, "glkit", "libglkit.a"))
for l in "skia harfbuzz-icu harfbuzz freetype png16 jpeg webpmux webpdemux webp sharpyuv curl ssl crypto nghttp2 brotlidec xml2 sqlite3 z icui18n icuuc icudata".split():
    copyfile(os.path.join(K, "sysroot", "lib", "lib%s.a" % l), os.path.join(T, "sysroot", "lib", "lib%s.a" % l))
print("liaison :", len(libs), "bibliothèques,", len(members), "objets")

# ---- 2. la compilation : les commandes de chaque objet, réécrites pour le GCC de Korin
cmds = []
dirs = set()
for l in libs:
    out = subprocess.run(["ninja", "-C", W, "-t", "commands", "lib/" + l], capture_output=True, text=True).stdout
    for line in out.splitlines():
        if " -c " not in line or " -o " not in line:
            continue
        a = shlex.split(line)
        if not os.path.basename(a[0]).startswith("x86_64-onyx-elf-"):
            continue
        tool = "/devtools/gcc/bin/" + ("g++" if a[0].endswith("g++") else "gcc")
        r, obj, i = [tool], None, 1
        while i < len(a):
            x = a[i]
            if x == "-MD" or x.startswith("-fdiagnostics-color"):
                i += 1
                continue
            if x in ("-MT", "-MF"):
                i += 2
                continue
            if x == "-o":
                obj = a[i + 1]
            if x.startswith("-I"):
                dirs.add(os.path.normpath(x[2:]))
            if x == "-isystem":
                dirs.add(os.path.normpath(a[i + 1]))
            r.append(x)
            i += 1
        if obj:
            cmds.append((obj, " ".join(shlex.quote(y.replace(K, PREFIX)) for y in r)))
seen = set()
with open(os.path.join(T, "commands.txt"), "w") as f:
    for o, c in cmds:
        if o not in seen:
            seen.add(o)
            f.write("%s\t%s\n" % (o, c))
print("compilation :", len(seen), "commandes,", len(dirs), "dossiers d'en-têtes")

# les sources de WebKit (patchées) en entier ; les dossiers d'en-têtes de la construction ; les cmake_pch.hxx
copytree(os.path.join(K, "webkit-gl", "Source"), os.path.join(T, "webkit-gl", "Source"))
posix = set()
for r, ds, fs in os.walk(os.path.join(K, "posix", "include")):
    for x in fs:
        posix.add(os.path.relpath(os.path.join(r, x), os.path.join(K, "posix", "include")))
for d in sorted(dirs):
    if not d.startswith(K) or d.startswith(os.path.join(K, "webkit-gl")):
        continue
    if d == W:
        for x in os.listdir(W):
            if x.endswith(".h"):
                copyfile(os.path.join(W, x), os.path.join(T, "webcore", x))
        continue
    if os.path.isdir(inside(d)) or not os.path.isdir(d):
        continue
    copytree(d, inside(d))
for r, ds, fs in os.walk(os.path.join(W, "Source")):
    for x in fs:
        if x == "cmake_pch.hxx":
            copyfile(os.path.join(r, x), inside(os.path.join(r, x)))
            # (et son source d'une ligne, pour refaire le .gch dans Korin : ninja cmake_pch.hxx.gch)
            c = os.path.join(r, x + ".cxx")
            if os.path.isfile(c):
                copyfile(c, inside(c))
            else:
                os.makedirs(os.path.dirname(inside(c)), exist_ok=True)
                open(inside(c), "w").write("/* generated by CMake */\n")
# (les en-têtes de libposix : le GCC de Korin a les siens, ceux du cloud casseraient ses #include_next)
inc = os.path.join(T, "sysroot", "include")
for p in posix:
    q = os.path.join(inc, p)
    if os.path.lexists(q):
        os.remove(q)
for r, ds, fs in os.walk(inc, topdown=False):
    if not os.listdir(r):
        os.rmdir(r)
spec = open(os.path.join(K, "sysroot", "lib", "onyx-cc.specs")).read().replace(K, PREFIX)
open(os.path.join(T, "sysroot", "lib", "onyx-cc.specs"), "w").write(spec)

# ---- 3. les liens symboliques : des copies (fichiers et dossiers)
n = 0
for r, ds, fs in os.walk(T):
    for x in ds + fs:
        p = os.path.join(r, x)
        if not os.path.islink(p):
            continue
        t = os.path.realpath(os.path.join(K, os.path.relpath(p, T)))
        os.remove(p)
        if os.path.isdir(t):
            shutil.copytree(t, p, symlinks=False)
        elif os.path.isfile(t):
            # une copie, pas un renvoi d'une ligne (22/11/2026) : un `#include "X.h"` du vrai fichier se cherche alors
            # à côté du lien, comme dans la construction du cloud (GCC ne résout pas le lien) ; avec un renvoi,
            # FontPlatformData.h de PrivateHeaders ne trouvait plus HbUniquePtr.h et aucune commande ne compilait
            shutil.copy2(t, p)
        n += 1
print("liens remplacés :", n)

# ---- 4. les chemins du cloud dans les fichiers texte
n = 0
for r, ds, fs in os.walk(T):
    for x in fs:
        if not x.endswith(TEXT):
            continue
        p = os.path.join(r, x)
        try:
            s = open(p, encoding="utf-8").read()
        except (UnicodeDecodeError, OSError):
            continue
        if K in s:
            open(p, "w", encoding="utf-8").write(s.replace(K, PREFIX))
            n += 1
print("fichiers réécrits :", n)
