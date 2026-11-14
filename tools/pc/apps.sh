#!/bin/sh
# apps.sh -- les apps C et C++ de Korin pour le PC (x86-64), dans WSL (docs/etude/PC-PILOTES.md, « Chaîne
# x86-64 et apps C/C++ ») : les mêmes sources et options que tools/office/build.sh (le Pi), la chaîne
# x86_64-onyx-elf (tools/pc/toolchain.sh) et libposix du PC (ARCH=x86_64 sh tools/posix/build.sh).
#
#   sh tools/pc/apps.sh                  les kits depuis le C, puis toutes les apps de la liste
#   sh tools/pc/apps.sh letters sheet    seulement celles-ci
#   KEEP_GOING=1 sh tools/pc/apps.sh     une app qui ne se construit pas est notée, les autres continuent
#
# Produit :
#   build/pc/gcc/include/kits, build/pc/gcc/lib   les interfaces C des kits (comme /gcc du Pi, tools/gcc/kits.sh) :
#                                                 <kit>_abi.h, lib<kit>.a (tools/kits/abi2c.py --arch x86_64)
#   build/pc/apps/sdroot/apps/<nom>.app/{main,app.txt,données}   (tools/pc/sysroot_apps.sh les met dans l'arbre)
#   build/pc/apps/sdroot/bin/basic, lib/*.bi     le runtime de BASIC (docs/BASIC.md)
#   build/pc/apps/status.txt                      ce qui est construit, ce qui ne l'est pas et pourquoi
# Les bibliothèques tierces (OpenSSL, MuPDF) : tools/pc/thirdparty.sh, dans build/pc/thirdparty.
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
TOOL=${TOOL:-${KORIN_TOOLCHAINS:-/opt/toolchains}/x86_64-onyx-elf-14.2}
TC=$TOOL/bin/x86_64-onyx-elf-
POSIX=$ROOT/build/pc/posix
GCC=$ROOT/build/pc/gcc
OUT=$ROOT/build/pc/apps
TP=$ROOT/build/pc/thirdparty
JOBS=${JOBS:-$(nproc)}
KEEP_GOING=${KEEP_GOING:-0}
[ -f "$POSIX/lib/posix.specs" ] || { echo "apps : d'abord ARCH=x86_64 sh tools/posix/build.sh" >&2; exit 1; }
# (la liste de tools/office/build.sh, avec les apps passées de l'asm au C ; printd est un service de src/sys, vector
# demande Skia : à part ; pkgman vient de tools/pkg/build.sh sur le Pi)
ALL="letters sheet slides cardfile media circuits basic qbasic qbstudio gpiolab turtle cosmozoom irc mail telegram
courier calendar notes paint pdf photos printd archiver iconedit disks pinball critters arabesque pocketqi help imageview
screenshot budget ndsemu nesemu gbemu gbaemu snesemu n64emu gcemu terminal files welcome editor viewer calculator uidemo builder mediaplayer mixer music piano
recorder targets bubbles arkanoid pkgman pkg pkgd webd smbd smbfs webstudio koton llm vibe claude
2048 clock control fmtracker freecell gamelib invaders mandelbrot pipes same snake solitaire taskman tetris"
[ $# -gt 0 ] || set -- $ALL
mkdir -p "$OUT" "$GCC/include/kits" "$GCC/lib"
: > "$OUT/status.new"

# ---- les kits depuis le C (comme tools/gcc/kits.sh) ----
K=$ROOT/src/kits
A=$ROOT/src/inc
W=$OUT/kits
rm -rf "$W" && mkdir -p "$W"
kit () { # nom, puis les .inc des constantes
	n=$1
	shift
	[ -f "$K/$n/$n.abi" ] || return 0
	python3 -I "$ROOT/tools/kits/abi2c.py" "$K/$n/$n.abi" "$W" --arch x86_64 --const "$@" >/dev/null
	${TC}gcc -c "$W/${n}_abi.S" -o "$W/${n}_abi.o"
	${TC}gcc -O2 -g0 -specs="$POSIX/lib/posix.specs" -c "$W/${n}_abi_open.c" -o "$W/${n}_abi_open.o"
	rm -f "$GCC/lib/lib$n.a"
	${TC}ar rcs "$GCC/lib/lib$n.a" "$W/${n}_abi.o" "$W/${n}_abi_open.o"
	cp "$W/${n}_abi.h" "$GCC/include/kits/"
}
kit uikit "$A/armpi.inc" "$A/ws_ext.inc" "$K/uikit/uikit.inc"
kit audiokit "$K/audiokit/audiokit.inc"
kit netkit "$K/netkit/netkit.inc"
kit emukit "$K/emukit/emukit.inc" "$A/pad.inc"
kit gpukit "$K/gpukit/gpukit.inc"
kit mediakit "$K/mediakit/mediakit.inc"
kit archivekit "$K/archivekit/archivekit.inc"
kit documentkit "$K/documentkit/documentkit.inc"
kit gpiokit "$K/gpiokit/gpiokit.inc"
kit printerkit "$K/printerkit/printerkit.inc"
kit buskit "$K/buskit/buskit.inc"
cp "$K/uikit/include/uikit.h" "$K/mediakit/mediakit.h" "$K/emukit/include/emukit.h" "$K/printerkit/include/printerkit.h" \
	"$K/printerkit/include/pdfprint.h" "$GCC/include/kits/"

SPECS=$POSIX/lib/posix.specs
BASEFLAGS="-specs=$SPECS -O2 -g0 -ffunction-sections -fdata-sections -MMD -MP -I. -I$GCC/include/kits -I$ROOT/src/kits/uikit/include"
note () { printf '%s\n' "$*" >> "$OUT/status.new"; }

# KotonStudio (src/apps/koton, README.md du dossier) : options de tools/webkit/build-koton.sh (le Pi) ; un programme
# pour l'app, un par greffon (plugins/<id>, POSIX seul), et bin/kptest ; ses chansons dans home/koton comme sur le Pi
build_koton () {
	S=$ROOT/src/apps/koton
	O=$OUT/obj/koton
	D=$OUT/sdroot/apps/koton.app
	FLAGS="$BASEFLAGS -std=gnu++17 -fno-exceptions -fno-rtti -fno-threadsafe-statics -Wno-write-strings -DFD_SETSIZE=1024 -I$S/ui"
	mkdir -p "$O" "$D" "$OUT/sdroot/bin" "$OUT/sdroot/home/koton/songs" "$OUT/sdroot/home/koton/demos"
	cd "$S"
	{
		echo 'all:'
		ls ui/*.cpp engine/*.cpp main.cpp plug/*.cpp plugins/*/main.cpp | while read -r f; do
			o=$O/${f%.cpp}.o
			printf 'all: %s\n%s: %s\n\t@mkdir -p $(dir $@)\n\t%sg++ %s -c %s -o $@\n-include %s\n' "$o" "$o" "$f" "$TC" "$FLAGS" "$f" "${o%.o}.d"
		done
	} > "$O/make.mk"
	if ! make -s -j"$JOBS" -f "$O/make.mk" > "$O/make.log" 2>&1; then
		note "koton : ECHEC de compilation ($(grep -m1 -E 'error|Error' "$O/make.log" | cut -c1-200))"
		grep -E 'error' "$O/make.log" | head -5 >&2
		return 1
	fi
	KL="-specs=$SPECS -Wl,--gc-sections"
	KITS="-L$GCC/lib -luikit -laudiokit -lm"
	if ! { ${TC}g++ $KL "$O"/ui/*.o "$O"/engine/*.o "$O/plug/plughost.o" "$O/plug/plugctx.o" "$O/main.o" $KITS -o "$O/main" &&
		${TC}g++ $KL "$O"/ui/*.o "$O"/engine/*.o "$O/plug/plughost.o" "$O/plug/plugctx.o" "$O/plug/kptest.o" $KITS -o "$O/kptest"; } > "$O/link.log" 2>&1; then
		note "koton : ECHEC de l'édition de liens ($(grep -m1 -E 'undefined|error' "$O/link.log" | cut -c1-200))"
		grep -E 'undefined|error' "$O/link.log" | sort -u | head -8 >&2
		return 1
	fi
	${TC}strip -o "$D/main" "$O/main"
	${TC}strip -o "$OUT/sdroot/bin/kptest" "$O/kptest"
	rm -rf "$D/plugins"
	for d in plugins/*/; do
		id=$(basename "$d")
		mkdir -p "$D/plugins/$id"
		${TC}g++ $KL "$O/plugins/$id/main.o" -lm -o "$O/plugins/$id/prog" || { note "koton : ECHEC du greffon $id"; return 1; }
		${TC}strip -o "$D/plugins/$id/main" "$O/plugins/$id/prog"
		cp "$d/plugin.json" "$D/plugins/$id/"
	done
	cp app.txt data/drums.json help_*.md "$D/"
	cp -r help "$D/"
	cp data/demo.kson "$OUT/sdroot/home/koton/songs/"
	cp demos/*.kson "$OUT/sdroot/home/koton/demos/"
	note "koton : construite ($(stat -c %s "$D/main") octets, $(ls -d "$D"/plugins/*/ | wc -l) greffons, bin/kptest)"
}

