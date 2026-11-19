"""Une maison devant le joueur : murs de planches, fenêtres, toit de briques, une lampe.

    python3 /apps/terracube.app/python/examples/house.py
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from terracube import TerraCube, Block

tc = TerraCube()
px, py, pz = tc.player.tile()
x0, z0 = px + 3, pz - 3          # (devant, à droite)
W, D, H = 7, 7, 4
y0 = tc.get_height(x0 + W // 2, z0 + D // 2) + 1

tc.fill(x0, y0 - 1, z0, x0 + W - 1, y0 - 1, z0 + D - 1, Block.COBBLESTONE)        # le sol
tc.fill(x0, y0, z0, x0 + W - 1, y0 + H - 1, z0 + D - 1, Block.PLANKS)             # les murs...
tc.fill(x0 + 1, y0, z0 + 1, x0 + W - 2, y0 + H - 1, z0 + D - 2, Block.AIR)        # ... creux
for x in (x0, x0 + W - 1):
    for z in (z0, z0 + D - 1):
        tc.fill(x, y0, z, x, y0 + H - 1, z, Block.LOG)                             # les coins
tc.fill(x0 + 3, y0, z0, x0 + 3, y0 + 1, z0, Block.AIR)                            # la porte
tc.set_block(x0, y0 + 2, z0 + 3, Block.GLASS)                                      # les fenêtres
tc.set_block(x0 + W - 1, y0 + 2, z0 + 3, Block.GLASS)
tc.set_block(x0 + 3, y0 + 2, z0 + D - 1, Block.GLASS)
for i in range(W // 2 + 1):                                                        # le toit
    tc.fill(x0 - 1 + i, y0 + H + i, z0 - 1, x0 + W - i, y0 + H + i, z0 + D, Block.BRICK)
tc.set_block(x0 + 3, y0 + H - 1, z0 + 3, Block.LAMP)
tc.flush()
tc.post("Une maison en %d, %d, %d" % (x0, y0, z0))
