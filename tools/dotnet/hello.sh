#!/bin/sh
# hello : publie un programme de tools/dotnet (hello, check) en un seul fichier, sur l'hote statique de
# tools/dotnet/build.sh a la place de celui de Microsoft -> build/dotnet/sdroot/bin/dotnet<nom>
# (recopie par build.ps1). Le SDK est dans ~/armpi-dotnet/sdk.
#     sh tools/dotnet/hello.sh [nom]
#     ARCH=x86_64 sh tools/dotnet/hello.sh [nom]   Korin PC (linux-x64) -> build/pc/dotnet/sdroot/bin/
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=$HOME/armpi-dotnet
export DOTNET_ROOT=$W/sdk DOTNET_CLI_TELEMETRY_OPTOUT=1 DOTNET_NOLOGO=1 PATH=$W/sdk:$PATH
export DOTNET_CLI_HOME=$W/cli NUGET_PACKAGES=$W/nuget
N=${1:-hello}
if [ "${ARCH:-aarch64}" = x86_64 ]; then
	RID=linux-x64
	H=$W/$N-x64
	HOST=$W/obj/coreclr-x64/Corehost.Static/singlefilehost
	O=$ROOT/build/pc/dotnet/sdroot/bin
	RIDOPT="-p:RuntimeIdentifier=$RID -p:NetCoreTargetingPackRoot=$W/nopacks -p:AllowMissingPrunePackageData=true"	# (les packs de NuGet, pas ceux du SDK : son hote linux-x64 serait celui de Microsoft)
else
	RID=linux-arm64
	H=$W/$N
	HOST=$W/obj/coreclr/Corehost.Static/singlefilehost
	O=$ROOT/build/dotnet/sdroot/bin
	RIDOPT=
fi
rm -rf "$H"
cp -r "$ROOT/tools/dotnet/$N" "$H"
cd "$H"
dotnet restore -v q $RIDOPT
P=$(ls -d "$NUGET_PACKAGES"/microsoft.netcore.app.host.$RID/10.0.14/runtimes/$RID/native 2>/dev/null || true)
[ -n "$P" ] || P=$(ls -d "$NUGET_PACKAGES"/microsoft.netcore.app.runtime.$RID/10.0.14/runtimes/$RID/native)
cp "$HOST" "$P/singlefilehost"
dotnet publish -c Release -o out -v q $RIDOPT
mkdir -p "$O"
cp "out/$N" "$O/dotnet$N"
ls -l "$O/dotnet$N"
