"""Une ferme : un enclos de bois, quatre poules, deux cochons, un loup apprivoisé qui garde l'entrée.

    python3 /apps/terracube.app/python/examples/farm.py

En mode survie, des graines (dans les hautes herbes) font se reproduire les poules, des pommes (dans les feuilles) les
cochons ; le poulet et le porc cuisent au four. Le loup assis attaque les créatures de la nuit qui approchent.
"""
import os, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from terracube import TerraCube, Block

tc = TerraCube()
x0, y0, z0 = tc.player.tile()
x0 += 3
y = tc.get_height(x0 + 4, z0 + 4) + 1
tc.fill(x0, y, z0, x0 + 9, y + 2, z0 + 9, Block.AIR)
tc.fill(x0, y - 1, z0, x0 + 9, y - 1, z0 + 9, Block.GRASS)
# la clôture : des planches tout autour, une entrée au milieu d'un côté
for i in range(10):
    for (x, z) in ((x0 + i, z0), (x0 + i, z0 + 9), (x0, z0 + i), (x0 + 9, z0 + i)):
        tc.fill(x, y, z, x, y + 1, z, Block.PLANKS)
tc.fill(x0, y, z0 + 4, x0, y + 1, z0 + 5, Block.AIR)
tc.flush()
for i in range(4):
    tc.spawn("chicken", x0 + 3 + i, y, z0 + 3)
for i in range(2):
    tc.spawn("pig", x0 + 4 + i * 2, y, z0 + 6)
wolf = tc.spawn("wolf", x0 - 1.5, y, z0 + 5, sitting=True)      # apprivoisé, assis : il garde l'entrée
tc.command("say La ferme est prête ; le loup garde l'entrée")
time.sleep(1)
for c in tc.creatures(16):
    print(c)
print("(clic droit sur le loup : il te suit ; encore : il garde là où il est)")
