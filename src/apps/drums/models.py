# models.py -- écrit les modèles de rythmes (.drum) de la Boîte à rythmes dans un dossier
#   python3 -I src/apps/drums/models.py src/apps/drums/models
# (la construction les pose dans /home/public/DrumMachine : le paquet drums-samples)
# Une piste : une note de percussion (GM/GS, 27..87) et ses pas : « x » une note, « X » un accent,
# « . » rien. Un motif plus court que le rythme est répété (une mesure recopiée dans les suivantes).
# Kits : programmes MIDI de la banque 128 de GeneralUser GS.
import os
import sys

KICK, ACKICK, RIM, SNARE, CLAP, ESNARE = 36, 35, 37, 38, 39, 40
HHC, HHP, HHO = 42, 44, 46
TOMLL, TOMLH, TOML, TOMML, TOMMH, TOMH = 41, 43, 45, 47, 48, 50
CRASH, RIDE, CHINA, RIDEBELL, SPLASH, CRASH2 = 49, 51, 52, 53, 55, 57
TAMB, COWBELL, VIBRA = 54, 56, 58
BONGOH, BONGOL, CONGAMH, CONGAOH, CONGAL, TIMBH, TIMBL = 60, 61, 62, 63, 64, 65, 66
AGOGOH, AGOGOL, CABASA, MARACAS = 67, 68, 69, 70
WHISTLE_S, WHISTLE_L, GUIRO_S, GUIRO_L, CLAVES, WOODH, WOODL = 71, 72, 73, 74, 75, 76, 77
CUICA_M, CUICA_O, TRI_M, TRI_O = 78, 79, 80, 81
SHAKER, JINGLE, BELLTREE, CASTANETS, SURDO_M, SURDO_O = 82, 83, 84, 85, 86, 87
STICKS = 31

STANDARD, STANDARD2, ROOM, POWER, ELECTRONIC, TR808, DANCE, JAZZ, BRUSH, ORCHESTRA = 0, 1, 8, 16, 24, 25, 26, 32, 40, 48

SPB = {"3/4": 12, "4/4": 16, "5/4": 20, "6/8": 12, "12/8": 24}

