#!/bin/sh
# build-vkkit-pc.sh -- VkKit du PC (Korin x86-64, docs/etude/VULKAN.md, etape 2) : anv (le pilote Vulkan d'Intel de
# Mesa), sa WSI de Korin et le faux libdrm en un kit partage, comme GlKit (build-glkit-pc.sh) : le meme chargeur
# (tools/mesa/glkit/glkit.c), le meme script d'edition de liens, le meme finish.py et le meme cote programme, copies
# ici et renommes vkkit (un programme peut avoir les deux kits).
#   build/pc/mesa/sdroot/lib/vkkit.so   le kit (/lib/vkkit.so du PC)
#   build/pc/mesa/lib/libvkkit.a        ce que les programmes lient : relais, chargeur du kit, imports, et le
#                                       chargeur Vulkan minimal (vkGetInstanceProcAddr, vkkit/vkkit_loader.c)
#   build/pc/mesa/include/vulkan/       les en-tetes de Vulkan (ceux de Mesa) et vulkan_korin.h
#   build/pc/mesa/sdroot/bin/vktest     vktest lie au kit (avec --win : une fenetre)
# Appele par build-anv.sh a la fin, ou seul (dans WSL, depuis la racine du depot) :
#     sh tools/mesa/build-vkkit-pc.sh
set -eu
. "$(dirname "$0")/env.sh"
TC=${KORIN_TOOLCHAINS:-/opt/toolchains}/x86_64-onyx-elf-14.2/bin/x86_64-onyx-elf-
POSIX=$ARMPI_ROOT/build/pc/posix
OUT=$A/out-x86_64
# VKKIT_DRIVER=lvp : le kit de lavapipe (build-lvp.sh) au lieu d'anv, sorties dans build/pc/mesa-lvp (pour les essais
# sous QEMU) ; VKKIT_TLS : la zone par thread du programme pour le kit (anv : 1024)
DRV=${VKKIT_DRIVER:-anv}
SFX=
[ "$DRV" = anv ] || SFX=-$DRV
TLSA=${VKKIT_TLS:-1024}
V=$OUT/$DRV
K=$OUT/vkkit$SFX
M=$ARMPI_ROOT/tools/mesa
G=$M/glkit
PM=$ARMPI_ROOT/build/pc/mesa$SFX
PCG=$ARMPI_ROOT/build/pc/gcc
[ -f "$V/whole.txt" ] || { echo "build-vkkit-pc.sh : d'abord sh tools/mesa/build-$DRV.sh" >&2; exit 1; }
# les objets du pilote liés au kit en plus des archives (anv : le faux libdrm et son identité)
OBJS=$(cat "$V/objs.txt" 2>/dev/null || echo "$V/korin_i915.o $V/korin_vk_id.o")
WHOLE=$(cat "$V/whole.txt")
LIBS=$(cat "$V/libs.txt")
mkdir -p "$K/src" "$K/gen" "$PM/lib" "$PM/sdroot/lib" "$PM/sdroot/bin" "$PM/include/vulkan"

# les sources du kit : celles de GlKit, renommees (le chargeur sans GL : KIT_NO_GLAPI). La zone par thread du
# programme pour le kit : 1 Ko (anv en demande plus que Mesa GL)
for f in glkit.c glkit_x86_64.S glkit_x86_64.ld finish.py host/glkit_host.c host/glkit_lazy_x86_64.S; do
	n=$(basename "$f" | sed 's/glkit/vkkit/')
	sed -e 's/glkit/vkkit/g' -e 's/GLKIT/VKKIT/g' -e 's/GlKit/VkKit/g' "$G/$f" > "$K/src/$n"
done
sed -i "s/^#define VKKIT_TLS_AREA 256\b.*/#define VKKIT_TLS_AREA $TLSA		\/* (anv : plus que GL) *\//" "$K/src/vkkit_host.c"
grep -q "VKKIT_TLS_AREA $TLSA" "$K/src/vkkit_host.c" || { echo "build-vkkit-pc.sh : VKKIT_TLS_AREA pas trouve" >&2; exit 1; }

# ce que le kit exporte : l'interface d'un pilote de Vulkan (vk_icd*), le reste par vk_icdGetInstanceProcAddr
EXPORTS="vk_icdGetInstanceProcAddr vk_icdNegotiateLoaderICDInterfaceVersion vk_icdGetPhysicalDeviceProcAddr"
{ echo "{ global:"; for e in $EXPORTS; do echo "$e;"; done; echo "kit_table;"; echo "local: *; };"; } > "$K/vkkit.ver"
U="-u kit_table"
for e in $EXPORTS; do U="$U -u $e"; done
CF="-fPIC -fvisibility=hidden -O2 -ffreestanding -fno-builtin -fno-tree-loop-distribute-patterns -fno-stack-protector"

