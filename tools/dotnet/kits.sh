#!/bin/sh
# kits : les DLL d'ARMPI pour C#, compilees par Roslyn sur le PC (docs/DOTNET.md) :
#     System.Kits.dll        l'ouverture des kits
#     System.UIKit.dll       UIKit, liaison generee depuis son .abi (classe UI) + Ui.cs
#     System.AudioKit.dll    AudioKit, de meme (classe AK)
#     System.NetKit.dll...   les autres kits
#     System.DocumentKit.dll DocumentKit (classe DK) + Doc, en types C#
#     System.Windows.Forms.dll  notre WinForms, sur UIKit (avec System.Windows.Forms.Primitives.dll et System.Drawing.Common.dll)
#     System.3D.dll          Viewport3D, sur GpuKit (docs/VIEWPORT3D.md)
# -> build/dotnet/sdroot/dotnet/. A lancer apres tools/dotnet/stage.sh.
#     sh tools/dotnet/kits.sh
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=$HOME/armpi-dotnet
O=$ROOT/build/dotnet/sdroot/dotnet
T=${T:-$W/kits}
K=$ROOT/src/kits
A=$ROOT/src/inc
D=$ROOT/tools/dotnet
rm -rf "$T"
mkdir -p "$T"
export DOTNET_ROOT=$W/sdk DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1
REF=$(ls -d "$W"/sdk-arm64/packs/Microsoft.NETCore.App.Ref/*/ref/net10.0)
CSC="$W/sdk/dotnet $W/sdk/sdk/10.0.403/Roslyn/bincore/csc.dll -nologo -nostdlib -target:library -unsafe -optimize+ -langversion:latest -nowarn:CS1591,CS0649,CS0169,CS0414"
refs=""
for f in "$REF"/*.dll; do refs="$refs -r:$f"; done
$CSC $refs -out:"$T/System.Kits.dll" "$D/kits/KitLoader.cs"
refs="$refs -r:$T/System.Kits.dll"

kit () {	# nom du kit, assembly, classe, .inc...
	k=$1; a=$2; c=$3
	shift 3
	python3 -I "$ROOT/tools/kits/abi2cs.py" "$K/$k/$k.abi" "$T/$a.g.cs" "$c" "$a" --const "$@"
	extra=""
	[ "$a" = System.UIKit ] && extra="$D/kits/Ui.cs"
	[ "$a" = System.DocumentKit ] && extra="$D/kits/Document.cs $D/kits/Office.cs"
	$CSC $refs -out:"$T/$a.dll" "$T/$a.g.cs" $extra
}
kit uikit System.UIKit UI "$A/armpi.inc" "$A/ws_ext.inc" "$K/uikit/uikit.inc"
kit audiokit System.AudioKit AK "$K/audiokit/audiokit.inc"
kit netkit System.NetKit NK "$K/netkit/netkit.inc"
kit gpukit System.GpuKit GK "$K/gpukit/gpukit.inc"
kit mediakit System.MediaKit MK "$K/mediakit/mediakit.inc"
kit documentkit System.DocumentKit DK "$K/documentkit/documentkit.inc"
# nos trois assemblys de WinForms portent le nom, la version et la cle publique de ceux de Windows : un binaire compile
# contre Windows les trouve ici (docs/WINFORMS-PORTABLE.md). -publicsign : pas de cle privee, CoreCLR ne verifie rien
K2=$D/portable/keys
$CSC $refs -publicsign -keyfile:"$K2/ecma.pub" -out:"$T/System.Windows.Forms.Primitives.dll" "$D"/forms/primitives/*.cs
$CSC $refs -r:"$T/System.UIKit.dll" -publicsign -keyfile:"$K2/open.pub" -out:"$T/System.Drawing.Common.dll" "$D"/forms/drawing/*.cs
refs="$refs -r:$T/System.Windows.Forms.Primitives.dll -r:$T/System.Drawing.Common.dll"
$CSC $refs -r:"$T/System.UIKit.dll" -publicsign -keyfile:"$K2/ecma.pub" -out:"$T/System.Windows.Forms.dll" "$D"/forms/*.cs
# System.3D : Viewport3D et sa scene (l'espace de noms est System.Windows.Media.Media3D)
ui="$refs -r:$T/System.UIKit.dll -r:$T/System.GpuKit.dll -r:$T/System.Windows.Forms.dll"
$CSC $ui -out:"$T/System.3D.dll" "$D"/3d/*.cs
# son essai, installe comme une application
$CSC $ui -r:"$T/System.3D.dll" -target:exe -out:"$T/view3d.dll" "$D"/view3d/*.cs
A3D=$ROOT/build/dotnet/sdroot/apps/view3d.app
mkdir -p "$A3D"
mv "$T/view3d.dll" "$A3D/main"
cp "$D/view3d/app.txt" "$A3D/"
# le Studio : un programme installe, comme ceux de pibuild
$CSC $refs -r:"$T/System.UIKit.dll" -r:"$T/System.Windows.Forms.dll" -target:exe -out:"$T/studio.dll" "$D"/studio/*.cs
A=$ROOT/build/dotnet/sdroot/apps/studio.app
mkdir -p "$A"
mv "$T/studio.dll" "$A/main"
cp "$D/studio/app.txt" "$A/"
# les .ui compiles : le generateur source, charge dans csc (pibuild)
RS=$W/sdk/sdk/10.0.403/Roslyn/bincore
$CSC $refs -r:"$RS/Microsoft.CodeAnalysis.dll" -r:"$RS/Microsoft.CodeAnalysis.CSharp.dll" -out:"$T/uigen.dll" "$D"/uigen/*.cs
# les .csproj sous ARMPI : le lecteur, partage avec le Studio, et csbuild, lance par pibuild
$CSC $refs -out:"$T/Armpi.Project.dll" "$D"/project/*.cs
$CSC $refs -r:"$T/Armpi.Project.dll" -r:"$W/sdk/sdk/10.0.403/Microsoft.Build.Framework.dll" -target:exe -out:"$T/csbuild.dll" "$D"/csbuild/*.cs
# (Courier, tools/dotnet/courier, ne va plus sur la carte : l'application est le port en C++, src/apps/courier. Un ancien
# courier.app d'ici ecraserait le sien : build.ps1 copie build/dotnet apres build/office)
rm -rf "$ROOT/build/dotnet/sdroot/apps/courier.app" "$O/test/courier"
# le programme d'essai : WinForms et .ui (build/dotnet/sdroot/dotnet/test)
$CSC $refs -r:"$T/System.UIKit.dll" -r:"$T/System.Windows.Forms.dll" -target:exe -out:"$T/formsdemo.dll" "$D"/formsdemo/*.cs
cp "$T/uigen.dll" "$O/roslyn/"
rm "$T/uigen.dll"
cp "$T"/*.dll "$O/"
cp "$T"/*.dll "$O/ref/"
cp "$T/formsdemo.dll" "$D"/formsdemo/*.ui "$O/test/"
rm -f "$O/formsdemo.dll" "$O/ref/formsdemo.dll" "$O/ref/csbuild.dll"
rm -rf "$O/test/multi"
cp -r "$D/csbuild/test/multi" "$O/test/"
# les options de Roslyn sous ARMPI : pibuild les donne a chaque compilation
{
	echo "-nologo -nostdlib -optimize+ -unsafe -langversion:latest"
	for f in "$O"/ref/*.dll; do echo "-r:/dotnet/ref/$(basename "$f")"; done
} > "$O/csc.rsp"
ls -l "$T"/*.dll
