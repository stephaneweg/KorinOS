#!/bin/sh
# build-forms : nos trois assemblys sous l'identite de ceux de Windows, compiles sous Windows (Git Bash), sans WSL :
#     sh tools/dotnet/portable/build-forms.sh <dossier des DLL d'ARMPI, System.Kits et System.UIKit> <sortie>
# -publicsign pose la cle publique de Microsoft sans sa cle privee : CoreCLR ne verifie pas la signature.
set -eu
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
KITS=$1
OUT=$2
CR="C:/Program Files/dotnet/packs/Microsoft.NETCore.App.Ref/10.0.12/ref/net10.0"
RS="C:/Program Files/dotnet/sdk/10.0.401/Roslyn/bincore"
D=$ROOT/tools/dotnet
mkdir -p "$OUT"
rsp=$OUT/refs.rsp
{
	echo "-nologo -nostdlib -target:library -unsafe -optimize+ -langversion:latest -nowarn:CS1591,CS0649,CS0169,CS0414,CS0067"
	for f in "$CR"/*.dll; do echo "\"-r:$f\""; done
	echo "\"-r:$KITS/System.Kits.dll\""
	echo "\"-r:$KITS/System.UIKit.dll\""
} > "$rsp"
dotnet "$RS/csc.dll" "@$rsp" -publicsign -keyfile:"$(cygpath -m "$D/portable/keys/ecma.pub")" -out:"$OUT/System.Windows.Forms.Primitives.dll" $(cygpath -m "$D"/forms/primitives/*.cs)
dotnet "$RS/csc.dll" "@$rsp" -publicsign -keyfile:"$(cygpath -m "$D/portable/keys/open.pub")" -out:"$OUT/System.Drawing.Common.dll" $(cygpath -m "$D"/forms/drawing/*.cs)
dotnet "$RS/csc.dll" "@$rsp" "-r:$OUT/System.Drawing.Common.dll" "-r:$OUT/System.Windows.Forms.Primitives.dll" -publicsign -keyfile:"$(cygpath -m "$D/portable/keys/ecma.pub")" -out:"$OUT/System.Windows.Forms.dll" $(cygpath -m "$D"/forms/*.cs)
ls -l "$OUT"/*.dll
