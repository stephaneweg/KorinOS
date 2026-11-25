#!/bin/sh
# stage.sh : l'arbre installé de MuseScore (build.sh) rangé en app de Korin, pour le paquet `musescore` :
#   build/pc/musescore-pkg/apps/musescore.app/main      mscore, dépouillé (strip)
#   build/pc/musescore-pkg/apps/musescore.app/share/    les données (share/mscore-4.7 : instruments, modèles,
#                                                       polices, SoundFont… ; resolveAppDataPath de Korin)
#   build/pc/musescore-pkg/apps/musescore.app/app.txt, icon.rgba, LICENSE.txt
set -e
. "$(dirname "$0")/env.sh"
[ -x "$MINST/bin/mscore" ] || { echo "stage.sh : pas de $MINST/bin/mscore (sh tools/musescore/build.sh)" >&2; exit 1; }
T=$ROOT/build/pc/musescore-pkg
A=$T/apps/musescore.app
rm -rf "$T"
mkdir -p "$A"
${TC}strip --strip-all -o "$A/main" "$MINST/bin/mscore"
chmod +x "$A/main"
cp -r "$MINST"/share/mscore-*/ "$A/share"
# (pas la SoundFont MS Basic, 51 Mo : MuseScore joue par le synthé d'AudioKit et la SoundFont du système,
# /res/soundfonts ; korin/korinfluidsynth.inc)
rm -f "$A"/share/sound/*.sf2 "$A"/share/sound/*.sf3
rmdir "$A/share/sound" 2>/dev/null || true
# les traductions : les .ts de la source compilés par le lrelease de l'hôte (qttools, sans LinguistTools pour
# Korin), et ceux de Qt (qttranslations : qtbase_xx) ; MuseScore charge locale/*_<langue>.qm
LREL=$K/host-obj/qttools/bin/lrelease
if [ ! -x "$LREL" ]; then
	sh "$QTT/fetch.sh" >/dev/null
	cmake -S "$SRC/qttools-everywhere-src-$QT_VER" -B "$K/host-obj/qttools" -G Ninja -DCMAKE_BUILD_TYPE=Release \
		-DCMAKE_PREFIX_PATH="$HOSTQT" -DQT_BUILD_EXAMPLES=OFF -DQT_BUILD_TESTS=OFF -DFEATURE_assistant=OFF \
		-DFEATURE_designer=OFF -DFEATURE_pixeltool=OFF -DFEATURE_qtdiag=OFF -DFEATURE_qtplugininfo=OFF \
		-DFEATURE_qdbus=OFF -DFEATURE_distancefieldgenerator=OFF -DFEATURE_clang=OFF -DFEATURE_linguist=ON >/dev/null
	ninja -C "$K/host-obj/qttools" lrelease >/dev/null
fi
for ts in "$MSRC"/share/locale/*.ts "$SRC/qttranslations-everywhere-src-$QT_VER"/translations/qtbase_*.ts; do
	n=${ts##*/}
	LC_ALL=C.UTF-8 "$LREL" -silent "$ts" -qm "$A/share/locale/${n%.ts}.qm"
done
cp "$MST/app.txt" "$A/app.txt"
cp "$MSRC/LICENSE.txt" "$A/LICENSE.txt"
# l'icône : celle de MuseScore (share/icons/AppIcon, 512 x 512), au format de Korin (tools/craft/icon_rgba.py)
python3 -E -P "$ROOT/tools/craft/icon_rgba.py" "$MSRC/share/icons/AppIcon/MS4_AppIcon_512x512.png" "$A/icon.rgba" 2>/dev/null
echo "stage.sh : $(find "$A" -type f | wc -l) fichiers, $(du -sh "$A" | cut -f1) dans $A"
