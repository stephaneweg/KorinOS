"""Le mode survie par un script : passer en survie, recevoir des troncs, poser un établi, fabriquer des outils.

    python3 /apps/terracube.app/python/examples/survival_kit.py
"""
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from terracube import TerraCube, Block

tc = TerraCube()
tc.set_mode("survival")
tc.give("log", 6)
print("planches :", tc.craft("planks", 6))          # 6 troncs -> 24 planches
print("bâtons :", tc.craft("stick", 2))             # 4 planches -> 8 bâtons
print("établi :", tc.craft("workbench"))            # 4 planches
x, y, z = tc.player.tile()
tc.set_block(x + 2, y, z, Block.WORKBENCH)          # (un établi posé à côté : les recettes de l'établi)
print("pioche :", tc.craft("wood_pickaxe"))
print("hache :", tc.craft("wood_axe"))
for slot, (item, n) in sorted(tc.inventory().items()):
    print(slot, item, n)
tc.post("Kit de survie prêt !")
