#!/bin/sh
# hello : publie un programme de tools/dotnet (hello, check) en un seul fichier, sur l'hote statique de
# tools/dotnet/build.sh a la place de celui de Microsoft -> build/dotnet/sdroot/bin/dotnet<nom>
# (recopie par build.ps1). Le SDK est dans ~/armpi-dotnet/sdk.
#     sh tools/dotnet/hello.sh [nom]
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=$HOME/armpi-dotnet
export DOTNET_ROOT=$W/sdk DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1 PATH=$W/sdk:$PATH
export DOTNET_CLI_HOME=$W/cli NUGET_PACKAGES=$W/nuget
N=${1:-hello}
H=$W/$N
rm -rf "$H"
cp -r "$ROOT/tools/dotnet/$N" "$H"
cd "$H"
dotnet restore -v q
P=$(ls -d "$NUGET_PACKAGES"/microsoft.netcore.app.host.linux-arm64/10.0.14/runtimes/linux-arm64/native 2>/dev/null || true)
[ -n "$P" ] || P=$(ls -d "$NUGET_PACKAGES"/microsoft.netcore.app.runtime.linux-arm64/10.0.14/runtimes/linux-arm64/native)
cp "$W/obj/coreclr/Corehost.Static/singlefilehost" "$P/singlefilehost"
dotnet publish -c Release -o out -v q
O=$ROOT/build/dotnet/sdroot/bin
mkdir -p "$O"
cp "out/$N" "$O/dotnet$N"
ls -l "$O/dotnet$N"
