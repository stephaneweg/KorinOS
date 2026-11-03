"""Une fois : genere src/apps/nesemu/nes_ops.inc et nes_mix.inc depuis les tables d'Onyx
(user/Emulators/nes/nes_optable.inc, nes_mix.inc).

    python -I tools/emu/gen_nes_tables.py $ONYX/user/Emulators/nes src/apps/nesemu
"""
import io
import re
import sys

src, dst = sys.argv[1], sys.argv[2]
t = io.open(src + "/nes_optable.inc", encoding="ascii").read()
ops = re.findall(r"\{ O_(\w+), M_(\w+), (\d+), (\d) \}", t)
assert len(ops) == 256
with io.open(dst + "/nes_ops.inc", "w", newline="") as f:
    f.write("; (genere : tools/emu/gen_nes_tables.py) numero, adressage, operation, cycles, +1 si page franchie\n")
    for i, (op, mode, cyc, cross) in enumerate(ops):
        f.write("opc %d, %s, %s, %s, %s\n" % (i, mode, op, cyc, cross))
t = io.open(src + "/nes_mix.inc", encoding="ascii").read()
tabs = re.findall(r"(MIX_\w+)\[\d+\] = \{(.*?)\}", t, re.S)
with io.open(dst + "/nes_mix.inc", "w", newline="") as f:
    f.write("; (genere : tools/emu/gen_nes_tables.py) le melangeur de l'APU : 95.52 / (8128 / n + 100) et 163.67 / (24329 / n + 100), x 40000\n")
    for name, body in tabs:
        v = re.findall(r"\d+", body)
        f.write(name.lower() + ":\n")
        for i in range(0, len(v), 16):
            f.write("\tdh " + ",".join(v[i:i + 16]) + "\n")
    f.write("\talign 4\n")
