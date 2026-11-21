#!/bin/bash
# korin-vendor : les caisses de crates.io qui connaissent Korin, livrées avec la chaîne Rust de Korin (paquet rust,
# /devtools/rust/vendor), et la configuration de cargo qui les donne à tous les projets (/devtools/rust/etc/cargo/
# config.toml, que le cargo de Korin lit après celle de l'utilisateur : tools/cargo/cargo.patch). docs/RUST.md,
# « rustc dans Korin ». Sans elles, une caisse qui dépend de libc (rand, tempfile, tokio...) ne compile pas dans Korin :
# la libc de crates.io n'a pas le module korin.
#     bash tools/rust/korin-vendor.sh          -> build/pc/rust/korin-vendor/{vendor,etc}
# Chaque caisse : l'archive de crates.io (sha256 vérifié), notre patch (tools/rust, tools/craft/patches, tools/cargo/patches),
# et une version haute de sa série (libc 0.2.999, rustix 1.999.0...) : cargo préfère un [patch] qui convient, mais une
# caisse qui demande une version plus récente que la nôtre prendrait sinon celle du registre, sans Korin. Sauf ring,
# dont le `links` porte le numéro de version (son patch ne vaut que pour la 0.17.14).
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
K=${KORIN_RUST:-$HOME/.cache/korin-rust-x86_64}
OUT=${OUT:-$ROOT/build/pc/rust/korin-vendor}
PREFIX=${PREFIX:-/devtools/rust}	# (où le paquet les pose)
DL=$K/dl
mkdir -p "$DL"
rm -rf "$OUT"
mkdir -p "$OUT/vendor" "$OUT/etc/cargo"
# caisse version sha256 patch version-livrée
LIST="libc 0.2.191 65cd87c9a6d60536580b316ad985726104730a5151c99845f2490428ef91240a - 0.2.999
getrandom 0.2.17 - tools/craft/patches/getrandom-0.2.17.patch 0.2.999
getrandom 0.3.4 899def5c37c4fd7b2664648c28120ecec138e4d395b459e5ca34f9cce2dd77fd tools/rust/getrandom.patch 0.3.999
getrandom 0.4.3 - tools/craft/patches/getrandom-0.4.3.patch 0.4.999
rustix 1.1.6 - tools/craft/patches/rustix-1.1.6.patch 1.999.0
tempfile 3.27.0 - tools/craft/patches/tempfile-3.27.0.patch 3.999.0
filetime 0.2.29 - tools/craft/patches/filetime-0.2.29.patch 0.2.999
socket2 0.6.5 - tools/craft/patches/socket2-0.6.5.patch 0.6.999
mio 1.2.4 - tools/craft/patches/mio-1.2.4.patch 1.999.0
tokio 1.54.1 - tools/craft/patches/tokio-1.54.1.patch 1.999.0
terminal_size 0.4.4 - tools/cargo/patches/terminal_size-0.4.4.patch 0.4.999
ring 0.17.14 - tools/craft/patches/ring-0.17.14.patch -"
CFG="[patch.crates-io]"
while read -r c v sum patch newv; do
	f=$DL/$c-$v.crate
	[ -f "$f" ] || curl -sSfL "https://static.crates.io/crates/$c/$c-$v.crate" -o "$f"
	# (la somme : celle de l'index de crates.io, la référence de cargo)
	want=$sum
	if [ "$want" = - ]; then
		p=$(echo "$c" | awk '{ n = length($0); if (n <= 2) print n "/" $0; else if (n == 3) print "3/" substr($0,1,1) "/" $0; else print substr($0,1,2) "/" substr($0,3,2) "/" $0 }')
		want=$(curl -sSfL "https://index.crates.io/$p" | python3 -I -c "import json,sys; v=sys.argv[1]; print([json.loads(l)['cksum'] for l in sys.stdin if json.loads(l)['vers']==v][0])" "$v")
	fi
	(cd "$DL" && echo "$want  $c-$v.crate" | sha256sum -c - >/dev/null) || { echo "korin-vendor : somme fausse : $c $v" >&2; exit 1; }
	series=$(echo "$v" | awk -F. '{ print ($1 == "0") ? $1 "." $2 : $1 }')
	d=$OUT/vendor/$c-$series
	tar xzf "$f" -C "$OUT/vendor"
	mv "$OUT/vendor/$c-$v" "$d"
	if [ "$c" = libc ]; then
		# libc : libc.patch rejouée comme tools/rust/libc-projects.sh (f! et safe_f! de la 0.2.191)
		bash "$ROOT/tools/rust/libc-projects.sh" "$K" >/dev/null 2>&1 || true
		rm -rf "$d" && cp -r "$K/vendor/libc-$v" "$d" && rm -f "$d/.korin"
	else
		(cd "$d" && patch -p1 -s --no-backup-if-mismatch < "$ROOT/$patch")
	fi
	[ "$newv" != - ] && python3 -I - "$d/Cargo.toml" "$v" "$newv" <<'EOF'
import sys, re
p, old, new = sys.argv[1:]
s = open(p, encoding="utf-8").read()
s2, n = re.subn(r'(?m)^(version = ")%s(")' % re.escape(old), r'\g<1>%s\g<2>' % new, s, count=1)
assert n == 1, (p, old)
open(p, "w", encoding="utf-8", newline="\n").write(s2)
EOF
	rm -f "$d/.cargo-checksum.json" "$d/Cargo.toml.orig" "$d/.cargo_vcs_info.json"
	key=$(echo "$c-$series" | tr . -)
	CFG="$CFG
$key = { path = \"$PREFIX/vendor/$c-$series\", package = \"$c\" }"
	echo "korin-vendor : $c $v -> $c-$series (${newv/-/$v})"
done <<< "$LIST"
cat > "$OUT/etc/cargo/config.toml" <<EOF
# $PREFIX/etc/cargo/config.toml (paquet rust de Korin, tools/rust/korin-vendor.sh) : la configuration de la chaîne, lue
# par le cargo de Korin après celle de l'utilisateur (~/.cargo/config.toml, .cargo/config.toml des projets), qui l'emporte.
# Les caisses de crates.io qui connaissent Korin (patchées, dans $PREFIX/vendor), données à tous les projets : sans
# elles, une caisse qui dépend de libc ne compile pas dans Korin (la libc de crates.io n'a pas le module korin).
$CFG

# mio (tokio) : poll() et un tube pour le réveil, ce que Korin a (pas d'epoll de Linux, pas d'eventfd)
[build]
rustflags = ["--cfg", "mio_unsupported_force_poll_poll", "--cfg", "mio_unsupported_force_waker_pipe"]
EOF
echo "korin-vendor : $OUT ($(du -sh "$OUT" | cut -f1))"
