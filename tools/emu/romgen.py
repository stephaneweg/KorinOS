"""ROM d'essai des emulateurs, ecrites ici : libres, puisqu'a nous (docs/EMULATEURS.md).

    python -I tools/emu/romgen.py nes-stress <mapper> <graine> sortie.nes [chr-ram] [pal]
    python -I tools/emu/romgen.py nes-demo sortie.nes
    python -I tools/emu/romgen.py snes-stress <graine> sortie.sfc [hirom] [pal]
    python -I tools/emu/romgen.py gba-stress <graine> sortie.gba [sram|flash|flash1m|eeprom]
    python -I tools/emu/romgen.py n64-stress <graine> sortie.z64

nes-stress : des milliers d'instructions 6502 au hasard, tous les codes sauf les sauts et KIL, sur la
RAM, les registres du PPU et de l'APU, la cartouche ; toutes les banques portent le meme code, la
commutation ne perd donc jamais le fil. Sert a comparer deux coeurs image par image.
snes-stress : pareil pour le 65C816 (les deux modes, M et X, emulation) et le SPC700, sur les registres
du PPU, le DMA, le HDMA, les interruptions : chaque section tire un mode du PPU, ses plans et ses fenetres.
gba-stress : ARM et Thumb au hasard, les modes du processeur, tous les modes du PPU, le son, DMA, les
appels du BIOS de remplacement, les sauvegardes.
nes-demo : une petite demo jouable pour les essais a l'ecran : motif qui defile, balle aux fleches,
son sur A.
"""
import io
import os
import random
import re
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


def nes_ops():
    ops = []
    with io.open(os.path.join(HERE, "../../src/apps/nesemu/nes_ops.inc"), encoding="ascii") as f:
        for l in f:
            m = re.match(r"opc (\d+), (\w+), (\w+), (\d+), (\d)", l)
            if m:
                ops.append((int(m[1]), m[2], m[3]))
    return ops


OPLEN = {"IMP": 1, "ACC": 1, "IMM": 2, "ZP": 2, "ZPX": 2, "ZPY": 2, "ABS": 3, "ABX": 3, "ABY": 3,
         "IND": 3, "IZX": 2, "IZY": 2, "REL": 2}
BRANCHES = {"BPL", "BMI", "BVC", "BVS", "BCC", "BCS", "BNE", "BEQ"}
SKIP = {"KIL", "JMP", "JSR", "RTS", "RTI"}


def abs_target(r):
    k = r.random()
    if k < 0.35:
        return r.randrange(0, 0x800)
    if k < 0.60:
        return 0x2000 + r.randrange(0, 8) + r.choice((0, 0, 8, 0x1FF0))
    if k < 0.78:
        return 0x4000 + r.randrange(0, 0x18)
    if k < 0.86:
        return 0x6000 + r.randrange(0, 0x2000)
    if k < 0.93:
        return r.randrange(0x8000, 0x10000)
    return r.randrange(0x4020, 0x6000)


