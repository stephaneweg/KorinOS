#!/bin/sh
# stage : ce qu'Avalonia pose sur la carte -> build/avalonia/sdroot/, recopie par build.ps1 :
# /dotnet/dotnet-skia (l'hote), SkiaSharp, HarfBuzzSharp, Avalonia, et les essais.
#     sh tools/avalonia/stage.sh
set -eu
. "$(dirname "$0")/env.sh"
S=$ARMPI_ROOT/build/avalonia/sdroot
export DOTNET_CLI_HOME=$DN/cli NUGET_PACKAGES=$A/nuget-cache DOTNET_NOLOGO=1 DOTNET_CLI_TELEMETRY_OPTOUT=1 AVALONIA_TELEMETRY_OPTOUT=1
DOTNET=$DN/sdk/dotnet
mkdir -p "$S/dotnet/test"
cp "$OUT/host/dotnet-skia.s" "$S/dotnet/dotnet-skia"
cp "$OUT/host/dotnet.s" "$S/dotnet/dotnet"

# skiatest : compile une fois, sans rien d'ARMPI
T=$OUT/skiatest
rm -rf "$T"
"$DOTNET" build "$ARMPI_ROOT/tools/avalonia/skiatest" -c Release -v q -o "$T/bin" \
	-p:BaseIntermediateOutputPath="$T/obj/" -p:UseAppHost=false
cp "$T/bin/SkiaSharp.dll" "$T/bin/HarfBuzzSharp.dll" "$S/dotnet/"
mkdir -p "$S/dotnet/test/skiatest"
cp "$T/bin/skiatest.dll" "$S/dotnet/test/skiatest/"

# Avalonia : les DLL du paquet, sauf Avalonia.Desktop.dll, remplacee par la notre (build-desktop.sh)
sh "$ARMPI_ROOT/tools/avalonia/build-desktop.sh" > /dev/null
for f in "$A"/lib/*.dll; do
	case "$f" in */Avalonia.Fonts.Inter.dll|*/Avalonia.Themes.Simple.dll) ;; *) cp "$f" "$S/dotnet/" ;; esac
done
cp "$OUT/desktop/Avalonia.Desktop.dll" "$S/dotnet/"
# compiler un projet Avalonia dans l'OS (csbuild) : les taches et le generateur du paquet, et les references
mkdir -p "$S/dotnet/avalonia"
cp "$A/nuget/avalonia/$AVALONIA_VERSION/tools/netstandard2.0/Avalonia.Build.Tasks.dll" 	"$A/nuget/avalonia/$AVALONIA_VERSION/analyzers/dotnet/cs/Avalonia.Generators.dll" 	"$DN/sdk-arm64/sdk/10.0.403/Microsoft.Build.Framework.dll" "$S/dotnet/avalonia/"
P=$S/dotnet/ref/packages
rm -rf "$P"
mkdir -p "$P/Avalonia" "$P/Avalonia.Desktop" "$P/Avalonia.Themes.Fluent" "$P/Avalonia.Fonts.Inter" "$P/Avalonia.Markup.Xaml.Loader" "$P/SkiaSharp" "$P/HarfBuzzSharp"
cp "$A/nuget/avalonia/$AVALONIA_VERSION"/ref/net10.0/*.dll "$P/Avalonia/"
cp "$A/nuget/avalonia.skia/$AVALONIA_VERSION"/lib/net10.0/*.dll "$A/nuget/avalonia.harfbuzz/$AVALONIA_VERSION"/lib/net10.0/*.dll "$P/Avalonia/"
cp "$OUT/desktop/Avalonia.Desktop.dll" "$P/Avalonia.Desktop/"
cp "$A/lib/Avalonia.Themes.Fluent.dll" "$P/Avalonia.Themes.Fluent/"
cp "$A/lib/Avalonia.Fonts.Inter.dll" "$P/Avalonia.Fonts.Inter/"
cp "$S/dotnet/SkiaSharp.dll" "$P/SkiaSharp/"
cp "$S/dotnet/HarfBuzzSharp.dll" "$P/HarfBuzzSharp/"
# avdemo : tel que dotnet build l'a compile sur le PC (docs/AVALONIA.md), jamais recompile ici
V=$ARMPI_ROOT/build/avalonia/avdemo-art/bin/avdemo/release
if [ -f "$V/avdemo.dll" ]; then
	mkdir -p "$S/apps/avdemo.app"
	cp "$V/avdemo.dll" "$S/apps/avdemo.app/main"
	echo skia > "$S/apps/avdemo.app/main.skia"	# (AppKit : l'hote dotnet-skia)
	cp "$ARMPI_ROOT/tools/avalonia/avdemo/app.txt" "$S/apps/avdemo.app/"
fi
# xamlload : le XAML lu a l'execution, de meme compile sur le PC
X=$ARMPI_ROOT/build/avalonia/xamlload-art/bin/xamlload/release
if [ -f "$X/xamlload.dll" ]; then
	mkdir -p "$S/apps/xamlload.app"
	cp "$X/xamlload.dll" "$S/apps/xamlload.app/main"
	cp "$X/Avalonia.Markup.Xaml.Loader.dll" "$S/dotnet/"
	cp "$X/Avalonia.Markup.Xaml.Loader.dll" "$P/Avalonia.Markup.Xaml.Loader/"
	cp "$ARMPI_ROOT/tools/avalonia/xamlload/essai.axaml" "$ARMPI_ROOT/tools/avalonia/xamlload/app.txt" "$S/apps/xamlload.app/"
	echo skia > "$S/apps/xamlload.app/main.skia"
fi
# avstudio : l'IDE en Avalonia, de meme compile sur le PC ; AvaloniaEdit pour tous les programmes
Y=$ARMPI_ROOT/build/avalonia/studio-art/bin/avstudio/release
if [ -f "$Y/avstudio.dll" ]; then
	mkdir -p "$S/apps/avstudio.app" "$S/dotnet/test/avdemo"
	cp "$Y/avstudio.dll" "$S/apps/avstudio.app/main"
	cp "$Y/AvaloniaEdit.dll" "$S/dotnet/"	# (Armpi.Project.dll : kits.sh)
	cp "$ARMPI_ROOT/tools/avalonia/studio/app.txt" "$S/apps/avstudio.app/"
	echo skia > "$S/apps/avstudio.app/main.skia"
	cp "$ARMPI_ROOT"/tools/avalonia/avdemo/* "$S/dotnet/test/avdemo/"
fi
# gpudemo : la demo du GPU ; ses sources sont dans skel/sd0/home/projects (le Studio les ouvre)
G=$OUT/gpudemo/bin
if [ -f "$G/gpudemo.dll" ]; then
	mkdir -p "$S/apps/gpudemo.app"
	cp "$G/gpudemo.dll" "$S/apps/gpudemo.app/main"
	cp "$ARMPI_ROOT/skel/sd0/home/projects/gpudemo/app.txt" "$S/apps/gpudemo.app/"
	echo skia > "$S/apps/gpudemo.app/main.skia"
fi
du -sh "$S"