link () {	# $1 : taille, $2 : alignement de la zone par thread du kit
	${TC}gcc $CF -DKIT_NO_GLAPI -DVKKIT_TLS_SIZE=$1 -DVKKIT_TLS_ALIGN=$2 -c "$K/src/vkkit.c" -o "$K/vkkit.o"
	${TC}gcc -c "$K/src/vkkit_x86_64.S" -o "$K/vkkit_s.o"
	${TC}ld -shared --gc-sections -z now --hash-style=sysv -z max-page-size=0x1000 -z noexecstack \
		-T "$K/src/vkkit_x86_64.ld" --version-script="$K/vkkit.ver" $U -o "$K/vkkit.elf" "$K/vkkit_s.o" "$K/vkkit.o" \
		$OBJS --whole-archive $WHOLE --no-whole-archive \
		--start-group $LIBS --end-group 2> "$K/link.log" \
		|| { grep -v warning "$K/link.log" | head -20; exit 1; }
	${TC}strip --strip-all -o "$K/vkkit.strip" "$K/vkkit.elf"
	python3 "$K/src/finish.py" "$K/vkkit.strip" "$K/vkkit.so" "$K/gen"
}
link "$TLSA" 64			# (premier passage : les besoins par thread ne sont connus qu'apres)
read TS TA < "$K/gen/tls.txt"
[ "$TS" -le "$TLSA" ] || { echo "build-vkkit-pc.sh : $DRV veut $TS octets par thread, libvkkit.a en a $TLSA (VKKIT_TLS)" >&2; exit 1; }
link "$TS" "$TA"
cp "$K/vkkit.so" "$PM/sdroot/lib/vkkit.so"

# libvkkit.a : relais, chargeur du kit, imports, chargeur Vulkan minimal
HF="-specs=$POSIX/lib/posix.specs -O2 -g0 -fno-builtin"
${TC}gcc $HF -c "$K/src/vkkit_host.c" -o "$K/vkkit_host.o"
${TC}gcc $HF -w -c "$K/gen/vkkit_imports.c" -o "$K/vkkit_imports.o"
${TC}gcc -c "$K/src/vkkit_lazy_x86_64.S" -o "$K/vkkit_lazy.o"
${TC}gcc -c "$K/gen/vkkit_stubs.S" -o "$K/vkkit_stubs.o"
${TC}gcc $HF -I"$SRC/include" -c "$M/vkkit/vkkit_loader.c" -o "$K/vkkit_loader.o"
# les trampolines de toutes les fonctions de vulkan_core.h (programmes qui appellent Vulkan en direct : Blender),
# une section par fonction : --gc-sections ne garde que celles qu'un programme appelle
python3 -I "$M/vkkit/gen_protos.py" "$SRC/include/vulkan/vulkan_core.h" "$K/gen/vkkit_protos.c"
${TC}gcc $HF -ffunction-sections -fdata-sections -I"$SRC/include" -c "$K/gen/vkkit_protos.c" -o "$K/vkkit_protos.o"
rm -f "$PM/lib/libvkkit.a"
${TC}ar rcsD "$PM/lib/libvkkit.a" "$K/vkkit_stubs.o" "$K/vkkit_lazy.o" "$K/vkkit_host.o" "$K/vkkit_imports.o" \
	"$K/vkkit_loader.o" "$K/vkkit_protos.o"
# les en-tetes pour les programmes (ceux de Mesa, la meme version que le pilote)
cp "$SRC"/include/vulkan/*.h "$PM/include/vulkan/"
cp -r "$SRC/include/vk_video" "$PM/include/" 2>/dev/null || true
cp "$M/vkkit/include/vulkan/vulkan_korin.h" "$PM/include/vulkan/"
ls -l "$PM/sdroot/lib/vkkit.so" "$PM/lib/libvkkit.a"

# vktest lie au kit (et a UIKit : vktest --win)
${TC}gcc -specs=$POSIX/lib/posix.specs -O2 -DVKTEST_KIT -I"$PM/include" -I"$PCG/include/kits" \
	-I"$ARMPI_ROOT/src/kits/uikit/include" -c "$M/anv/vktest.c" -o "$K/vktest.o"
${TC}g++ -specs=$POSIX/lib/posix.specs -Wl,--gc-sections "$K/vktest.o" "$PM/lib/libvkkit.a" "$PCG/lib/libuikit.a" \
	-o "$K/vktestk"
${TC}strip -o "$PM/sdroot/bin/vktest" "$K/vktestk"
ls -l "$PM/sdroot/bin/vktest"