# (fichier, tempo, swing, signature, mesures, kit, [(note, motif), ...])
MODELS = [
    ("01-rock", 112, 0, "4/4", 4, POWER, [
        (KICK,  "X.......x.x....." * 3 + "X.......x.x.x..."),
        (SNARE, "....X.......X..." * 3 + "....X.....x.XxXx"),
        (HHC,   "X.x.x.x.X.x.x.x." * 3 + "X.x.x.x.X.x....."),
        (HHO,   "................"),
        (TOMH,  "................" * 3 + "..........x....."),
        (TOML,  "................" * 3 + "............x.x."),
        (CRASH, "X..............." + "................" * 3),
    ]),
    ("02-pop", 100, 0, "4/4", 2, STANDARD, [
        (KICK,   "X.....x.x......." + "X.....x.x.....x."),
        (SNARE,  "....X.......X..."),
        (CLAP,   "....x.......x..."),
        (HHC,    "X.x.X.x.X.x.X.x."),
        (HHO,    "................" + "..............x."),
        (CABASA, ".x.x.x.x.x.x.x.x"),
        (CRASH,  "X..............." + "................"),
    ]),
    ("03-funk", 98, 18, "4/4", 2, STANDARD, [
        (KICK,    "X..x..x...x..x.." + "X..x..x...x.x..."),
        (SNARE,   "....X..x.x..X..x" + "....X..x.x..X.x."),
        (HHC,     "XxxxXxxxXxxxXxx." + "XxxxXxxxXxxxXx.."),
        (HHO,     "...............x" + "..............x."),
        (COWBELL, "x.......x......."),
        (TAMB,    "....x.......x..."),
    ]),
    ("04-disco", 120, 0, "4/4", 2, ROOM, [
        (KICK,    "X...X...X...X..."),
        (CLAP,    "....X.......X..."),
        (SNARE,   "....x.......x..."),
        (HHC,     "x...x...x...x..."),
        (HHO,     "..X...X...X...X."),
        (TAMB,    "xxxxxxxxxxxxxxxx"),
        (CONGAOH, "......x.......x." + "......x.....x.x."),
        (CRASH,   "X..............." + "................"),
    ]),
    ("05-house", 124, 0, "4/4", 4, TR808, [
        (KICK,   "X...X...X...X..."),
        (CLAP,   "....x.......x..."),
        (HHC,    ".x.x.x.x.x.x.x.x"),
        (HHO,    "..X...X...X...X."),
        (SHAKER, "xxxxxxxxxxxxxxxx"),
        (RIDE,   "................" * 2 + "x.x.x.x.x.x.x.x." * 2),
        (CRASH,  "X..............." + "................" * 3),
    ]),
    ("06-techno", 132, 0, "4/4", 2, ELECTRONIC, [
        (KICK,  "X...X...X...X..."),
        (CLAP,  "....x.......x..."),
        (HHO,   "..x...x...x...x."),
        (HHC,   "xx.xxx.xxx.xxx.x"),
        (RIM,   "...x.....x.....x" + "...x.....x..x..."),
        (TOMH,  "................" + "..............x."),
        (WOODH, "......x.......x." + "..x...x.......x."),
    ]),
    ("07-hip-hop", 90, 28, "4/4", 2, STANDARD2, [
        (KICK,  "X......xx.x....." + "X......x..x..x.."),
        (SNARE, "....X.......X..."),
        (HHC,   "x.x.x.x.x.x.x.xx" + "x.x.x.x.x.x.x.x."),
        (RIM,   "..........x....." + "......x........."),
        (HHO,   "................" + "..............x."),
    ]),
    ("08-trap", 70, 0, "4/4", 2, TR808, [
        (KICK,  "X......x..x....." + "X.....x...x..x.."),
        (SNARE, "........X......."),
        (CLAP,  "........X......."),
        (HHC,   "x.x.x.x.xxx.x.x." + "x.x.x.x.x.x.xxxx"),
        (HHO,   "..............x." + "................"),
        (CRASH, "X..............." + "................"),
    ]),
    ("09-drum-and-bass", 174, 0, "4/4", 2, DANCE, [
        (KICK,   "X.........X....." + "..X..x....x....."),
        (SNARE,  "....X.......X..." + "....X..x....X..."),
        (HHC,    "x.x.x.x.x.x.x.x."),
        (RIDE,   "..x...x...x...x."),
        (SHAKER, "xxxxxxxxxxxxxxxx"),
        (CRASH,  "X..............." + "................"),
    ]),
    ("10-breakbeat", 104, 10, "4/4", 2, ROOM, [
        (KICK,  "X.........X....." + "..X.x.....x....."),
        (SNARE, "....X..x.x..X..x" + "....X..x.x..X..."),
        (HHC,   "x.x.x.x.x.x.x.x."),
        (HHO,   "................" + "..............x."),
        (CRASH, "X..............." + "................"),
    ]),
    ("11-motown", 116, 8, "4/4", 2, ROOM, [
        (KICK,  "X.x.....X.x....."),
        (SNARE, "X...X...X...X..."),
        (TAMB,  "....X.......X..."),
        (HHC,   "x.x.x.x.x.x.x.x."),
        (JINGLE, "................" + "............x..."),
    ]),
    ("12-valse", 150, 0, "3/4", 2, BRUSH, [
        (KICK,  "X..........." + "X..........."),
        (SNARE, "....x...x..." + "....x...x.x."),
        (HHP,   "....x...x..."),
        (RIDE,  "x...x...x..."),
        (TRI_O, "X..........." + "............"),
    ]),
    ("13-ballade-6-8", 66, 0, "6/8", 4, STANDARD, [
        (KICK,  "X.........x." + "X..........." + "X.........x." + "X.....x....."),
        (SNARE, "......X....."),
        (HHC,   "x.x.x.x.x.x."),
        (TOML,  "............" * 3 + "......x.x.xx"),
        (CRASH, "X..........." + "............" * 3),
    ]),
    ("14-blues-shuffle-12-8", 72, 0, "12/8", 2, BRUSH, [
        (KICK,  "X...........X..........." + "X...........X.....x....."),
        (SNARE, "......X...........X....."),
        (RIDE,  "X...x.x...x.X...x.x...x."),
        (HHP,   "......x...........x....."),
    ]),
    ("15-jazz-swing", 138, 33, "4/4", 2, JAZZ, [
        (RIDE,     "X...x.xX...x.xX." + "X...x.xX...x.x.."),
        (HHP,      "....x.......x..."),
        (KICK,     "x...x...x...x..."),
        (SNARE,    "..........x....." + "......x......x.x"),
        (RIDEBELL, "................" + "..............X."),
    ]),
    ("16-jazz-5-4", 170, 33, "5/4", 2, JAZZ, [
        (RIDE,  "X...x.xX...x.x.X...x"),
        (HHP,   "....x.......x......."),
        (KICK,  "X.......x.....x....."),
        (SNARE, "..........x...x....." + "......x...x...x...x."),
    ]),
    ("17-bossa-nova", 140, 0, "4/4", 2, BRUSH, [
        (KICK,    "x..xx..xx..xx..x"),
        (RIM,     "x..x..x...x..x.." + "..x..x...x..x..."),
        (HHC,     "x.x.x.x.x.x.x.x."),
        (SHAKER,  "XxxxXxxxXxxxXxxx"),
        (CLAVES,  "................"),
    ]),
    ("18-samba", 100, 0, "4/4", 2, STANDARD, [
        (SURDO_O, "........X......." + "........X......."),
        (SURDO_M, "X..............."),
        (KICK,    "x..xx..xx..xx..x"),
        (SNARE,   "x.xx.x.xx.x.x.xx"),
        (TAMB,    "XxxxXxxxXxxxXxxx"),
        (AGOGOH,  "X..x..x........." + "..x..x...x......"),
        (AGOGOL,  "..........x.x..." + "X.........x.x..."),
        (CUICA_O, "......x.......x." + "..x.....x.....x."),
        (WHISTLE_S, "................" + "..............X."),
    ]),
    ("19-salsa", 96, 0, "4/4", 2, STANDARD, [
        (CLAVES,  "x..x..x........." + "....x.x...x....."),
        (COWBELL, "X.x.X.x.X.xxX.x."),
        (CONGAOH, "............x.x." + "............x.x."),
        (CONGAMH, "....x.......x..."),
        (CONGAL,  "......xx......xx"),
        (BONGOH,  "x.xx.x.xx.xx.x.x"),
        (TIMBH,   "................" + "..........xxx.x."),
        (GUIRO_S, "x.x.x.x.x.x.x.x."),
        (KICK,    "...x.......x...."),
    ]),
    ("20-reggae-one-drop", 76, 30, "4/4", 2, STANDARD, [
        (KICK,  "........X......."),
        (RIM,   "........X......."),
        (HHC,   "x.X.x.X.x.X.x.X."),
        (HHO,   "..............x." + "................"),
        (TOMH,  "................" + "............x.x."),
    ]),
    ("21-reggaeton", 96, 0, "4/4", 2, TR808, [
        (KICK,    "X...X...X...X..."),
        (SNARE,   "...x..x....x..x."),
        (RIM,     "...x..x....x..x."),
        (HHC,     "x.x.x.x.x.x.x.x."),
        (TIMBH,   "................" + "..........x.x.x."),
    ]),
    ("22-afrobeat", 112, 12, "4/4", 2, STANDARD, [
        (KICK,    "X.....x...X.x..." + "X.....x...x....."),
        (SNARE,   "....x...x...x.x." + "....x..x..x...x."),
        (HHC,     "x.xxx.xxx.xxx.xx"),
        (HHO,     "..x.......x....."),
        (COWBELL, "x.x.xx.x.x.xx.x."),
        (SHAKER,  ".x.x.x.x.x.x.x.x"),
        (CONGAL,  "......x.......xx"),
    ]),
    ("23-flamenco-rumba", 108, 0, "4/4", 2, ROOM, [
        (CASTANETS, "x.xxx.xxx.xxx.xx" + "x.xxx.xxx.x.xxxx"),
        (CLAP,      "X..X..X...X.X..." + "X..X..X...X.X.X."),
        (CONGAMH,   "x.......x......."),
        (CONGAL,    "...x.x.....x.x.."),
        (KICK,      "X.......X......."),
    ]),
    ("24-electro-808", 128, 0, "4/4", 4, TR808, [
        (KICK,    "X..x..X...X..x.."),
        (CLAP,    "....X.......X..."),
        (HHC,     "xxXxxxXxxxXxxxXx"),
        (TOMH,    "......x.......x."),
        (COWBELL, "...x.......x...."),
        (CLAVES,  "................" * 2 + "..x.....x....x.." * 2),
        (CRASH,   "X..............." + "................" * 3),
    ]),
    ("25-marche-orchestre", 112, 0, "4/4", 2, ORCHESTRA, [
        (KICK,    "X.......X......." + "X.......X...X..."),
        (SNARE,   "X..xX..xX.xxX..." + "X..xX..xX.xxXxxx"),
        (CRASH,   "....X.......X..."),
        (TRI_O,   "X..............." + "................"),
        (TIMBL,   "................" + "............X..."),
    ]),
]


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "."
    os.makedirs(out, exist_ok=True)
    for name, tempo, swing, sig, bars, kit, tracks in MODELS:
        steps = SPB[sig] * bars
        lines = ["# Boîte à rythmes de Korin OS", f"tempo = {tempo}", f"swing = {swing}", f"signature = {sig}",
                 f"bars = {bars}", f"steps = {steps}", f"kit = {kit}", "volume = 80"]
        seen = set()
        for note, pat in tracks:
            assert note not in seen, (name, note)
            seen.add(note)
            assert steps % len(pat) == 0 and len(pat) % SPB[sig] == 0, (name, note, len(pat))
            pat = pat * (steps // len(pat))
            lines.append(f"track = {note} 100 0 0 {pat}")
        with open(os.path.join(out, name + ".drum"), "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")
    print(len(MODELS), "modèles dans", out)


main()
