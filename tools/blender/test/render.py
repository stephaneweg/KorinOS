# render.py : rendu Cycles sur le processeur, sans écran (jalon 2, docs/etude/BLENDER.md) :
#   /apps/blender/blender -b --factory-startup --python /home/blender/render.py -- /tmp/cube.png [échantillons]
# La scène de départ (cube, lumière, caméra) en 320 × 240 ; écrit le PNG, sa taille et « RENDU OK ».
import os
import sys
import time
import bpy

args = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
out = args[0] if args else "/tmp/korin-cube.png"
spp = int(args[1]) if len(args) > 1 else 16

sc = bpy.context.scene
sc.render.engine = "CYCLES"
sc.cycles.device = "CPU"
sc.cycles.samples = spp
sc.cycles.use_denoising = False
sc.render.resolution_x, sc.render.resolution_y = 320, 240
sc.render.resolution_percentage = 100
sc.render.image_settings.file_format = "PNG"
sc.render.filepath = out
print("fils de rendu :", sc.render.threads, "échantillons :", spp)
t0 = time.time()
bpy.ops.render.render(write_still=True)
print("rendu en %.1f s" % (time.time() - t0))
size = os.path.getsize(out)
print("image :", out, size, "octets")
assert size > 1000, size

# l'image relue : le cube gris au centre ne doit pas être noir
img = bpy.data.images.load(out)
w, h = img.size
px = img.pixels[:]
c = ((h // 2) * w + w // 2) * 4
print("pixel central :", [round(v, 3) for v in px[c:c + 4]])
assert sum(px[c:c + 3]) > 0.05, "pixel central noir"
print("RENDU OK")
