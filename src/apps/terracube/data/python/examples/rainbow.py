"""Un arc-en-ciel de laine au-dessus du joueur.

    python3 /apps/terracube.app/python/examples/rainbow.py
"""
import math, os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from terracube import TerraCube, Block

COLORS = [Block.WOOL_RED, Block.WOOL_ORANGE, Block.WOOL_YELLOW, Block.WOOL_GREEN, Block.WOOL_BLUE, Block.WOOL_PURPLE]
tc = TerraCube()
x, y, z = tc.player.tile()
R = 24
for i, color in enumerate(COLORS):
    r = R - i
    for a in range(0, 181):
        t = math.radians(a)
        bx, by = x + round(r * math.cos(t)), y + round(r * math.sin(t))
        if by < 127:
            for dz in range(2):
                tc.set_block(bx, by, z - 10 + dz, color)
tc.flush()
tc.post("Un arc-en-ciel !")
