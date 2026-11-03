#!/bin/sh
# build-desktop : notre Avalonia.Desktop.dll (la plateforme d'ARMPI), meme nom, meme version et meme cle
# publique que celle du paquet NuGet, compilee par Roslyn contre lib/ : les ref/ du paquet n'ont pas les API
# privees dont un backend a besoin. -publicsign : pas de cle privee, CoreCLR ne verifie rien.
#     sh tools/avalonia/build-desktop.sh
set -eu
. "$(dirname "$0")/env.sh"
D=$ARMPI_ROOT/tools/avalonia/desktop
L=$A/lib
K=$ARMPI_ROOT/build/dotnet/sdroot/dotnet
mkdir -p "$L" "$OUT/desktop"
for p in avalonia avalonia.skia avalonia.harfbuzz avalonia.themes.fluent avalonia.themes.simple avalonia.fonts.inter avalonia.remote.protocol; do
	nuget $p $AVALONIA_VERSION
	cp "$A"/nuget/$p/$AVALONIA_VERSION/lib/net10.0/*.dll "$L/" 2>/dev/null || cp "$A"/nuget/$p/$AVALONIA_VERSION/lib/net8.0/*.dll "$L/"
done
nuget microcom.runtime 0.11.6
cp "$A/nuget/microcom.runtime/0.11.6/lib/net8.0/MicroCom.Runtime.dll" "$L/"
nuget skiasharp $SKIASHARP_VERSION
nuget harfbuzzsharp $HARFBUZZSHARP_VERSION
cp "$A/nuget/skiasharp/$SKIASHARP_VERSION/lib/net10.0/SkiaSharp.dll" "$A/nuget/harfbuzzsharp/$HARFBUZZSHARP_VERSION/lib/net8.0/HarfBuzzSharp.dll" "$L/"
export DOTNET_ROOT=$DN/sdk DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1
REF=$(ls -d "$DN"/sdk-arm64/packs/Microsoft.NETCore.App.Ref/*/ref/net10.0)
refs=""
for f in "$REF"/*.dll "$L"/Avalonia*.dll "$L"/SkiaSharp.dll; do
	case "$f" in */Avalonia.Desktop.dll|*/Avalonia.Fonts.Inter.dll|*/Avalonia.Themes.Simple.dll) ;; *) refs="$refs -r:$f" ;; esac
done
cat > "$OUT/desktop/AssemblyInfo.cs" <<CS
[assembly: System.Reflection.AssemblyVersion("${AVALONIA_VERSION}.0")]
[assembly: System.Reflection.AssemblyFileVersion("${AVALONIA_VERSION}.0")]
CS
"$DN/sdk/dotnet" "$DN/sdk/sdk/10.0.403/Roslyn/bincore/csc.dll" -nologo -nostdlib -target:library -unsafe -optimize+ -langversion:latest \
	-nowarn:CS1591,CS0649,CS0169,CS0414,CS0067 $refs -r:"$K/System.Kits.dll" -r:"$K/System.UIKit.dll" \
	-publicsign -keyfile:"$D/avalonia.pub" -out:"$OUT/desktop/Avalonia.Desktop.dll" "$D"/*.cs "$OUT/desktop/AssemblyInfo.cs"
ls -l "$OUT/desktop/Avalonia.Desktop.dll"
