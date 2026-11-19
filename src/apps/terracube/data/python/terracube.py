"""terracube -- piloter TerraCube (le bac à sable de blocs de Korin OS) depuis Python.

Le jeu doit tourner, avec Pilotage > Scripts coché (port 4730, sur la même machine par défaut ; le jeu lancé avec
--api-any l'ouvre au réseau). Aucune dépendance : un socket TCP, une commande par ligne (docs/TERRACUBE.md).

    import sys; sys.path.insert(0, "/apps/terracube.app/python")
    from terracube import TerraCube, Block

    tc = TerraCube()                       # ("127.0.0.1", 4730) par défaut ; TERRACUBE_HOST, TERRACUBE_PORT
    tc.post("Bonjour !")
    x, y, z = tc.player.tile()
    tc.set_block(x + 2, y, z, Block.STONE)
    tc.fill(x - 3, y - 1, z - 3, x + 3, y - 1, z + 3, "gold_block")
    print(tc.get_height(x, z), tc.get_block(x, y - 1, z))

Les blocs se donnent par numéro (Block.STONE), ou par nom anglais ("stone"). Les coordonnées : x vers l'est, y vers
le haut (0 à 127, la mer à 48), z vers le sud. yaw 0 regarde le nord (-z), 90 l'est (+x).

MIT License -- Copyright (c) 2026 Stéphane Wegener.
"""
import math
import os
import socket

__all__ = ["TerraCube", "Block", "Item", "Vec3", "BlockEvent", "TerraCubeError"]


class TerraCubeError(RuntimeError):
    """Une commande refusée par le jeu (argument faux, hors du monde...)."""


class Vec3(tuple):
    """Un triplet (x, y, z) avec ses noms."""

    def __new__(cls, x, y, z):
        return tuple.__new__(cls, (x, y, z))

    x = property(lambda s: s[0])
    y = property(lambda s: s[1])
    z = property(lambda s: s[2])

    def __add__(self, o):
        return Vec3(self[0] + o[0], self[1] + o[1], self[2] + o[2])

    def __sub__(self, o):
        return Vec3(self[0] - o[0], self[1] - o[1], self[2] - o[2])

    def __repr__(self):
        return "Vec3(%r, %r, %r)" % self


class Block:
    """Les numéros des blocs (ceux de blocks.h ; on en ajoute à la fin, jamais ailleurs)."""
    AIR = 0
    STONE = 1
    GRASS = 2
    DIRT = 3
    COBBLESTONE = 4
    PLANKS = 5
    BEDROCK = 6
    WATER = 7
    SAND = 8
    GRAVEL = 9
    LOG = 10
    LEAVES = 11
    GLASS = 12
    BRICK = 13
    SANDSTONE = 14
    SNOW = 15
    ICE = 16
    COAL_ORE = 17
    IRON_ORE = 18
    GOLD_ORE = 19
    CRYSTAL_ORE = 20
    LAMP = 21
    TALL_GRASS = 22
    FLOWER_RED = 23
    FLOWER_YELLOW = 24
    WOOL_WHITE = 25
    WOOL_RED = 26
    WOOL_ORANGE = 27
    WOOL_YELLOW = 28
    WOOL_GREEN = 29
    WOOL_BLUE = 30
    WOOL_PURPLE = 31
    WOOL_BLACK = 32
    GOLD_BLOCK = 33
    STONE_BRICK = 34
    CLAY = 35
    PINE_LOG = 36
    PINE_LEAVES = 37
    CACTUS = 38
    BOOKSHELF = 39
    WORKBENCH = 40
    FURNACE = 41

    @classmethod
    def name_of(cls, n):
        for k, v in vars(cls).items():
            if v == n and k.isupper():
                return k.lower()
        return str(n)


