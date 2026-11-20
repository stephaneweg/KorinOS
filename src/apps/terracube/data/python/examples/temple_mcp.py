"""Ce que ferait un agent branché sur le MCP de TerraCube à qui l'on demande :
« Réalise-moi un temple aztèque grandeur nature que je peux explorer ».

Il n'utilise que les outils MCP (get_player, get_heightmap, fill, set_blocks, use_block, get_block, run_command,
teleport, screenshot), comme Vibe ou Claude le feraient ; chaque appel est noté dans temple_log.txt, les captures
dans temple_NN.png (dans le dossier donné, sinon le dossier courant).

    python3 /apps/terracube.app/python/examples/temple_mcp.py [dossier]

Le jeu doit avoir son pilotage par un agent ouvert (Pilotage > Piloter par un agent IA, ou --mcp) ; TERRACUBE_MCP
(http://127.0.0.1:7312/mcp par défaut) et TERRACUBE_MCP_TOKEN (le jeton, s'il y en a un) le désignent.

Le temple (inspiré du Templo Mayor de Tenochtitlan) : une pyramide de 10 gradins (81 x 81 à la base, 40 de haut), une
frise rouge et noire, un double escalier à l'ouest bordé de rampes et de têtes de serpent, au sommet les deux
sanctuaires jumeaux (Tlaloc, bleu et blanc ; Huitzilopochtli, rouge et noir) et des brasiers de lave ; une place
dallée et ses colonnes ; dedans, par un tunnel au sud, une grande salle à piliers, une porte secrète de jade (un
piston collant, un levier), un passage et un escalier jusqu'à la chambre du trésor.
"""
import base64, json, os, statistics, sys, time, urllib.request

URL = os.environ.get("TERRACUBE_MCP", os.environ.get("TC_MCP", "http://127.0.0.1:7312/mcp"))
TOKEN = os.environ.get("TERRACUBE_MCP_TOKEN", "")
OUT = sys.argv[1] if len(sys.argv) > 1 else "."
LOG = open(os.path.join(OUT, "temple_log.txt"), "w", encoding="utf-8")
T0 = time.time()
_id = [0]


def log(*a):
    line = "[%5.0f s] " % (time.time() - T0) + " ".join(str(x) for x in a)
    print(line, flush=True)
    LOG.write(line + "\n")
    LOG.flush()


def rpc(method, params=None):
    _id[0] += 1
    body = json.dumps({"jsonrpc": "2.0", "id": _id[0], "method": method, "params": params or {}}).encode()
    head = {"Content-Type": "application/json", "Accept": "application/json, text/event-stream"}
    if TOKEN:
        head["Authorization"] = "Bearer " + TOKEN
    req = urllib.request.Request(URL, body, head)
    return json.loads(urllib.request.urlopen(req, timeout=1800).read())


def tool(name, why="", **args):
    shown = {k: (("%d éléments" % len(v)) if isinstance(v, list) and len(v) > 8 else v) for k, v in args.items()}
    r = rpc("tools/call", {"name": name, "arguments": args})["result"]
    text = ""
    for c in r["content"]:
        if c["type"] == "image":
            f = os.path.join(OUT, "temple_%02d.png" % _id[0])
            open(f, "wb").write(base64.b64decode(c["data"]))
            text = "image " + os.path.basename(f)
        elif c["type"] == "text":
            text = c["text"]
    log("%-14s %s%s -> %s" % (name, json.dumps(shown, ensure_ascii=False)[:150], ("  # " + why) if why else "", text[:160].replace("\n", " ")))
    if r.get("isError"):
        raise SystemExit("erreur : " + text)
    try:
        return json.loads(text)
    except ValueError:
        return text


def put_many(blocks, why):
    for i in range(0, len(blocks), 60000):
        tool("set_blocks", why, blocks=blocks[i:i + 60000])


