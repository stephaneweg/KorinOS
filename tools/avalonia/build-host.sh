#!/bin/sh
# build-host : le second hote, /dotnet/dotnet-skia : l'hote statique de tools/dotnet/build.sh, avec
# libSkiaSharp et libHarfBuzzSharp liees dedans et resolues par PINVOKE_OVERRIDE. L'hote /dotnet/dotnet
# ne change pas : on reprend ses objets et sa ligne de liaison (~/armpi-dotnet/obj/coreclr), seul
# armpihost.cpp est recompile, avec un appel a la table de gen-table.py. Skia avec Ganesh et Mesa par armgl
# (tools/mesa/build-skia.sh, build-armgl.sh) : le rendu sur le GPU du Pi, ArmpiGl.cs de notre Avalonia.Desktop.
#     sh tools/avalonia/build-host.sh
set -eu
. "$(dirname "$0")/env.sh"
H=$OUT/host
O=$DN/obj/coreclr
M=$HOME/armpi-mesa/out
mkdir -p "$H"

# le C# d'en face : les seules fonctions gardees sont celles qu'il importe
nuget skiasharp $SKIASHARP_VERSION
nuget harfbuzzsharp $HARFBUZZSHARP_VERSION
SKDLL=$A/nuget/skiasharp/$SKIASHARP_VERSION/lib/net10.0/SkiaSharp.dll
HBDLL=$A/nuget/harfbuzzsharp/$HARFBUZZSHARP_VERSION/lib/net8.0/HarfBuzzSharp.dll
${TC}nm -g --defined-only "$M"/skia/libskia.a "$M"/skia/libSkiaSharp.a "$OUT"/hb/libHarfBuzzSharp.a "$ARMPI_ROOT"/prebuilt/mesa/lib/libarmgl.a 2>/dev/null > "$H/nm.txt"
python3 "$ARMPI_ROOT/tools/avalonia/gen-table.py" "$H/table.c" "$H/nm.txt" "libSkiaSharp=$SKDLL" "libHarfBuzzSharp=$HBDLL" "libarmgl=$OUT/desktop/Avalonia.Desktop.dll"

cd "$O"
OBJ=Corehost.Static/CMakeFiles/singlefilehost.dir/armpihost.cpp.o
[ -f "$OBJ" ] || OBJ=$(ninja -t query Corehost.Static/singlefilehost | grep 'armpihost.cpp.o' | tr -d ' ')
CMD=$(ninja -t commands "$OBJ" | tail -1)
SRCF=$DN/runtime/src/native/corehost/apphost/static/armpihost.cpp
sed -e 's/^static const void\* armpi_pinvoke_override(const char\* library, const char\* entry)$/extern "C" const void* armpi_native_resolve(const char* library, const char* entry);\n&/' \
    -e 's/^    if (strcmp(library, "libarmpi") == 0)$/    if (const void* p = armpi_native_resolve(library, entry))\n        return p;\n&/' \
    "$SRCF" > "$H/armpihost.cpp"
grep -q 'return p;' "$H/armpihost.cpp"
# (la commande de ninja, sur notre copie ; un seul objet)
CMD2=$(echo "$CMD" | sed -e "s| -MD -MT [^ ]* -MF [^ ]*||" -e "s| -o [^ ]*| -o $H/armpihost.o|" -e "s| -c [^ ]*| -c $H/armpihost.cpp|" -e 's/&&.*$//' -e 's/^: *//')
sh -c "$CMD2"
${TC}gcc -specs="$ARMPI_ROOT/build/posix/lib/posix.specs" -mcpu=cortex-a72 -O2 -c "$H/table.c" -o "$H/table.o"

# (la libposix de ce depot : ninja garde celle du depot qui a configure l'arbre)
LINK=$(ninja -t commands Corehost.Static/singlefilehost | tail -1 | sed "s|-specs=[^ ]*/build/posix/lib/posix.specs|-specs=$ARMPI_ROOT/build/posix/lib/posix.specs|g")
EXTRA="$H/table.o -Wl,--start-group $M/skia/libSkiaSharp.a $M/skia/libskia.a $M/skia/libskcms.a $OUT/hb/libHarfBuzzSharp.a $ARMPI_ROOT/prebuilt/mesa/lib/libarmgl.a $ARMPI_ROOT/prebuilt/mesa/lib/libmesa_v3d.a \
$WK/lib/libfreetype.a $WK/lib/libpng16.a $WK/lib/libjpeg.a $WK/lib/libwebpdemux.a $WK/lib/libwebpmux.a $WK/lib/libwebp.a $WK/lib/libsharpyuv.a $WK/lib/libbrotlidec.a $WK/lib/libz.a -Wl,--end-group"
LINK2=$(echo "$LINK" | sed -e "s|$OBJ|$H/armpihost.o|" -e "s|-Wl,--dependency-file=[^ ]*||" \
	-e "s| -o Corehost.Static/singlefilehost| -o $H/dotnet-skia $EXTRA|" -e 's/^: && //' -e 's/ && .*$//')
# ce qui manque sans GPU (les entrees gr_*, sk_surface_new_render_target...) : une fonction qui rend 0,
# SkiaSharp en fait un objet nul. Le rendu logiciel n'y passe pas.
: > "$H/stubs.c"
sh -c "$LINK2" 2> "$H/link.err" || {
	sed -n "s/.*undefined reference to \`\(.*\)'.*/\1/p" "$H/link.err" | sort -u | sed 's/.*/void* & (void) { return 0; }/' > "$H/stubs.c"
	[ -s "$H/stubs.c" ] || { tail -20 "$H/link.err"; exit 1; }
	wc -l "$H/stubs.c"
	${TC}gcc -specs="$ARMPI_ROOT/build/posix/lib/posix.specs" -mcpu=cortex-a72 -O2 -c "$H/stubs.c" -o "$H/stubs.o"
	sh -c "$(echo "$LINK2" | sed "s| $H/table.o | $H/table.o $H/stubs.o |")"
}
${TC}strip -o "$H/dotnet-skia.s" "$H/dotnet-skia"
# l'hote ordinaire, relie a la meme libposix (armpihost.cpp de l'arbre, sans table)
sh -c "$(echo "$LINK" | sed -e "s|-Wl,--dependency-file=[^ ]*||" -e "s| -o Corehost.Static/singlefilehost| -o $H/dotnet|" -e 's/^: \&\& //' -e 's/ \&\& .*$//')"
${TC}strip -o "$H/dotnet.s" "$H/dotnet"
ls -l "$O/Corehost.Static/singlefilehost" "$H/dotnet.s" "$H/dotnet-skia" "$H/dotnet-skia.s"
${TC}size "$O/Corehost.Static/singlefilehost" "$H/dotnet-skia"