# GlKit du PC (tools/mesa/build-glkit-pc.sh : libglkit.a) et GpuKit par GlKit (src/kits/gpukit/gl/gkgl.c) : les apps
# GPU les lient (sans GlKit construit : l'app est sautée et notée)
PM=$ROOT/build/pc/mesa
glkit_extra () { # [gk] : avec GpuKit par GlKit (gkgl.o, gk_gl_attach)
	[ -f "$PM/lib/libglkit.a" ] || { note "$app : pas construite (GlKit du PC absent : sh tools/mesa/build-iris.sh)"; return 1; }
	FLAGS="$FLAGS -I$ROOT/tools/mesa/armgl -I$ROOT/prebuilt/mesa/include -I$ROOT/src/kits/gpukit/gl"
	EXTRA="$PM/lib/libglkit.a"
	if [ "${1:-}" = gk ]; then
		${TC}gcc -O2 -specs="$SPECS" -I"$GCC/include/kits" -I"$ROOT/tools/mesa/armgl" -I"$ROOT/prebuilt/mesa/include" \
			-c "$ROOT/src/kits/gpukit/gl/gkgl.c" -o "$OUT/gkgl.o"
		EXTRA="$OUT/gkgl.o $EXTRA -L$GCC/lib -lgpukit"
	fi
}

