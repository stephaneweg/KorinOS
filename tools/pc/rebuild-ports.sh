#!/bin/bash
# rebuild-ports.sh -- refait tous les gros ports du PC (x86-64), dans WSL, depuis la racine du dépôt, après un
# changement qui touche leurs chemins (l'arbre unique du 15/11/2026 : /devtools, HOME:) ou après la perte des arbres
# de WSL. Chaque étape est le script du dépôt qui la fait d'habitude ; une étape qui échoue est notée et les
# suivantes continuent (sauf celles qui en dépendent). Journal par étape : build/pc/rebuild/<étape>.log ; le bilan à
# la fin dans build/pc/rebuild/bilan.txt.
#
#     bash tools/pc/rebuild-ports.sh                 tout
#     bash tools/pc/rebuild-ports.sh gcc python      seulement ces étapes (dans l'ordre de la liste)
#     bash tools/pc/rebuild-ports.sh --list          les étapes
#
# Ensuite : sh tools/pc/sysroot.sh, sysroot_apps.sh, sysroot_webkit.sh, sysroot_git.sh, sysroot_python.sh,
# sysroot_dotnet.sh, sysroot_gcc.sh, sysroot_cao.sh, sysroot_godot.sh (tools/pc/rebuild-ports.sh sysroot le fait).
set -u
cd "$(dirname "$0")/../.."
LOG=build/pc/rebuild
mkdir -p "$LOG"
export ARCH=x86_64
# les archives de bash et de PCRE2 (rg) : celles de tools/cloud/setup.sh sources, s'il les a mises dans le cache
K=$HOME/.cache/korin-src
[ -f "$K/bash/bash_5.2.37.orig.tar.xz" ] && export BASH_TAR=$K/bash/bash_5.2.37.orig.tar.xz
[ -f "$K/rg-deps/pcre2_10.46.orig.tar.gz" ] && export PCRE2_TAR=$K/rg-deps/pcre2_10.46.orig.tar.gz

STEPS="gcc gcckits make fbc fasm openssl webkitdeps mesa webkitui anglesrc webkit jet jsc git rg bash python rust spotify sitepackages
agent claude dotnetsetup dotnet dotnetstage dotnetkits avalonia cao godot sysroot"
declare -A CMD NEED
CMD[gcc]="sh tools/gcc/build.sh"
CMD[gcckits]="sh tools/gcc/kits.sh";                       NEED[gcckits]="gcc"
CMD[make]="sh tools/gcc/make.sh";                          NEED[make]="gcc"
CMD[fbc]="sh tools/gcc/fbc.sh";                            NEED[fbc]="gcc"
CMD[fasm]="sh tools/pc/fasm.sh"
CMD[openssl]="sh tools/openssl/build.sh"
CMD[webkitdeps]="sh tools/webkit/build-deps.sh"
CMD[mesa]="sh tools/mesa/fetch.sh && sh tools/mesa/configure-iris.sh && sh tools/mesa/build-iris.sh && sh tools/mesa/build-glkit-pc.sh && sh tools/mesa/configure-anv.sh && sh tools/mesa/build-anv.sh"; NEED[mesa]="webkitdeps"
CMD[webkitui]="sh tools/webkit/build-ui.sh && sh tools/webkit/build-media.sh"; NEED[webkitui]="webkitdeps"
CMD[anglesrc]="sh tools/mesa/fetch-angle.sh"
CMD[webkit]="sh tools/mesa/build-webkit.sh";               NEED[webkit]="webkitui mesa anglesrc"
CMD[jet]="sh tools/webkit/build-jet.sh";                   NEED[jet]="webkit"
CMD[jsc]="INTERP=ftl sh tools/webkit/build-jsc.sh install"; NEED[jsc]="webkitdeps"
CMD[git]="sh tools/git/build.sh";                          NEED[git]="openssl"
CMD[rg]="sh tools/rg/build.sh"
CMD[bash]="sh tools/bash/build.sh"
CMD[python]="sh tools/python/build.sh";                    NEED[python]="openssl"
CMD[rust]="SKIP_TESTS=1 bash tools/rust/build.sh"
CMD[spotify]="bash tools/spotify/build.sh && sh tools/pc/apps.sh spotify"; NEED[spotify]="rust openssl"
CMD[sitepackages]="sh tools/python/site-packages.sh";      NEED[sitepackages]="python rust"
CMD[agent]="sh tools/agent/build.sh"
CMD[claude]="bash tools/claude/build.sh";                  NEED[claude]="python"
CMD[dotnetsetup]="sh tools/dotnet/setup.sh";               NEED[dotnetsetup]="webkitdeps"
CMD[dotnet]="sh tools/dotnet/build.sh";                    NEED[dotnet]="dotnetsetup"
CMD[dotnetstage]="sh tools/dotnet/stage.sh";               NEED[dotnetstage]="dotnet"
CMD[dotnetkits]="sh tools/dotnet/kits.sh";                 NEED[dotnetkits]="dotnetstage"
CMD[avalonia]="sh tools/avalonia/build-skia.sh && sh tools/avalonia/build-desktop.sh && sh tools/avalonia/build-host.sh && sh tools/avalonia/stage.sh"; NEED[avalonia]="dotnetkits"
CMD[cao]="sh tools/cao/fetch.sh && sh tools/cao/build-planegcs.sh && sh tools/cao/build-occt.sh && sh tools/cao/build-test.sh"
CMD[godot]="sh tools/godot/fetch.sh && sh tools/godot/build.sh && sh tools/godot/build.sh template_release && sh tools/godot/build.sh template_debug"; NEED[godot]="mesa"
CMD[sysroot]="sh tools/pc/sysroot.sh && sh tools/pc/sysroot_apps.sh; for x in webkit git python dotnet gcc cao godot; do sh tools/pc/sysroot_\$x.sh || echo \"sysroot_\$x : échec\"; done"

if [ "${1:-}" = --list ]; then
	for s in $STEPS; do printf '%-13s %s\n' "$s" "${CMD[$s]}"; done
	exit 0
fi
WANT="$*"
declare -A RES
: > "$LOG/bilan.txt"
for s in $STEPS; do
	if [ -n "$WANT" ] && ! echo " $WANT " | grep -q " $s "; then continue; fi
	skip=""
	for d in ${NEED[$s]:-}; do
		[ "${RES[$d]:-ok}" = ok ] || skip=$d
	done
	if [ -n "$skip" ]; then
		RES[$s]="sauté ($skip a échoué)"
	else
		t0=$(date +%s)
		echo "== $s : ${CMD[$s]} ($(date +%H:%M))"
		if bash -c "${CMD[$s]}" > "$LOG/$s.log" 2>&1; then RES[$s]=ok; else RES[$s]="ÉCHEC (code $?)"; fi
		RES[$s]="${RES[$s]}"
		echo "   -> ${RES[$s]} en $(( ($(date +%s) - t0) / 60 )) min"
	fi
	printf '%-13s %s\n' "$s" "${RES[$s]}" >> "$LOG/bilan.txt"
done
echo; cat "$LOG/bilan.txt"
