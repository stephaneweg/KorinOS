#!/bin/sh
# .NET dans l'arbre système du PC (docs/DOTNET.md, « Le PC » ; docs/etude/PC-PILOTES.md, « .NET pour le PC »), dans
# WSL, après tools/pc/sysroot.sh (qui refait build/pc/sysroot) ; comme build.ps1 pour le Pi :
#     dotnet/                    le runtime partagé, vu comme /devtools/dotnet (etc/paths.txt) : l'hôte (ARCH=x86_64 sh
#                                tools/dotnet/build.sh), le framework de linux-x64, Roslyn, les références, nos
#                                assemblys (kits, WinForms, csbuild, uigen : ARCH=x86_64 sh tools/dotnet/stage.sh puis
#                                kits.sh), dllpreload ; dotnet-skia et Avalonia (ARCH=x86_64 sh tools/avalonia/...)
#     apps/studio.app, view3d.app   le Studio WinForms et l'essai de System.3D (kits.sh)
#     apps/avdemo.app, xamlload.app, avstudio.app, gpudemo.app   Avalonia (tools/avalonia/stage.sh), avec main.skia
#     bin/dotnethello, dotnetcheck   avec TESTS=1 : les essais publiés en un seul fichier (ARCH=x86_64 sh tools/dotnet/hello.sh check)
#     etc/session                « /devtools/dotnet/dllpreload boot » : les DLL cochées dans Réglages > .NET, gardées
#                                dès le démarrage (le volet .NET n'est montré que si dotnet/dllpreload est là)
#
#   wsl sh tools/pc/sysroot.sh && wsl sh tools/pc/sysroot_dotnet.sh [arbre]
# Ce qui n'est pas construit est sauté et dit. Écrire dans build/pc/sysroot pendant que d'autres scripts y
# travaillent : sous le verrou (mkdir build/pc.lock && { ...; rmdir build/pc.lock; }).
set -e
cd "$(dirname "$0")/../.."

OUT=${1:-build/pc/sysroot}
[ -d "$OUT/bin" ] || { echo "absent : $OUT (d'abord : wsl sh tools/pc/sysroot.sh)"; exit 1; }
n0=$(du -sk "$OUT" | cut -f1)
D=build/pc/dotnet/sdroot
if [ -x "$D/dotnet/dotnet" ]; then
	rm -rf "$OUT/dotnet" "$OUT/devtools/dotnet"
	cp -r "$D/dotnet" "$OUT/"
	[ ! -d "$D/apps" ] || cp -r "$D/apps" "$OUT/"
	# les essais publiés en un seul fichier (20 Mo chacun) : seulement avec TESTS=1
	[ -z "$TESTS" ] || [ ! -d "$D/bin" ] || cp "$D"/bin/* "$OUT/bin/"
	[ -f "$D/dotnet/System.UIKit.dll" ] || echo "sysroot_dotnet : pas de kits ni de WinForms (ARCH=x86_64 sh tools/dotnet/kits.sh)"
else
	echo "sysroot_dotnet : pas de .NET (ARCH=x86_64 sh tools/dotnet/build.sh, puis stage.sh)"
	exit 0
fi
A=build/pc/avalonia/sdroot
if [ -x "$A/dotnet/dotnet-skia" ]; then
	cp -r "$A"/. "$OUT/"	# (son dotnet/dotnet : le même hôte, relié à la même libposix)
else
	echo "sysroot_dotnet : pas d'Avalonia (ARCH=x86_64 sh tools/avalonia/build-skia.sh, build-desktop.sh, build-host.sh, stage.sh)"
fi
# l'icône de chaque app (tools/icons/rendered, comme build.ps1 sur le Pi)
for d in "$OUT"/apps/studio.app "$OUT"/apps/view3d.app "$OUT"/apps/avdemo.app "$OUT"/apps/xamlload.app \
	"$OUT"/apps/avstudio.app "$OUT"/apps/gpudemo.app; do
	f=tools/icons/rendered/$(basename "$d" .app).rgba
	[ -d "$d" ] && [ -f "$f" ] && cp "$f" "$d/icon.rgba"
done
# le préchargement des DLL, relancé à chaque séance (rien si la liste est vide)
S=$OUT/etc/session
if [ -f "$OUT/dotnet/dllpreload" ] && [ -f "$S" ] && ! grep -q "^/devtools/dotnet/dllpreload" "$S"; then
	printf '# les DLL .NET cochees dans Reglages > .NET (tools/pc/sysroot_dotnet.sh)\n/devtools/dotnet/dllpreload boot\n' >> "$S"
fi
n=$(du -sk "$OUT" | cut -f1)
echo "sysroot_dotnet : $OUT, $(( (n - n0) / 1024 )) Mo de plus"

# les outils de développement sous /devtools (arbre unique)
sh tools/devtools_move.sh "$OUT"
