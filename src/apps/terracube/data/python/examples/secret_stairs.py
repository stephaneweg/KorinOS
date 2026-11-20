"""Un escalier caché dans le sol : un levier, et quatre dalles de sable s'ouvrent l'une après l'autre (0,1 s d'écart, de
la marche du haut à celle du bas) sur un escalier qui descend vers une salle ; le levier rebaissé, elles se referment.

    python3 /apps/terracube.app/python/examples/secret_stairs.py

Construit devant le joueur (dans la direction où il regarde), puis essayé : tc.test() bascule le levier, fait tourner
la simulation et rend ce qui a changé, pas par pas.

Le montage (repère : x à droite, z devant, y0 le niveau du sol, celui des dalles) :
  * marche k (1 à 4) : un bloc de pierre en (0..1, y0 - k, z k), de l'air au-dessus ; la dalle de sable en (0..1, y0, k) ;
  * de chaque côté, un piston collant (x -2 vers l'est, x 3 vers l'ouest) : sorti, il tient la dalle sur l'escalier
    (fermé) ; rentré, il la tire sous la rampe (ouvert) ;
  * sous chaque piston, un bloc de pierre ; derrière ce bloc, un répéteur de délai k qui l'alimente fort : la cascade ;
  * les répéteurs prennent le courant d'un fil (un bus de chaque côté, x -4 et x 5), relié par une ligne en z -3 ;
  * la commande : un bloc au sol (0, y0, -2) portant le levier ; contre lui, une torche de cuivre (l'inverseur), enfermée
    sous un bloc d'or, qui alimente la ligne sous elle. Levier baissé : torche allumée, pistons sortis, fermé ; levier
    levé : torche éteinte, pistons rentrés, ouvert ;
  * tout le circuit est en y0 - 1, enterré ; des rampes de pierre taillée en y0 + 1 cachent pistons et têtes.
"""
import os, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
from terracube import TerraCube, Block, Facing

N = 4
tc = TerraCube()
px, py, pz = tc.player.tile()
yaw = tc.player.rotation() % 360
# le regard ramené à un axe : f (devant), r (à droite)
F = {0: (0, -1), 1: (1, 0), 2: (0, 1), 3: (-1, 0)}[int((yaw + 45) // 90) % 4]
R = (-F[1], F[0])
X0, Y0, Z0 = px + 6 * F[0], py - 1, pz + 6 * F[1]   # (le levier à trois pas devant le joueur)


def at(x, y, z):
    """Du repère du montage au monde."""
    return X0 + R[0] * x + F[0] * z, Y0 + y, Z0 + R[1] * x + F[1] * z


def facing(dx, dz):
    """Une direction du repère (dx à droite, dz devant) en direction du monde."""
    wx, wz = R[0] * dx + F[0] * dz, R[1] * dx + F[1] * dz
    return {(1, 0): Facing.EAST, (-1, 0): Facing.WEST, (0, 1): Facing.SOUTH, (0, -1): Facing.NORTH}[(wx, wz)]


def put(x, y, z, block, state=None):
    tc.set_block(*at(x, y, z), block, state)


def box(x1, y1, z1, x2, y2, z2, block):
    a, b = at(x1, y1, z1), at(x2, y2, z2)
    tc.fill(a[0], a[1], a[2], b[0], b[1], b[2], block)


EAST, WEST = facing(1, 0), facing(-1, 0)
# le terrain : plein jusqu'au sol, de l'air au-dessus
box(-6, -N - 2, -5, 7, 0, N + 9, Block.SANDSTONE)
box(-6, 1, -5, 7, 4, N + 9, Block.AIR)
# l'escalier, ses dalles, la salle
for k in range(1, N + 1):
    for x in (0, 1):
        if k > 1:
            box(x, -k + 1, k, x, -1, k, Block.AIR)
        put(x, -k, k, Block.STONE_BRICK)
        put(x, 0, k, Block.SAND)
box(-2, -N, N + 1, 3, -1, N + 8, Block.AIR)
for x, z in ((-2, N + 1), (3, N + 1), (-2, N + 8), (3, N + 8)):
    put(x, -2, z, Block.LAMP)
put(0, -N, N + 7, Block.GOLD_BLOCK), put(1, -N, N + 7, Block.CRYSTAL_ORE)
tc.flush()
# les pistons (rentrés : on les pose au repos, la torche les fera sortir), leurs blocs, les répéteurs, les fils
for k in range(1, N + 1):
    rep = lambda d: d | ((k - 1) << 3)   # (le délai k)
    put(-2, 0, k, Block.STICKY_PISTON, EAST)
    put(-2, -1, k, Block.STONE)
    put(-3, -1, k, Block.REPEATER, rep(EAST))
    put(-1, 0, k, Block.AIR)
    put(3, 0, k, Block.STICKY_PISTON, WEST)
    put(3, -1, k, Block.STONE)
    put(4, -1, k, Block.REPEATER, rep(WEST))
    put(2, 0, k, Block.AIR)
    for x in (0, 1):     # (les dalles, ouvertes : dans leurs cavités, sous les rampes)
        put(x, 0, k, Block.AIR)
    put(-1, 0, k, Block.SAND), put(2, 0, k, Block.SAND)
for z in range(-3, N + 1):
    put(-4, -1, z, Block.WIRE), put(5, -1, z, Block.WIRE)
for x in range(-4, 6):
    if x != 0:
        put(x, -1, -3, Block.WIRE)
put(0, -1, -3, Block.WIRE)
put(0, 0, -2, Block.STONE_BRICK)
put(0, 1, -3, Block.GOLD_BLOCK)
put(0, 0, -3, Block.COPPER_TORCH, facing(0, 1))      # (contre le bloc du levier, derrière lui)
for z in range(0, N + 2):
    for x in (-4, -3, -2, -1, 2, 3, 4, 5):
        put(x, 1, z, Block.STONE_BRICK)
put(0, 1, -2, Block.LEVER, Facing.DOWN)
tc.flush()
time.sleep(1.5)          # (la torche s'allume, les pistons sortent : le sol se ferme)

lever = at(0, 1, -2)
a, b = at(-5, -2, -4), at(6, 1, N + 1)
watch = (min(a[0], b[0]), a[1], min(a[2], b[2]), max(a[0], b[0]), b[1], max(a[2], b[2]))
slabs = [at(0, 0, k) for k in range(1, N + 1)]


def state():
    return "".join("#" if tc.get_block(*s) == Block.SAND else "." for s in slabs)


print("fermé au départ :", state())
for what in ("ouvrir", "fermer"):
    ch = tc.test(watch, 60, use=lever)
    times = sorted({t for t, p, blk, st, pw in ch if p in slabs})
    print("%s : dalles %s, les marches bougent aux pas %s (x 0,05 s)" % (what, state(), times))
tc.post("L'escalier caché est prêt : le levier devant toi l'ouvre")
print("le levier :", lever)
