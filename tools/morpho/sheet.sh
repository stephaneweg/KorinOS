#!/bin/sh
# sheet.sh -- une planche d'images de Morpho sur l'hôte (tools/morpho/run.sh shot), une par scénario :
#   sh tools/morpho/sheet.sh out.png "titre|VAR=... VAR=..." ...
set -eu
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
out=$1; shift
B=$ROOT/build/morpho-host/sheet
rm -rf "$B"; mkdir -p "$B"
i=0
for sc; do
	t=${sc%%|*}; v=${sc#*|}
	# shellcheck disable=SC2086
	sh "$ROOT/tools/morpho/run.sh" shot "$B/$i.png" $v 2>&1 | grep -E "images|error" | sed "s/^/$t : /" >&2
	echo "$t" > "$B/$i.txt"
	i=$((i + 1))
done
python3 - "$B" "$out" "$i" <<'PY'
import sys
from PIL import Image, ImageDraw
d, out, n = sys.argv[1], sys.argv[2], int(sys.argv[3])
ims = [Image.open(f"{d}/{k}.png") for k in range(n)]
w, h = ims[0].size
cols = 2 if n > 1 else 1
rows = (n + cols - 1) // cols
s = 0.5
W, H = int(w * s), int(h * s)
sheet = Image.new("RGB", (W * cols, (H + 18) * rows), "white")
dr = ImageDraw.Draw(sheet)
for k, im in enumerate(ims):
    x, y = (k % cols) * W, (k // cols) * (H + 18)
    sheet.paste(im.resize((W, H), Image.LANCZOS), (x, y + 18))
    dr.text((x + 4, y + 3), open(f"{d}/{k}.txt").read().strip(), fill="black")
sheet.save(out)
PY
