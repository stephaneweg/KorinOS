#!/bin/sh
# stage : le runtime partage -> build/dotnet/sdroot/dotnet/, SD0:/dotnet sur la carte, vu comme /dotnet
# (l'equivalent du GAC : docs/DOTNET.md) : l'hote de tools/dotnet/build.sh, les bibliotheques C#
# de linux-arm64, et le compilateur C#.
#     sh tools/dotnet/stage.sh
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=$HOME/armpi-dotnet
FW=$W/rt-arm64/shared/Microsoft.NETCore.App/10.0.14
O=$ROOT/build/dotnet/sdroot/dotnet
rm -rf "$O"
mkdir -p "$O"
cp "$W/obj/coreclr/Corehost.Static/singlefilehost" "$O/dotnet"
cp "$FW"/*.dll "$O/"
# Roslyn de l'archive arm64 (precompile ReadyToRun) et les assemblys de reference du framework
A=$W/sdk-arm64/sdk/10.0.403/Roslyn/bincore
mkdir -p "$O/roslyn" "$O/ref"
cp "$A"/csc.dll "$A"/Microsoft.CodeAnalysis.dll "$A"/Microsoft.CodeAnalysis.CSharp.dll "$O/roslyn/"
cp "$W"/sdk-arm64/packs/Microsoft.NETCore.App.Ref/*/ref/net10.0/*.dll "$O/ref/"
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
${TC:-/opt/toolchains/aarch64-onyx-elf-14.2/bin/aarch64-onyx-elf-}gcc -specs="$ROOT/build/posix/lib/posix.specs" -O2 -s "$ROOT/tools/dotnet/dllpreload/dllpreload.c" -o "$O/dllpreload"
du -sh "$O"