# ---- 1. regarder où l'on est -----------------------------------------------------------------------------------
rpc("initialize", {"protocolVersion": "2025-06-18"})
log("outils :", ", ".join(t["name"] for t in rpc("tools/list")["result"]["tools"]))
tool("get_game_info", "le monde, le mode")
pl = tool("get_player", "où est le joueur")
px, py, pz = pl["tile"]
tool("run_command", "le temps du chantier", command="weather clear")
tool("run_command", command="weather cycle off")
tool("run_command", command="time 10")
tool("run_command", command="time stop")
tool("run_command", "pas de créatures de la nuit pendant le chantier", command="mobs off")

# le site : 75 blocs à l'est du joueur ; le sol (sans les arbres) donne le niveau de la place
cx, cz = px + 75, pz
hm = tool("get_heightmap", "le relief du site (121 x 121)", **{"from": [cx - 60, cz - 60], "to": [cx + 60, cz + 60]})
ground = [h for row_h, row_b in zip(hm["heights"], hm["top_blocks"]) for h, b in zip(row_h, row_b) if b in (1, 2, 3, 8, 9, 15, 35)]
y0 = int(statistics.median(ground)) + 1
if y0 + 50 > 126:
    y0 = 126 - 50
log("site : centre (%d, %d), la place à y = %d (%d colonnes de sol)" % (cx, cz, y0 - 1, len(ground)))

# ---- 2. le terrain : dégager, fonder, daller ----------------------------------------------------------------------
tool("fill", "dégager le site (arbres, collines)", **{"from": [cx - 60, y0, cz - 60], "to": [cx + 60, min(127, y0 + 55), cz + 60]}, block="air")
tool("fill", "les fondations (combler les creux)", **{"from": [cx - 60, y0 - 8, cz - 60], "to": [cx + 60, y0 - 2, cz + 60]}, block="stone")
tool("fill", "la place dallée", **{"from": [cx - 60, y0 - 1, cz - 60], "to": [cx + 60, y0 - 1, cz + 60]}, block="stone_brick")

# ---- 3. la pyramide : 10 gradins, chacun un cœur de grès sous une peau de pierre taillée ------------------------
N, H, STEP = 10, 4, 3
for i in range(N):
    h, ya = 40 - STEP * i, y0 + H * i
    tool("fill", "gradin %d : le cœur" % (i + 1), **{"from": [cx - h, ya, cz - h], "to": [cx + h, ya + H - 1, cz + h]}, block="sandstone")
    tool("fill", "gradin %d : la peau" % (i + 1), **{"from": [cx - h, ya, cz - h], "to": [cx + h, ya + H - 1, cz + h]}, block="stone_brick", hollow=True)
