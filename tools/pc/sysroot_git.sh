#!/bin/sh
# git et rg dans l'arbre système du PC (docs/GIT.md « Sur le PC », docs/etude/PC-PILOTES.md « git et rg sur le
# PC »), dans WSL, après tools/pc/sysroot.sh (qui refait build/pc/sysroot) :
#     bin/git               git 2.51.0 (commandes internes), comme SD0:/bin/git du Pi
#     bin/git-core/         gitexecdir : git-remote-http, git-remote-https (libcurl, OpenSSL), scripts
#     etc/gitconfig         la configuration du système (tools/git/gitconfig)
#     etc/git-core/         le modèle de git init
#     etc/ca-bundle         les autorités de https (http.sslCAInfo), si l'arbre ne l'a pas
#     bin/rg                rg (ripgrep réécrit en C sur PCRE2 : docs/RG.md)
# Construire d'abord (dans WSL, depuis la racine du dépôt) :
#     ARCH=x86_64 sh tools/posix/build.sh
#     ARCH=x86_64 sh tools/git/build.sh       -> build/pc/git/sdroot (OpenSSL : build/pc/thirdparty/openssl)
#     ARCH=x86_64 sh tools/rg/build.sh        -> build/pc/rg/rg
# puis :
#     sh tools/pc/sysroot.sh && sh tools/pc/sysroot_git.sh [arbre]
# Ce qui n'est pas construit est sauté. Rien ne change pour le Pi.
set -e
cd "$(dirname "$0")/../.."

OUT=${1:-build/pc/sysroot}		# (un autre arbre : son dossier en argument)
[ -d "$OUT/bin" ] || { echo "absent : $OUT (d'abord : wsl sh tools/pc/sysroot.sh)"; exit 1; }
G=build/pc/git/sdroot
R=build/pc/rg/rg

# un programme du PC, pas celui du Pi
x86() { od -An -tx1 -j18 -N2 "$1" | grep -q '3e 00' || { echo "$1 n'est pas un programme x86-64"; exit 1; }; }

k0=$(du -sk "$OUT" | cut -f1)
if [ -f "$G/bin/git" ]; then
	x86 "$G/bin/git"
	rm -rf "$OUT/bin/git-core" "$OUT/etc/git-core"
	cp -r "$G"/. "$OUT/"
	[ -f "$OUT/etc/ca-bundle" ] || cp skel/sd0/etc/ca-bundle "$OUT/etc/"
	# le protocole 0 : sur le PC, un clone ou un fetch HTTP en protocole 2 s'arrête (le process git attend, son
	# index-pack aussi, l'aide git-remote-http a fini : docs/etude/PC-PILOTES.md « git et rg sur le PC ») ; en
	# protocole 0, c'est git-remote-http qui reçoit le paquet, et tout passe. A retirer avec la cause.
	cat >> "$OUT/etc/gitconfig" <<-'EOF'
	# Korin PC : protocole 0 (tools/pc/sysroot_git.sh)
	[protocol]
		version = 0
	EOF
else
	echo "git pas construit ($G : ARCH=x86_64 sh tools/git/build.sh)"
fi
if [ -f "$R" ]; then
	x86 "$R"
	cp "$R" "$OUT/bin/rg"
else
	echo "rg pas construit ($R : ARCH=x86_64 sh tools/rg/build.sh)"
fi
k=$(du -sk "$OUT" | cut -f1)
echo "$OUT : git et rg, $((k - k0)) Ko de plus, $k Ko en tout"
