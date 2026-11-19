"""Les circuits de cuivre : construit trois montages à l'est du joueur, puis les fait marcher.

    python3 /apps/terracube.app/python/examples/circuits.py

  1. un levier, du fil, un piston collant qui pousse un bloc d'or (le script bascule le levier deux fois) ;
  2. un bouton, une chaîne de répéteurs et de blocs musicaux : un arpège (le script appuie sur le bouton) ;
  3. une horloge : une torche contre un bloc, du fil, un répéteur qui revient dans le bloc ; une lampe clignote.
"""
import os, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from terracube import TerraCube, Block, Facing

tc = TerraCube()
x0, y0, z0 = tc.player.tile()
x0 += 3
y = y0 - 1                                  # le sol des montages
tc.fill(x0, y, z0 - 2, x0 + 14, y, z0 + 8, Block.STONE)
tc.fill(x0, y + 1, z0 - 2, x0 + 14, y + 6, z0 + 8, Block.AIR)
tc.player.set_rotation(90)                  # regarder l'est

# 1. levier -> fil -> piston collant -> bloc d'or
z = z0
tc.set_block(x0 + 1, y + 1, z, Block.LEVER, Facing.DOWN)
for x in range(x0 + 2, x0 + 5):
    tc.set_block(x, y + 1, z, Block.WIRE)
tc.set_block(x0 + 5, y + 1, z, Block.STICKY_PISTON, Facing.EAST)
tc.set_block(x0 + 6, y + 1, z, Block.GOLD_BLOCK)

# 2. bouton -> répéteur -> bloc musical -> répéteur -> ... (le bloc plein passe le signal au répéteur suivant)
z = z0 + 3
tc.set_block(x0 + 1, y + 1, z, Block.STONE_BUTTON, Facing.DOWN)
notes = [0, 4, 7, 12, 16, 19]               # un arpège majeur (0 : fa dièse ; + 3 : la)
x = x0 + 2
for n in notes:
    tc.set_block(x, y + 1, z, Block.REPEATER, Facing.EAST | (1 << 3))      # délai 2
    tc.set_block(x + 1, y, z, Block.DIRT)                                  # (sur de la terre : une harpe)
    tc.set_block(x + 1, y + 1, z, Block.NOTE_BLOCK, n + 3)
    x += 2

# 3. l'horloge : la torche (l'inverseur) s'éteint quand le répéteur alimente son bloc, ce qui coupe le répéteur...
z = z0 + 6
b = (x0 + 3, y + 1, z)                                                     # le bloc qui porte la torche
tc.set_block(*b, Block.STONE)
tc.set_block(b[0] + 1, b[1], z, Block.COPPER_TORCH, Facing.WEST)            # contre le bloc, à l'est
tc.set_block(b[0] + 1, b[1], z + 1, Block.WIRE)
tc.set_block(b[0] + 1, b[1], z + 2, Block.WIRE)
tc.set_block(b[0], b[1], z + 2, Block.WIRE)
tc.set_block(b[0], b[1], z + 1, Block.REPEATER, Facing.NORTH | (3 << 3))   # vers le bloc, délai 4
tc.set_block(b[0] + 1, b[1], z - 1, Block.SIGNAL_LAMP)                      # à côté de la torche
tc.flush()
tc.post("Circuits : trois montages à l'est")

time.sleep(1)
tc.use(x0 + 1, y + 1, z0)                   # le levier : le piston sort
time.sleep(1.5)
print("piston :", Block.name_of(tc.get_block(x0 + 6, y + 1, z0)), "| or poussé en", x0 + 7,
      Block.name_of(tc.get_block(x0 + 7, y + 1, z0)), "| fil :", tc.get_power(x0 + 2, y + 1, z0))
tc.use(x0 + 1, y + 1, z0)                   # le levier : il rentre et ramène l'or
time.sleep(1.5)
print("ramené :", Block.name_of(tc.get_block(x0 + 6, y + 1, z0)))
tc.use(x0 + 1, y + 1, z0 + 3)               # le bouton : l'arpège
time.sleep(3)
states = []
for i in range(8):
    states.append(tc.get_block_state(x0 + 4, y + 1, z0 + 5)[1] & Facing.ON)
    time.sleep(0.25)
print("lampe de l'horloge (allumée / éteinte, 4 fois par seconde) :", states)