frieze = []
for i in range(N):
    h, y = 40 - STEP * i, y0 + H * i + 2
    ring = [(x, cz - h) for x in range(cx - h, cx + h + 1)] + [(x, cz + h) for x in range(cx - h, cx + h + 1)] + \
           [(cx - h, z) for z in range(cz - h + 1, cz + h)] + [(cx + h, z) for z in range(cz - h + 1, cz + h)]
    for k, (x, z) in enumerate(ring):
        frieze.append([x, y, z, "wool_red" if (x + z) // 2 % 2 == 0 else "wool_black"])
put_many(frieze, "la frise de chaque gradin (rouge et noire)")

# ---- 4. le double escalier (ouest), ses rampes, les têtes de serpent ---------------------------------------------
TOP = y0 + N * H   # le sol du sommet
stairs = []
for k in range(N * H):
    x = cx - 53 + k
    for z in list(range(cz - 11, cz - 2)) + list(range(cz + 3, cz + 12)):
        for y in range(y0, y0 + k + 1):
            stairs.append([x, y, z, "stone_brick" if y == y0 + k else "sandstone"])
    for z in list(range(cz - 2, cz + 3)) + [cz - 12, cz + 12]:     # les rampes : un bloc plus haut
        for y in range(y0, min(y0 + k + 2, TOP)):
            stairs.append([x, y, z, "stone_brick"])
put_many(stairs, "le double escalier : 40 marches, 3 rampes")
head = []
for zc, w in ((cz, 2), (cz - 12, 0), (cz + 12, 0)):     # une tête au pied de chaque rampe
    for z in range(zc - w - 1, zc + w + 2):
        for x in (cx - 55, cx - 54):
            for y in range(y0, y0 + 3):
                head.append([x, y, z, "stone_brick"])
    head += [[cx - 56, y0 + 2, zc - 1, "gold_block"], [cx - 56, y0 + 2, zc + 1, "gold_block"], [cx - 56, y0, zc, "wool_red"],
             [cx - 55, y0 + 3, zc, "wool_green"]]
put_many(head, "les têtes de serpent au pied des rampes")

# ---- 5. le sommet : les sanctuaires jumeaux, les brasiers ----------------------------------------------------------
for name, z1, z2, c1, c2 in (("Huitzilopochtli", cz - 11, cz - 2, "wool_red", "wool_black"), ("Tlaloc", cz + 2, cz + 11, "wool_blue", "wool_white")):
    tool("fill", "le sanctuaire de " + name, **{"from": [cx - 8, TOP, z1], "to": [cx + 9, TOP + 8, z2]}, block="sandstone", hollow=True)
    deco = []
    for y in (TOP + 3, TOP + 6):
        for x in range(cx - 8, cx + 10):
            for z in (z1, z2):
                deco.append([x, y, z, c1 if (x // 2) % 2 else c2])
        for z in range(z1, z2 + 1):
            for x in (cx - 8, cx + 9):
                deco.append([x, y, z, c1 if (z // 2) % 2 else c2])
    for x in range(cx - 8, cx + 10):     # les créneaux du toit
        for z in range(z1, z2 + 1):
            if (x in (cx - 8, cx + 9) or z in (z1, z2)) and (x + z) % 2 == 0:
                deco.append([x, TOP + 9, z, c1])
    zc = (z1 + z2) // 2
    deco += [[cx + 7, TOP + 1, zc, "gold_block"], [cx + 7, TOP + 2, zc, "lamp"], [cx + 7, TOP + 1, zc - 1, c1], [cx + 7, TOP + 1, zc + 1, c1]]
    put_many(deco, "ses bandes de couleur, ses créneaux, son autel")
    tool("fill", "la porte du sanctuaire, face à l'escalier", **{"from": [cx - 8, TOP + 1, zc - 1], "to": [cx - 8, TOP + 4, zc + 1]}, block="air")
tool("set_blocks", "trois brasiers de lave, chacun dans une fosse d'un bloc (la lave ne peut pas couler)",
     blocks=[[cx - 11, TOP - 1, cz, "lava"], [cx - 12, TOP - 1, cz - 7, "lava"], [cx - 12, TOP - 1, cz + 7, "lava"]])

# ---- 6. dedans : le tunnel, la grande salle --------------------------------------------------------------------
tool("fill", "le tunnel d'entrée, au sud", **{"from": [cx - 1, y0, cz - 42], "to": [cx + 1, y0 + 3, cz - 8]}, block="air")
portal = [[x, y0 + 4, cz - 40, "gold_block"] for x in range(cx - 2, cx + 3)]
portal += [[x, y, cz - 40, "obsidian"] for x in (cx - 2, cx + 2) for y in range(y0, y0 + 4)]
portal += [[cx - 3, y0 + 2, cz - 40, "lamp"], [cx + 3, y0 + 2, cz - 40, "lamp"]]
put_many(portal, "le portail : un linteau d'or, des montants d'obsidienne, deux lampes")
tool("fill", "la grande salle (15 x 15, 8 de haut)", **{"from": [cx - 7, y0, cz - 7], "to": [cx + 7, y0 + 7, cz + 7]}, block="air")
hall = []
for x in (cx - 4, cx + 4):
    for z in (cz - 4, cz + 4):
        hall += [[x, y, z, "stone_brick"] for y in range(y0, y0 + 7)] + [[x, y0 + 7, z, "gold_block"]]
for x in (cx - 4, cx, cx + 4):
    for z in (cz - 4, cz, cz + 4):
        hall.append([x, y0 + 8, z, "lamp"])
for x in range(cx - 7, cx + 8):
    for z in (cz - 8, cz + 8):
        hall.append([x, y0 + 5, z, "wool_green" if x % 2 else "gold_block"])
for z in range(cz - 7, cz + 8):
    for x in (cx - 8, cx + 8):
        hall.append([x, y0 + 5, z, "wool_green" if z % 2 else "gold_block"])
hall += [[x, y0, z, "stone_brick"] for x in range(cx - 1, cx + 2) for z in range(cz - 1, cz + 2)]
hall += [[cx, y0 + 1, cz, "gold_block"], [cx, y0 + 2, cz, "lamp"]]
put_many(hall, "quatre piliers à chapiteau d'or, neuf lampes au plafond, une frise de jade et d'or, l'autel")

# ---- 7. la porte secrète : de la colle (le jade), un piston collant, un levier ------------------------------------
# la porte glisse dans le mur nord, dans une cavité à l'ouest ; tout ce que la colle touche est en obsidienne (un
# piston ne la déplace pas) : seuls le bloc de jade et la pierre posée dessus bougent
zD = cz + 9
door = [[cx, y0, cz + 8, "air"], [cx, y0 + 1, cz + 8, "air"],                                # la niche
        [cx - 1, y0, cz + 8, "obsidian"], [cx - 1, y0 + 1, cz + 8, "obsidian"],
        [cx + 1, y0, cz + 8, "obsidian"], [cx + 1, y0 + 1, cz + 8, "obsidian"], [cx, y0 + 2, cz + 8, "gold_block"],
        [cx - 1, y0 - 1, zD, "obsidian"], [cx, y0 - 1, zD, "obsidian"], [cx + 1, y0, zD, "obsidian"], [cx - 1, y0, zD + 1, "obsidian"],
        [cx - 2, y0, zD, "sticky_piston", "east"],                                          # le piston, caché dans le mur
        [cx - 1, y0, zD, "glue_block"], [cx - 1, y0 + 1, zD, "stone_brick"],                # la porte, ouverte (dans sa cavité)
        [cx, y0, zD, "air"], [cx, y0 + 1, zD, "air"]]
put_many(door, "la porte secrète, d'abord ouverte")
tool("fill", "le passage derrière la porte", **{"from": [cx, y0, zD + 1], "to": [cx, y0 + 2, zD + 2]}, block="air")
tool("set_block", "le levier, contre le mur de la salle, à gauche de la niche", x=cx - 2, y=y0, z=cz + 7, block="lever", state="south")
tool("use_block", "basculer le levier : le piston pousse la porte de jade dans la niche", x=cx - 2, y=y0, z=cz + 7)
time.sleep(2)
closed = tool("get_block", "la porte est-elle fermée ?", x=cx, y=y0, z=zD)
log("porte :", "fermée" if closed.get("block") == "glue_block" else "PAS fermée : " + str(closed))

# ---- 8. l'escalier caché, la chambre du trésor ---------------------------------------------------------------------
zS = zD + 2
for k in range(1, 13):
    tool("fill", "l'escalier caché, marche %d" % k if k in (1, 12) else "", **{"from": [cx + k, y0 + k, zS], "to": [cx + k, y0 + k + 2, zS]}, block="air")
tool("fill", "le palier, vers le sud", **{"from": [cx + 12, y0 + 12, cz], "to": [cx + 12, y0 + 14, zS]}, block="air")
tool("fill", "le couloir, vers l'ouest", **{"from": [cx + 6, y0 + 12, cz], "to": [cx + 12, y0 + 14, cz]}, block="air")
tool("fill", "la chambre du trésor (11 x 9, 6 de haut)", **{"from": [cx - 5, y0 + 12, cz - 4], "to": [cx + 5, y0 + 17, cz + 4]}, block="air")
tomb = [[x, y0 + 12, z, "stone_brick"] for x in range(cx - 2, cx + 1) for z in range(cz - 1, cz + 2)]
tomb += [[x, y0 + 13, cz, "gold_block"] for x in range(cx - 2, cx + 1)]
for x, z in ((cx - 5, cz - 4), (cx - 5, cz + 4), (cx + 5, cz - 4), (cx + 5, cz + 4)):
    tomb += [[x, y0 + 12, z, "gold_block"], [x, y0 + 13, z, "gold_block"]]
tomb += [[cx - 4, y0 + 12, cz - 4, "crystal_ore"], [cx - 4, y0 + 12, cz + 4, "crystal_ore"]]
tomb += [[x, y0 + 18, z, "lamp"] for x in (cx - 3, cx + 3) for z in (cz - 2, cz + 2)]
for x in range(cx - 5, cx + 6):
    tomb += [[x, y0 + 15, cz - 5, "wool_red"], [x, y0 + 15, cz + 5, "wool_red"]]
for z in range(cz - 4, cz + 5):
    tomb += [[cx - 6, y0 + 15, z, "wool_red"], [cx + 6, y0 + 15, z, "wool_red"]]
put_many(tomb, "le sarcophage d'or, le trésor, les lampes, la frise")

# ---- 9. la place : des colonnes et leurs lampes -------------------------------------------------------------------
cols = []
for x, z in ((cx - 50, cz - 50), (cx - 50, cz + 50), (cx + 50, cz - 50), (cx + 50, cz + 50), (cx - 58, cz - 16), (cx - 58, cz + 16), (cx - 6, cz - 50), (cx + 6, cz - 50)):
    cols += [[x, y, z, "stone_brick"] for y in range(y0, y0 + 4)] + [[x, y0 + 4, z, "gold_block"], [x, y0 + 5, z, "lamp"]]
put_many(cols, "les colonnes de la place")

# ---- 10. vérifier, en regardant -----------------------------------------------------------------------------------
tool("run_command", "les créatures de la nuit reviennent (le temple est sombre : elles peuvent s'y cacher)", command="mobs on")
tool("screenshot", "vue d'ensemble", eye=[cx - 115, y0 + 62, cz - 80], look_at=[cx, y0 + 18, cz], max_width=1024)
tool("screenshot", "l'escalier, du pied", eye=[cx - 78, y0 + 9, cz + 0.5], look_at=[cx - 25, y0 + 30, cz + 0.5], max_width=1024)
tool("screenshot", "le sommet, les deux sanctuaires", eye=[cx - 24, TOP + 7, cz + 0.5], look_at=[cx, TOP + 3, cz + 0.5], max_width=1024)
tool("screenshot", "l'entrée, au sud", eye=[cx + 0.5, y0 + 3, cz - 56], look_at=[cx + 0.5, y0 + 2, cz - 40], max_width=1024)
tool("screenshot", "la grande salle", eye=[cx - 6, y0 + 5.5, cz - 6], look_at=[cx + 2, y0 + 1, cz + 6], max_width=1024)
tool("screenshot", "la porte secrète et son levier", eye=[cx - 0.5, y0 + 1.7, cz + 3], look_at=[cx - 0.5, y0 + 1, cz + 8], max_width=1024)
tool("screenshot", "la chambre du trésor", eye=[cx + 4.5, y0 + 15.5, cz + 3.5], look_at=[cx - 1, y0 + 12.5, cz], max_width=1024)
tool("teleport", "le joueur au pied de l'escalier, face au temple", position=[cx - 62.5, y0, cz + 0.5], yaw=90, pitch=12)
tool("post_message", text="Le temple est prêt : montez l'escalier, entrez par le tunnel du sud ; un levier ouvre la porte de jade...")
tool("save_world", "sauver")
log("fini en %.0f s, %d appels" % (time.time() - T0, _id[0]))