class Item:
    """Les objets du mode survie qui ne sont pas des blocs (items.h ; les blocs sont aussi des objets)."""
    STICK = 100
    COAL = 101
    IRON_INGOT = 102
    GOLD_INGOT = 103
    CRYSTAL = 104
    WOOD_PICKAXE = 105
    STONE_PICKAXE = 106
    IRON_PICKAXE = 107
    CRYSTAL_PICKAXE = 108
    WOOD_AXE = 109
    STONE_AXE = 110
    IRON_AXE = 111
    CRYSTAL_AXE = 112
    WOOD_SHOVEL = 113
    STONE_SHOVEL = 114
    IRON_SHOVEL = 115
    CRYSTAL_SHOVEL = 116


class BlockEvent:
    """Un bloc cassé (kind 1), posé (2) ou touché par la touche E (3)."""
    BROKEN, PLACED, TOUCHED = 1, 2, 3

    def __init__(self, kind, x, y, z, face, block):
        self.kind, self.pos, self.face, self.block = kind, Vec3(x, y, z), face, block

    def __repr__(self):
        k = {1: "broken", 2: "placed", 3: "touched"}.get(self.kind, self.kind)
        return "BlockEvent(%s, %r, face=%d, block=%s)" % (k, self.pos, self.face, Block.name_of(self.block))


def _arg(v):
    if isinstance(v, float):
        return repr(v)
    return str(v)


class _Player:
    def __init__(self, conn):
        self._c = conn

    def pos(self):
        """La position des pieds (réels)."""
        return Vec3(*[float(v) for v in self._c.ask("player.getPos").split(",")])

    def tile(self):
        """Le bloc des pieds (entiers)."""
        return Vec3(*[int(v) for v in self._c.ask("player.getTile").split(",")])

    def set_pos(self, x, y, z):
        self._c.send("player.setPos", x, y, z)

    def set_tile(self, x, y, z):
        self._c.send("player.setTile", int(x), int(y), int(z))

    def rotation(self):
        """yaw en degrés (0 : nord, 90 : est)."""
        return float(self._c.ask("player.getRotation"))

    def pitch(self):
        return float(self._c.ask("player.getPitch"))

    def set_rotation(self, yaw):
        self._c.send("player.setRotation", yaw)

    def set_pitch(self, pitch):
        self._c.send("player.setPitch", pitch)

    def direction(self):
        """Le vecteur unité du regard."""
        return Vec3(*[float(v) for v in self._c.ask("player.getDirection").split(",")])

    def set_fly(self, on=True):
        self._c.send("player.setFly", 1 if on else 0)

    # (les noms du protocole marchent aussi)
    getPos, getTilePos, setPos, setTilePos = pos, tile, set_pos, set_tile


