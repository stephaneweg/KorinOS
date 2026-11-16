#!/bin/sh
# bench_pi.sh -- les emulateurs en asm contre leur port C++ d'Onyx, dans Korin sous QEMU raspi4b (Git Bash, racine
# du depot ; docs/etude/PC-PILOTES.md « Emulateurs d'Onyx, Pi et PC ») : pour chaque ROM d'essai, l'app asm
# (apps/<emu>.app) puis le C++ (apps/<emu>_c.app, de build/emu/sdroot : wsl sh tools/emu/build.sh) font
# « --thumb » : 900 images sans fenetre (EmuKit), la derniere reduite. Le temps de chacun (bash « time ») et
# la comparaison des deux vignettes vont dans build/emubench/bench.txt.
#   sh tools/emu/bench_pi.sh [EMU...]        (nesemu gbemu gbaemu snesemu par defaut)
# ROM : tools/emu/romgen.py (libres, a nous). Carte : build/card_emubench, tiree de build/sdroot, kits de
# build/kits/aarch64. QEMU : emplacement RUN_PI_SLOT (3 par defaut : telnet 2328, QMP 4470, VNC 70), verrou
# build/qemu<N>.lock. QEMU ne dit rien de la vitesse du Pi : c'est une comparaison entre les deux.
set -e
cd "$(dirname "$0")/../.."
N=${RUN_PI_SLOT:-3}
L=build/qemu$N.lock
C=build/card_emubench
O=build/emubench
R=${REPEAT:-2}
[ $# -gt 0 ] || set -- nesemu gbemu gbaemu snesemu
rm -rf $C && mkdir -p $O
cp -a build/sdroot $C
[ ! -d build/kits/aarch64 ] || cp build/kits/aarch64/*.so $C/lib/
: > $C/etc/preload.txt
printf 'password = essai\n' > $C/etc/remote.txt
mkdir -p $C/home/roms
G="python -I tools/emu/romgen.py"
$G nes-demo $C/home/roms/demo.nes
$G nes-stress 4 1 $C/home/roms/mmc3.nes
$G gb-stress 1 $C/home/roms/dmg.gb mbc1
$G gb-stress 2 $C/home/roms/cgb.gbc cgb mbc5
$G gba-stress 1 $C/home/roms/stress.gba sram
$G gba-stress 2 $C/home/roms/stress2.gba flash
$G snes-stress 1 $C/home/roms/lorom.sfc
$G snes-stress 2 $C/home/roms/hirom.sfc hirom
S=$C/home/bench.sh
{
	echo '#!/bin/bash'
	echo 'cd HOME:/roms'
	for e; do
		mkdir -p $C/apps/${e}_c.app
		cp build/emu/sdroot/apps/$e.app/main $C/apps/${e}_c.app/main
		cp src/apps/$e/app.txt $C/apps/${e}_c.app/
		case $e in nesemu) roms="demo.nes mmc3.nes" ;; gbemu) roms="dmg.gb cgb.gbc" ;;
		gbaemu) roms="stress.gba stress2.gba" ;; snesemu) roms="lorom.sfc hirom.sfc" ;; esac
		for r in $roms; do
			for i in $(seq $R); do
				echo "echo \"== $e $r asm\"; time /apps/$e.app/main --thumb HOME:/a.raw HOME:/roms/$r"
				echo "echo \"== $e $r c\"; time /apps/${e}_c.app/main --thumb HOME:/c.raw HOME:/roms/$r"
			done
			echo "if diff HOME:/a.raw HOME:/c.raw > /dev/null; then echo \"== $e $r : vignettes identiques\"; else echo \"== $e $r : vignettes DIFFERENTES\"; fi"
		done
	done
	echo 'echo FINI'
} > $S
cat > $O/drive.txt <<EOF
wait 45
run python -I tools/armpi_remote.py telnet 127.0.0.1:$((2325 + N)) "@5400 bash HOME:/bench.sh > HOME:/bench.txt 2>&1; cat HOME:/bench.txt"
EOF
python -I tools/mkdisk.py build/sd_emubench.img --fat32 $C --exfat build/exfat_root >/dev/null
until mkdir $L 2>/dev/null; do sleep 3; done
ARMPI_REMOTE_PW=essai python -I tools/qemu_drive.py $O/drive.txt --sd build/sd_emubench.img --fwd $((2325 + N)):23 \
	--port $((4467 + N)) --vnc $((67 + N)) --uart $O/uart.log > $O/bench.txt 2>&1 || true
rmdir $L
grep -E '^==|^real|FINI' $O/bench.txt || cat $O/bench.txt | tail -20
