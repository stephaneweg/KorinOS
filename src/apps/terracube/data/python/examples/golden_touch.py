"""Les évènements : tout bloc que vous touchez (touche E) ou posez devient de l'or ; Ctrl+C pour finir.

    python3 /apps/terracube.app/python/examples/golden_touch.py
"""
import os, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from terracube import TerraCube, Block, BlockEvent

tc = TerraCube()
tc.post("Golden touch : touchez un bloc (E) ou posez-en un")
tc.events()   # (oublier ceux d'avant)
try:
    while True:
        for e in tc.events():
            if e.kind in (BlockEvent.TOUCHED, BlockEvent.PLACED):
                tc.set_block(e.pos.x, e.pos.y, e.pos.z, Block.GOLD_BLOCK)
        time.sleep(0.1)
except KeyboardInterrupt:
    tc.post("Golden touch : fini")
