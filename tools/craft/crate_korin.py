"""crate_korin : les retouches pour Korin de quelques caisses de crates.io, par ancres (docs/CRAFT.md). Elles
produisent les patchs de tools/craft/patches (rustix : tools/craft/rustix_korin.py) :

    python3 -I tools/craft/crate_korin.py <caisse-version> <dossier de la caisse>

cap-primitives 4.0.3 (cap-std, sur rustix) : O_DSYNC approché par O_SYNC comme sur FreeBSD, pas d'O_RSYNC ni de
ttyname (termios) ; uid_t et gid_t sur 16 bits (newlib). fs4 1.1.0 : la réservation de place sans fallocate, comme sur
les autres Unix qui n'en ont pas.
"""
import os
import sys

name, top = sys.argv[1], sys.argv[2]
K = 'target_os = "korin"'


def edit(rel, pairs):
    p = os.path.join(top, rel)
    s = open(p, encoding="utf-8").read()
    for a, b in pairs:
        if s.count(a) < 1:
            sys.exit("crate_korin : %s : texte absent : %r" % (rel, a[:80]))
        s = s.replace(a, b)
    open(p, "w", encoding="utf-8", newline="\n").write(s)


if name == "cap-primitives-4.0.3":
    edit("src/rustix/fs/oflags.rs", [
        ('        #[cfg(not(target_os = "freebsd"))]\n        {\n            oflags |= OFlags::DSYNC;',
         '        #[cfg(not(any(target_os = "freebsd", %s)))]\n        {\n            oflags |= OFlags::DSYNC;' % K),
        ('        // Where needed, approximate `DSYNC` with `SYNC`.\n        #[cfg(target_os = "freebsd")]',
         '        // Where needed, approximate `DSYNC` with `SYNC`.\n        #[cfg(any(target_os = "freebsd", %s))]' % K),
        ('        target_os = "freebsd",\n        target_os = "fuchsia"\n    )))]\n    if options.rsync {',
         '        target_os = "freebsd",\n        target_os = "fuchsia",\n        %s\n    )))]\n    if options.rsync {' % K),
    ])
    edit("src/rustix/fs/file_path.rs", [
        ('not(any(target_os = "wasi", target_os = "fuchsia"))', 'not(any(target_os = "wasi", target_os = "fuchsia", %s))' % K),
    ])
    edit("src/rustix/fs/metadata_ext.rs", [
        ("                uid: stat.st_uid,", "                uid: stat.st_uid.into(),"),
        ("                gid: stat.st_gid,", "                gid: stat.st_gid.into(),"),
    ])
elif name == "fs4-1.1.0":
    for rel in ("src/unix/sync_impl.rs", "src/unix/async_impl.rs"):
        edit(rel, [('      target_os = "cygwin",', '      target_os = "cygwin",\n      %s,' % K)])
else:
    sys.exit("crate_korin : %s inconnue" % name)
print("crate_korin : %s" % name)
