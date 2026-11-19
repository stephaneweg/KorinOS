"""Une pyramide de grès, pierre par pierre, à côté du joueur.

    python3 /apps/terracube.app/python/examples/pyramid.py [taille]
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from terracube import TerraCube, Block

size = int(sys.argv[1]) if len(sys.argv) > 1 else 21
tc = TerraCube()
x, y, z = tc.player.tile()
cx, cz = x + size // 2 + 3, z
base = tc.get_height(cx, cz) + 1
for level in range(size // 2 + 1):
    r = size // 2 - level
    tc.fill(cx - r, base + level, cz - r, cx + r, base + level, cz + r, Block.SANDSTONE)
tc.set_block(cx, base + size // 2 + 1, cz, Block.GOLD_BLOCK)
tc.flush()
tc.post("Pyramide de %d blocs de côté" % size)
