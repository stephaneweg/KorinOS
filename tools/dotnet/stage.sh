#!/bin/sh
# stage : le runtime partage -> build/dotnet/sdroot/dotnet/, /devtools/dotnet sur la carte, vu comme /devtools/dotnet
# (l'equivalent du GAC : docs/DOTNET.md) : l'hote de tools/dotnet/build.sh, les bibliotheques C#
# de linux-arm64, et le compilateur C#.
#     sh tools/dotnet/stage.sh
#     ARCH=x86_64 sh tools/dotnet/stage.sh   Korin PC -> build/pc/dotnet/sdroot/dotnet/ : l'hote de
#         ARCH=x86_64 sh tools/dotnet/build.sh, le framework et Roslyn de linux-x64 (le SDK du PC, meme
#         version 10.0.14), dllpreload pour x86-64 ; tools/pc/sysroot_dotnet.sh le pose dans l'arbre
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=$HOME/armpi-dotnet
if [ "${ARCH:-aarch64}" = x86_64 ]; then
	FW=$W/sdk/shared/Microsoft.NETCore.App/10.0.14
	O=$ROOT/build/pc/dotnet/sdroot/dotnet
	HOST=$W/obj/coreclr-x64/Corehost.Static/singlefilehost
	SDK=$W/sdk
	TC=${TC:-${KORIN_TOOLCHAINS:-/opt/toolchains}/x86_64-onyx-elf-14.2/bin/x86_64-onyx-elf-}
	SPECS=$ROOT/build/pc/posix/lib/posix.specs
else
	FW=$W/rt-arm64/shared/Microsoft.NETCore.App/10.0.14
	O=$ROOT/build/dotnet/sdroot/dotnet
	HOST=$W/obj/coreclr/Corehost.Static/singlefilehost
	SDK=$W/sdk-arm64
	TC=${TC:-${KORIN_TOOLCHAINS:-/opt/toolchains}/aarch64-onyx-elf-14.2/bin/aarch64-onyx-elf-}
	SPECS=$ROOT/build/posix/lib/posix.specs
fi
rm -rf "$O"
mkdir -p "$O"
cp "$HOST" "$O/dotnet"
cp "$FW"/*.dll "$O/"
# Roslyn de l'archive de la machine (precompile ReadyToRun) et les assemblys de reference du framework
A=$SDK/sdk/10.0.403/Roslyn/bincore
mkdir -p "$O/roslyn" "$O/ref"
cp "$A"/csc.dll "$A"/Microsoft.CodeAnalysis.dll "$A"/Microsoft.CodeAnalysis.CSharp.dll "$O/roslyn/"
cp "$SDK"/packs/Microsoft.NETCore.App.Ref/*/ref/net10.0/*.dll "$O/ref/"
# Newtonsoft.Json, pour tous les programmes (licence MIT, gardee a cote)
cp "$W/newtonsoft/lib/net6.0/Newtonsoft.Json.dll" "$O/"
cp "$W/newtonsoft/lib/net6.0/Newtonsoft.Json.dll" "$O/ref/"
cp "$W/newtonsoft/LICENSE.md" "$O/Newtonsoft.Json.LICENSE.md"
# un programme C# sans bundle, pour l'essai de l'hote
T=$W/hello-fd
rm -rf "$T"
cp -r "$ROOT/tools/dotnet/hello" "$T"
(cd "$T" && DOTNET_CLI_HOME=$W/cli NUGET_PACKAGES=$W/nuget DOTNET_NOLOGO=1 "$W/sdk/dotnet" build -c Release -v q \
	-p:RuntimeIdentifier= -p:SelfContained=false -p:PublishSingleFile=false -p:PublishTrimmed=false -p:UseAppHost=false -o out)
mkdir -p "$O/test"
cp "$T/out/hello.dll" "$ROOT/tools/dotnet/hello/Program.cs" "$O/test/"
cp -r "$ROOT/tools/dotnet/cshello" "$ROOT/tools/dotnet/libdemo" "$ROOT/tools/dotnet/libuse" "$ROOT/tools/dotnet/docdemo" "$ROOT/tools/dotnet/uidemo" "$ROOT/tools/dotnet/jsondemo" "$O/test/"
${TC}gcc -specs="$SPECS" -O2 -s "$ROOT/tools/dotnet/dllpreload/dllpreload.c" -o "$O/dllpreload"
du -sh "$O"