def stress(mapper, seed, chr_ram, pal):
    r = random.Random(seed)
    ops = [o for o in nes_ops() if o[2] not in SKIP]
    code = bytearray()
    base = 0x8000
    size = 0x2000 - 0x100		# le code ; ensuite des donnees, puis les vecteurs

    def lda(v):
        code.extend((0xA9, v & 255))

    def sta(a):
        code.extend((0x8D, a & 255, a >> 8))
    code.extend((0x78, 0xD8, 0xA2, 0xFF, 0x9A))	# sei, cld, ldx #ff, txs
    lda(0x0F)
    sta(0x4015)
    lda(0x90)
    sta(0x2000)
    lda(0x1E)
    sta(0x2001)
    if mapper == 4:
        lda(0x20)
        sta(0xC000)
        sta(0xC001)
        sta(0xE001)
    loop = base + len(code)
    while len(code) < size - 120:
        k = r.random()
        if k < 0.05:				# palette, sprite, defilement : de quoi voir
            lda(0x3F)
            sta(0x2006)
            lda(r.randrange(32))
            sta(0x2006)
            lda(r.randrange(64))
            sta(0x2007)
            lda(r.randrange(256))
            sta(0x2003)
            lda(r.randrange(256))
            sta(0x2004)
            lda(r.randrange(256))
            sta(0x2005)
            continue
        if k < 0.08:				# table de noms
            lda(0x20 + r.randrange(16))
            sta(0x2006)
            lda(r.randrange(256))
            sta(0x2006)
            for _ in range(4):
                lda(r.randrange(256))
                sta(0x2007)
            continue
        op = r.choice(ops)
        n, mode, name = op
        ln = OPLEN[mode]
        if name in BRANCHES:
            # par-dessus des octets NOP
            d = r.randrange(0, 12)
            code.extend((n, d))
            code.extend([0xEA] * d)
            continue
        code.append(n)
        if ln == 2:
            code.append(r.randrange(256))
        elif ln == 3:
            a = abs_target(r)
            code.extend((a & 255, a >> 8))
    code.extend((0x4C, loop & 255, loop >> 8))
    # interruptions (dans le meme bloc)
    nmi = base + len(code)
    code.extend((0x48, 0xAD, 0x02, 0x20, 0xE6, 0x10, 0x68, 0x40))	# pha, lda $2002, inc $10, pla, rti
    irq = base + len(code)
    code.extend((0x48, 0xAD, 0x15, 0x40, 0x8D, 0x00, 0xE0, 0x8D, 0x01, 0xE0, 0xE6, 0x11, 0x68, 0x40))
    assert len(code) <= size
    code.extend([0xEA] * (size - len(code)))
    banks = 16
    prg = bytearray()
    for b in range(banks):
        d = bytearray(code)
        d.extend(r.randrange(256) for _ in range(0x100 - 6))
        d.extend(struct.pack("<HHH", nmi, base, irq))
        prg += d
    chrb = 0 if chr_ram else 8
    chrd = bytes(r.randrange(256) for _ in range(chrb * 0x2000))
    flags6 = ((mapper & 15) << 4) | 2 | r.randrange(2)
    flags7 = mapper & 0xF0
    h = b"NES\x1a" + bytes((banks // 2, chrb, flags6, flags7, 0, 1 if pal else 0)) + bytes(6)
    return h + bytes(prg) + chrd


class A6502:
    def __init__(self, base):
        self.b = bytearray()
        self.base = base
        self.lab = {}
        self.fix = []

    def here(self, name):
        self.lab[name] = self.base + len(self.b)

    def op(self, *v):
        self.b.extend(v)

    def lda(self, v):
        self.op(0xA9, v)

    def ldx(self, v):
        self.op(0xA2, v)

    def ldy(self, v):
        self.op(0xA0, v)

    def sta(self, a):
        self.op(0x8D, a & 255, a >> 8)

    def lda_a(self, a):
        self.op(0xAD, a & 255, a >> 8)

    def br(self, opc, lab):
        self.op(opc, 0)
        self.fix.append((len(self.b) - 1, lab, "r"))

    def jmp(self, lab):
        self.op(0x4C, 0, 0)
        self.fix.append((len(self.b) - 2, lab, "a"))

    def done(self):
        for at, lab, k in self.fix:
            t = self.lab[lab]
            if k == "a":
                self.b[at] = t & 255
                self.b[at + 1] = t >> 8
            else:
                d = t - (self.base + at + 1)
                assert -128 <= d < 128
                self.b[at] = d & 255
        return self.b


def demo():
    a = A6502(0x8000)
    a.here("reset")
    a.op(0x78, 0xD8)
    a.ldx(0xFF)
    a.op(0x9A)
    a.lda(0)
    a.sta(0x2000)
    a.sta(0x2001)
    for w in ("w1", "w2"):
        a.here(w)
        a.lda_a(0x2002)
        a.br(0x10, w)
    a.lda(0x3F)
    a.sta(0x2006)
    a.lda(0)
    a.sta(0x2006)
    for c in (0x0F, 0x11, 0x21, 0x30, 0x0F, 0x16, 0x26, 0x36, 0x0F, 0x19, 0x29, 0x39, 0x0F, 0x14, 0x24, 0x34,
              0x0F, 0x27, 0x17, 0x30, 0x0F, 0x12, 0x22, 0x32, 0x0F, 0x1A, 0x2A, 0x3A, 0x0F, 0x15, 0x25, 0x35):
        a.lda(c)
        a.sta(0x2007)
    a.lda(0x20)
    a.sta(0x2006)
    a.lda(0)
    a.sta(0x2006)
    a.ldy(0)			# 30 lignes de tuiles 1..3 ; puis les attributs
    a.here("row")
    a.ldx(0)
    a.here("col")
    a.op(0x8A)			# txa
    a.op(0x84, 0x20)		# sty $20
    a.op(0x18)			# clc
    a.op(0x65, 0x20)		# adc $20
    a.op(0x29, 0x03)		# and #3
    a.op(0x09, 0x01)		# ora #1
    a.sta(0x2007)
    a.op(0xE8)			# inx
    a.op(0xE0, 32)
    a.br(0xD0, "col")
    a.op(0xC8)			# iny
    a.op(0xC0, 30)
    a.br(0xD0, "row")
    a.ldx(0)
    a.here("attr")
    a.op(0x8A)
    a.op(0x29, 0x03)
    a.op(0xA8)			# tay
    a.op(0xB9, 0, 0)		# lda palettes,y (corrige plus bas)
    fix = len(a.b) - 2
    a.sta(0x2007)
    a.op(0xE8)
    a.op(0xE0, 64)
    a.br(0xD0, "attr")
    a.lda(120)
    a.op(0x85, 0x11)		# x
    a.op(0x85, 0x12)		# y
    a.lda(0x0F)
    a.sta(0x4015)
    a.lda(0x88)
    a.sta(0x2000)
    a.lda(0x1E)
    a.sta(0x2001)
    a.here("idle")
    a.jmp("idle")
    a.here("nmi")
    a.op(0x48)
    a.lda(1)
    a.sta(0x4016)
    a.lda(0)
    a.sta(0x4016)
    a.op(0x85, 0x10)
    a.ldx(8)
    a.here("pad")
    a.lda_a(0x4016)
    a.op(0x4A)			# lsr a
    a.op(0x26, 0x10)		# rol $10
    a.op(0xCA)			# dex
    a.br(0xD0, "pad")
    # A B Sel St H B G D : bit 7 = A
    a.op(0xA5, 0x10)
    a.op(0x29, 0x01)
    a.br(0xF0, "nr")
    a.op(0xE6, 0x11)
    a.here("nr")
    a.op(0xA5, 0x10)
    a.op(0x29, 0x02)
    a.br(0xF0, "nl")
    a.op(0xC6, 0x11)
    a.here("nl")
    a.op(0xA5, 0x10)
    a.op(0x29, 0x04)
    a.br(0xF0, "nd")
    a.op(0xE6, 0x12)
    a.here("nd")
    a.op(0xA5, 0x10)
    a.op(0x29, 0x08)
    a.br(0xF0, "nu")
    a.op(0xC6, 0x12)
    a.here("nu")
    a.lda(0)
    a.sta(0x2003)
    a.op(0xA5, 0x12)
    a.sta(0x2004)
    a.lda(4)
    a.sta(0x2004)
    a.lda(0)
    a.sta(0x2004)
    a.op(0xA5, 0x11)
    a.sta(0x2004)
    a.op(0xE6, 0x13)		# defilement
    a.op(0xA5, 0x13)
    a.sta(0x2005)
    a.lda(0)
    a.sta(0x2005)
    a.op(0xA5, 0x10)
    a.op(0x29, 0x80)
    a.br(0xF0, "quiet")
    a.lda(0xBF)
    a.sta(0x4000)
    a.lda(0)
    a.sta(0x4001)
    a.op(0xA5, 0x11)		# hauteur : la position de la balle
    a.sta(0x4002)
    a.lda(0x08)
    a.sta(0x4003)
    a.jmp("end")
    a.here("quiet")
    a.lda(0xB0)
    a.sta(0x4000)
    a.here("end")
    a.op(0x68, 0x40)
    a.here("rti")
    a.op(0x40)
    a.here("pal")
    a.op(0x00, 0x55, 0xAA, 0xFF)
    code = a.done()
    p = a.lab["pal"]
    code[fix] = p & 255
    code[fix + 1] = p >> 8
    prg = bytearray(code)
    prg.extend(bytes(0x8000 - 6 - len(prg)))
    prg.extend(struct.pack("<HHH", a.lab["nmi"], a.lab["reset"], a.lab["rti"]))
    tiles = bytearray(0x2000)
    for t, rows in ((1, (0xAA, 0x55) * 4), (2, (0xFF, 0x00) * 4), (3, (0xF0, 0xF0, 0xF0, 0xF0, 0x0F, 0x0F, 0x0F, 0x0F))):
        for i in range(8):
            tiles[t * 16 + i] = rows[i]
            tiles[t * 16 + 8 + i] = rows[i] ^ (0xFF if t == 3 else 0)
    ball = (0x3C, 0x7E, 0xFF, 0xFF, 0xFF, 0xFF, 0x7E, 0x3C)
    for i in range(8):
        tiles[4 * 16 + i] = ball[i]
    h = b"NES\x1a" + bytes((2, 1, 0, 0)) + bytes(8)
    return h + bytes(prg) + bytes(tiles)


# ---- SNES --------------------------------------------------------------------------------------

def snes_len(op):
    """Octets d'un code 65C816 ; 'm' / 'x' : immediat selon M ou X."""
    lo = op & 0x1F
    if op in (0xA2, 0xA0, 0xE0, 0xC0):
        return "x"
    if op == 0x89:
        return "m"
    if ((op & 1) and (op & 0x0F) != 0x0B) or lo == 0x12:		# le groupe de la UAL
        return {1: 2, 3: 2, 5: 2, 7: 2, 9: "m", 0x0D: 3, 0x0F: 4, 0x11: 2, 0x12: 2, 0x13: 2, 0x15: 2, 0x17: 2,
                0x19: 3, 0x1D: 3, 0x1F: 4}[lo]
    if op in (0x04, 0x14, 0x06, 0x16, 0x24, 0x34, 0x26, 0x36, 0x46, 0x56, 0x64, 0x74, 0x66, 0x76, 0x84, 0x94, 0x86, 0x96,
              0xA4, 0xB4, 0xA6, 0xB6, 0xC4, 0xC6, 0xD6, 0xE4, 0xE6, 0xF6, 0x42):
        return 2
    if op in (0x0C, 0x1C, 0x0E, 0x1E, 0x2C, 0x3C, 0x2E, 0x3E, 0x4E, 0x5E, 0x6E, 0x7E, 0x8C, 0x8E, 0x9C, 0x9E, 0xAC, 0xBC, 0xAE,
              0xBE, 0xCC, 0xCE, 0xDE, 0xEC, 0xEE, 0xFE):
        return 3
    return 1


# ce que le tirage evite : sauts, pile, modes (emis a part, en blocs equilibres)
SNES_SKIP = {0x4C, 0x5C, 0x6C, 0x7C, 0xDC, 0xFC, 0x20, 0x22, 0x60, 0x6B, 0x40, 0xDB, 0xCB, 0x00, 0x02,
             0x10, 0x30, 0x50, 0x70, 0x90, 0xB0, 0xD0, 0xF0, 0x80, 0x82, 0x54, 0x44, 0xFB, 0xC2, 0xE2,
             0x28, 0x08, 0x48, 0x68, 0xDA, 0xFA, 0x5A, 0x7A, 0x8B, 0xAB, 0x4B, 0x0B, 0x2B, 0xF4, 0xD4, 0x62,
             0x1B, 0x9A}


def snes_target(r):
    k = r.random()
    if k < 0.40:
        return r.randrange(0, 0x1E00)				# la WRAM
    if k < 0.66:
        return 0x2100 + r.randrange(0, 0x40)			# le PPU
    if k < 0.72:
        return 0x2140 + r.randrange(0, 4)
    if k < 0.76:
        return 0x2180 + r.randrange(0, 4)
    if k < 0.90:
        return 0x4200 + r.randrange(0, 0x20)
    if k < 0.95:
        return 0x4300 + r.randrange(0, 0x7F)
    if k < 0.97:
        return r.choice((0x4016, 0x4017))
    return r.randrange(0x8000, 0x10000)


SPC_SKIP = {0xEF, 0xFF, 0x0F, 0x5F, 0x1F, 0x3F, 0x4F, 0x6F, 0x7F, 0x2F, 0x10, 0x30, 0x50, 0x70, 0x90, 0xB0, 0xD0, 0xF0,
            0x2E, 0xDE, 0x6E, 0xFE, 0x40, 0x8E, 0xC7, 0xD7}	# (sans la page 1, ni ecrire n'importe ou)


def spc_len(op):
    lo, hi = op & 15, op >> 4
    if lo == 1 or lo == 3:
        return None						# TCALL, BBS : perdraient le fil
    if hi < 0xC and 4 <= lo <= 9:
        if lo == 8:
            return 3 if hi & 1 else 2
        if lo == 9:
            return 1 if hi & 1 else 3
        return {4: 2, 5: 3, 6: 3 if hi & 1 else 1, 7: 2}[lo]
    if lo == 2:
        return 2
    if op in (0xC5, 0xD5, 0xD6, 0xE5, 0xF5, 0xF6, 0xE9, 0xC9, 0xEC, 0xCC, 0x1E, 0x5E, 0x0E, 0x4E, 0x8F, 0xFA, 0x0C, 0x2C, 0x4C,
              0x6C, 0x8C, 0xAC, 0x0A, 0x2A, 0x4A, 0x6A, 0x8A, 0xAA, 0xCA, 0xEA):
        return 3
    if op in (0xC4, 0xD4, 0xC7, 0xD7, 0xE4, 0xF4, 0xE7, 0xF7, 0xE8, 0xCD, 0x8D, 0xF8, 0xF9, 0xEB, 0xFB, 0xD8, 0xD9, 0xCB, 0xDB,
              0xC8, 0x3E, 0xAD, 0x7E, 0x0B, 0x2B, 0x4B, 0x6B, 0x8B, 0xAB, 0x1B, 0x3B, 0x5B, 0x7B, 0x9B, 0xBB, 0x1A, 0x3A, 0x5A,
              0x7A, 0x9A, 0xBA, 0xDA):
        return 2
    return 1


def spc_program(r, sleep):
    """Le programme du SPC700 : les voix, l'echo, les minuteurs, puis une boucle de codes au hasard."""
    base = 0x0300
    c = bytearray()

    def dsp(a, v):
        c.extend((0x8F, a, 0xF2, 0x8F, v, 0xF3))		# mov $F2,#a ; mov $F3,#v
    dsp(0x6C, 0x20)
    dsp(0x5D, 0x10)						# les sources en $1000
    dsp(0x0C, 0x7F)
    dsp(0x1C, 0x7F)
    dsp(0x2C, r.randrange(0x80))
    dsp(0x3C, r.randrange(0x80))
    dsp(0x0D, r.randrange(256))
    dsp(0x6D, 0x60)						# l'echo en $6000
    dsp(0x7D, r.randrange(1, 4))
    for i in range(8):
        dsp(i * 16 + 0x0F, r.randrange(256))
    for v in range(8):
        dsp(v * 16 + 0, r.randrange(128))
        dsp(v * 16 + 1, r.randrange(128))
        dsp(v * 16 + 2, r.randrange(256))
        dsp(v * 16 + 3, r.randrange(0x08, 0x20))
        dsp(v * 16 + 4, r.randrange(4))
        dsp(v * 16 + 5, r.choice((0, 0x80 | r.randrange(128))))
        dsp(v * 16 + 6, r.randrange(256))
        dsp(v * 16 + 7, r.randrange(256))
    dsp(0x2D, r.randrange(256) & 0xFE)
    dsp(0x3D, r.randrange(256) & r.randrange(256))
    dsp(0x4D, r.randrange(256))
    dsp(0x4C, 0xFF)
    c.extend((0x8F, 0x20, 0xFA, 0x8F, 0x40, 0xFB, 0x8F, 0x80, 0xFC, 0x8F, 0x07, 0xF1))	# les minuteurs
    loop = base + len(c)
    ops = [o for o in range(256) if o not in SPC_SKIP and spc_len(o) is not None]
    while len(c) < 0xA00:
        k = r.random()
        if k < 0.07:
            a = r.randrange(128)
            if a != 0x6C or r.random() < 0.2:
                dsp(a, r.randrange(256))
            continue
        if k < 0.11:
            dsp(0x4C, r.randrange(256))
            continue
        if k < 0.13:
            dsp(0x5C, r.randrange(256))
            continue
        if k < 0.16:						# un saut par-dessus, conditionnel
            o = r.choice((0x10, 0x30, 0x50, 0x70, 0x90, 0xB0, 0xD0, 0xF0, 0x2F))
            d = r.randrange(6)
            c.extend((o, d))
            c.extend([0x00] * d)
            continue
        if k < 0.18:						# DBNZ, CBNE, BBS, BBC
            d = r.randrange(6)
            o = r.choice((0x2E, 0x6E, 0xDE, (r.randrange(16) << 4) | 3))
            c.extend((o, r.randrange(0x20, 0xE0), d))
            c.extend([0x00] * d)
            continue
        if k < 0.185:						# SETP, POP PSW, [d+X] / [d]+Y en ecriture
            t = r.randrange(3)
            if t == 0:
                c.extend((0x40, 0xE8, r.randrange(256), 0xC4, r.randrange(0x20, 0xE0), 0x20))
            elif t == 1:
                c.extend((0x0D, 0x8E))
            else:
                d = r.randrange(0x20, 0xD0)
                c.extend((0x8F, r.randrange(256), d, 0x8F, 0x58, d + 1, 0xCD, 0, 0x8D, 0, r.choice((0xC7, 0xD7)), d))
            continue
        if k < 0.19:						# DBNZ Y
            d = r.randrange(6)
            c.extend((0xFE, d))
            c.extend([0x00] * d)
            continue
        if k < 0.20:						# CALL, RET, PCALL, TCALL, BRK : vers un RET
            ret = base + 0xA80
            t = r.randrange(5)
            if t == 0:
                c.extend((0x3F, ret & 255, ret >> 8))
            elif t == 1:
                c.extend((0x4F, 0x00))				# PCALL $FF00
            elif t == 2:
                c.append((r.randrange(1, 16) << 4) | 1)		# TCALL 1-15
            elif t == 3:
                c.append(0x0F)					# BRK -> RETI
            else:
                at = base + len(c) + 5				# mov x,#0 ; jmp [!at+x] ; at: suite
                n = at + 2
                c.extend((0xCD, 0, 0x1F, at & 255, at >> 8, n & 255, n >> 8))
            continue
        o = r.choice(ops)
        n = spc_len(o)
        c.append(o)
        if n == 2:
            c.append(r.choice((0xF2, 0xF3, 0xF4, 0xF5, 0xF6, 0xF7, 0xF8, 0xF9, 0xFD, 0xFE, 0xFF)) if r.random() < 0.15 else r.randrange(0x20, 0xE0))
        elif n == 3:
            c.append(r.randrange(256))
            c.append(r.randrange(0x58, 0x60))			# la memoire libre, et a 13 bits pour MOV1
    if sleep:
        c.extend((0x8B, 0x1F, 0xD0, 1, 0xEF))			# dec $1F ; bne + ; sleep
    c.extend((0x5F, loop & 255, loop >> 8))			# jmp
    c.extend([0] * (0xA80 - len(c)))
    c.extend((0x6F, 0x7F))					# ret ; reti
    ret = base + 0xA80
    vec = bytearray()
    for i in range(15):
        vec.extend((ret & 255, ret >> 8))
    vec.extend(((ret + 1) & 255, (ret + 1) >> 8))
    src = bytearray()
    for i in range(4):
        a = 0x1100 + i * 0x40
        src.extend((a & 255, a >> 8, a & 255, a >> 8))
    brr = bytearray()
    for i in range(4):
        for b in range(7):
            brr.append((r.randrange(0, 15) << 4) | (r.randrange(4) << 2) | (3 if b == 6 else 0))
            brr.extend(r.randrange(256) for _ in range(8))
        brr.extend([0] * (0x40 - 63))
    return [(base, bytes(c)), (0x1000, bytes(src)), (0x1100, bytes(brr)), (0xFF00, b"\x6F"), (0xFFC0, bytes(vec))], base


def snes_stress(seed, hirom, pal):
    r = random.Random(seed)
    org = 0x8000
    c = bytearray()

    def here():
        return org + len(c)

    def op(*v):
        c.extend(v)

    def lda(v):
        op(0xA9, v & 255)

    def sta(a):
        op(0x8D, a & 255, a >> 8)

    def reg(a, v):
        lda(v)
        sta(a)

    def word(a, v):						# un registre a double ecriture
        reg(a, v & 255)
        reg(a, v >> 8)

    def known():						# M = 1, X = 0, banques et page directe connues
        op(0x18, 0xFB)						# clc ; xce
        op(0xC2, 0x10, 0xE2, 0x20)				# rep #$10 ; sep #$20
        op(0xA9, 0, 0x48, 0xAB)					# lda #0 ; pha ; plb
        op(0xF4, 0, 0, 0x2B)					# pea 0 ; pld
    # les vecteurs (remplis a la fin)
    handlers = {}
    op(0x78, 0x18, 0xFB)					# sei ; clc ; xce
    op(0xC2, 0x10, 0xE2, 0x20)
    op(0xA2, 0xFF, 0x1F, 0x9A)				# ldx #$1FFF ; txs
    reg(0x4200, 0)
    reg(0x2100, 0x80)
    # le son, par le protocole de la ROM de demarrage. Les donnees : apres le code
    blocks, entry = spc_program(r, seed & 1)
    data_at = 0x8000 + 0x5000
    w = here()
    op(0xAD, 0x40, 0x21, 0xC9, 0xAA, 0xD0, 0xF9)		# lda $2140 ; cmp #$AA ; bne
    kick = 0xCC
    pos = data_at
    data = bytearray()
    for dst, d in blocks:
        op(0xA2, dst & 255, dst >> 8, 0x8E, 0x42, 0x21)		# ldx #dst ; stx $2142
        reg(0x2141, 1)
        reg(0x2140, kick)
        l = here()
        op(0xCD, 0x40, 0x21, 0xD0, l - (here() + 5) & 255)	# cmp $2140 ; bne
        op(0xA2, 0, 0)						# ldx #0
        l = here()
        op(0xBD, pos & 255, pos >> 8)				# lda d,x
        sta(0x2141)
        op(0x8A)						# txa
        sta(0x2140)
        m = here()
        op(0xCD, 0x40, 0x21, 0xD0, m - (here() + 5) & 255)	# cmp $2140 ; bne
        op(0xE8, 0xE0, len(d) & 255, len(d) >> 8)		# inx ; cpx #n
        op(0xD0, l - (here() + 2) & 255)			# bne
        data += d
        pos += len(d)
        kick = ((len(d) - 1) + 3) & 255
        if kick == 0:
            kick = 1
    op(0xA2, entry & 255, entry >> 8, 0x8E, 0x42, 0x21)
    reg(0x2141, 0)
    reg(0x2140, kick)
    l = here()
    op(0xCD, 0x40, 0x21, 0xD0, l - (here() + 5) & 255)
    # VRAM, CGRAM, OAM depuis la banque 1 (au hasard)

    def dma(mode, bbad, src, n):
        reg(0x4300, mode)
        reg(0x4301, bbad)
        reg(0x4302, src & 255)
        reg(0x4303, src >> 8)
        reg(0x4304, 1)
        reg(0x4305, n & 255)
        reg(0x4306, n >> 8)
        reg(0x420B, 1)
    reg(0x2115, 0x80)
    reg(0x2116, 0)
    reg(0x2117, 0)
    dma(1, 0x18, 0x8000, 0)
    reg(0x2121, 0)
    dma(2, 0x22, 0x8000, 512)
    reg(0x2102, 0)
    reg(0x2103, 0)
    dma(2, 0x04, 0x9000, 544)
    reg(0x2100, 0x0F)
    reg(0x212C, 0x1F)
    reg(0x212D, 0x1F)
    op(0x58)							# cli
    loop = here()
    ops = [o for o in range(256) if o not in SNES_SKIP]
    limit = 0x4000
    while len(c) < limit:
        # une section : un jeu de registres du PPU
        known()
        reg(0x2100, r.randrange(1, 16))
        mode = r.randrange(8)
        reg(0x2105, mode | (r.randrange(256) & 0xF8))
        reg(0x2106, r.choice((0, 0, r.randrange(256))))
        for a in (0x2107, 0x2108, 0x2109, 0x210A):
            reg(a, r.randrange(256))
        reg(0x210B, r.randrange(256))
        reg(0x210C, r.randrange(256))
        for n in range(8):
            word(0x210D + n, r.randrange(0x10000) & r.choice((0x3FF, 0xFFFF)))
        reg(0x211A, r.randrange(256))
        for a in range(0x211B, 0x2121):
            word(a, r.randrange(0x10000))
        for a in range(0x2123, 0x2134):
            reg(a, r.randrange(256))
        reg(0x212C, r.randrange(256) | 0x11)
        reg(0x2101, r.randrange(256))
        reg(0x4200, r.choice((0x81, 0xB1, 0xA1, 0x91, 0x01)))
        word(0x4207, r.randrange(0x200))
        word(0x4209, r.randrange(0x106))
        # HDMA : une table de la banque 0
        t = r.randrange(0x8000, 0xFF00)
        reg(0x4310, r.choice((0x00, 0x01, 0x02, 0x03, 0x04, 0x42, 0x41)))
        reg(0x4311, r.choice((0x0D, 0x05, 0x21, 0x22, 0x32, 0x26)))
        reg(0x4312, t & 255)
        reg(0x4313, t >> 8)
        reg(0x4314, 0)
        reg(0x4317, 1)
        reg(0x420C, r.randrange(256))
        # des instructions, dans un etat de M et X connu
        for _ in range(r.randrange(10, 80)):
            k = r.random()
            if k < 0.04:					# M, X au hasard, ou le mode emulation
                m8, x8 = r.randrange(2), r.randrange(2)
                p = (0x20 if m8 else 0) | (0x10 if x8 else 0)
                if r.random() < 0.25:
                    op(0x38, 0xFB)				# sec ; xce
                    m8 = x8 = 1
                    emu = True
                else:
                    op(0x18, 0xFB, 0xC2, 0x30, 0xE2, p)
                    emu = False
                state = (m8, x8)
                body(c, r, ops, state, emu, loop, here)
                known()
                continue
            body(c, r, ops, (1, 0), False, loop, here, 1)
    known()
    if seed & 2:						# STP, au bout d'un moment
        op(0xEE, 0x00, 0x1E, 0xAD, 0x00, 0x1E, 0xC9, 0x20, 0xD0, 1, 0xDB)
    op(0x4C, loop & 255, loop >> 8)
    # les interruptions : long, pour ne pas dependre de DB

    def irq_common(ack):
        op(0xC2, 0x30, 0x48, 0xDA, 0x5A)			# rep #$30 ; pha ; phx ; phy
        op(0xE2, 0x20)
        op(0xAF, ack & 255, ack >> 8, 0x00)			# lda long
        op(0xEE, 0x10, 0x03)					# inc $0310 (selon DB)
        op(0xA9, r.randrange(1, 16), 0x8F, 0x00, 0x21, 0x00)	# lda ; sta long $2100
        op(0xC2, 0x30, 0x7A, 0xFA, 0x68, 0x40)			# rep #$30 ; ply ; plx ; pla ; rti
    nmi = here()
    irq_common(0x4210)
    irq = here()
    irq_common(0x4211)
    brk = here()
    op(0x40)
    assert len(c) <= 0x5000
    c.extend([0xEA] * (0x5000 - len(c)))
    c += data
    assert len(c) < 0x7FB0
    c.extend(r.randrange(256) for _ in range(0x7FC0 - len(c)))
    # l'en-tete
    h = bytearray(b"ARMPI SNES STRESS    ")
    h.append(0x21 if hirom else 0x20)
    h.append(0x02)						# ROM + RAM + pile
    h.append(0x08)						# 256 Ko
    h.append(0x03)						# 8 Ko
    h.append(0x02 if pal else 0x01)
    h.append(0x00)
    h.append(0x00)
    h.extend((0, 0, 0, 0))
    v = bytearray(32)

    def vec(i, a):
        v[i] = a & 255
        v[i + 1] = a >> 8
    vec(0x04, brk)						# COP
    vec(0x06, brk)						# BRK
    vec(0x0A, nmi)
    vec(0x0E, irq)
    vec(0x14, brk)
    vec(0x1A, nmi)
    vec(0x1C, org)
    vec(0x1E, irq)
    bank0 = bytes(c) + bytes(h) + bytes(v)
    assert len(bank0) == 0x8000
    size = 0x40000
    rnd = bytearray(r.randrange(256) for _ in range(size))
    if hirom:
        rom = bytearray(rnd[:0x8000] + bank0 + rnd[0x10000:])
        at = 0xFFC0
    else:
        rom = bytearray(bank0 + rnd[0x8000:])
        at = 0x7FC0
    s = sum(rom) + 0x1FE
    s &= 0xFFFF
    rom[at + 0x1C] = (s ^ 0xFFFF) & 255
    rom[at + 0x1D] = (s ^ 0xFFFF) >> 8
    rom[at + 0x1E] = s & 255
    rom[at + 0x1F] = s >> 8
    return bytes(rom)


def body(c, r, ops, state, emu, loop, here, count=None):
    """Des codes 65C816 tires au hasard, M et X donnes par state."""
    m8, x8 = state

    def imm(n):
        c.extend(r.randrange(256) for _ in range(n))

    def ad():
        a = snes_target(r)
        c.extend((a & 255, a >> 8))
    for _ in range(count or r.randrange(5, 40)):
        k = r.random()
        if k < 0.04:						# un saut conditionnel
            d = r.randrange(0, 10)
            o = r.choice((0x10, 0x30, 0x50, 0x70, 0x90, 0xB0, 0xD0, 0xF0, 0x80))
            c.extend((o, d))
            c.extend([0xEA] * d)
            continue
        if k < 0.05:						# BRL, JMP, JML, JSR + RTS, JSL + RTL, indirects
            t = r.randrange(10)
            if t == 0:
                c.extend((0x82, 2, 0, 0xEA, 0xEA))
            elif t == 1:
                n = here() + 3 + 2
                c.extend((0x4C, n & 255, n >> 8, 0xEA, 0xEA))
            elif t == 2:
                n = here() + 4 + 2
                c.extend((0x5C, n & 255, n >> 8, 0, 0xEA, 0xEA))
            elif t == 5:
                c.extend((0x3B, 0x1B))				# tsc ; tcs
            elif t == 6:
                at = here() + 3				# jmp (at) ; at: suite
                n = at + 2
                c.extend((0x6C, at & 255, at >> 8, n & 255, n >> 8))
            elif t == 7:
                ldx0 = (0xA2, 0) + (() if x8 else (0,))
                c.extend(ldx0)
                at = here() + 3
                n = at + 2
                c.extend((r.choice((0x7C,)), at & 255, at >> 8, n & 255, n >> 8))
            elif t == 8:
                c.extend((0xA2, 0) + (() if x8 else (0,)))
                at = here() + 5				# jsr (at,x) ; bra + ; at: rts' ; rts ; +
                n = at + 2
                c.extend((0xFC, at & 255, at >> 8, 0x80, 3, n & 255, n >> 8, 0x60))
            elif t == 9:
                at = here() + 3				# jml [at] ; at: suite
                n = at + 3
                c.extend((0xDC, at & 255, at >> 8, n & 255, n >> 8, 0))
            elif t == 3:
                n = here() + 3 + 2				# jsr + ; bra ++ ; + rts
                c.extend((0x20, n & 255, n >> 8, 0x80, 1, 0x60))
            else:
                n = here() + 4 + 2
                c.extend((0x22, n & 255, n >> 8, 0, 0x80, 1, 0x6B))
            continue
        if k < 0.07:						# la pile, equilibree
            t = r.randrange(9)
            c.extend(((0x48, 0x68), (0xDA, 0xFA), (0x5A, 0x7A), (0x08, 0x28), (0x8B, 0xAB), (0x4B, 0xAB),
                      (0x0B, 0x2B), (0xF4, 0x34, 0x12, 0x68, 0x68 if m8 else 0xEA),
                      (0x62, 0, 0, 0xFA, 0xFA if x8 else 0xEA))[t])
            continue
        if k < 0.08:						# PEI, BRK, COP, WDM
            t = r.randrange(3)
            if t == 0:
                c.extend((0xD4, r.randrange(256), 0xFA, 0xFA if x8 else 0xEA))
            elif t == 1:
                c.extend((0x00, r.randrange(256)))
            else:
                c.extend((0x02, r.randrange(256)))
            continue
        if k < 0.09:						# un bloc
            c.extend((0xA9, 3) + ((0,) if not m8 else ()))
            if m8:
                c.extend((0xEB, 0xA9, 0, 0xEB))			# xba ; lda #0 ; xba
            c.extend((r.choice((0x54, 0x44)), r.choice((0x7E, 0x7F, 0x00)), r.choice((0x00, 0x7E, 0x01))))
            c.extend((0x8B, 0xAB) if not emu else ())		# (DB a change)
            continue
        if k < 0.095:						# WAI : la NMI reveille
            c.extend((0xA9, 0x81) + (() if m8 else (0,)))
            c.extend((0x8F, 0x00, 0x42, 0x00, 0xCB))
            continue
        o = r.choice(ops)
        c.append(o)
        n = snes_len(o)
        if n == "m":
            imm(1 if m8 else 2)
        elif n == "x":
            imm(1 if x8 else 2)
        elif n == 2:
            imm(1)
        elif n == 3:
            ad()
        elif n == 4:
            ad()
            c.append(r.choice((0x00, 0x7E, 0x7F, 0x80, 0x01, 0xC0, 0x70)))


# ---- GBA ---------------------------------------------------------------------------------------

class AGba:
    """Assemble en ARM et Thumb, dans une ROM a l'adresse 0x08000000."""

    def __init__(self, r):
        self.b = bytearray()
        self.r = r

    def here(self):
        return 0x08000000 + len(self.b)

    def w(self, v):
        self.b.extend(struct.pack("<I", v & 0xFFFFFFFF))

    def h(self, v):
        self.b.extend(struct.pack("<H", v & 0xFFFF))

    def mov32(self, rd, v):
        """mov rd,#octet ; orr rd,rd,#octet, ... (ARM)"""
        self.w(0xE3A00000 | rd << 12 | (v & 255))
        for sh in (8, 16, 24):
            b = (v >> sh) & 255
            if b:
                self.w(0xE3800000 | rd << 16 | rd << 12 | (32 - sh) // 2 << 8 | b)

    def st16(self, off, v):
        """IO [off] = v (r10 = 0x04000000)"""
        self.mov32(0, v)
        self.mov32(1, 0x04000000 + off)
        self.w(0xE1C100B0)					# strh r0,[r1]

    def st32(self, a, v):
        self.mov32(0, v)
        self.mov32(1, a)
        self.w(0xE5810000)					# str r0,[r1]


def gba_regs(a, r):
    """Un etat des registres : r0-r7 petits, r8-r12 des adresses utiles."""
    for i in range(8):
        a.mov32(i, r.choice((r.randrange(256), r.randrange(1 << 32), 0x02000000 + r.randrange(0x1000),
                             0x03000000 + r.randrange(0x4000) & ~1)))
    a.mov32(8, 0x02000000 + r.randrange(0x20000))
    a.mov32(9, 0x03000000 + r.randrange(0x4000))
    a.mov32(10, 0x04000000)
    a.mov32(11, 0x06000000 + r.randrange(0x10000))
    a.mov32(12, 0x05000000 + r.randrange(0x200))


GBA_IO_OK = [o for o in range(0, 0x60, 2) if o not in (4,)] + list(range(0x60, 0xA8, 2)) + [0x100, 0x102, 0x104, 0x106, 0x108, 0x10A, 0x10C, 0x10E, 0x130, 0x204]


def arm_op(a, r):
    cond = r.choice((14, 14, 14, r.randrange(15))) << 28
    k = r.random()
    rd = r.randrange(8)
    rn = r.randrange(13)
    rm = r.randrange(13)
    rs = r.randrange(13)
    if k < 0.45:						# traitement de donnees
        opc = r.randrange(16)
        s = r.randrange(2)
        if opc in (8, 9, 10, 11):
            s = 1
        if r.randrange(3) == 0:
            a.w(cond | 1 << 25 | opc << 21 | s << 20 | rn << 16 | rd << 12 | r.randrange(1 << 12))
        elif r.randrange(3) == 0:
            a.w(cond | opc << 21 | s << 20 | rn << 16 | rd << 12 | rs << 8 | r.randrange(4) << 5 | 1 << 4 | rm)
        else:
            a.w(cond | opc << 21 | s << 20 | rn << 16 | rd << 12 | r.randrange(32) << 7 | r.randrange(4) << 5 | rm)
    elif k < 0.52:						# MUL, MLA, longs
        s = r.randrange(2)
        t = r.randrange(2)
        rd2 = r.randrange(8)
        if t:
            a.w(cond | r.randrange(2) << 21 | s << 20 | rd << 16 | rn << 12 | rs << 8 | 9 << 4 | rm)
        else:
            a.w(cond | 1 << 23 | r.randrange(4) << 21 | s << 20 | rd << 16 | rd2 << 12 | rs << 8 | 9 << 4 | rm)
    elif k < 0.72:						# LDR, STR : les bases r8-r12
        base = r.randrange(8, 13)
        pre, up, wb = r.randrange(2), r.randrange(2), r.randrange(4) == 0
        by = r.randrange(2)
        l = r.randrange(2)
        if wb and base == 10:
            wb = False
        if base == 10:
            o = r.choice(GBA_IO_OK) + r.randrange(2)
            a.w(cond | 1 << 26 | 1 << 24 | 1 << 23 | by << 22 | l << 20 | base << 16 | rd << 12 | o)
        else:
            a.w(cond | 1 << 26 | pre << 24 | up << 23 | by << 22 | (1 if wb else 0) << 21 | l << 20 | base << 16 | rd << 12 | r.randrange(0x400))
    elif k < 0.82:						# LDRH, STRH, LDRSB, LDRSH
        base = r.choice((8, 9, 11, 12))
        sh = r.randrange(1, 4)
        l = r.randrange(2) if sh == 1 else 1
        o = r.randrange(256)
        if l and r.randrange(3) == 0:				# decalage par registre, en lecture
            a.w(cond | r.randrange(2) << 24 | r.randrange(2) << 23 | l << 20 | base << 16 | rd << 12 | 1 << 7 | sh << 5 | 1 << 4 | r.randrange(8))
        else:
            a.w(cond | r.randrange(2) << 24 | r.randrange(2) << 23 | 1 << 22 | l << 20 | base << 16 | rd << 12 | (o >> 4) << 8 | 1 << 7 | sh << 5 | 1 << 4 | (o & 15))
    elif k < 0.90:						# LDM, STM
        base = r.choice((8, 9, 11, 12))
        l = r.randrange(2)
        lst = r.randrange(1, 256) if l else r.choice((r.randrange(1, 256), 0))	# (LDM vide : un saut)
        a.w(cond | 1 << 27 | r.randrange(2) << 24 | r.randrange(2) << 23 | (0 if l else r.randrange(2)) << 22 | r.randrange(2) << 21 | l << 20 | base << 16 | lst)
    elif k < 0.93:						# SWP
        base = r.choice((8, 9))
        a.w(cond | 1 << 24 | r.randrange(2) << 22 | base << 16 | rd << 12 | 9 << 4 | r.randrange(8))
    elif k < 0.96:						# MRS, MSR sur les drapeaux
        if r.randrange(2):
            a.w(cond | 0x010F0000 | r.randrange(2) << 22 | rd << 12)
        else:
            a.w(cond | 0x0328F000 | r.randrange(256) | r.randrange(16) << 8)
    else:							# un saut par-dessus
        n = r.randrange(0, 4)
        a.w(cond | 0x0A000000 | r.randrange(2) << 24 | n)
        for _ in range(n):
            a.w(0xE1A00000)
        a.w(0xE1A00000)


def thumb_op(a, r):
    k = r.random()
    rd, rs, rb = r.randrange(8), r.randrange(8), r.randrange(8)
    if k < 0.25:
        a.h(r.randrange(3) << 11 | r.randrange(32) << 6 | rs << 3 | rd)
    elif k < 0.33:
        a.h(0x1800 | r.randrange(4) << 9 | r.randrange(8) << 6 | rs << 3 | rd)
    elif k < 0.48:
        a.h(0x2000 | r.randrange(4) << 11 | rd << 8 | r.randrange(256))
    elif k < 0.70:
        a.h(0x4000 | r.randrange(16) << 6 | rs << 3 | rd)
    elif k < 0.76:						# registres hauts
        t = r.randrange(3)
        h1 = r.randrange(2)
        h2 = r.randrange(2)
        if t == 2 and h1 and rd >= 5:
            rd = 0
        if t == 0 and h1 and rd >= 5:
            rd = 0
        a.h(0x4400 | t << 8 | h1 << 7 | h2 << 6 | rs << 3 | rd)
    elif k < 0.80:
        a.h(0x4800 | rd << 8 | r.randrange(256))
    elif k < 0.86:						# charges par registre, r6 + r7 : RAM
        a.h(0x5000 | r.randrange(8) << 9 | r.choice((3, 7)) << 6 | 6 << 3 | rd)
    elif k < 0.90:
        a.h(0x6000 | r.randrange(4) << 11 | r.randrange(32) << 6 | 6 << 3 | rd)
    elif k < 0.93:
        a.h(0x8000 | r.randrange(2) << 11 | r.randrange(32) << 6 | 6 << 3 | rd)
    elif k < 0.95:						# SP
        a.h(0x9000 | r.randrange(2) << 11 | rd << 8 | r.randrange(64))
    elif k < 0.97:
        a.h(0xA000 | r.randrange(2) << 11 | rd << 8 | r.randrange(256))
    elif k < 0.98:
        lst = r.randrange(1, 32)
        a.h(0xB400 | lst)
        a.h(0xBC00 | lst)
    elif k < 0.99:
        n = r.randrange(4)
        a.h(0xD000 | r.randrange(14) << 8 | n)
        for _ in range(n):
            a.h(0x46C0)
        a.h(0x46C0)
    else:
        t = r.randrange(6)
        n = r.randrange(4)
        if t == 0:
            a.h(0xC000 | 6 << 8 | r.randrange(1, 32))		# STMIA
        elif t == 1:
            a.h(0xC800 | 6 << 8 | r.randrange(1, 16))		# LDMIA
        elif t == 2:
            v = r.randrange(128)
            a.h(0xB000 | v)					# ADD SP
            a.h(0xB080 | v)
        elif t == 3:
            a.h(0xE000 | n)					# B
            for _ in range(n + 1):
                a.h(0x46C0)
        elif t == 4:
            a.h(0xF000)						# BL
            a.h(0xF800 | n)
            for _ in range(n):
                a.h(0x46C0)
        else:
            a.h(0xDF06)						# SWI Div


def gba_ppu_section(a, r):
    mode = r.randrange(6)
    disp = mode | r.randrange(2) << 4 | r.randrange(2) << 6 | 0x1F00 | r.choice((0, 0, 0x2000, 0x4000, 0x6000, 0xE000))
    a.st16(0, disp & 0xFFFF)
    a.st16(0x4, 0x28 | 0x10 | r.randrange(160) << 8)
    for o in (0x08, 0x0A, 0x0C, 0x0E):
        a.st16(o, r.randrange(0x10000))
    for o in range(0x10, 0x20, 2):
        a.st16(o, r.randrange(0x200))
    for o in (0x20, 0x22, 0x24, 0x26, 0x30, 0x32, 0x34, 0x36):
        a.st16(o, r.choice((0x100, r.randrange(0x10000))))
    for o in (0x28, 0x2A, 0x2C, 0x2E, 0x38, 0x3A, 0x3C, 0x3E):
        a.st16(o, r.randrange(0x1000))
    for o in range(0x40, 0x56, 2):
        a.st16(o, r.randrange(0x10000))


def gba_swi(a, r, seed):
    """Un appel au BIOS, en ARM, avec des arguments qui ont un sens."""
    n = r.choice((1, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 0x19, 0x1F, 0x20))
    wram = 0x02000000 + r.randrange(0x30000)
    iw = 0x03000000 + r.randrange(0x1000)
    src = 0x08100000 + r.randrange(0x10000) * 4
    vr = 0x06000000 + r.randrange(0x8000) * 2
    args = {
        1: (0x1C,), 4: (1, 1), 5: (), 6: (r.randrange(1 << 32), r.choice((0, r.randrange(1 << 32), r.randrange(1, 100)))),
        7: (r.randrange(1 << 32), r.randrange(1 << 32)), 8: (r.randrange(1 << 32),), 9: (r.randrange(0x10000),),
        10: (r.randrange(0x10000), r.randrange(0x10000)), 11: (src, wram, r.randrange(1, 64) | r.choice((0, 1 << 24, 1 << 26, 5 << 24))),
        12: (src, wram, r.randrange(1, 64) | r.choice((0, 1 << 24))), 13: (), 14: (src, iw, 2), 15: (src, iw, 2, 2),
        16: (src, wram, 0x08180000), 17: (0x08180100, wram), 18: (0x08180100, vr), 19: (0x08180200, wram), 20: (0x08180300, wram),
        21: (0x08180300, vr), 22: (0x08180400, wram), 23: (0x08180400, vr), 24: (0x08180400, wram), 0x19: (), 0x1F: (src, r.randrange(256), r.randrange(256)),
        0x20: ()}[n]
    for i, v in enumerate(args):
        a.mov32(i, v)
    a.w(0xEF000000 | n << 16)


def gba_stress(seed, save):
    r = random.Random(seed)
    a = AGba(r)
    a.w(0xEA00002E)						# b 0xC0
    a.b.extend(bytes(0xA0 - 4))
    a.b.extend(b"ARMPI STRESS")
    a.b.extend(b"ASTE01")
    a.b.extend(bytes(0xC0 - len(a.b)))
    # les interruptions : en tete, 0x03007FFC -> irq
    a.w(0xEA000000)						# b +1 : par-dessus la suivante
    a.w(0xE1A00000)
    n = a.here()
    a.w(0)							# (b over, plus bas)
    irq = a.here()
    a.mov32(0, 0x04000200)					# accuser, noter pour IntrWait
    a.w(0xE1D010B2)						# ldrh r1,[r0,#2]
    a.w(0xE1C010B2)						# strh r1,[r0,#2]
    a.mov32(2, 0x03007FF8)
    a.w(0xE1D230B0)						# ldrh r3,[r2]
    a.w(0xE1833001)						# orr r3,r3,r1
    a.w(0xE1C230B0)						# strh r3,[r2]
    a.w(0xE12FFF1E)						# bx lr
    a.b[n - 0x08000000:n - 0x08000000 + 4] = struct.pack("<I", 0xEA000000 | ((a.here() - (n + 8)) >> 2))
    a.st32(0x03007FFC, irq)
    a.st16(0x200, 0x3FFF)					# IE
    a.st16(0x208, 1)
    a.st16(0x4, 0x38 | 80 << 8)
    # VRAM, palette, OAM par DMA 3, depuis la ROM au hasard
    for dst, n in ((0x06000000, 0x4000), (0x05000000, 0x100), (0x07000000, 0x100)):
        a.st32(0x040000D4, 0x08100000)
        a.st32(0x040000D8, dst)
        a.st32(0x040000DC, (0x8400 << 16) | (n & 0xFFFF))
    # le son : Direct Sound sur le minuteur 0, DMA 1 et 2
    a.st16(0x84, 0x80)
    a.st16(0x82, 0x0B0E | 0x3300)
    a.st16(0x80, 0xFF77)
    a.st32(0x040000BC, 0x08110000)
    a.st32(0x040000C0, 0x040000A0)
    a.st16(0xC6, 0xB640)
    a.st32(0x040000C8, 0x08118000)
    a.st32(0x040000CC, 0x040000A4)
    a.st16(0xD2, 0xB640)
    a.st16(0x100, 0xFF00)
    a.st16(0x102, 0x80)
    a.st16(0x104, 0xFFF0)
    a.st16(0x106, 0xC4 | r.randrange(4))
    a.st16(0x108, 0xFFFF & r.randrange(0x10000))
    a.st16(0x10A, 0xC0 | r.randrange(4))
    loop = a.here()
    limit = 0xC0000
    while len(a.b) < limit:
        gba_regs(a, r)
        k = r.random()
        if k < 0.25:
            gba_ppu_section(a, r)
            gba_regs(a, r)
        elif k < 0.40:						# le PSG
            for _ in range(8):
                o = r.randrange(0x60, 0x90) & ~1
                a.st16(o, r.randrange(0x10000))
            a.st16(0x64, 0x8000 | r.randrange(0x800))
            a.st16(0x6C, 0x8000 | r.randrange(0x800))
            a.st16(0x74, 0x8000 | r.randrange(0x800))
            a.st16(0x7C, 0x8000 | r.randrange(0x100))
            a.st16(0x70, 0x80 | r.randrange(2) << 6 | r.randrange(2) << 5)
            a.st16(0x90, r.randrange(0x10000))
            gba_regs(a, r)
        elif k < 0.50:
            gba_swi(a, r, seed)
            gba_regs(a, r)
        elif k < 0.55:						# DMA 3 : immediat, tous les modes
            a.st32(0x040000D4, r.choice((0x08100000, 0x02000100, 0x03000100)))
            a.st32(0x040000D8, r.choice((0x02001000, 0x03001000, 0x06002000)))
            a.st32(0x040000DC, r.randrange(1, 0x40) | (0x8000 | r.randrange(2) << 10 | r.randrange(3) << 7 | r.randrange(3) << 5) << 16)
            gba_regs(a, r)
        elif k < 0.60 and save:					# la sauvegarde
            if save == "eeprom":
                a.st32(0x040000D4, 0x08100000)
                a.st32(0x040000D8, 0x0D000000)
                a.st32(0x040000DC, r.choice((9, 17, 73, 81)) | 0x8000 << 16)
                a.mov32(0, 0x0D000000)
                for b in (1, 1):
                    a.mov32(1, b)
                    a.w(0xE1C010B0)
                for _ in range(6):
                    a.mov32(1, r.randrange(2))
                    a.w(0xE1C010B0)
                a.mov32(1, 0)
                a.w(0xE1C010B0)
                a.w(0xE1D010B0)
            else:
                u = ((0x5555, 0xAA), (0x2AAA, 0x55))
                t = r.randrange(5)
                x = r.randrange(0x10000)
                seq = {0: u + ((0x5555, 0x90), (0, 0), (1, 0)) + u + ((0x5555, 0xF0),), 1: u + ((0x5555, 0xA0), (x, r.randrange(256))),
                       2: u + ((0x5555, 0xB0), (0, r.randrange(2))), 3: u + ((0x5555, 0x80),) + u + ((x, 0x30),),
                       4: u + ((0x5555, 0x80),) + u + ((0x5555, 0x10),)}[t] if save != "sram" else ((x & 0x7FFF, r.randrange(256)),)
                for at, v in seq:
                    a.mov32(1, 0x0E000000 + at)
                    a.mov32(0, v)
                    a.w(0xE5C10000)				# strb r0,[r1]
                a.w(0xE5D10000)
                gba_regs(a, r)
        elif k < 0.70:						# les modes du processeur, leurs banques
            m = r.choice((0x11, 0x12, 0x13, 0x17, 0x1B, 0x1F))
            a.w(0xE10F0000)					# mrs r0,cpsr
            a.w(0xE1A02000)					# mov r2,r0
            a.w(0xE3C0001F)
            a.w(0xE3800000 | m | 0xC0)
            a.w(0xE129F000)					# msr cpsr_fc,r0
            a.mov32(8, r.randrange(1 << 32))
            a.mov32(13, 0x03006000 + r.randrange(0x100) * 4)
            a.w(0xE14F1000)					# mrs r1,spsr
            a.w(0xE169F001)					# msr spsr_fc,r1
            a.w(0xE92D0003)					# stmfd sp!,{r0,r1}
            a.w(0xE8BD0003)
            a.w(0xE129F002)					# msr cpsr_fc,r2
            gba_regs(a, r)
        # ARM
        for _ in range(r.randrange(10, 60)):
            arm_op(a, r)
        # Thumb : BX, puis retour par BX
        a.w(0xE28F0001)						# add r0,pc,#1
        a.w(0xE12FFF10)						# bx r0
        n = r.randrange(10, 60)
        a.h(0x2601)						# (r6, r7 : la RAM)
        a.h(0x0636)						# lsl r6,r6,#24 -> 0x01000000
        a.h(0x0076)						# lsl r6,r6,#1 -> 0x02000000
        a.h(0x2700)
        for _ in range(n):
            thumb_op(a, r)
        if len(a.b) & 2:
            a.h(0x46C0)
        a.h(0x4778)						# bx pc
        a.h(0x46C0)
    a.w(0xEA000000 | (((loop - (a.here() + 8)) >> 2) & 0xFFFFFF))
    d = bytearray(a.b)
    assert len(d) < 0x100000
    d.extend(bytes(0x100000 - len(d)))
    d.extend(r.randrange(256) for _ in range(0x20000))		# 0x08100000 : au hasard
    d.extend(bytes(0x180000 - len(d)))
    # 0x08180000 : les donnees des decompressions
    z = bytearray(0x1000)
    z[0:8] = struct.pack("<HBBI", 16, 2, 8, 0x80000001)		# BitUnPack
    lz = bytearray(struct.pack("<I", 0x10 | 0x100 << 8))
    while len(lz) < 0xF0:
        lz.extend((0x28, 1, 2, 0x10, 0x00, 3, 0x20, 0x01, 4, 5, 6))
    z[0x100:0x100 + len(lz)] = lz
    hf = bytearray(struct.pack("<I", 0x20 | 8 | 0x40 << 8))
    hf.extend((3, 0xC0, 1, 2, 3, 4, 5, 6))
    hf.extend(r.randrange(256) for _ in range(0x60))
    z[0x200:0x200 + len(hf)] = hf
    rl = bytearray(struct.pack("<I", 0x30 | 0x40 << 8))
    rl.extend((0x83, 0x55, 0x03, 1, 2, 3, 4, 0x90, 0xAA, 0x20) + tuple(r.randrange(256) for _ in range(33)))
    z[0x300:0x300 + len(rl)] = rl
    df = bytearray(struct.pack("<I", 0x81 | 0x40 << 8))
    df.extend(r.randrange(256) for _ in range(0x40))
    z[0x400:0x400 + len(df)] = df
    d += z
    d.extend(bytes(0x200000 - len(d)))
    if save:
        tag = {"sram": b"SRAM_V113", "flash": b"FLASH_V126", "flash1m": b"FLASH1M_V103", "eeprom": b"EEPROM_V124"}[save]
        d[0x1FFF00:0x1FFF00 + len(tag)] = tag
    return bytes(d)


# ---- N64 : des instructions R4300 au hasard, en boucle, pour l'interprete et le recompilateur ----
# r20, r21, r22 : les bases des acces (kseg0, kseg1, useg par le TLB), r23 : le mot de code qu'on recrit,
# r25 : les registres du RCP, r24 : de passage, k0 k1 : le traitant, qui saute l'instruction fautive.

N64_DST = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 28, 29, 30, 31]
N64_SRC = N64_DST + [0, 20, 21, 22]


def m_r(op, rs, rt, rd, sa, fn):
    return (op << 26) | (rs << 21) | (rt << 16) | (rd << 11) | (sa << 6) | fn


def m_i(op, rs, rt, imm):
    return (op << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFF)


def m_li(reg, v):
    return [m_i(0x0F, 0, reg, v >> 16), m_i(0x0D, reg, reg, v)]


def n64_simple(r):
    """une instruction sans branchement"""
    d, a, b = r.choice(N64_DST), r.choice(N64_SRC), r.choice(N64_SRC)
    sa = r.randrange(32)
    imm = r.choice([r.randrange(0x10000), r.randrange(16), 0xFFFF, 0x8000, 0x7FFF, 0])
    k = r.randrange(100)
    if k < 22:
        return m_r(0, a, b, d, 0, r.choice([0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x2F]))
    if k < 30:
        return m_r(0, 0, b, d, sa, r.choice([0, 2, 3, 0x38, 0x3A, 0x3B, 0x3C, 0x3E, 0x3F]))
    if k < 34:
        return m_r(0, a, b, d, 0, r.choice([4, 6, 7, 0x14, 0x16, 0x17]))
    if k < 42:
        return m_i(r.choice([8, 9, 0xA, 0xB, 0xC, 0xD, 0xE, 0x18, 0x19]), a, d, imm)
    if k < 44:
        return m_i(0xF, 0, d, imm)
    if k < 50:
        return m_r(0, a, b, 0, 0, r.choice([0x18, 0x19, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E, 0x1F]))
    if k < 54:
        return r.choice([m_r(0, 0, 0, d, 0, 0x10), m_r(0, 0, 0, d, 0, 0x12), m_r(0, a, 0, 0, 0, 0x11), m_r(0, a, 0, 0, 0, 0x13)])
    if k < 74:
        base = r.choice([20, 20, 20, 21, 21, 22])
        off = r.randrange(0x10000) & r.choice([0xFFF8, 0xFFF8, 0xFFFC, 0xFFFE, 0xFFFF, 0x00FF])
        if r.randrange(2):
            return m_i(r.choice([0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x1A, 0x1B, 0x30, 0x34, 0x37]), base, d, off)
        return m_i(r.choice([0x28, 0x29, 0x2A, 0x2B, 0x2C, 0x2D, 0x2E, 0x3F, 0x38, 0x3C]), base, b, off)
    if k < 92:
        return n64_fpu(r, d, a)
    if k < 94:
        return m_r(0, a, b, 0, r.randrange(32), r.choice([0x30, 0x31, 0x32, 0x33, 0x34, 0x36]))
    if k < 95:
        return m_i(1, a, r.choice([8, 9, 0xA, 0xB, 0xC, 0xE]), imm)
    if k < 96:
        return r.choice([0xC, 0xD, 0xF]) | (r.randrange(1024) << 6)
    if k < 97:
        return m_i(0x2F, 20, r.randrange(32), r.randrange(0x1000))
    if k < 98:
        return m_i(0x23, 25, d, r.choice([0x10, 0x10, 0x14, 0x18]))
    # COP0
    if r.randrange(2):
        return m_r(0x10, r.choice([0, 1]), d, r.choice([0, 1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 30]), 0, 0)
    if r.randrange(2):
        return m_r(0x10, 4, a, r.choice([0, 1, 4, 6, 8, 9, 11, 13, 14, 15, 16, 30]), 0, 0)
    if r.randrange(2):
        return r.choice([0x48000000, 0x4C000000, 0x4A000000, 0xCC000000, 0x7C000000, 0x0000003D | (r.randrange(32) << 21), 0x04040000, 0x42000000 | 0x3F])
    return 0x42000000 | r.choice([1, 2, 6, 8])


def n64_fpu(r, d, a):
    fs, ft, fd = r.randrange(32), r.randrange(32), r.randrange(32)
    if r.randrange(4):
        fs &= 30
        ft &= 30
        fd &= 30
    k = r.randrange(100)
    if k < 40:
        return m_r(0x11, r.choice([16, 17]), ft, fs, fd, r.choice(list(range(8)) + list(range(8, 16)) + [0x20, 0x21, 0x24, 0x25]))
    if k < 50:
        return m_r(0x11, r.choice([20, 21]), 0, fs, fd, r.choice([0x20, 0x21]))
    if k < 66:
        return m_r(0x11, r.choice([16, 17]), ft, fs, 0, 0x30 + r.randrange(16))
    if k < 82:
        return m_r(0x11, r.choice([0, 1, 4, 5]), r.choice([d, a]), fs, 0, 0)
    if k < 86:
        return m_r(0x11, 2, d, r.choice([0, 31]), 0, 0)
    off = r.randrange(0x10000) & r.choice([0xFFF8, 0xFFF8, 0xFFFC])
    base = r.choice([20, 20, 21, 22])
    return m_i(r.choice([0x31, 0x35, 0x39, 0x3D]), base, ft, off)


N64_IO = [(0xA4600000, "ram"), (0xA4600004, "cart"), (0xA4600008, "len"), (0xA460000C, "len"), (0xA4600010, 3),
          (0xA4600014, "b"), (0xA4600018, "b"), (0xA460001C, "b"), (0xA4600020, "b"),
          (0xA4500000, "ram"), (0xA4500004, "len8"), (0xA4500008, 1), (0xA450000C, 0), (0xA4500010, "b"), (0xA4500014, 15),
          (0xA430000C, "mask"), (0xA4300000, "mask"), (0xA4800000, "ram"), (0xA4800004, 0x1FC007C0), (0xA4800010, 0x1FC007C0),
          (0xA4800018, 0), (0xA4040000, "sp"), (0xA4040004, "ram"), (0xA4040008, "len8"), (0xA404000C, "len8"),
          (0xA4040010, "spst"), (0xA404001C, 0), (0xA4080000, "len"), (0xA4100000, "ram"), (0xA4100004, "ram"), (0xA410000C, "mask"),
          (0xA4700000, "b"), (0xA4700010, "b"), (0xA3F00000, "b"), (0xA4400000, "vit"), (0xA4400008, "vw"), (0xA440000C, "b"),
          (0xA4400010, 0), (0xA4400018, "vl"), (0xA4400030, 0x200), (0xA4400034, 0x400), (0xA4400024, 0x006C0200), (0xA4400028, 0x002501FF),
          (0xA4001000, "b"), (0xA8000000, "b"), (0xBFC007C0, "b"), (0xBFC007FC, 1), (0xBFC007FC, 8)]
N64_RD = [0xA4040010, 0xA404001C, 0xA4080000, 0xA410000C, 0xA4300008, 0xA430000C, 0xA4400010, 0xA4400000, 0xA4500004, 0xA450000C,
          0xA4600010, 0xA4600008, 0xA4600024, 0xA4700004, 0xA4800018, 0xA4800000, 0xA3F00000, 0xA4000010, 0xA4001000,
          0xB0000000, 0xB0001234, 0xB2000000, 0xA8000000, 0xA8000100, 0xA6000000, 0xBFC007C0, 0xBFC007FC, 0xBFC00000, 0xA5000000]


def n64_io(r):
    c = []
    d = r.choice(N64_DST)
    if r.randrange(2):
        a = r.choice(N64_RD)
        c += [m_i(0xF, 0, 24, a >> 16)]
        return c + [m_i(r.choice([0x23, 0x23, 0x20, 0x21, 0x24, 0x25, 0x37, 0x27]), 24, d, a & 0xFFFF)] if a & 0x8000 == 0 else c + [m_i(0xD, 24, 24, a & 0xFFFF), m_i(0x23, 24, d, 0)]
    a, v = r.choice(N64_IO)
    if v == "ram":
        v = 0x180000 + r.randrange(0x10000) * 8
    elif v == "cart":
        v = r.choice([0x10001000 + r.randrange(0x10000) * 2, 0x08000000 + r.randrange(0x1000) * 2, 0x05000000, 0x1FFF0000])
    elif v == "len":
        v = r.choice([r.randrange(0x800), 7, 127, 128, 129, 0x1FF])
    elif v == "len8":
        v = r.randrange(0x200) * 8 | r.choice([0, 0x1000])
    elif v == "mask":
        v = r.randrange(0x4000)
    elif v == "b":
        v = r.getrandbits(32) & r.choice([0xFF, 0xFFFFFFFF])
    elif v == "sp":
        v = r.randrange(0x400) * 8
    elif v == "spst":
        v = r.getrandbits(25) & ~1
    elif v == "vit":
        v = r.choice([0, 2, 3, 0x42, 0x43])
    elif v == "vw":
        v = r.choice([0, 320, 640])
    elif v == "vl":
        v = r.choice([0x20D, 0x271, 0x100, 50])
    c += m_li(24, v) + m_li(1, a)
    c.append(m_i(r.choice([0x2B, 0x2B, 0x2B, 0x28, 0x29, 0x3F]), 1, 24, 0))
    return c


def n64_branch(r, n):
    a, b = r.choice(N64_SRC), r.choice(N64_SRC)
    k = r.randrange(5)
    if k == 0:
        return m_i(r.choice([4, 5, 0x14, 0x15]), a, b, n)
    if k == 1:
        return m_i(r.choice([6, 7, 0x16, 0x17]), a, 0, n)
    if k == 2:
        return m_i(1, a, r.choice([0, 1, 2, 3, 0x10, 0x11, 0x12, 0x13]), n)
    if k == 3:
        return m_r(0x11, 8, r.randrange(4), 0, 0, 0) | n
    return m_i(4, 0, 0, n)


def n64_stress(seed):
    r = random.Random(seed)
    def init(patch):
        c = []
        for v in (0x80000000, 0x80000080, 0x80000180):
            c += m_li(1, v)
            for k, w in enumerate((0x401A7000, 0x275A0004, 0x409A7000, 0x401B6800, 0, 0x42000018)):
                c += m_li(2, w)
                c.append(m_i(0x2B, 1, 2, k * 4))
        c += m_li(20, 0x80100000) + m_li(21, 0xA0200000) + m_li(22, 0x00300000) + m_li(23, patch) + m_li(25, 0xA4400000)
        for n in N64_DST:
            c += m_li(n, r2.getrandbits(32))
        for n in range(0, 32):
            c += m_li(24, r2.choice([r2.getrandbits(32), 0x3F800000, 0x40490FDB, 0xC1200000, 0x7F800000, 0xFFC00000, 0x4F000000, 0]))
            c.append(m_r(0x11, 4, 24, n, 0, 0))
        return c
    r2 = random.Random(seed)
    n0 = len(init(0))
    start = 0x80001000 + (n0 + 2) * 4
    body = []
    patch = None
    while len(body) < 3000:
        k = r.randrange(100)
        if k == 0:
            v = m_i(9, 1, 1, r.randrange(0x10000))
            body += m_li(24, v) + [m_i(0x2B, 23, 24, 0)]
            if patch is None:
                patch = start + (len(body) + 4) * 4
                body += [0, 0, 0, 0, m_i(9, 1, 1, 7), 0]
        elif k == 1:
            body += m_li(24, r.choice([0, 1, 2, 3, 0x1000, 0x1000000 | 1]))
            body.append(m_r(0x11, 6, 24, 31, 0, 0))
        elif k == 2:
            body += m_li(24, r.choice([0, 0x01000000, 0x02000000, 0x03000000]) | 0x30000000 if r.randrange(4) == 0 else r.choice([0x34000000, 0x34000000, 0x34008001]))
            body.append(m_r(0x10, 4, 24, 12, 0, 0))
        elif k == 3:
            body += m_li(24, 0x00300000 + (r.randrange(0x40) << 13) | r.randrange(4))
            body.append(m_r(0x10, 4, 24, 10, 0, 0))
            body += m_li(24, (0x200 + r.randrange(0x80)) << 6 | r.choice([0x1E, 0x06, 0x02, 0x1F]))
            body.append(m_r(0x10, 4, 24, 2, 0, 0))
            body += m_li(24, (0x280 + r.randrange(0x80)) << 6 | r.choice([0x1E, 0x06, 0x02, 0x1F]))
            body.append(m_r(0x10, 4, 24, 3, 0, 0))
            body += m_li(24, r.choice([0, 0, 0x6000, 0x1E000]))
            body.append(m_r(0x10, 4, 24, 5, 0, 0))
            body += m_li(24, r.randrange(32))
            body.append(m_r(0x10, 4, 24, 0, 0, 0))
            body.append(0x42000000 | r.choice([2, 2, 6]))
        elif k == 4:
            body += [m_i(1, 0, 0x11, 1), 0, m_i(9, 31, 31, 12), m_r(0, 31, 0, 30, 0, r.choice([8, 9])), n64_simple(r)]
        elif k < 10:
            body += n64_io(r)
        elif k < 12:
            body += [0x0C000000 if k == 10 else 0x08000000, 0]
            at = (start + (len(body) + 2) * 4) & 0x0FFFFFFF
            body[-2] |= at >> 2
            body[-1] = n64_simple(r)
            body.append(n64_simple(r))
        elif k < 22:
            n = 1 + r.randrange(4)
            body.append(n64_branch(r, n))
            body += [n64_simple(r) for _ in range(n + 1)]
        else:
            body.append(n64_simple(r))
    if patch is None:
        patch = start + 4
    r2 = random.Random(seed)
    code = init(patch)
    code += m_li(1, 0)
    loop = start
    body += [0x08000000 | ((loop & 0x0FFFFFFF) >> 2), 0]
    code += body
    d = bytearray(0x101000)
    d[0:4] = bytes((0x80, 0x37, 0x12, 0x40))
    d[8:12] = struct.pack(">I", 0x80001000)
    d[0x20:0x2B] = b"N64 STRESS "
    for i, w in enumerate(code):
        d[0x1000 + i * 4:0x1004 + i * 4] = struct.pack(">I", w & 0xFFFFFFFF)
    return bytes(d)


def main():
    c = sys.argv[1]
    if c == "nes-stress":
        d = stress(int(sys.argv[2]), int(sys.argv[3]), "chr-ram" in sys.argv, "pal" in sys.argv)
        out = sys.argv[4]
    elif c == "nes-demo":
        d = demo()
        out = sys.argv[2]
    elif c == "gba-stress":
        d = gba_stress(int(sys.argv[2]), sys.argv[4] if len(sys.argv) > 4 else None)
        out = sys.argv[3]
    elif c == "n64-stress":
        d = n64_stress(int(sys.argv[2]))
        out = sys.argv[3]
    elif c == "snes-stress":
        d = snes_stress(int(sys.argv[2]), "hirom" in sys.argv, "pal" in sys.argv)
        out = sys.argv[3]
    else:
        sys.exit("commande inconnue")
    with io.open(out, "wb") as f:
        f.write(d)


main()
