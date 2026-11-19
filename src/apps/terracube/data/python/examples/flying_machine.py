"""Une machine qui avance seule : deux observateurs, un piston, un piston collant et quatre blocs de colle.

    python3 /apps/terracube.app/python/examples/flying_machine.py

Elle est construite en l'air devant le joueur (la colle collerait au sol), vers l'est, et part quand un bloc est posé
devant son observateur de derrière. Elle s'arrête au premier obstacle (un bloc qu'elle ne peut pas pousser).

Le cycle : l'observateur de derrière (B) voit un changement -> le piston pousse l'avant ; l'observateur de devant (F),
déplacé, envoie une impulsion -> le piston collant sort, puis rentre et tire l'arrière ; B, déplacé, recommence.
"""
import os, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from terracube import TerraCube, Block, Facing

tc = TerraCube()
px, py, pz = tc.player.tile()
x, y, z = px + 3, py + 4, pz                # en l'air, à l'est
tc.fill(x - 2, y - 1, z, x + 60, y + 2, z, Block.AIR)
tc.player.set_rotation(90)
blocks = [
    (x - 1, y + 1, Block.GLUE_BLOCK, 0), (x, y + 1, Block.GLUE_BLOCK, 0),
    (x, y, Block.PISTON, Facing.EAST),                                     # l'arrière : pousse l'avant
    (x + 1, y, Block.GLUE_BLOCK, 0), (x + 2, y, Block.GLUE_BLOCK, 0),
    (x + 1, y + 1, Block.STICKY_PISTON, Facing.WEST),                      # l'avant : tire l'arrière
    (x + 2, y + 1, Block.OBSERVER, Facing.EAST),                           # (sa sortie : le piston collant)
    (x - 1, y, Block.OBSERVER, Facing.WEST),                               # (sa sortie : le piston)
]
for bx, by, b, st in blocks:
    tc.set_block(bx, by, z, b, st)
tc.flush()
time.sleep(0.5)
tc.set_block(x - 2, y, z, Block.DIRT)       # le départ : un bloc devant l'observateur de derrière
tc.set_block(x - 2, y, z, Block.AIR)
tc.post("La machine part vers l'est")
for i in range(6):
    time.sleep(1)
    row = tc.get_blocks(x - 2, y, z, x + 40, y, z)
    print("observateur de derrière en x +", row.index(Block.OBSERVER) - 1 if Block.OBSERVER in row else "?")