class TerraCube:
    """Une connexion au jeu. Les commandes sans réponse partent tout de suite (des milliers par seconde) ; celles
    qui rendent une valeur attendent la leur, après toutes celles d'avant."""

    def __init__(self, host=None, port=None, timeout=300.0):
        host = host or os.environ.get("TERRACUBE_HOST", "127.0.0.1")
        port = int(port or os.environ.get("TERRACUBE_PORT", "4730"))
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self._buf = b""
        self.player = _Player(self)

    @classmethod
    def connect(cls, host=None, port=None):
        return cls(host, port)

    def close(self):
        try:
            self.sock.close()
        except OSError:
            pass

    def __enter__(self):
        return self

    def __exit__(self, *a):
        self.close()

    # -- le protocole
    def send(self, name, *args):
        """Une commande sans réponse : « nom(a,b,...) »."""
        line = "%s(%s)\n" % (name, ",".join(_arg(a) for a in args))
        self.sock.sendall(line.encode("utf-8"))

    def ask(self, name, *args):
        """Une commande à réponse -> le texte de la réponse."""
        self.send(name, *args)
        while b"\n" not in self._buf:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise TerraCubeError("TerraCube closed the connection")
            self._buf += chunk
        line, self._buf = self._buf.split(b"\n", 1)
        text = line.decode("utf-8", "replace")
        if text.startswith("error:"):
            raise TerraCubeError(text[6:].strip())
        return text

    def flush(self):
        """Attend que tout ce qui a été envoyé soit fait."""
        self.ask("ping")

    # -- le monde
    def get_block(self, x, y, z):
        return int(self.ask("world.getBlock", int(x), int(y), int(z)))

    def set_block(self, x, y, z, block):
        self.send("world.setBlock", int(x), int(y), int(z), block)

    def fill(self, x1, y1, z1, x2, y2, z2, block):
        """Un pavé plein, coins compris (1 000 000 de blocs au plus)."""
        self.send("world.setBlocks", int(x1), int(y1), int(z1), int(x2), int(y2), int(z2), block)

    def get_blocks(self, x1, y1, z1, x2, y2, z2):
        """Les numéros du pavé, dans l'ordre y, puis z, puis x."""
        t = self.ask("world.getBlocks", int(x1), int(y1), int(z1), int(x2), int(y2), int(z2))
        return [int(v) for v in t.split(",")] if t else []

    def get_height(self, x, z):
        """y du plus haut bloc non vide de la colonne."""
        return int(self.ask("world.getHeight", int(x), int(z)))

    def block_types(self):
        """{numéro: nom}"""
        out = {}
        for item in self.ask("world.getBlockTypes").split(","):
            n, _, name = item.partition("=")
            out[int(n)] = name
        return out

    def get_time(self):
        return float(self.ask("world.getTime"))

    def set_time(self, hour, running=True):
        self.send("world.setTime", hour, 1 if running else 0)

    def save(self):
        self.ask("world.save")

    def post(self, text):
        """Un message dans la conversation du jeu."""
        self.send("chat.post", str(text).replace("\n", " "))

    def events(self):
        """Les blocs cassés, posés, touchés depuis le dernier appel."""
        t = self.ask("events.blocks")
        out = []
        for item in t.split("|") if t else []:
            v = [int(n) for n in item.split(",")]
            out.append(BlockEvent(*v))
        return out

    def info(self):
        return self.ask("game.info")

    # -- le mode, la météo, la console
    def mode(self):
        """« creative » ou « survival »."""
        return self.ask("game.getMode")

    def set_mode(self, mode):
        self.send("game.setMode", mode)

    def weather(self):
        """« clear », « rain », « snow » ou « storm »."""
        return self.ask("world.getWeather")

    def set_weather(self, weather, seconds=0):
        self.send("world.setWeather", weather, seconds)

    def command(self, line):
        """Une commande de la console du jeu (« time 18 », « weather rain », « mode survival », « help »…) -> ses lignes."""
        t = self.ask("game.command", line.replace("\n", " "))
        return t.split(" | ") if t else []

    # -- le mode survie
    def inventory(self):
        """{case: (objet, nombre)} ; les cases 0 à 8 sont la barre."""
        out = {}
        t = self.ask("player.getInventory")
        for item in t.split("|") if t else []:
            slot, name, n = item.split(":")
            out[int(slot)] = (name, int(n))
        return out

    def give(self, item, n=1):
        self.send("player.give", item, n)

    def craft(self, recipe, times=1):
        """Fabrique une recette (« planks », « wood_pickaxe »…) -> le nombre de fois faites."""
        return int(self.ask("player.craft", recipe, times))

    # -- quelques formes
    def sphere(self, cx, cy, cz, r, block, hollow=False):
        r2, inner = r * r, (r - 1) * (r - 1)
        ri = int(math.ceil(r))
        for y in range(-ri, ri + 1):
            for z in range(-ri, ri + 1):
                for x in range(-ri, ri + 1):
                    d = x * x + y * y + z * z
                    if d <= r2 and (not hollow or d > inner):
                        self.set_block(cx + x, cy + y, cz + z, block)

    def line(self, x1, y1, z1, x2, y2, z2, block):
        n = int(max(abs(x2 - x1), abs(y2 - y1), abs(z2 - z1))) or 1
        for i in range(n + 1):
            t = i / n
            self.set_block(round(x1 + (x2 - x1) * t), round(y1 + (y2 - y1) * t), round(z1 + (z2 - z1) * t), block)

    # (les noms du protocole marchent aussi)
    getBlock, setBlock, setBlocks, getHeight, postToChat = get_block, set_block, fill, get_height, post
