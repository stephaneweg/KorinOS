---
name: korin-koton
description: Écrire une chanson pour Koton, le studio de composition de Korin, en écrivant son fichier .kson (JSON) - le tempo, la tonalité, la piste d'accords (l'harmonie), les pistes d'instruments General MIDI et de batterie, les blocs (accords joués, riffs note à note, batterie par style, lignes mélodiques, anneaux euclidiens, générateurs) et les greffons (pluck, fm2, subsynth, delay, reverb...). À charger dès qu'on demande une chanson, un arrangement ou un morceau pour Koton, ou qu'on veut lire ou changer un .kson.
user-invocable: true
---

# Écrire une chanson pour Koton (le fichier .kson)

Koton est un studio : des **pistes**, et sur chacune des **blocs** posés les uns après les autres. La **piste
d'accords** donne l'harmonie. Les autres pistes s'y règlent : une guitare « joue l'accord en bossa », une ligne
mélodique prend ses notes dans l'accord du moment. On peut aussi écrire les notes une à une dans un **riff**. Une
chanson est un fichier **`.kson`** : du JSON, que l'on écrit au mieux par un script Python.

## Où sont les choses

| Chemin | Contenu |
|---|---|
| `/home/public/koton/demos/*.kson` | cinq morceaux qui passent par tous les blocs et tous les greffons : **lisez celui qui ressemble à la demande** |
| `HOME:/koton/songs/` (`/home/koton/songs`) | les chansons de l'utilisateur ; écrivez-y les vôtres |
| `/apps/koton.app/plugins/<id>/plugin.json` | chaque greffon : ses paramètres, leurs bornes et leurs valeurs par défaut |
| `/home/public/soundfonts/*.sf2`, puis `/res/soundfonts/*.sf2` | la SoundFont : d'abord le dossier partagé entre les comptes (`PUBLIC:/soundfonts`, où chacun peut en copier), puis celle du système (paquet `generaluser-gs`) ; sans elle, Koton joue en FM |

Pour faire entendre le résultat, ouvrez-le sans bloquer le shell :

```sh
(/apps/koton.app/main /home/koton/songs/ma-chanson.kson >/dev/null 2>&1 &)
```

L'utilisateur lit la chanson (Espace) et l'exporte (Fichier > Exporter en WAV). Vous ne l'entendez pas : dites ce
que vous avez écrit (grille, instruments, structure), demandez-lui son avis et retouchez le fichier.

## Les unités

* **Un temps = une noire.** Positions et durées des blocs sont en temps.
* **Positions relatives** : un bloc commence là où finit le précédent de sa piste, plus son `SilenceBefore` (en
  temps). Une piste qui entre à la mesure 9 (4/4) a un premier bloc avec `"SilenceBefore": 32`.
* **Notes : 0 à 95, 0 = do 0 = MIDI 12.** Do 4 (le do du milieu, MIDI 60) vaut donc 48.
* **Riffs : 24 tranches par temps** (`SlicesPerQuarter`) ; une croche dure 12 tranches.
* Instruments : le programme General MIDI, de 0 à 127 (0 piano, 24 guitare nylon, 32 contrebasse, 48 cordes...).

## Le fichier

```json
{"Format": "koton-onyx", "Version": 1,
 "Project": {
   "Tempo": [{"Beat": 0, "Bpm": 132}],
   "Key": {"TonicLetter": 3, "Accidental": 0, "Mode": 0, "FullMode": -1},
   "TimeSigNum": 4, "TimeSigDen": 4, "PickupBeats": 0, "TimeSigScale": 1, "MinBeats": 0,
   "SwingPercent": 50, "HumanizePercent": 20, "AgogicPercent": 0, "OnyxReverbMix": 0.3,
   "Markers": [{"Beat": 0, "Name": "Thème"}],
   "UserChordStyles": [], "UserMelodicLines": [], "UserDrumStyles": [],
   "Tracks": [ ... ]},
 "Riffs": [ ... ]}
```

