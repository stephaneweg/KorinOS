#!/bin/sh
# setup.sh : met en place une machine Linux x86-64 neuve (Ubuntu ou Debian, sans WSL ni Windows) pour construire
# Korin OS et l'essayer sous QEMU : PC (q35 + OVMF) et Pi (raspi4b). Guide : docs/agents/CLOUD.md.
# Depuis la racine du dépôt :
#
#     sudo sh tools/cloud/setup.sh root      les paquets de la distribution (apt) et /opt/toolchains :
#                                            la SEULE étape qui demande root (elle le dit, et ne fait que ça)
#     sh tools/cloud/setup.sh all            tout le reste, dans l'ordre ci-dessous
#     sh tools/cloud/setup.sh base toolchains qemu verify     seulement ces étapes
#     . ~/korin-cloud/env.sh                 ensuite, dans chaque shell : les variables des scripts
#
# Étapes (chacune idempotente : ce qui est fait et vérifié est gardé) :
#   base        fasm 1.73.32 et FASMARM 1.44 pour Linux, assemblés ici depuis les sources du dépôt
#               (ref/fasm17330, ref/fasm/SOURCE, tools/fasmarm/source : même sortie octet pour octet que
#               FASM.EXE et fasmarm.exe), `python` s'il manque, $PREFIX/env.sh
#   toolchains  aarch64-onyx-elf-14.2 (précompilée, dépôt onyx-toolchain, SHA-256 vérifiés) puis
#               x86_64-onyx-elf-14.2 (construite par tools/pc/toolchain.sh : ≈ 12 min sur 16 cœurs, rien de
#               publié ; X86_TC_URL + X86_TC_SHA256 : une archive précompilée à la place)
#   qemu        qemu-system-x86_64 + OVMF et qemu-system-aarch64 avec la machine raspi4b (QEMU ≥ 9.0) ; sinon
#               QEMU 10.2.1 construit depuis qemu.org (SHA-256 fixé) dans $PREFIX/qemu ; sans root et sans
#               QEMU : les paquets .deb extraits dans $PREFIX/qemu-deb (apt-get download, dpkg -x)
#   sources     les archives que bash, git et rg attendent (~/.cache/korin-src, SHA-256 fixés, de
#               snapshot.debian.org) ; la SoundFont d'Onyx (build/onyx/sdcard/res/soundfonts)
#   python      un Python 3.14 de construction (BUILD_PYTHON : /usr/bin/python3.14, sinon par uv 0.9.5) et uv
#   rust        les cibles Rust x86_64-unknown-korin (et aarch64 si RUST_ARCHES le dit) : tools/rust/build.sh,
#               après libposix (≈ 2 min sur 16 cœurs)
#   webkit      src/extra (bibliothèques et WebKit, depuis Onyx à la révision fixée : tools/extra/fetch.sh)
#   dotnet      les archives .NET (SDK 10.0.403, runtime 10.0.14, crossgen2) : tools/dotnet/setup.sh
#   pwsh        PowerShell 7.4.6 pour Linux (korin.ps1 -DryRun, build.ps1 sous Linux), SHA-256 fixé
#   verify      tools/pc/build.sh (noyau du PC, kits, commandes, services), démarrage QEMU q35 de quelques
#               secondes avec ktest ; puis le Pi : build.ps1 par pwsh (sinon tools/ci/carte_linux.py) et
#               tools/ci/boot_test.py sous QEMU raspi4b (VERIFY_PI=0 : sans le Pi)
#   all         base toolchains qemu sources python pwsh verify rust webkit dotnet
#
# Variables : PREFIX (défaut ~/korin-cloud : outils, env.sh, journaux), TOOLCHAINS (défaut /opt/toolchains ;
# exporté en KORIN_TOOLCHAINS, que lisent tous les scripts du dépôt), JOBS, RUST_ARCHES (« x86_64 »,
# « x86_64 aarch64 »), VERIFY_PI=0, QEMU_BUILD=1 (construire QEMU même si celui du système suffit).
# Rien de secret ici : la clé de signature des paquets n'est jamais sur ces machines (docs/PAQUETS.md).
set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT"
: "${PREFIX:=$HOME/korin-cloud}"
: "${TOOLCHAINS:=${KORIN_TOOLCHAINS:-/opt/toolchains}}"
: "${JOBS:=$(nproc 2>/dev/null || echo 4)}"
: "${RUST_ARCHES:=x86_64}"
: "${VERIFY_PI:=1}"
: "${QEMU_BUILD:=0}"
export KORIN_TOOLCHAINS="$TOOLCHAINS"
# GNU tar en root garde les propriétaires des archives : refusé dans un conteneur sans privilèges (espace de
# noms d'utilisateurs), et inutile ici (vaut aussi pour les scripts appelés : chaîne d'Onyx, QEMU)
export TAR_OPTIONS="${TAR_OPTIONS:+$TAR_OPTIONS }--no-same-owner"
BIN=$PREFIX/bin
LOGS=$PREFIX/logs
DL=$PREFIX/dl

