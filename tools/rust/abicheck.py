"""abicheck : compare le module korin de la caisse libc (src/unix/korin/mod.rs) aux en-têtes C de
Korin (build/posix/include sur newlib), docs/RUST.md. Lancé par tools/rust/build.sh (WSL).

    python3 tools/rust/abicheck.py MOD_RS GCC SPECS OUT_DIR

1. Lit les `pub type`, les `pub const` (entiers) et les structures `s! { pub struct ... }` du module.
2. Écrit un C qui donne, par `gcc -S`, la taille, l'alignement et le signe de chaque type, la valeur
   de chaque constante, la taille, l'alignement et le décalage des champs publics de chaque structure
   (« ->X nom valeur » dans l'assembleur, la méthode des asm-offsets de Linux).
3. Écrit OUT_DIR/src/lib.rs : des `const _: () = assert!(...)` sur les mêmes grandeurs côté Rust ;
   build.sh le compile pour la cible (cargo check) : une différence est une erreur de compilation
   qui nomme la ligne.
Les noms absents des en-têtes C sont listés (un `pub const` sans `#define` : ce serait une valeur
inventée) et font échouer le contrôle, sauf ceux de NON_C.
"""
import os
import re
import subprocess
import sys

mod_rs, gcc, specs, out = sys.argv[1:5]
src = open(mod_rs, encoding="utf-8").read()

HEADERS = """sys/types.h sys/stat.h fcntl.h unistd.h errno.h time.h sys/time.h signal.h dirent.h poll.h
termios.h sys/ioctl.h sys/socket.h sys/un.h netinet/in.h netinet/tcp.h netdb.h sys/resource.h
pthread.h semaphore.h sched.h sys/wait.h sys/mman.h dlfcn.h sys/random.h sys/epoll.h sys/utsname.h
sys/statvfs.h sys/file.h pwd.h limits.h stdlib.h locale.h""".split()
# typedefs C (les autres structures sont des « struct X »)
TYPEDEFS = {"fd_set", "sem_t", "Dl_info", "siginfo_t", "stack_t", "cpu_set_t", "pthread_attr_t",
            "pthread_mutex_t", "pthread_mutexattr_t", "pthread_cond_t", "pthread_condattr_t",
            "pthread_rwlock_t", "pthread_rwlockattr_t", "pthread_barrier_t", "pthread_barrierattr_t",
            "pthread_once_t"}
ENUMS = {"P_ALL", "P_PID", "P_PGID"}		# idtype_t : une énumération, pas un #define
NON_C = set()					# noms Rust sans équivalent C voulu
SKIP_STRUCT_FIELDS = {"epoll_event": {"u64"}}	# union epoll_data en C
# champs Rust (noms de Linux) -> expression C : stat de newlib a des struct timespec st_atim...
FIELD_C = {"stat." + r + x: "st_" + r[3:-1] + "." + ("tv_sec" if x == "" else "tv_nsec")
           for r in ("st_atime", "st_mtime", "st_ctime") for x in ("", "_nsec")}

types = re.findall(r"^pub type (\w+) = [^;]+;", src, re.M)
consts = []
for m in re.finditer(r"^pub const (\w+): ([\w:]+) = [^;]+;", src, re.M):
    name, ty = m.group(1), m.group(2)
    if ty.startswith("*") or "_t" in ty and ty.split("::")[-1] in TYPEDEFS:
        continue
    consts.append((name, ty))
structs = []
for blk in re.finditer(r"^s! \{(.*?)^\}", src, re.M | re.S):
    for sm in re.finditer(r"pub struct (\w+) \{(.*?)\n    \}", blk.group(1), re.S):
        fields = re.findall(r"^\s+pub (\w+):", sm.group(2), re.M)
        structs.append((sm.group(1), fields))


def cname(s):
    return s if s in TYPEDEFS else "struct " + s