* `Key` : `TonicLetter` 0 do, 1 ré, 2 mi, 3 fa, 4 sol, 5 la, 6 si ; `Accidental` -1 bémol, 0, 1 dièse ; `Mode` 0
  majeur, 1 mineur. `FullMode` (-1 : d'après `Mode`) choisit un mode : 0 ionien, 1 éolien, 2 mineur harmonique,
  3 mineur mélodique, 4 dorien, 5 phrygien, 6 lydien, 7 mixolydien, 8 locrien.
* `SwingPercent` : 50 droit, 66 ternaire. `HumanizePercent` : le jeu un peu humain.
* Une mesure à 3/4 : `TimeSigNum` 3.

### Une piste

```json
{"Name": "Guitare", "Type": 0, "Instrument": 24, "DrumKit": 0, "Volume": 0.8, "Pan": -0.3,
 "Mute": false, "Solo": false, "ReverbOffset": 0, "Collapsed": false, "ReverbBusSend": 0,
 "VolumeAutomation": [], "AutomationLanes": [], "Items": [{"SilenceBefore": 0, "Module": {...}}]}
```

`Type` : 0 instrument, 1 batterie, 2 **accords** (une seule par chanson). `Volume` de 0 à 1, `Pan` de -1 à 1,
`ReverbOffset` s'ajoute à l'envoi de réverbération (40). `VolumeAutomation` : `[{"Beat": 0, "Volume": 0}, ...]`
pour un fondu. Greffons facultatifs : `"OnyxInstrument"` (un instrument à la place de la SoundFont) et
`"OnyxInserts"` (des effets, dans l'ordre).

### Les blocs

Chaque bloc a `"$type"`, un `"Id"` unique (un UUID), `"X": 0, "Y": 0, "WidthHint": 0, "Collapsed": false`, puis ses
champs :

| `$type` | Sur quelle piste | Ce qu'il joue | Champs principaux |
|---|---|---|---|
| `Pattern` | accords (ou instrument) | **un accord** | `Root` (0 do... 11 si), `Quality`, `Octave` (4), `Degree` (-1 : absolu), `BeatsPerBar` (sa durée en temps), `Repeats` (1), `Style` (0 : tenu), `VoiceLeadMode` (1), `Inversion` |
| `ChordArticulation` | instrument | l'accord de la piste d'accords, **à la manière** d'un style | `Style`, `Beats` (le motif, en temps : 4), `LengthBeats` (la durée du bloc), `Octave`, `Bass`, `VoiceLeadMode` (1) |
| `PlayRiff` | instrument | **un riff** de `Riffs`, note à note | `RiffId` |
| `DrumKit` | batterie | un rythme de batterie par style | `Style`, `Density` (0 auto, 1 léger, 2 normal, 3 dense), `Kit` (0), `FillLast` (un break à la fin), `BeatsPerBar` (4), `Repeats` (nombre de mesures) |
| `MelodicLine` | instrument | un **rythme** écrit, dont Koton choisit les notes dans l'harmonie | `BeatsPerBar` (durée totale), `VoiceCount` (1), `SlicesPerQuarter` (4), `Notes` (le rythme, `Note` 0), `Contour`, `Continuity`, `Amplitude`, `Ornaments` |
| `Cadence` | accords | une cadence toute faite | `CadenceStyle`, `Measures`, `ChordsPerMeasure`, `Chords` : voir la démo `neons` |
| `PolyDrum` | batterie | des **anneaux euclidiens** E(k, n) | `Beats`, `Repeats`, `Layers` : `Lane`, `Hits`, `Steps`, `Rotation`. Voir `rituel` |
| `PolyChord`, `MelodicPoly` | instrument | des anneaux sur les notes de l'accord | voir `aube-sur-le-lac` et `rituel` |
| `KotonGenerator` | instrument | un greffon générateur (`arp`, `euclid`, `automaton`) | `GeneratorId`, `GeneratorState` (base64 de `{"v":1,"params":{...}}`), `DurationBeats` |

Un riff, dans `"Riffs"` à la racine :
`{"Id": "...", "Name": "Thème", "SlicesPerQuarter": 24, "LengthSlices": 1536, "Notes": [{"Note": 72, "Start": 0,
"Length": 34, "Voice": 0, "GlideFromNote": 72, "GlideDurationSlices": 0, "Bend": null}]}`. `GlideFromNote`
reprend `Note`. Raccourcissez chaque note de deux tranches pour qu'elles se détachent.

**Qualités** (`Quality`) : 0 majeur, 1 mineur, 2 diminué, 3 augmenté, 4 sus2, 5 sus4, 6 maj7, 7 m7, 8 7, 9 m7♭5,
10 dim7, 11 6, 12 m6, 13 add9, 14 m(add9), 15 9, 16 maj9, 17 m9, 18 7♭9, 19 7♯9, 20 11, 21 13, 22 maj7♯11,
23 7sus4, 24 7sus2, 25 9sus4, 26 9sus2, 27 6sus4, 28 6sus2, 29 maj7sus4, 30 maj7sus2, 31 maj9sus4, 32 maj9sus2,
33 add9sus4, 34 7♯5.

**Styles d'accords** (`Style` de `Pattern` et de `ChordArticulation`) : 0 tenus, 1 en noires, 2 en croches,
3 arpège montant, 4 montant-descendant, 5 Alberti, 6 comping jazz (Charleston), 7 rock en croches, 8 pop (basse +
accord), 9 shuffle blues, 10 arpège descendant, 11 arpège en croches, 12 valse, 13 skank reggae, 14 marche,
15 tango, 16 bossa nova, 17 funk, 18 habanera (basse), 19 ballade, 20 country, 21 slow rock 12/8, 22 à 26 arpèges
rythmés, 27 harpe (accord roulé).

**Styles de batterie** (`Style` de `DrumKit`) : 0 rock, 1 rock appuyé, 2 pop, 3 funk, 4 disco, 5 jazz swing,
6 shuffle, 7 bossa nova, 8 demi-temps, 9 hip-hop, 10 marche, 11 reggae, 12 valse, 13 punk, 14 ballade, 15 trap.

**Lignes de batterie** (`Lane` de `PolyDrum`) : 0 grosse caisse, 1 caisse claire, 2 charleston fermé, 3 ouvert,
5 cross-stick, 6 mains, 7 à 9 toms, 10 crash, 11 ride, 22 tambourin, 23 cloche, 25 et 26 bongos, 27 à 29 congas,
34 cabasa, 35 maracas, 40 claves, 41 et 42 woodblocks.

**Greffons** : `{"Id": "reverb", "Enabled": true, "State": "{\"v\":1,\"params\":{\"room\":0.8,\"wet\":0.3}}"}` (le
`State` est du JSON dans une chaîne). Instruments : `pluck` (cordes pincées), `fm2`, `subsynth`. Effets : `reverb`,
`delay`, `chorus`, `drive`, `eq3`. Générateurs : `arp`, `euclid`, `automaton`. Les noms, bornes et valeurs par
défaut des paramètres sont dans leur `plugin.json`.

## Écrire une chanson par un script

Ce script (`python3 chanson.py`) écrit une chanson complète en fa majeur : grille, guitare en bossa, contrebasse,
batterie, et un thème de flûte écrit note à note. Changez la grille, les styles et les notes.

```python
import json, os, uuid

SPQ = 24
L = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}
riffs = []

def uid():
    return str(uuid.uuid4())

def note(s):                       # "F#5" -> 0..95 (do 0 = 0 ; do 4 = 48 = MIDI 60)
    n, rest = L[s[0]], s[1:]
    while rest and rest[0] in "#b":
        n += 1 if rest[0] == "#" else -1
        rest = rest[1:]
    return int(rest) * 12 + n

def module(kind, **f):
    m = {"$type": kind, "Id": uid(), "X": 0, "Y": 0, "WidthHint": 0, "Collapsed": False}
    m.update(f)
    return m

def item(m, silence=0):
    return {"SilenceBefore": silence, "Module": m}

def track(name, items, kind=0, program=0, volume=0.8, pan=0.0, reverb=0, inserts=None):
    t = {"Name": name, "Type": kind, "Instrument": program, "DrumKit": 0, "Volume": volume, "Pan": pan,
         "Mute": False, "Solo": False, "ReverbOffset": reverb, "Collapsed": False, "ReverbBusSend": 0.0,
         "VolumeAutomation": [], "AutomationLanes": [], "Items": items}
    if inserts:
        t["OnyxInserts"] = inserts
    return t

def plugin(pid, **params):
    return {"Id": pid, "Enabled": True, "State": json.dumps({"v": 1, "params": params}, separators=(",", ":"))}

def riff(name, notes, beats):      # notes : [("A5", début en temps, durée en temps)] -> un bloc PlayRiff
    rid = uid()
    riffs.append({"Id": rid, "Name": name, "SlicesPerQuarter": SPQ, "LengthSlices": round(beats * SPQ),
                  "Notes": [{"Note": note(n), "Start": round(s * SPQ), "Length": max(1, round(d * SPQ) - 2),
                             "Voice": 0, "GlideFromNote": note(n), "GlideDurationSlices": 0, "Bend": None}
                            for n, s, d in notes]})
    return module("PlayRiff", RiffId=rid)

def seq(pairs):                    # [("A5", durée) ou (None, durée) pour un silence] -> [(note, début, durée)]
    out, t = [], 0.0
    for n, d in pairs:
        if n:
            out.append((n, t, d))
        t += d
    return out

def chords(grid):                  # [("F", qualité, temps)] -> la piste d'accords
    items = []
    for r, q, b in grid:
        root = (L[r[0]] + (1 if "#" in r else -1 if "b" in r[1:] else 0)) % 12
        items.append(item(module("Pattern", Root=root, Octave=4, Quality=q, Degree=-1, BeatsPerBar=b, Repeats=1,
                                 VoiceLeadMode=1, Style=0)))
    return track("Accords", items, kind=2)

MAJ7, MIN7, DOM7, DOM9 = 6, 7, 8, 15
grid = [("F", MAJ7, 8), ("G", DOM9, 8), ("G", MIN7, 4), ("C", DOM7, 4), ("F", MAJ7, 8)] * 2
total = sum(b for _, _, b in grid)                     # 64 temps = 16 mesures
theme = seq([("C6", 1.5), ("A5", .5), ("G5", 1), ("A5", 1), ("E5", 3), (None, 1),
             ("D6", 1.5), ("B5", .5), ("A5", 1), ("B5", 1), ("A5", 3), (None, 1)])
tracks = [
    track("Guitare", [item(module("ChordArticulation", Beats=4, LengthBeats=total, Style=16, Octave=4,
                                  VoiceLeadMode=1, Bass=False))], program=24, pan=-0.3,
          inserts=[plugin("chorus", rate=0.6, depth=2.5, mix=0.35)]),
    track("Contrebasse", [item(module("ChordArticulation", Beats=4, LengthBeats=total, Style=18, Octave=2,
                                      VoiceLeadMode=1, Bass=False))], program=32, volume=0.85),
    track("Batterie", [item(module("DrumKit", Style=7, Density=1, Kit=0, FillLast=True, BeatsPerBar=4,
                                   Repeats=total // 4))], kind=1, volume=0.7),
    track("Flûte", [item(riff("Thème", theme, 16), 32)], program=73, volume=0.9, pan=0.15, reverb=15),
    chords(grid)]
doc = {"Format": "koton-onyx", "Version": 1,
       "Project": {"Tempo": [{"Beat": 0, "Bpm": 132}], "Tracks": tracks,
                   "UserChordStyles": [], "UserMelodicLines": [], "UserDrumStyles": [],
                   "Key": {"TonicLetter": 3, "Accidental": 0, "Mode": 0, "FullMode": -1},
                   "TimeSigNum": 4, "TimeSigDen": 4, "PickupBeats": 0, "TimeSigScale": 1, "MinBeats": 0,
                   "SwingPercent": 50, "HumanizePercent": 20, "AgogicPercent": 0,
                   "Markers": [{"Beat": 0, "Name": "Intro"}, {"Beat": 32, "Name": "Thème"}], "OnyxReverbMix": 0.3},
       "Riffs": riffs}
os.makedirs("/home/koton/songs", exist_ok=True)
with open("/home/koton/songs/ma-chanson.kson", "w", encoding="utf-8") as f:
    json.dump(doc, f, ensure_ascii=False, indent=1)
```

## Composer

* **Commencez par la grille** : elle fait l'harmonie de tout le morceau. Les blocs `ChordArticulation` la suivent
  seuls ; un `LengthBeats` égal à la durée de la grille couvre toute la chanson.
* **Une structure** : intro, thème, pont, reprise. Les `Markers` les nomment. Faites entrer et sortir les pistes par
  `SilenceBefore` et par plusieurs blocs, pas tout d'un coup.
* **Le style** : le tempo, la tonalité et le mode, les styles d'accords et de batterie, les instruments, le swing.
  Bossa : `Style` 16 et 18, batterie 7, guitare nylon. Valse : 3/4, `Style` 12, batterie 12. Jazz : septièmes et
  neuvièmes, comping 6, swing 5, `SwingPercent` 66.
* Écrivez la mélodie principale en riff (`PlayRiff`). Laissez Koton improviser les contre-chants
  (`MelodicLine`).
* Ne recopiez pas la mélodie d'une œuvre existante : écrivez-en une dans le style demandé.

## Vérifier

* Le fichier doit être du JSON valide : `python3 -c "import json; json.load(open('/home/koton/songs/ma-chanson.kson'))"`.
* Un champ inconnu ou absent prend sa valeur par défaut, sans erreur. Une faute de nom (`"LenghtBeats"`) ne se voit
  donc pas, sauf à l'écoute : comparez vos noms à ceux d'une démo.
* Chaque bloc a son `Id`, chaque `RiffId` désigne un riff de `Riffs`.