# le serveur web (src/sys/webd -> sys/webd) et son volet de Réglages, un module que Réglages charge
# (sys/applets/webd.so : src/apps/control/control.h, « les applets installées » ; docs/WEB.md)
build_webd () {
	[ -f "$TP/openssl/lib/libssl.a" ] || { note "webd : pas construit (OpenSSL x86-64 absent : tools/pc/thirdparty.sh openssl)"; return 0; }
	S=$ROOT/src/sys/webd
	O=$OUT/obj/webd
	mkdir -p "$O" "$OUT/sdroot/sys/applets"
	F="-specs=$SPECS -O2 -g0 -std=gnu11 -Wall -ffunction-sections -fdata-sections -I$TP/openssl/include"
	for f in main conf server tls util; do
		${TC}gcc $F -c "$S/$f.c" -o "$O/$f.o" || { note "webd : ECHEC de compilation ($f.c)"; return 1; }
	done
	${TC}gcc -specs="$SPECS" -Wl,--gc-sections "$O/main.o" "$O/conf.o" "$O/server.o" "$O/tls.o" "$O/util.o" \
		-L"$TP/openssl/lib" -lssl -lcrypto -lm -o "$O/webd" || { note "webd : ECHEC de l'édition de liens"; return 1; }
	${TC}strip -o "$OUT/sdroot/sys/webd" "$O/webd"
	# (le même dans bin : lancé par bash ou par le volet, il écrit au Terminal ; un programme de SD0:/sys passe par
	# init2 et n'écrit qu'au journal, comme intelgpu et iwl)
	mkdir -p "$OUT/sdroot/bin"
	cp "$OUT/sdroot/sys/webd" "$OUT/sdroot/bin/webd"
	# (le module : sans libc ni libstdc++, il prend celles de Réglages, relié en hôte : -rdynamic)
	M="-specs=$SPECS -O2 -g0 -fPIC -ffunction-sections -fdata-sections -I$ROOT/src/apps/control -I$GCC/include/kits \
		-I$ROOT/src/kits/uikit/include -I$TP/openssl/include"
	${TC}gcc $M -std=gnu11 -c "$S/conf.c" -o "$O/m_conf.o" && ${TC}gcc $M -std=gnu11 -c "$S/util.c" -o "$O/m_util.o" &&
	${TC}g++ $M -std=gnu++17 -fno-exceptions -fno-rtti -Wno-write-strings -c "$S/applet/applet.cpp" -o "$O/m_applet.o" &&
	${TC}g++ -specs="$SPECS" -fPIC -shared -nostdlib++ -Wl,--gc-sections "$O/m_applet.o" "$O/m_conf.o" "$O/m_util.o" \
		-o "$O/webd.so" || { note "webd : ECHEC du volet de Réglages"; return 1; }
	${TC}strip --strip-unneeded -o "$OUT/sdroot/sys/applets/webd.so" "$O/webd.so"
	note "webd : construit ($(stat -c %s "$OUT/sdroot/sys/webd") octets, sys/webd ; volet $(stat -c %s "$OUT/sdroot/sys/applets/webd.so") octets, sys/applets/webd.so)"
}

