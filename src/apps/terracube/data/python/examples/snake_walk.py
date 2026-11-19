"""Le chemin suit le joueur : un bloc de pierre taillée sous ses pieds à chaque pas ; Ctrl+C pour finir.

    python3 /apps/terracube.app/python/examples/snake_walk.py
"""
import os, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from terracube import TerraCube, Block

tc = TerraCube()
last = None
try:
    while True:
        x, y, z = tc.player.tile()
        if (x, y, z) != last:
            if tc.get_block(x, y - 1, z) not in (Block.AIR, Block.WATER):
                tc.set_block(x, y - 1, z, Block.STONE_BRICK)
            last = (x, y, z)
        time.sleep(0.05)
except KeyboardInterrupt:
    pass
