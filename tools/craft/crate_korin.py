"""crate_korin : les retouches pour Korin de quelques caisses de crates.io, par ancres (docs/CRAFT.md). Elles
produisent les patchs de tools/craft/patches (rustix : tools/craft/rustix_korin.py) :

    python3 -I tools/craft/crate_korin.py <caisse-version> <dossier de la caisse>

cap-primitives 4.0.3 (cap-std, sur rustix) : O_DSYNC approché par O_SYNC comme sur FreeBSD, pas d'O_RSYNC ni de
ttyname (termios), ni d'O_PATH ; uid_t et gid_t sur 16 bits (newlib). fs4 1.1.0 : la réservation de place sans fallocate, comme sur
les autres Unix qui n'en ont pas. process-wrap 10.0.1 (Craft Anything) : la partie de nix qu'il prend, sur libc.
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
    # pas d'O_PATH : ouvrir le dossier tel quel
    edit("src/rustix/fs/dir_utils.rs", [
        ('        target_os = "illumos",\n        target_os = "solaris",\n    ))]\n    {\n        OFlags::empty()',
         '        target_os = "illumos",\n        target_os = "solaris",\n        %s,\n    ))]\n    {\n        OFlags::empty()' % K),
    ])
    edit("src/rustix/fs/metadata_ext.rs", [
        ("                uid: stat.st_uid,", "                uid: stat.st_uid.into(),"),
        ("                gid: stat.st_gid,", "                gid: stat.st_gid.into(),"),
    ])
elif name == "fs4-1.1.0":
    for rel in ("src/unix/sync_impl.rs", "src/unix/async_impl.rs"):
        edit(rel, [('      target_os = "cygwin",', '      target_os = "cygwin",\n      %s,' % K)])
elif name == "process-wrap-10.0.1":
    # nix (sur lequel il est bâti) ne connaît pas Korin : la petite partie qu'il en prend, sur libc
    # (tools/craft/crate/process-wrap-korin_nix.rs), nommée nix dans les fichiers qui l'emploient
    import shutil
    shutil.copy(os.path.join(os.path.dirname(os.path.abspath(__file__)), "crate", "process-wrap-korin_nix.rs"),
                os.path.join(top, "src", "korin_nix.rs"))
    USE = '#[cfg(target_os = "korin")]\nuse crate::korin_nix as nix;\n'
    edit("src/lib.rs", [("pub(crate) mod generic_wrap;\n",
                         "pub(crate) mod generic_wrap;\n\n#[cfg(target_os = \"korin\")]\npub(crate) mod korin_nix;\n")])
    edit("src/std/core.rs", [("#[cfg(unix)]\nuse nix::{\n\tsys::signal::{Signal, kill},", USE + "#[cfg(unix)]\nuse nix::{\n\tsys::signal::{Signal, kill},")])
    edit("src/std/process_group.rs", [("use nix::{\n\terrno::Errno,", USE + "use nix::{\n\terrno::Errno,")])
    edit("Cargo.toml", [('[target."cfg(unix)".dependencies.nix]',
                         '[target."cfg(target_os = \\"korin\\")".dependencies.libc]\nversion = "0.2"\n\n'
                         '[target."cfg(all(unix, not(target_os = \\"korin\\")))".dependencies.nix]')])
else:
    sys.exit("crate_korin : %s inconnue" % name)
print("crate_korin : %s" % name)
