#!/bin/sh
# build-host : le second hote, /devtools/dotnet/dotnet-skia : l'hote statique de tools/dotnet/build.sh, avec
# libSkiaSharp et libHarfBuzzSharp liees dedans et resolues par PINVOKE_OVERRIDE. L'hote /devtools/dotnet/dotnet
# ne change pas : on reprend ses objets et sa ligne de liaison (~/armpi-dotnet/obj/coreclr), seul
# armpihost.cpp est recompile, avec un appel a la table de gen-table.py. Skia avec Ganesh et Mesa par armgl
# (tools/mesa/build-skia.sh, build-armgl.sh) : le rendu sur le GPU du Pi, ArmpiGl.cs de notre Avalonia.Desktop.
#     sh tools/avalonia/build-host.sh
#     ARCH=x86_64 sh tools/avalonia/build-host.sh   Korin PC : Skia sur le processeur seul (build-skia.sh), sans
#         Mesa ni GlKit ; les entrees GPU et armgl rendent 0 (« le processeur (armgl 0) »)
set -eu
. "$(dirname "$0")/env.sh"
H=$OUT/host
O=$DNOBJ
if [ "$SKIA_CPU" = x64 ]; then
	SK=$OUT/skia
	GL=$OUT/host/armgl_cpu.o	# (armgl_init rend 0 : ArmpiGl.cs reste sur le processeur, Skia n'a pas Ganesh ici)
	GLLIB=$GL
else
	SK=$HOME/armpi-mesa/out/skia
	GL=$ARMPI_ROOT/prebuilt/mesa/lib/libarmgl.a
	GLLIB=$ARMPI_ROOT/prebuilt/mesa/lib/libglkit.a
fi
SPECS=$POSIX/lib/posix.specs
mkdir -p "$H"
if [ "$SKIA_CPU" = x64 ]; then
	echo '/* PC : Skia sans GPU, armgl sans contexte (build-host.sh) */' > "$H/armgl_cpu.c"
	echo 'int armgl_init (int flags) { (void) flags; return 0; }' >> "$H/armgl_cpu.c"
	${TC}gcc -specs="$SPECS" -O2 -c "$H/armgl_cpu.c" -o "$GL"
fi

# le C# d'en face : les seules fonctions gardees sont celles qu'il importe
nuget skiasharp $SKIASHARP_VERSION
nuget harfbuzzsharp $HARFBUZZSHARP_VERSION
SKDLL=$A/nuget/skiasharp/$SKIASHARP_VERSION/lib/net10.0/SkiaSharp.dll
HBDLL=$A/nuget/harfbuzzsharp/$HARFBUZZSHARP_VERSION/lib/net8.0/HarfBuzzSharp.dll
${TC}nm -g --defined-only "$SK"/libskia.a "$SK"/libSkiaSharp.a "$OUT"/hb/libHarfBuzzSharp.a $GL 2>/dev/null > "$H/nm.txt"
python3 "$ARMPI_ROOT/tools/avalonia/gen-table.py" "$H/table.c" "$H/nm.txt" "libSkiaSharp=$SKDLL" "libHarfBuzzSharp=$HBDLL" "libarmgl=$OUT/desktop/Avalonia.Desktop.dll"

cd "$O"
OBJ=Corehost.Static/CMakeFiles/singlefilehost.dir/armpihost.cpp.o
[ -f "$OBJ" ] || OBJ=$(ninja -t query Corehost.Static/singlefilehost | grep 'armpihost.cpp.o' | tr -d ' ')
# (les chemins d'un worktree supprime, gardes par ninja : ceux de ce depot)
WT="s#/mnt/c/Projects/ARMPI/.claude/worktrees/[^/ ]*/#$ARMPI_ROOT/#g"
CMD=$(ninja -t commands "$OBJ" | tail -1 | sed "$WT")
SRCF=$DN/runtime/src/native/corehost/apphost/static/armpihost.cpp
sed -e 's/^static const void\* armpi_pinvoke_override(const char\* library, const char\* entry)$/extern "C" const void* armpi_native_resolve(const char* library, const char* entry);\n&/' \
    -e 's/^    if (strcmp(library, "libarmpi") == 0)$/    if (const void* p = armpi_native_resolve(library, entry))\n        return p;\n&/' \
    "$SRCF" > "$H/armpihost.cpp"
grep -q 'return p;' "$H/armpihost.cpp"
# (la commande de ninja, sur notre copie ; un seul objet)
CMD2=$(echo "$CMD" | sed -e "s| -MD -MT [^ ]* -MF [^ ]*||" -e "s| -o [^ ]*| -o $H/armpihost.o|" -e "s| -c [^ ]*| -c $H/armpihost.cpp|" -e 's/&&.*$//' -e 's/^: *//')
sh -c "$CMD2"
${TC}gcc -specs="$SPECS" $CPUFLAGS -O2 -c "$H/table.c" -o "$H/table.o"

# (la libposix de ce depot : ninja garde celle du depot qui a configure l'arbre)
LINK=$(ninja -t commands Corehost.Static/singlefilehost | tail -1 | sed "s|-specs=[^ ]*/build/posix/lib/posix.specs|-specs=$SPECS|g;s|-specs=[^ ]*/build/pc/posix/lib/posix.specs|-specs=$SPECS|g" | sed "$WT")
EXTRA="$H/table.o -Wl,--start-group $SK/libSkiaSharp.a $SK/libskia.a $SK/libskcms.a $OUT/hb/libHarfBuzzSharp.a $GLLIB \
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
	${TC}gcc -specs="$SPECS" $CPUFLAGS -O2 -c "$H/stubs.c" -o "$H/stubs.o"
	sh -c "$(echo "$LINK2" | sed "s| $H/table.o | $H/table.o $H/stubs.o |")"
}
${TC}strip -o "$H/dotnet-skia.s" "$H/dotnet-skia"
# l'hote ordinaire, relie a la meme libposix (armpihost.cpp de l'arbre, sans table)
sh -c "$(echo "$LINK" | sed -e "s|-Wl,--dependency-file=[^ ]*||" -e "s| -o Corehost.Static/singlefilehost| -o $H/dotnet|" -e 's/^: \&\& //' -e 's/ \&\& .*$//')"
${TC}strip -o "$H/dotnet.s" "$H/dotnet"
ls -l "$O/Corehost.Static/singlefilehost" "$H/dotnet.s" "$H/dotnet-skia" "$H/dotnet-skia.s"
${TC}size "$O/Corehost.Static/singlefilehost" "$H/dotnet-skia"
