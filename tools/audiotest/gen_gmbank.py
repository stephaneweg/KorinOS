"""gen_gmbank.py -- la banque General MIDI en FM d'AudioKit (src/kits/audiokit/ak_gmbank.inc) : le
synthe MIDI s'en sert quand aucune SoundFont n'est sur la carte. Les instruments viennent des .FMI
de FM Tracker d'Onyx (sdcard/apps/fmtracker.app/ins, instruments de l'utilisateur) ; les 128
programmes General MIDI et les percussions du canal 10 sont associes ci-dessous.

  python -I tools/audiotest/gen_gmbank.py $ONYX/sdcard/apps/fmtracker.app/ins src/kits/audiokit/ak_gmbank.inc
"""
import os
import sys

GM = """PIANO PIANO BELPIANO BELPIANO ELPIANO ELPIANO HARPSI HARPSI
CELESTA VIBRA BELLS VIBRA MARIMBA XYLOFONE BELLS BANJO
ORGAN ORGAN ORGAN PIPES ORGAN ACCORDN HARMONCA ACCORDN
GUITAR GUITAR ELGUITAR ELGUITAR ELGUITAR ELGUITAR ELGUITAR HARP
BASSHARP POPBASS POPBASS POPBASS SLAPBAS SLAPBAS POPBASS SLAPBAS
VIOLIN ALTOVIOL CELLO CONTRAB STRINGS HARP HARP TOM
STRINGS STRINGS STRINGS STRINGS FLUTE FLUTE SYNTH1 BRASS
TRUMPET TROMBONE TUBA TRUMPET FREHORN BRASS SFTBRASS SFTBRASS
SAXAFONE SAXAFONE TENSAX TENSAX OBOE ENGLHORN BASSOON CLARINET
PICCOLO FLUTE FLUTE FLUTE PIPES FLUTE FLUTE FLUTE
SQUARE SYNTH2 SYNTH1 SYNTH1 ELGUITAR SYNTH1 SYNTH2 SYNTH2
SYNTH1 STRINGS SYNTH2 STRINGS STRINGS SFTBRASS STRINGS SYNTH1
MOON BELLS MARS MOON ALIEN MOON MARS MOON
SITAR BANJO BANJO HARP VIBRA BAGPIPES VIOLIN OBOE
BELLS TINCAN STEEL LOGDRUM TOM TOM SYNDRUM CYMBAL
SCRATCH NOISEON NOISEIN CHIRP DRIP DRILL NOISEIN LASER1""".replace("STEEL", "TINCAN").split()
assert len(GM) == 128

# touche -> instrument, touche jouee (35..81)
DR = {35: "BDRUM1:40", 36: "BDRUM2:40", 37: "CLAVES:72", 38: "SNARE1:60", 39: "BRUSH:72", 40: "SNARE2:60",
      41: "TOM:41", 42: "HIHAT:84", 43: "TOM:45", 44: "HIHAT:84", 45: "TOM:48", 46: "HIHAT:80", 47: "TOM:52",
      48: "TOM:55", 49: "CYMBAL:72", 50: "TOM:59", 51: "CYMBAL:84", 52: "CYMBAL:64", 53: "TRIANGLE:84",
      54: "TINCAN:84", 55: "CYMBAL:79", 56: "TINCAN:72", 57: "CYMBAL:69", 58: "CHIRP:60", 59: "CYMBAL:88",
      60: "LOGDRUM:72", 61: "LOGDRUM:67", 62: "LOGDRUM:64", 63: "LOGDRUM:60", 64: "LOGDRUM:55",
      65: "TOM:60", 66: "TOM:55", 67: "TINCAN:76", 68: "TINCAN:69", 69: "BRUSH:84", 70: "BRUSH:88",
      71: "CHIRP:84", 72: "CHIRP:76", 73: "CLAVES:84", 74: "CLAVES:79", 75: "CLAVES:91", 76: "LOGDRUM:84",
      77: "LOGDRUM:79", 78: "CHIRP:72", 79: "CHIRP:67", 80: "TRIANGLE:96", 81: "TRIANGLE:91"}

MIN_RR = 6
ORDER = ["AR", "DR", "SL", "RR", "TL", "KSL", "VIB", "AM", "MULT", "KSR", "WAVE", "EGT"]


def read_fmi(path):
    lines = [l.strip().strip('"') for l in open(path, encoding="latin-1").read().splitlines() if l.strip()]
    assert lines[0].startswith("fm-song instrument"), path
    p = {}
    for k, l in zip(ORDER, lines[2:14]):
        p[k] = [int(x) for x in l.split(",")]
    fb, con = [int(x) for x in lines[14].split(",")]
    # (un relachement de 0 ne finit jamais, comme sur l'OPL2 : un tracker coupe ses notes lui-meme,
    # pas un fichier MIDI -- les voix de la banque s'eteignent en un peu plus d'une seconde)
    for o in range(2):
        p["RR"][o] = max(p["RR"][o] & 15, MIN_RR)
    out = []
    for o in range(2):
        fl = (1 if p["EGT"][o] else 0) | (2 if p["AM"][o] else 0) | (4 if p["VIB"][o] else 0) | (8 if p["KSR"][o] else 0)
        out += [p["AR"][o] & 15, p["DR"][o] & 15, p["SL"][o] & 15, p["RR"][o] & 15, p["TL"][o] & 63, p["KSL"][o] & 3,
                p["MULT"][o] & 15, p["WAVE"][o] & 3, fl, 0]
    return out + [fb & 7, con & 1, 0, 0]


dirn, dest = sys.argv[1], sys.argv[2]
names = []
for n in GM + [v.split(":")[0] for v in DR.values()]:
    if n not in names:
        names.append(n)
o = ["; genere par tools/audiotest/gen_gmbank.py : ne pas modifier",
     "; (instruments .FMI de FM Tracker d'Onyx ; un instrument : AKFI_SIZE octets)",
     "ak_gm_prog:\t\t\t\t; programme -> instrument"]
idx = [names.index(n) for n in GM]
for i in range(0, 128, 16):
    o.append("\tdb " + ",".join(map(str, idx[i:i + 16])))
o.append("ak_gm_drum:\t\t\t\t; touche 35..81 -> instrument, touche jouee")
for k in range(35, 82):
    n, t = DR[k].split(":")
    o.append("\tdb %d,%s\t\t\t; %d" % (names.index(n), t, k))
o.append("ak_gm_ins:")
for n in names:
    v = read_fmi(os.path.join(dirn, n + ".FMI"))
    o.append("\tdb " + ",".join(map(str, v)) + "\t; " + n)
o.append("\talign 4")
with open(dest, "w", newline="") as f:
    f.write("\n".join(o) + "\n")
print(len(names), "instruments")
