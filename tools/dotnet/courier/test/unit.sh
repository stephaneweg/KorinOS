#!/bin/sh
# unit : le coeur de Courier (Model.cs, Net.cs, Tools.cs) essaye sur le PC, dans WSL, avec le SDK d'armpi-dotnet.
#     sh tools/dotnet/courier/test/unit.sh
set -eu
ROOT=$(cd "$(dirname "$0")/../../../.." && pwd)
W=$HOME/armpi-dotnet
T=$W/courier-unit
C=$ROOT/tools/dotnet/courier
mkdir -p "$T"
export DOTNET_ROOT=$W/sdk DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1
REF=$(ls -d "$W"/sdk/packs/Microsoft.NETCore.App.Ref/*/ref/net10.0 2>/dev/null || ls -d "$W"/sdk-arm64/packs/Microsoft.NETCore.App.Ref/*/ref/net10.0)
refs=""
for f in "$REF"/*.dll; do refs="$refs -r:$f"; done
"$W/sdk/dotnet" "$W/sdk/sdk/10.0.403/Roslyn/bincore/csc.dll" -nologo -nostdlib -target:exe -optimize+ -langversion:latest -nowarn:CS8632 \
	$refs -out:"$T/unit.dll" "$C/Model.cs" "$C/Net.cs" "$C/Tools.cs" "$C/test/unit/Program.cs"
cat > "$T/unit.runtimeconfig.json" <<EOF
{ "runtimeOptions": { "tfm": "net10.0", "framework": { "name": "Microsoft.NETCore.App", "version": "10.0.0" }, "rollForward": "LatestMinor",
  "configProperties": { "System.Globalization.Invariant": true } } }
EOF
"$W/sdk/dotnet" exec "$T/unit.dll" "$C/test/seed/collections/essai.postman_collection.json"