QEMU_VER=10.2.1
QEMU_SHA256=a3717477d8e2c84d630bfffbc20f6cd3293eb45aa1e6dac6d0cc27689991c9e1
UV_VER=0.9.5
UV_SHA256=2cf10babba653310606f8b49876cfb679928669e7ddaa1fb41fb00ce73e64f66
PWSH_VER=7.4.6
PWSH_SHA256=6f6015203c47806c5cc444c19d8ed019695e610fbd948154264bf9ca8e157561
# fasm et FASMARM assemblés ici : des sources fixes donnent ces binaires, octet pour octet
FASM_SHA256=81a9467a39f8481922943623032dca8724ef5e9aa8f93ac716cf51e225e7c9c7
FASMARM_SHA256=830ad11854ecb81adcb97054d45605b6d5ee4062d93525699a3c7a54888bbcf5
# la révision d'Onyx : celle de tools/extra/fetch.sh (une seule source)
ONYX_REPO=${ONYX_REPO:-https://github.com/stephaneweg/Onyx}
ONYX_COMMIT=$(sed -n 's/^ONYX_COMMIT=\([0-9a-f]*\)$/\1/p' tools/extra/fetch.sh)

say () { printf '\n=== %s ===\n' "$*"; }
die () { echo "setup : $*" >&2; exit 1; }
have () { command -v "$1" >/dev/null 2>&1; }
sha256 () { sha256sum "$1" | cut -d' ' -f1; }
mins () { echo $(( ($(date +%s) - $1) / 60 )); }
fetch () {	# url fichier sha256
	if [ -f "$2" ] && [ "$(sha256 "$2")" = "$3" ]; then return 0; fi
	mkdir -p "$(dirname "$2")"
	curl -fsSL --retry 3 -o "$2.part" "$1" || die "téléchargement : $1"
	[ "$(sha256 "$2.part")" = "$3" ] || { rm -f "$2.part"; die "sha256 faux : $1"; }
	mv "$2.part" "$2"
}

# ---- env.sh : refait à la fin de chaque étape, d'après ce qui est là -------------------------------------------
write_env () {
	mkdir -p "$PREFIX"
	qx= qa= code= vars=
	if [ -x "$PREFIX/qemu/bin/qemu-system-x86_64" ]; then
		qx=$PREFIX/qemu/bin/qemu-system-x86_64; qa=$PREFIX/qemu/bin/qemu-system-aarch64
		code=$PREFIX/qemu/share/qemu/edk2-x86_64-code.fd; vars=$PREFIX/qemu/share/qemu/edk2-i386-vars.fd
	elif [ -x "$BIN/qemu-system-x86_64" ]; then
		qx=$BIN/qemu-system-x86_64; qa=$BIN/qemu-system-aarch64
	else
		qx=$(command -v qemu-system-x86_64 || true); qa=$(command -v qemu-system-aarch64 || true)
	fi
	# l'edk2 de QEMU (avec le shell UEFI, comme le QEMU de Windows : startup.nsh des disques de mkkorindisk.py)
	if [ -z "$code" ] && [ -f "$PREFIX/qemu-fw/edk2-x86_64-code.fd" ]; then
		code=$PREFIX/qemu-fw/edk2-x86_64-code.fd; vars=$PREFIX/qemu-fw/edk2-i386-vars.fd
	fi
	if [ -z "$code" ] && [ -d "$PREFIX/qemu-deb" ]; then
		code=$PREFIX/qemu-deb/usr/share/OVMF/OVMF_CODE_4M.fd; vars=$PREFIX/qemu-deb/usr/share/OVMF/OVMF_VARS_4M.fd
		[ -f "$code" ] || { code=$PREFIX/qemu-deb/usr/share/OVMF/OVMF_CODE.fd; vars=$PREFIX/qemu-deb/usr/share/OVMF/OVMF_VARS.fd; }
	fi
	bp=${BUILD_PYTHON:-}
	[ -n "$bp" ] || [ ! -x /usr/bin/python3.14 ] || bp=/usr/bin/python3.14
	[ -n "$bp" ] || [ ! -f "$PREFIX/build-python" ] || bp=$(cat "$PREFIX/build-python")
	{
		echo "# Korin OS : variables des scripts du dépôt (tools/cloud/setup.sh, $(date -u +%Y-%m-%dT%H:%MZ))"
		echo "export KORIN_CLOUD='$PREFIX'"
		echo "export KORIN_TOOLCHAINS='$TOOLCHAINS'"
		echo "export PATH='$BIN':\"\$PATH\""
		[ ! -x "$BIN/fasm" ] || echo "export KORIN_FASM='$BIN/fasm'"
		[ ! -x "$BIN/fasmarm" ] || { echo "export KORIN_FASMARM='$BIN/fasmarm'"; echo "export FASMARM='$BIN/fasmarm'"; }
		[ -z "$qx" ] || echo "export KORIN_QEMU_X86='$qx'"
		[ -z "$qa" ] || echo "export ARMPI_QEMU='$qa'"
		[ -z "$code" ] || [ ! -f "$code" ] || { echo "export KORIN_OVMF_CODE='$code'"; echo "export KORIN_OVMF_VARS='$vars'"; }
		[ -z "$bp" ] || echo "export BUILD_PYTHON='$bp'"
		echo "export PYTHONIOENCODING=utf-8"
	} > "$PREFIX/env.sh.part"
	mv "$PREFIX/env.sh.part" "$PREFIX/env.sh"
	. "$PREFIX/env.sh"
}

# ---- root : la seule étape qui demande root ---------------------------------------------------------------------
step_root () {
	[ "$(id -u)" = 0 ] || die "l'étape root demande root : sudo sh tools/cloud/setup.sh root"
	have apt-get || die "apt-get absent : installer l'équivalent des paquets de cette étape à la main"
	say "paquets de la distribution (apt-get)"
	export DEBIAN_FRONTEND=noninteractive
	apt-get update
	# construire : le noyau, les kits, libposix, les chaînes (GCC 14 se construit avec le GCC de l'hôte)
	P="build-essential git curl ca-certificates unzip xz-utils bzip2 zstd file patch rsync cmake ninja-build
	pkg-config bison flex gperf ruby perl texinfo gawk m4 python3 python3-venv python3-pip mtools dosfstools"
	# QEMU (raspi4b demande QEMU 9.0 : sinon l'étape qemu le construit avec ce qui suit) et OVMF
	P="$P qemu-system-x86 qemu-system-arm qemu-utils ovmf libglib2.0-dev libpixman-1-dev libslirp-dev
	libfdt-dev zlib1g-dev"
	# .NET (le SDK de construction) ; Jet et Mesa (Python de Mesa par tools/mesa/fetch.sh)
	P="$P libicu-dev libssl-dev"
	# shellcheck disable=SC2086
	apt-get install -y --no-install-recommends $P
	# facultatifs selon la version : Python 3.14 (sinon l'étape python le prend par uv), LLVM 21 et libclc (Mesa
	# iris du GPU Intel : tools/mesa/build-clc.sh), qemu-user (essais d'AArch64 hors QEMU système)
	for p in python3.14 python3.14-venv python3.14-dev llvm-21-dev libclang-21-dev libclang-cpp21-dev clang-21 \
		libclc-21-dev llvm-spirv-21 libllvmspirvlib-21-dev spirv-tools qemu-user; do
		c=$(apt-cache policy "$p" 2>/dev/null | sed -n 's/^ *Candidate: //p')
		if [ -n "$c" ] && [ "$c" != "(none)" ]; then apt-get install -y --no-install-recommends "$p" || true
		else echo "(pas de paquet $p ici)"; fi
	done
	u=${SUDO_USER:-root}
	mkdir -p "$TOOLCHAINS"
	chown "$u" "$TOOLCHAINS"
	echo "root : fait ; $TOOLCHAINS appartient à $u. La suite sans root : sh tools/cloud/setup.sh all"
}

# ---- base : fasm, FASMARM, python, env.sh -------------------------------------------------------------------
step_base () {
	say "outils de l'hôte"
	miss=
	for c in gcc g++ make ld objcopy readelf git curl tar xz sha256sum python3 patch sed; do
		have "$c" || miss="$miss $c"
	done
	[ -z "$miss" ] || die "il manque :$miss (sudo sh tools/cloud/setup.sh root)"
	echo "gcc : $(gcc -dumpfullversion) ; python3 : $(python3 -c 'import sys; print(sys.version.split()[0])')"
	mkdir -p "$BIN" "$LOGS"
	W=$PREFIX/src/fasm
	if [ -x "$BIN/fasm" ] && [ -x "$BIN/fasmarm.bin" ] && [ "$(sha256 "$BIN/fasm")" = "$FASM_SHA256" ] &&
	   [ "$(sha256 "$BIN/fasmarm.bin")" = "$FASMARM_SHA256" ]; then
		echo "fasm et FASMARM : déjà faits"
	else
		say "fasm et FASMARM pour Linux (depuis les sources du dépôt)"
		rm -rf "$W" && mkdir -p "$W"
		# 1. fasm 1.73.30 pour Linux x64, binaire de l'archive officielle gardée dans le dépôt
		tar -C "$W" -xzf ref/fasm17330/fasm-1.73.30.tgz
		# 2. fasm 1.73.32 (ref/fasm/SOURCE : celui de FASM.EXE, qui assemble le noyau du PC), assemblé par le
		#    1.73.30 ; noms en minuscules (les include le demandent hors de Windows)
		mkdir -p "$W/f/linux/x64"
		for f in ref/fasm/SOURCE/*.INC; do cp "$f" "$W/f/$(basename "$f" | tr 'A-Z' 'a-z')"; done
		for f in ref/fasm/SOURCE/LINUX/X64/*; do cp "$f" "$W/f/linux/x64/$(basename "$f" | tr 'A-Z' 'a-z')"; done
		(cd "$W/f/linux/x64" && "$W/fasm/fasm.x64" -m 262144 fasm.asm "$BIN/fasm") >"$LOGS/fasm.log" 2>&1 ||
			{ cat "$LOGS/fasm.log"; die "fasm"; }
		# 3. FASMARM 1.44 (fasm 1.73.30 + l'ajout ARM), comme docs/CI.md
		mkdir -p "$W/a/linux/x64"
		cp "$W"/fasm/source/*.inc tools/fasmarm/source/*.inc "$W/a/"
		cp "$W/fasm/source/Linux/system.inc" "$W/a/linux/"
		cp "$W"/fasm/source/Linux/x64/* tools/fasmarm/source/linux/x64/* "$W/a/linux/x64/"
		(cd "$W/a/linux/x64" && "$BIN/fasm" -m 262144 fasmarm.asm "$BIN/fasmarm.bin") >"$LOGS/fasmarm.log" 2>&1 ||
			{ cat "$LOGS/fasmarm.log"; die "FASMARM"; }
		# la mémoire par défaut (16 Mo) ne suffit pas à gbemu : -m comme carte_linux.py (une option -m
		# donnée par l'appelant passe après, et compte)
		printf '#!/bin/sh\nexec "%s" -m 262144 "$@"\n' "$BIN/fasmarm.bin" > "$BIN/fasmarm"
		chmod +x "$BIN/fasm" "$BIN/fasmarm.bin" "$BIN/fasmarm"
		rm -rf "$W"
	fi
	for p in fasm:"$FASM_SHA256" fasmarm.bin:"$FASMARM_SHA256"; do
		f=${p%%:*}; want=${p#*:}
		got=$(sha256 "$BIN/$f")
		if [ "$got" = "$want" ]; then echo "ok    $f (sha256 attendu)"
		else echo "note  $f : sha256 $got, attendu $want (sources de fasm changées ? mettre à jour setup.sh)"; fi
	done
	"$BIN/fasm" 2>&1 | head -n 1
	"$BIN/fasmarm" 2>&1 | head -n 1
	# build.ps1, tools/ci et les scripts Python de Windows appellent « python »
	if ! have python; then ln -sf "$(command -v python3)" "$BIN/python"; echo "python -> python3 ($BIN/python)"; fi
	write_env
}

# ---- toolchains --------------------------------------------------------------------------------------------
onyx_sparse () {	# chemins d'Onyx voulus en plus (build/onyx, révision de tools/extra/fetch.sh)
	O=$ROOT/build/onyx
	if [ ! -e "$O/.git" ]; then
		mkdir -p "$ROOT/build"
		git clone -q --filter=blob:none --no-checkout "$ONYX_REPO" "$O"
	fi
	cur=$(git -C "$O" sparse-checkout list 2>/dev/null | tr '\n' ' ' || true)
	git -C "$O" sparse-checkout set --cone $cur "$@"
	git -C "$O" -c advice.detachedHead=false checkout -q "$ONYX_COMMIT"
	[ "$(git -C "$O" rev-parse HEAD)" = "$ONYX_COMMIT" ] || die "build/onyx n'est pas à $ONYX_COMMIT"
}

step_toolchains () {
	mkdir -p "$TOOLCHAINS" 2>/dev/null && [ -w "$TOOLCHAINS" ] ||
		die "$TOOLCHAINS n'est pas inscriptible : sudo sh tools/cloud/setup.sh root, ou TOOLCHAINS=\$HOME/toolchains"
	A=$TOOLCHAINS/aarch64-onyx-elf-14.2
	X=$TOOLCHAINS/x86_64-onyx-elf-14.2
	if [ -x "$A/bin/aarch64-onyx-elf-gcc" ]; then
		echo "aarch64-onyx-elf-14.2 : déjà là ($A)"
	else
		say "aarch64-onyx-elf-14.2 (précompilée, dépôt onyx-toolchain)"
		t0=$(date +%s)
		onyx_sparse tools/toolchain
		PREFIX=$TOOLCHAINS sh "$ROOT/build/onyx/tools/toolchain/fetch.sh"
		echo "aarch64 : $(mins "$t0") min"
	fi
	"$A/bin/aarch64-onyx-elf-gcc" --version | head -n 1
	if [ -x "$X/bin/x86_64-onyx-elf-gcc" ]; then
		echo "x86_64-onyx-elf-14.2 : déjà là ($X)"
	elif [ -n "${X86_TC_URL:-}" ]; then
		say "x86_64-onyx-elf-14.2 (archive précompilée : $X86_TC_URL)"
		[ -n "${X86_TC_SHA256:-}" ] || die "X86_TC_URL sans X86_TC_SHA256"
		fetch "$X86_TC_URL" "$DL/x86_64-onyx-elf-14.2.tar.xz" "$X86_TC_SHA256"
		tar -C "$TOOLCHAINS" -xJf "$DL/x86_64-onyx-elf-14.2.tar.xz"
	else
		say "x86_64-onyx-elf-14.2 (construite : tools/pc/toolchain.sh, ≈ 12 min sur 16 cœurs)"
		t0=$(date +%s)
		PREFIX=$X JOBS=$JOBS sh tools/pc/toolchain.sh
		echo "x86_64 : $(mins "$t0") min"
	fi
	STAGES=check PREFIX=$X sh tools/pc/toolchain.sh | tail -n 2
	write_env
}

# ---- qemu ------------------------------------------------------------------------------------------------------
qemu_ok () {	# qemu-system-x86_64 qemu-system-aarch64 : q35 et raspi4b connus
	[ -n "$1" ] && [ -n "$2" ] && "$1" -M help 2>/dev/null | grep -q '^q35' &&
		"$2" -M help 2>/dev/null | grep -q '^raspi4b'
}

qemu_from_debs () {	# sans root : les .deb de la distribution, extraits (les bibliothèques doivent être là)
	have apt-get && have dpkg || return 1
	D=$PREFIX/qemu-deb
	say "QEMU sans root : paquets .deb extraits dans $D"
	rm -rf "$D" "$DL/deb" && mkdir -p "$D" "$DL/deb"
	for p in qemu-system-x86 qemu-system-arm qemu-system-data qemu-system-common qemu-system-gui \
		qemu-system-modules-opengl qemu-system-modules-spice qemu-block-extra seabios ipxe-qemu ovmf ovmf-generic; do
		(cd "$DL/deb" && apt-get download "$p" >/dev/null 2>&1) || echo "(pas de paquet $p)"
	done
	for f in "$DL"/deb/*.deb; do dpkg -x "$f" "$D"; done
	for q in qemu-system-x86_64 qemu-system-aarch64; do
		[ -x "$D/usr/bin/$q" ] || continue
		# (QEMU_MODULE_DIR : pas les modules d'un autre QEMU installé, qu'il refuserait : « same build »)
		printf '#!/bin/sh\nQEMU_MODULE_DIR="%s" exec "%s" -L "%s" -L "%s" "$@"\n' "$D/usr/lib/x86_64-linux-gnu/qemu" \
			"$D/usr/bin/$q" "$D/usr/share/qemu" "$D/usr/share/seabios" > "$BIN/$q"
		chmod +x "$BIN/$q"
	done
	"$BIN/qemu-system-x86_64" --version >/dev/null 2>&1
}

qemu_build () {	# QEMU depuis qemu.org : x86_64 et aarch64, dans $PREFIX/qemu (≈ 2 min sur 16 cœurs)
	for m in glib-2.0 pixman-1 slirp; do
		pkg-config --exists "$m" || die "QEMU à construire : $m manque (sudo sh tools/cloud/setup.sh root)"
	done
	say "QEMU $QEMU_VER (construit, x86_64-softmmu et aarch64-softmmu)"
	t0=$(date +%s)
	fetch "https://download.qemu.org/qemu-$QEMU_VER.tar.xz" "$DL/qemu-$QEMU_VER.tar.xz" "$QEMU_SHA256"
	S=$PREFIX/src/qemu-$QEMU_VER
	rm -rf "$S" && mkdir -p "$PREFIX/src" && tar -C "$PREFIX/src" -xJf "$DL/qemu-$QEMU_VER.tar.xz"
	(cd "$S" && ./configure --prefix="$PREFIX/qemu" --target-list=x86_64-softmmu,aarch64-softmmu \
		--enable-slirp --disable-docs --disable-werror --disable-gtk --disable-sdl --disable-vnc-sasl \
		>"$LOGS/qemu-configure.log" 2>&1 && make -j"$JOBS" >"$LOGS/qemu-build.log" 2>&1 &&
		make install >"$LOGS/qemu-install.log" 2>&1) || { tail -n 30 "$LOGS"/qemu-*.log; die "QEMU"; }
	rm -rf "$S"
	echo "QEMU construit : $(mins "$t0") min"
}

qemu_fw () {	# l'edk2 des sources de QEMU : l'OVMF des distributions n'a pas le shell UEFI
	[ ! -f "$PREFIX/qemu/share/qemu/edk2-x86_64-code.fd" ] || return 0
	[ ! -f "$PREFIX/qemu-fw/edk2-x86_64-code.fd" ] || return 0
	have bunzip2 || die "bunzip2 manque (paquet bzip2 : sudo sh tools/cloud/setup.sh root)"
	say "edk2 (OVMF avec le shell UEFI) des sources de QEMU $QEMU_VER"
	fetch "https://download.qemu.org/qemu-$QEMU_VER.tar.xz" "$DL/qemu-$QEMU_VER.tar.xz" "$QEMU_SHA256"
	rm -rf "$PREFIX/qemu-fw" && mkdir -p "$PREFIX/qemu-fw"
	for f in edk2-x86_64-code.fd edk2-i386-vars.fd; do
		tar -xJOf "$DL/qemu-$QEMU_VER.tar.xz" "qemu-$QEMU_VER/pc-bios/$f.bz2" | bunzip2 > "$PREFIX/qemu-fw/$f"
	done
}

step_qemu () {
	mkdir -p "$BIN" "$LOGS"
	qemu_fw
	write_env
	if [ "$QEMU_BUILD" != 1 ] && qemu_ok "${KORIN_QEMU_X86:-}" "${ARMPI_QEMU:-}"; then
		echo "QEMU : $("$KORIN_QEMU_X86" --version | head -n 1) (q35, raspi4b)"
	else
		if [ "$QEMU_BUILD" != 1 ] && [ -z "${KORIN_QEMU_X86:-}" ] && [ "$(id -u)" != 0 ]; then
			qemu_from_debs || true
			write_env
		fi
		if [ "$QEMU_BUILD" = 1 ] || ! qemu_ok "${KORIN_QEMU_X86:-}" "${ARMPI_QEMU:-}"; then
			echo "QEMU du système : ${KORIN_QEMU_X86:-absent} ; raspi4b demande QEMU 9.0 ou plus"
			qemu_build
			write_env
		fi
		qemu_ok "${KORIN_QEMU_X86:-}" "${ARMPI_QEMU:-}" || die "QEMU : q35 ou raspi4b manque"
	fi
	[ -n "${KORIN_OVMF_CODE:-}" ] || python3 -I -c "import sys; sys.path.insert(0, 'tools/pc'); import qemu_pc, os; \
c = qemu_pc.CODE; sys.exit(0 if os.path.isfile(c) else 'OVMF absent : ' + c)" || die "OVMF (paquet ovmf)"
	echo "OVMF : $(python3 -I -c "import sys; sys.path.insert(0, 'tools/pc'); import qemu_pc; print(qemu_pc.CODE)")"
}

# ---- sources -------------------------------------------------------------------------------------------------
# archives .orig de Debian/Ubuntu (sources amont), par leur SHA-1 sur snapshot.debian.org, SHA-256 vérifiés
SRC_LIST="
bash/bash_5.2.37.orig.tar.xz cf4da6fb3798f10b9510ab13fc69a5a9bf75e86e 370704c9c859f4060b7df19055e43bb9b5fa09d887699cf6ba87885c5485d36a
git-deps/git_2.51.0.orig.tar.xz ce50edff73741438857296be5749ea99859c73aa 60a7c2251cc2e588d5cd87bae567260617c6de0c22dca9cdbfc4c7d2b8990b62
git-deps/zlib_1.3.dfsg+really1.3.1.orig.tar.gz 3b19b81105d3436095134a648c521b678905eaac 60dd315c07f616887caa029408308a018ace66e3d142726a97db164b3b8f69fb
git-deps/openssl_3.5.5.orig.tar.gz 72a5ebbdd30bc28a66f069e2d50c66a007c324d2 b28c91532a8b65a1f983b4c28b7488174e4a01008e29ce8e69bd789f28bc2a89
git-deps/curl_8.18.0.orig.tar.gz 1a13070ccd2c5fe1d47cb34dd56138f69708f3f9 e9274a5f8ab5271c0e0e6762d2fce194d5f98acc568e4ce816845b2dcc0cf88f
rg-deps/pcre2_10.46.orig.tar.gz 6858f0eb287c8285f53a038c8a95dc43ba51c653 8d28d7f2c3b970c3a4bf3776bcbb5adfc923183ce74bc8df1ebaad8c1985bd07
"

step_sources () {
	say "archives de bash, git, rg (~/.cache/korin-src)"
	K=$HOME/.cache/korin-src
	echo "$SRC_LIST" | while read -r f sha1 sum; do
		[ -n "$f" ] || continue
		fetch "https://snapshot.debian.org/file/$sha1" "$K/$f" "$sum"
		echo "ok    $f"
	done
	say "SoundFont d'Onyx (build/onyx/sdcard/res/soundfonts : tools/pc/sysroot.sh la copie)"
	onyx_sparse sdcard/res/soundfonts
	ls build/onyx/sdcard/res/soundfonts
}

# ---- python, rust ----------------------------------------------------------------------------------------------
step_python () {
	mkdir -p "$BIN" "$DL"
	if ! have uv; then
		say "uv $UV_VER"
		fetch "https://github.com/astral-sh/uv/releases/download/$UV_VER/uv-x86_64-unknown-linux-gnu.tar.gz" \
			"$DL/uv-$UV_VER.tar.gz" "$UV_SHA256"
		tar -C "$DL" -xzf "$DL/uv-$UV_VER.tar.gz"
		cp "$DL/uv-x86_64-unknown-linux-gnu/uv" "$DL/uv-x86_64-unknown-linux-gnu/uvx" "$BIN/"
	fi
	export PATH="$BIN:$PATH"
	uv --version
	if [ -n "${BUILD_PYTHON:-}" ] && "$BUILD_PYTHON" -c 'import sys; assert sys.version_info[:2] == (3, 14)' 2>/dev/null; then
		echo "Python de construction : $BUILD_PYTHON"
	elif [ -x /usr/bin/python3.14 ]; then
		echo "Python de construction : /usr/bin/python3.14"
	else
		say "Python 3.14 par uv (pas de python3.14 dans la distribution)"
		UV_PYTHON_INSTALL_DIR=$PREFIX/python uv python install 3.14
		UV_PYTHON_INSTALL_DIR=$PREFIX/python uv python find 3.14 > "$PREFIX/build-python"
		echo "Python de construction : $(cat "$PREFIX/build-python")"
	fi
	write_env
}

step_rust () {
	for a in $RUST_ARCHES; do
		say "cible Rust $a-unknown-korin (tools/rust/build.sh)"
		t0=$(date +%s)
		ARCH=$a SKIP_TESTS=1 bash tools/rust/build.sh > "$LOGS/rust-$a.log" 2>&1 || { tail -n 30 "$LOGS/rust-$a.log"; die "Rust $a"; }
		tail -n 3 "$LOGS/rust-$a.log"
		echo "Rust $a : $(mins "$t0") min"
	done
}

# ---- webkit, dotnet, pwsh -------------------------------------------------------------------------------------
step_webkit () {
	say "src/extra et WebKit (tools/extra/fetch.sh : Onyx $ONYX_COMMIT ; ≈ 2 Go)"
	t0=$(date +%s)
	sh tools/extra/fetch.sh
	echo "src/extra : $(mins "$t0") min ; la construction : docs/CONSTRUIRE.md et PC-PILOTES.md « Jet pour le PC »"
}

step_dotnet () {
	say ".NET : archives fixées (tools/dotnet/setup.sh -> ~/armpi-dotnet)"
	sh tools/dotnet/setup.sh || true	# (il dit ce qui manque encore : ICU de build-deps.sh, la chaîne)
}

step_pwsh () {
	if [ -x "$PREFIX/pwsh/pwsh" ]; then echo "pwsh : déjà là"; else
		say "PowerShell $PWSH_VER"
		fetch "https://github.com/PowerShell/PowerShell/releases/download/v$PWSH_VER/powershell-$PWSH_VER-linux-x64.tar.gz" \
			"$DL/pwsh-$PWSH_VER.tar.gz" "$PWSH_SHA256"
		rm -rf "$PREFIX/pwsh" && mkdir -p "$PREFIX/pwsh" "$BIN"
		tar -C "$PREFIX/pwsh" -xzf "$DL/pwsh-$PWSH_VER.tar.gz"
		chmod +x "$PREFIX/pwsh/pwsh"
		ln -sf "$PREFIX/pwsh/pwsh" "$BIN/pwsh"
	fi
	"$PREFIX/pwsh/pwsh" -NoProfile -Command '$PSVersionTable.PSVersion.ToString()' || echo "pwsh ne démarre pas (libicu ? DOTNET_SYSTEM_GLOBALIZATION_INVARIANT=1)"
}

# ---- verify ---------------------------------------------------------------------------------------------------
step_verify () {
	write_env
	[ -n "${KORIN_FASM:-}" ] || die "lancer d'abord : sh tools/cloud/setup.sh base"
	say "noyau du PC, kits, commandes et services (tools/pc/build.sh)"
	t0=$(date +%s)
	sh tools/pc/build.sh > "$LOGS/pc-build.log" 2>&1 || { tail -n 40 "$LOGS/pc-build.log"; die "tools/pc/build.sh"; }
	tail -n 3 "$LOGS/pc-build.log"
	ls -l build/pc/esp/korin/kernel.elf build/pc/esp/EFI/BOOT/BOOTX64.EFI
	echo "construction : $(mins "$t0") min"
	say "démarrage QEMU q35 + OVMF (moniteur du noyau, run ktest)"
	t0=$(date +%s)
	rm -f build/pc/ovmf_vars.fd	# (le modèle d'OVMF de cette machine)
	python3 -I tools/pc/qemu_pc.py test --wait "${VERIFY_WAIT:-15}" --type "run ktest" --after "${VERIFY_AFTER:-25}" \
		--shot build/pc/verify.png > "$LOGS/pc-qemu.log" 2>&1 || true
	echo "QEMU : $(( $(date +%s) - t0 )) s ; journal du port 0xE9 : $LOGS/pc-qemu.log"
	grep -a 'ktest : ' "$LOGS/pc-qemu.log" | tail -n 3 || true
	# (le bilan de src/kernel/arch/pc/initrd/ktest.c : « ktest : N ok, 0 échec(s) ; ... »)
	grep -a -q 'ktest : [0-9][0-9]* ok, 0 ' "$LOGS/pc-qemu.log" ||
		die "ktest : pas de bilan sans échec ($LOGS/pc-qemu.log, build/pc/verify.png ; VERIFY_WAIT, VERIFY_AFTER : plus de temps)"
	echo "VÉRIFIÉ : noyau du PC construit et démarré sous QEMU"
	if [ "$VERIFY_PI" = 1 ]; then
		t0=$(date +%s)
		if have pwsh; then
			say "Pi : build.ps1 par pwsh (KORIN_FASMARM), puis boot_test.py (raspi4b)"
			pwsh -NoProfile -File build.ps1 > "$LOGS/pi-build.log" 2>&1 || { tail -n 30 "$LOGS/pi-build.log"; die "build.ps1"; }
		else
			say "Pi : carte_linux.py (asm sur la carte sdcard/ ; sans pwsh), puis boot_test.py (raspi4b)"
			python3 -I tools/ci/carte_linux.py > "$LOGS/pi-build.log" 2>&1 || { tail -n 30 "$LOGS/pi-build.log"; die "carte_linux.py"; }
		fi
		python3 -I tools/ci/boot_test.py --timeout 300 || die "boot_test.py (build/ci-uart.log)"
		echo "VÉRIFIÉ : carte du Pi construite et démarrée sous QEMU raspi4b ($(( $(date +%s) - t0 )) s)"
	fi
}

[ $# -gt 0 ] || { sed -n '2,40p' "$0"; exit 0; }
STEPS=$*
[ "$STEPS" != all ] || STEPS="base toolchains qemu sources python pwsh verify rust webkit dotnet"
T0=$(date +%s)
for s in $STEPS; do
	case $s in
	root|base|toolchains|qemu|sources|python|rust|webkit|dotnet|pwsh|verify) ;;
	*) die "étape inconnue : $s (root base toolchains qemu sources python rust webkit dotnet pwsh verify all)" ;;
	esac
	[ "$s" = root ] || [ -d "$PREFIX" ] || mkdir -p "$PREFIX"
	[ "$s" = root ] || mkdir -p "$LOGS"
	"step_$s"
done
echo
echo "setup : $STEPS : fait en $(mins "$T0") min."
[ ! -f "$PREFIX/env.sh" ] || echo "Dans chaque shell : . $PREFIX/env.sh"
