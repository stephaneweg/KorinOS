#!/bin/sh
# view3dtest : System.3D sur le PC (WSL), par le processeur : images .ppm et verifications, sans ARMPI.
# A lancer apres tools/dotnet/kits.sh (il prend ses System.*.dll).
#     sh tools/dotnet/view3dtest.sh [dossier des images]
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=$HOME/armpi-dotnet
D=$ROOT/tools/dotnet
K=${KITS:-$W/kits}
T=$(mktemp -d)
O=${1:-$T}
export DOTNET_ROOT=$W/sdk DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1
REF=$(ls -d "$W"/sdk-arm64/packs/Microsoft.NETCore.App.Ref/*/ref/net10.0)
refs=""
for f in "$REF"/*.dll; do refs="$refs -r:$f"; done
for f in System.Kits System.UIKit System.GpuKit System.Windows.Forms System.3D; do refs="$refs -r:$K/$f.dll"; done
"$W/sdk/dotnet" "$W/sdk/sdk/10.0.403/Roslyn/bincore/csc.dll" -nologo -nostdlib -target:exe -unsafe -optimize+ -nowarn:CS1591 $refs \
	-out:"$T/view3dtest.dll" "$D"/view3dtest/*.cs "$D/view3d/Scenes.cs" "$D/view3d/Teapot.cs"
cp "$K"/System.Kits.dll "$K"/System.UIKit.dll "$K"/System.GpuKit.dll "$K"/System.Windows.Forms.dll "$K"/System.3D.dll "$T/"
fw=$(ls -d "$W"/sdk/shared/Microsoft.NETCore.App/* | head -1)
cat > "$T/view3dtest.runtimeconfig.json" <<EOF
{ "runtimeOptions": { "tfm": "net10.0", "framework": { "name": "Microsoft.NETCore.App", "version": "$(basename "$fw")" } } }
EOF
"$W/sdk/dotnet" "$T/view3dtest.dll" "$O"