# le partage de fichiers SMB (docs/SMB.md) : le serveur smbd (src/sys/smbd -> sys/smbd, et bin/smbd) et le client,
# le service de fichiers smbfs que vfs lance au montage (src/svc/smbfs -> sys/svc/smbfs) ; sur libsmb2
# (ARCH=x86_64 sh tools/libsmb2/build.sh)
build_smb () { # smbd | smbfs
	[ -f "$TP/libsmb2/lib/libsmb2.a" ] || { note "$1 : pas construit (libsmb2 x86-64 absente : ARCH=x86_64 sh tools/libsmb2/build.sh)"; return 0; }
	O=$OUT/obj/$1
	mkdir -p "$O" "$OUT/sdroot/sys/svc" "$OUT/sdroot/bin"
	F="-specs=$SPECS -O2 -g0 -std=gnu11 -Wall -ffunction-sections -fdata-sections -I$TP/libsmb2/include"
	if [ "$1" = smbd ]; then
		S=$ROOT/src/sys/smbd
		${TC}gcc $F -Wl,--gc-sections "$S/main.c" "$S/fs.c" -L"$TP/libsmb2/lib" -lsmb2 -o "$O/smbd" ||
			{ note "smbd : ECHEC de la construction"; return 1; }
		${TC}strip -o "$OUT/sdroot/sys/smbd" "$O/smbd"
		cp "$OUT/sdroot/sys/smbd" "$OUT/sdroot/bin/smbd"	# (lancé par bash : il écrit au Terminal, comme webd)
		note "smbd : construit ($(stat -c %s "$OUT/sdroot/sys/smbd") octets, sys/smbd)"
	else
		S=$ROOT/src/svc/smbfs
		${TC}gcc $F -Wl,--gc-sections "$S/smbfs.c" -L"$TP/libsmb2/lib" -lsmb2 -o "$O/smbfs" ||
			{ note "smbfs : ECHEC de la construction"; return 1; }
		${TC}strip -o "$OUT/sdroot/sys/svc/smbfs" "$O/smbfs"
		note "smbfs : construit ($(stat -c %s "$OUT/sdroot/sys/svc/smbfs") octets, sys/svc/smbfs)"
	fi
}

