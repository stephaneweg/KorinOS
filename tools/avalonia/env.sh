# env.sh -- les versions et les dossiers du portage d'Avalonia (docs/AVALONIA.md). Tout se construit
# dans WSL, hors du depot et du selfhost, sous ~/armpi-avalonia. Inclus par les autres scripts.
AVALONIA_VERSION=12.1.4
SKIASHARP_VERSION=3.119.4
HARFBUZZSHARP_VERSION=8.3.1.3
SKIA_COMMIT=7dbfc07dd33181f84e0958afb7ee805c6c769f0b		# externals/skia de SkiaSharp v3.119.4 (mono/skia, m119)
HARFBUZZ_TAG=8.3.1

ARMPI_ROOT=$(cd "$(dirname "$0")/../.." && pwd)
A=$HOME/armpi-avalonia
SRC=$A/src
DN=$HOME/armpi-dotnet
if [ "${ARCH:-aarch64}" = x86_64 ]; then	# Korin PC (docs/AVALONIA.md, « Le PC ») : Skia sur le processeur seul
	OUT=$A/out-x64
	TC=/opt/toolchains/x86_64-onyx-elf-14.2/bin/x86_64-onyx-elf-
	WK=$HOME/korin-webkit-x86_64/sysroot		# freetype, libpng, libjpeg, libwebp, zlib, brotli (ceux de Jet)
	CPUFLAGS=
	SKIA_CPU=x64
	DNOBJ=$DN/obj/coreclr-x64
	POSIX=$ARMPI_ROOT/build/pc/posix
	KITS=$ARMPI_ROOT/build/pc/dotnet/sdroot/dotnet
	SDROOT=$ARMPI_ROOT/build/pc/avalonia/sdroot
else
	OUT=$A/out
	TC=/opt/toolchains/aarch64-onyx-elf-14.2/bin/aarch64-onyx-elf-
	WK=$HOME/armpi-webkit-build/sysroot			# freetype, libpng, libjpeg, libwebp, zlib, brotli
	CPUFLAGS=-mcpu=cortex-a72
	SKIA_CPU=arm64
	DNOBJ=$DN/obj/coreclr
	POSIX=$ARMPI_ROOT/build/posix
	KITS=$ARMPI_ROOT/build/dotnet/sdroot/dotnet
	SDROOT=$ARMPI_ROOT/build/avalonia/sdroot
fi
JOBS=${JOBS:-$(nproc)}
mkdir -p "$SRC" "$OUT"

nuget () {	# id version : le paquet NuGet deballe dans ~/armpi-avalonia/nuget/id/version
	d=$A/nuget/$1/$2
	[ -d "$d/lib" ] || [ -d "$d/build" ] && return 0
	mkdir -p "$d"
	curl -sfL -o "$d.nupkg" "https://api.nuget.org/v3-flatcontainer/$1/$2/$1.$2.nupkg"
	(cd "$d" && unzip -q -o "$d.nupkg")
}
