# arch.sh : les variables de la machine visee, lues (« . ») par les scripts de tools/python et
# tools/vibe/build-korin.sh. ARCH=aarch64 (le Pi, par defaut) ou x86_64 (Korin PC,
# docs/etude/PC-PILOTES.md « Python et Vibe pour le PC »). Chaque variable garde la valeur deja donnee
# dans l'environnement.
#   TOOL, TC        la chaine d'Onyx (/opt/toolchains/<cible>-14.2, prefixe <cible>-)
#   WORK            le dossier de travail (~/.cache/armpi-python, ~/.cache/korin-python-x86_64)
#   OUT             l'arbre produit (build/python, build/pc/python)
#   SPECS, SSL      libposix et OpenSSL de la machine
#   MCPU            les options du processeur (-mcpu=cortex-a72 ; rien pour le PC : x86-64 de base)
#   HOST_TRIPLE     le --host de configure (CPython, libffi)
#   PLAT            l'etiquette des roues (linux_aarch64, linux_x86_64)
#   RUST_TARGET     la cible Rust (aarch64-unknown-korin, x86_64-unknown-korin : tools/rust)
#   KORIN_RUST      la chaine Rust de cette cible (~/.cache/korin-rust, ~/.cache/korin-rust-x86_64)
ARCH=${ARCH:-aarch64}
case $ARCH in
aarch64)
	TOOL=${TOOL:-${KORIN_TOOLCHAINS:-/opt/toolchains}/aarch64-onyx-elf-14.2}
	TC=aarch64-onyx-elf-
	WORK=${WORK:-$HOME/.cache/armpi-python}
	OUT=${OUT:-$ROOT/build/python}
	SPECS=${SPECS:-$ROOT/build/posix/lib/posix.specs}
	SSL=${SSL:-$ROOT/build/openssl}
	MCPU="-mcpu=cortex-a72"
	HOST_TRIPLE=aarch64-unknown-linux-gnu
	PLAT=linux_aarch64
	RUST_TARGET=aarch64-unknown-korin
	KORIN_RUST=${KORIN_RUST:-$HOME/.cache/korin-rust}
	;;
x86_64)
	TOOL=${TOOL:-${KORIN_TOOLCHAINS:-/opt/toolchains}/x86_64-onyx-elf-14.2}
	TC=x86_64-onyx-elf-
	WORK=${WORK:-$HOME/.cache/korin-python-x86_64}
	OUT=${OUT:-$ROOT/build/pc/python}
	SPECS=${SPECS:-$ROOT/build/pc/posix/lib/posix.specs}
	SSL=${SSL:-$ROOT/build/pc/thirdparty/openssl}
	MCPU=""
	HOST_TRIPLE=x86_64-unknown-linux-gnu
	PLAT=linux_x86_64
	RUST_TARGET=x86_64-unknown-korin
	KORIN_RUST=${KORIN_RUST:-$HOME/.cache/korin-rust-x86_64}
	;;
*)
	echo "ARCH=aarch64 ou x86_64" >&2
	exit 1
	;;
esac