build_one () { # nom
	app=$1
	FLAGS=$BASEFLAGS
	EXTRA=
	LINKX=
	case $app in
	koton)	build_koton; return ;;
	webd)	build_webd; return ;;
	smbd|smbfs)	build_smb $app; return ;;
	control)	# (hôte de dlopen : les volets installés par des paquets, sys/applets/*.so, prennent son cadre et ses relais)
		LINKX=-rdynamic ;;
	mail|telegram|courier|pocketqi|pkgman|pkg|pkgd|llm)	# (OpenSSL : tools/pc/thirdparty.sh openssl)
		[ -f "$TP/openssl/lib/libssl.a" ] || { note "$app : pas construite (OpenSSL x86-64 absent : tools/pc/thirdparty.sh openssl)"; return 0; }
		FLAGS="$FLAGS -I$TP/openssl/include"
		case $app in pkg*) FLAGS="$FLAGS -I$ROOT/src/lib/pkg" ;; esac	# (pkglib.h, comme tools/pkg/build.sh)
		[ "$app" != llm ] || FLAGS="$FLAGS -I$ROOT/src/apps/koton/ui"	# (json.hpp, le même que celui de Koton)
		EXTRA="-L$TP/openssl/lib -lssl -lcrypto" ;;
	pdf)	# (MuPDF : tools/pc/thirdparty.sh mupdf)
		[ -f "$TP/mupdf/libmupdf.a" ] || { note "pdf : pas construite (MuPDF x86-64 absent : tools/pc/thirdparty.sh mupdf)"; return 0; }
		FLAGS="$FLAGS -I$TP/mupdf/include -DHAVE_SIGSETJMP=0"
		EXTRA="-L$TP/mupdf -lmupdf" ;;
	printd)	# (son main.cpp appelle le noyau par svc 0 en asm : à passer à <armpi/syscall.h>, comme les apps)
		if grep -q '"svc 0"' "$ROOT/src/sys/printd/main.cpp"; then
			note "printd : pas construit (src/sys/printd/main.cpp : appel système en asm AArch64, à passer à <armpi/syscall.h>)"
			return 0
		fi ;;
	pinball|arabesque)	# (la physique d'Onyx, le moteur d'Arabesque : mêmes bits partout)
		FLAGS="$FLAGS -ffp-contract=off" ;;
	vibe|claude)	# (libacp, le client ACP de Vibe : src/lib/acp, compilée avec l'app, comme tools/office/build.sh ;
		# claude : le source de Vibe avec le profil Claude, src/apps/claude/main.c)
		FLAGS="$FLAGS -I$ROOT/src/lib/acp" ;;
	ndsemu|nesemu|gbemu|gbaemu|snesemu)	# (comme tools/emu/build.sh ; ndsemu : l'interpréteur seul, son JIT n'est construit que pour AArch64)
		FLAGS="$FLAGS -O3 -w -fno-math-errno -fno-trapping-math" ;;
	n64emu)	# (le C++ et son recompilateur x86-64 ; gl/ (Mesa) reste a l'asm du Pi : -DN64_CPP ; le GPU Intel par
		# GpuKit et GlKit, gk_gl_attach)
		FLAGS="$FLAGS -O2 -w -ffp-contract=off -DN64_CPP"
		glkit_extra gk || return 0 ;;
	cosmozoom)	# (GpuKit par GlKit sur le PC : gk_gl_attach)
		glkit_extra gk || return 0 ;;
	gcemu)	# (le Gekko et son recompilateur x86-64 d'Onyx, gc/gc_jit_x64.cpp ; le GX par GlKit : le GPU Intel, sinon
		# softpipe ; gx_asm.asm reste au Pi)
		FLAGS="$FLAGS -O3 -w -ffp-contract=off"
		glkit_extra || return 0
		# (RVZ / WIA : zstd, tools/zstd/build.sh)
		[ -f "$ROOT/build/zstd/x86_64/lib/libzstd.a" ] || ARCHS=x86_64 sh "$ROOT/tools/zstd/build.sh" >/dev/null
		FLAGS="$FLAGS -I$ROOT/build/zstd/x86_64/include"
		EXTRA="$EXTRA $ROOT/build/zstd/x86_64/lib/libzstd.a" ;;
	esac
	S=$ROOT/src/apps/$app
	O=$OUT/obj/$app
	D=$OUT/sdroot/apps/$app.app
	[ "$app" != basic ] || D=$OUT/sdroot/bin
	# (la commande pkg et le service pkgd : docs/PAQUETS.md ; un seul main.cpp, comme tools/pkg/build.sh pour le Pi)
	[ "$app" != pkg ] || { S=$ROOT/src/bin/pkg; D=$OUT/sdroot/bin; }
	[ "$app" != pkgd ] || { S=$ROOT/src/sys/pkgd; D=$OUT/sdroot/sys; }
	# (l'aide réseau de « Composer avec l'IA » de Koton : src/bin/llm/main.cpp -> bin/llm)
	[ "$app" != llm ] || { S=$ROOT/src/bin/llm; D=$OUT/sdroot/bin; }
	if [ "$app" = printd ]; then	# (le service d'impression, docs/IMPRESSION.md)
		S=$ROOT/src/sys/printd
		D=$OUT/sdroot/sys
		FLAGS="$FLAGS -I$ROOT/src/apps/letters -I$ROOT/src/apps/paint"
	fi
	mkdir -p "$O" "$D"
	cd "$S"
	{
		echo 'all:'
		{ find . -name '*.c' -o -name '*.cpp' | sed 's#^\./##' | grep -v '^data/'
		  case $app in vibe|claude) ls "$ROOT"/src/lib/acp/*.c ;; esac; } | while read -r f; do
			# (une source hors du dossier de l'app : son .o sous son seul nom)
			case $f in /*) o=$O/$(echo "$f" | sed 's#.*/##; s/\.[a-z]*$//').o ;; *) o=$O/$(echo "$f" | sed 's/\.[a-z]*$//').o ;; esac
			echo "all: $o"
			echo "$o: $f"
			echo "	@mkdir -p \$(dir \$@)"
			case $f in
			*.cpp) echo "	${TC}g++ -std=gnu++20 -Wno-write-strings $FLAGS -c $f -o \$@" ;;
			*) echo "	${TC}gcc $FLAGS -c $f -o \$@" ;;
			esac
			echo "-include $(echo "$o" | sed 's/\.o$/.d/')"
		done
	} > "$O/make.mk"
	if ! make -s -j"$JOBS" -f "$O/make.mk" > "$O/make.log" 2>&1; then
		note "$app : ECHEC de compilation ($(grep -m1 -E 'error|Error' "$O/make.log" | cut -c1-200))"
		cat "$O/make.log" | grep -E 'error' | head -5 >&2
		return 1
	fi
	LD=${TC}gcc
	[ -z "$(find . -name '*.cpp' | grep -v '^\./data/')" ] || LD=${TC}g++
	LIBS=$(ls "$GCC"/lib/lib*.a | sed 's#.*/lib\(.*\)\.a#-l\1#' | tr '\n' ' ')
	if ! $LD -specs="$SPECS" $LINKX -Wl,--gc-sections $(find "$O" -name '*.o') -L"$GCC/lib" $LIBS $EXTRA -lm -o "$O/main" > "$O/link.log" 2>&1; then
		note "$app : ECHEC de l'édition de liens ($(grep -m1 -E 'undefined|error' "$O/link.log" | cut -c1-200))"
		grep -E 'undefined|error' "$O/link.log" | sort -u | head -8 >&2
		return 1
	fi
	if [ "$app" = basic ]; then
		${TC}strip -o "$D/basic" "$O/main"
		L=$OUT/sdroot/lib
		mkdir -p "$L"
		cp "$ROOT"/src/lib/basic/bi/*.bi "$L/"
		for k in audiokit netkit gpukit mediakit archivekit documentkit gpiokit printerkit; do python3 -I "$ROOT/tools/kits/abi2bas.py" "$ROOT/src/kits/$k/$k.abi" "$L" >/dev/null; done
		for k in uikit appkit; do python3 -I "$ROOT/tools/kits/abi2bas.py" "$ROOT/src/kits/$k/$k.abi" "$L" --head "$ROOT/src/lib/basic/bi/$k.head" >/dev/null; done
		note "basic : construite ($(stat -c %s "$D/basic") octets, bin/basic)"
		return 0
	fi
	if [ "$app" = printd ] || [ "$app" = pkg ] || [ "$app" = pkgd ] || [ "$app" = llm ]; then
		${TC}strip -o "$D/$app" "$O/main"
		note "$app : construit ($(stat -c %s "$D/$app") octets, $(basename "$D")/$app)"
		return 0
	fi
	${TC}strip -o "$D/main" "$O/main"
	cp app.txt "$D/"
	[ ! -d data ] || cp -r data/. "$D/"
	# (l'aide de l'app et ses images, comme build.ps1 sur le Pi : le menu Aide de menubar, help.app)
	for f in help_*.md; do [ ! -f "$f" ] || cp "$f" "$D/"; done
	[ ! -d help ] || cp -r help "$D/"
	[ "$app" != arabesque ] || [ ! -d presets ] || { mkdir -p "$OUT/sdroot/home/arabesque"; cp presets/*.arb "$OUT/sdroot/home/arabesque/"; }
	# (FM Tracker : instruments et mots dans l'app, morceaux d'exemple dans SD0:/home/music/fms, comme build.ps1)
	if [ "$app" = fmtracker ]; then
		cp -r ins lang "$D/"
		mkdir -p "$OUT/sdroot/home/music/fms"
		cp songs/* "$OUT/sdroot/home/music/fms/"
	fi
	note "$app : construite ($(stat -c %s "$D/main") octets)"
}

fails=0
for app; do
	if ! (build_one "$app"); then
		fails=$((fails + 1))
		[ "$KEEP_GOING" = 1 ] || { cat "$OUT/status.new"; exit 1; }
	fi
done
# (le tableau d'ensemble : les lignes de ce lancement remplacent celles des mêmes apps)
touch "$OUT/status.txt"
for app; do grep -v "^$app : " "$OUT/status.txt" > "$OUT/status.tmp" || true; mv "$OUT/status.tmp" "$OUT/status.txt"; done
cat "$OUT/status.new" >> "$OUT/status.txt"
sort -o "$OUT/status.txt" "$OUT/status.txt"
cat "$OUT/status.new"
echo "apps : $fails échec(s)"
