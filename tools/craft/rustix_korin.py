"""rustix_korin : rustix pour Korin (docs/CRAFT.md). Korin est un Unix sur newlib, comme Horizon (la 3DS), que rustix
connaît déjà et tient à l'écart de ce que newlib n'a pas : Korin prend partout les branches d'Horizon. Fait la copie
patchée d'une version de rustix (tools/craft/build.sh en tire tools/craft/patches/rustix-<version>.patch).

    python3 -I tools/craft/rustix_korin.py <dossier de rustix>
"""
import os
import re
import sys

top = sys.argv[1]
H = re.compile(r'target_os = "horizon"')
# là où Korin ressemble à Linux et pas à Horizon : struct pollfd, sockaddr sans sun_len (en-têtes de libposix)
LINUX_LIKE = {"src/backend/libc/event/poll_fd.rs", "src/backend/libc/net/addr.rs", "src/backend/libc/net/read_sockaddr.rs"}
# ce qu'Horizon a et Korin pas
EXTRA = [("src/backend/libc/net/send_recv.rs",
          '            target_os = "vita",\n        )))]\n        const MORE = bitcast!(c::MSG_MORE);',
          '            target_os = "vita",\n            target_os = "korin",\n        )))]\n        const MORE = bitcast!(c::MSG_MORE);'),
         # Dir::seek : Korin n'a pas seekdir
         ("src/backend/libc/fs/dir.rs",
          '    #[cfg(target_pointer_width = "64")]\n    #[cfg_attr(docsrs, doc(cfg(target_pointer_width = "64")))]\n    #[doc(alias = "seekdir")]',
          '    #[cfg(all(target_pointer_width = "64", not(target_os = "korin")))]\n    #[cfg_attr(docsrs, doc(cfg(target_pointer_width = "64")))]\n    #[doc(alias = "seekdir")]'),
         # (AtFlags, rendu à Korin, nomme ffi)
         ("src/backend/libc/fs/types.rs",
          '#[cfg(not(any(target_os = "horizon", target_os = "korin")))]\nuse crate::ffi;',
          '#[cfg(not(target_os = "horizon"))]\nuse crate::ffi;'),
         # clockid_t : unsigned long sur Korin (newlib), comme Cygwin et DragonFly
         ("src/clockid.rs",
          'not(any(target_os = "aix", target_os = "cygwin", target_os = "dragonfly")),\n    repr(i32)',
          'not(any(target_os = "aix", target_os = "cygwin", target_os = "dragonfly", target_os = "korin")),\n    repr(i32)'),
         ("src/clockid.rs",
          '#[cfg_attr(any(target_os = "cygwin", target_os = "dragonfly"), repr(u64))]',
          '#[cfg_attr(any(target_os = "cygwin", target_os = "dragonfly", target_os = "korin"), repr(u64))]')]
n = 0
for d, dirs, files in os.walk(top):
    for f in files:
        if not (f.endswith(".rs") or f == "Cargo.toml"):
            continue
        p = os.path.join(d, f)
        if os.path.relpath(p, top).replace(os.sep, "/") in LINUX_LIKE:
            continue
        s = open(p, encoding="utf-8").read()
        if 'target_os = "korin"' in s:
            continue
        t = H.sub('any(target_os = "horizon", target_os = "korin")', s)
        # (Cargo.toml : target_os="horizon" sans espaces)
        t = t.replace('target_os="horizon"', 'any(target_os="horizon", target_os="korin")')
        if t != s:
            open(p, "w", encoding="utf-8", newline="\n").write(t)
            n += 1
# ce que Korin a et Horizon pas (libposix : flock, pipe2, utimensat, futimens, posix_fallocate) : devant la définition
# ou le `use` de ces noms, les cfg reprennent Horizon seul
KORIN_HAS = ["flock", "FlockOperation", "pipe_with", "PipeFlags", "utimensat", "futimens", "UTIME_NOW", "UTIME_OMIT",
             "SYMLINK_NOFOLLOW", "Timestamps",
             # les *at (libposix : openat, fstatat, mkdirat, unlinkat, renameat, readlinkat, linkat, symlinkat,
             # fchmodat, faccessat, fchownat ; pas mknodat) et ce qui va avec
             "CWD", "AtFlags", "openat", "statat", "mkdirat", "unlinkat", "renameat", "readlinkat", "readlinkat_raw",
             "linkat", "symlinkat", "chmodat", "accessat", "Access", "chownat", "fstatat", "fchmodat", "faccessat",
             "fchownat", "_openat", "_renameat", "_readlinkat", "_linkat", "_symlinkat", "_chmodat", "_accessat",
             # la lecture des dossiers, deux drapeaux d'open (pas termios : Korin n'a pas la moitié de ses constantes)
             "dir", "Dir", "DirEntry", "DIRECTORY", "NOFOLLOW", "from_dirent_d_type", "owned_fd", "IntoRawFd"]
# (mais pas fcntl_lock, qui nomme FlockOperation : il émule flock par struct flock de fcntl, que Korin n'a pas)
NOT_ITEMS = ("fcntl_lock",)
ITEM = re.compile(r'^\s*(pub(\([a-z]+\))?\s+)?((unsafe\s+)?fn|enum|struct|const|type|use|mod)\b.*\b(%s)\b|^\s*const (%s)\b'
                  % ("|".join(KORIN_HAS), "|".join(KORIN_HAS)))
UNDO = 'any(target_os = "horizon", target_os = "korin")'
undone = 0
for d, dirs, files in os.walk(os.path.join(top, "src")):
    for f in files:
        if not f.endswith(".rs"):
            continue
        p = os.path.join(d, f)
        lines = open(p, encoding="utf-8").read().split("\n")
        changed = False
        for i, line in enumerate(lines):
            if not ITEM.match(line) or any(x in line for x in NOT_ITEMS):
                continue
            j = i - 1				# les attributs et la doc juste au-dessus (un cfg peut tenir sur plusieurs lignes)
            while j >= 0 and (lines[j].lstrip().startswith(("#[", "///", "//")) or lines[j].strip().endswith((",", "(", ")))]", "))]", ")]"))
                              or lines[j].strip().startswith(("bitflags!", "target_", "all(", "any(", "not(", "linux", "bsd", "apple", "solarish", "windows", "freebsdlike", "netbsdlike", "linux_kernel", "fix_", ")"))):
                if UNDO in lines[j]:
                    lines[j] = lines[j].replace(UNDO, 'target_os = "horizon"')
                    changed = True
                j -= 1
        if changed:
            open(p, "w", encoding="utf-8", newline="\n").write("\n".join(lines))
            undone += 1
print("rustix_korin : %d fichiers où Korin reprend ce qu'il a" % undone)

for rel, a, b in EXTRA:
    p = os.path.join(top, rel)
    s = open(p, encoding="utf-8").read()
    if b not in s:
        if a not in s:
            sys.exit("rustix_korin : %s : texte absent" % rel)
        open(p, "w", encoding="utf-8", newline="\n").write(s.replace(a, b))
print("rustix_korin : %d fichiers" % n)