c = ["#define _GNU_SOURCE", "#include <stddef.h>"] + ["#include <%s>" % h for h in HEADERS]
c += ['#define V(tag, n, v) __asm__ volatile ("\\n->" tag " " n " %0" :: "i" ((long long) (v)))',
      "void f (void) {"]
for t in types:
    c.append('V ("T", "%s.size", sizeof (%s)); V ("T", "%s.align", _Alignof (%s)); '
             'V ("T", "%s.signed", ((%s) -1) < (%s) 0);' % (t, t, t, t, t, t, t))
for n, _ in consts:
    if n in ENUMS:
        c.append('V ("C", "%s", %s);' % (n, n))
    else:
        c.append('#ifdef %s\nV ("C", "%s", %s);\n#else\nV ("U", "%s", 0);\n#endif' % (n, n, n, n))
for s, fields in structs:
    cn = cname(s)
    c.append('V ("S", "%s.size", sizeof (%s)); V ("S", "%s.align", _Alignof (%s));' % (s, cn, s, cn))
    for fl in fields:
        if fl in SKIP_STRUCT_FIELDS.get(s, ()):
            continue
        c.append('V ("F", "%s.%s", offsetof (%s, %s));' % (s, fl, cn, FIELD_C.get(s + "." + fl, fl)))
c.append("}")
os.makedirs(os.path.join(out, "src"), exist_ok=True)
cfile = os.path.join(out, "probe.c")
open(cfile, "w").write("\n".join(c) + "\n")
asm = subprocess.run([gcc, "-specs=" + specs, "-S", "-o", "-", cfile], capture_output=True, text=True)
if asm.returncode != 0:
    sys.exit("abicheck : le C ne compile pas :\n" + asm.stderr[-4000:])
vals, missing = {}, []
for line in asm.stdout.splitlines():
    m = re.match(r"\s*->(\w) (\S+) #?(-?\d+)", line)
    if m:
        if m.group(1) == "U":
            missing.append(m.group(2))
        else:
            vals[m.group(2)] = int(m.group(3))

rs = ["#![no_std]", "#![allow(unused)]", "use core::mem::{align_of, offset_of, size_of};", ""]
for t in types:
    rs.append("const _: () = assert!(size_of::<libc::%s>() == %d);" % (t, vals[t + ".size"]))
    rs.append("const _: () = assert!(align_of::<libc::%s>() == %d);" % (t, vals[t + ".align"]))
    sig = "true" if vals[t + ".signed"] else "false"
    rs.append("const _: () = assert!((libc::%s::MIN != 0) == %s);" % (t, sig))
for n, ty in consts:
    if n in vals:
        # comparés à la largeur du type Rust (0x80000000 vaut -2147483648 dans un c_int)
        rs.append("const _: () = assert!(((libc::%s as i128 - %d_i128) & ((1_i128 << (8 * "
                  "core::mem::size_of_val(&libc::%s))) - 1)) == 0);" % (n, vals[n], n))
for s, fields in structs:
    rs.append("const _: () = assert!(size_of::<libc::%s>() == %d);" % (s, vals[s + ".size"]))
    rs.append("const _: () = assert!(align_of::<libc::%s>() == %d);" % (s, vals[s + ".align"]))
    for fl in fields:
        k = "%s.%s" % (s, fl)
        if k in vals:
            rs.append("const _: () = assert!(offset_of!(libc::%s, %s) == %d);" % (s, fl, vals[k]))
open(os.path.join(out, "src", "lib.rs"), "w").write("\n".join(rs) + "\n")
nchk = sum(1 for l in rs if l.startswith("const _"))
print("abicheck : %d types, %d constantes, %d structures ; %d contrôles écrits" %
      (len(types), len(consts), len(structs), nchk))
bad = [m for m in missing if m not in NON_C]
if bad:
    print("abicheck : absentes des en-têtes C : " + " ".join(bad))
    sys.exit(1)
