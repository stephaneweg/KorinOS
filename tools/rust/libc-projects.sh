#!/bin/bash
# La libc des projets Rust pour Korin (docs/RUST.md, « Limites connues » ; docs/CRAFT.md) : la std garde la 0.2.185 de
# rust-src (tools/rust/libc.patch) ; les projets récents demandent plus (nix 0.31 : ^0.2.186 ; cargo 0.98 : 0.2.186).
# La même libc.patch sur la 0.2.191, posée dans $K/vendor/libc-0.2.191, et une seconde entrée dans le
# [patch.crates-io] de $CARGO_HOME/config.toml (libc pour la std, libc-0-2-191 pour les projets) : une seule entrée
# remplacerait celle de la std, qui retomberait sur la libc du registre, sans « korin ». La 0.2.191 veut « unsafe »
# ou « safe » dans f! et n'a plus safe_f! : ajoutés au module korin à l'application.
#     . $K/env.sh ; bash tools/rust/libc-projects.sh $K       (tools/craft/build.sh, tools/cargo/build.sh)
# Un projet dont le Cargo.lock a une autre 0.2.x y est ramené par « cargo update -p libc@<v> --precise 0.2.191 ».
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
K=${1:?usage : libc-projects.sh <dossier de la chaîne Rust>}
[ -n "$CARGO_HOME" ] || { echo "libc-projects : CARGO_HOME vide (. $K/env.sh)" >&2; exit 1; }
LV=0.2.191
L=$K/vendor/libc-$LV
mkdir -p "$K/dl" "$K/vendor"
if [ ! -f "$L/.korin" ] || [ "$ROOT/tools/rust/libc.patch" -nt "$L/.korin" ]; then
	[ -f "$K/dl/libc-$LV.crate" ] || curl -sSfL "https://static.crates.io/crates/libc/libc-$LV.crate" -o "$K/dl/libc-$LV.crate"
	(cd "$K/dl" && echo "65cd87c9a6d60536580b316ad985726104730a5151c99845f2490428ef91240a  libc-$LV.crate" | sha256sum -c - >/dev/null)
	rm -rf "$L"
	tar xzf "$K/dl/libc-$LV.crate" -C "$K/vendor"
	sed 's#vendor/libc-0\.2\.185/##g' "$ROOT/tools/rust/libc.patch" | (cd "$L" && patch -p1 -s --no-backup-if-mismatch)
	python3 -I - "$L/src/unix/korin/mod.rs" <<'EOF'
import re, sys
p = sys.argv[1]
out, mode = [], None
for line in open(p).read().split('\n'):
    if line.startswith('f! {'):
        mode = 'unsafe'
    elif line.startswith('safe_f! {'):
        mode, line = 'safe', 'f! {'
    elif line.startswith('}'):
        mode = None
    if mode:
        line = re.sub(r'^(\s*)pub (const )?fn ', r'\1pub \2%s fn ' % mode, line)
    out.append(line)
open(p, 'w').write('\n'.join(out))
EOF
	touch "$L/.korin"
fi
python3 -I - "$CARGO_HOME/config.toml" "$L" <<'EOF'
import sys
p, path = sys.argv[1], sys.argv[2]
t = open(p).read()
if 'libc-0-2-191' not in t:
    t = t.replace('[patch.crates-io]\n', '[patch.crates-io]\nlibc-0-2-191 = { path = "%s", package = "libc" }\n' % path, 1)
    open(p, 'w').write(t)
EOF
