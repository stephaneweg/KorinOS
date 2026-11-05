"""make_demos.py -- les morceaux d'exemple de Koton Studio (paquet koton-demos, SD0:/home/koton/demos) :
ecrits ici, de la musique a la note, puis enregistres en .kson (le format de Koton : src/apps/koton/engine/model.h).

    python -I tools/koton/make_demos.py          -> src/apps/koton/demos/*.kson

Ensemble, ils passent par tous les modules et tous les greffons :
  aube-sur-le-lac   accords (piste d'accords), articulation, PolyChord (cloches), riff ; greffon pluck, delay, reverb
  bossa-du-port     articulation bossa et habanera, batterie, riff, ligne melodique (le 2e chorus improvise) ; chorus
  neons             cadence, generateur arp, riffs ; greffons subsynth et fm2, drive, delay, eq3
  rituel            PolyDrum (anneaux euclidiens), generateurs euclid et automaton, MelodicPoly, ligne melodique
  petite-valse      3/4, articulation valse, modules Pattern sur une piste d'instrument, riff repris par l'accordeon
Notes : 0 = C0 = MIDI 12 ; un temps = une noire ; riffs a 24 tranches par temps.
"""
import base64
import json
import os
import uuid

OUT = os.path.join(os.path.dirname(__file__), "..", "..", "src", "apps", "koton", "demos")
SPQ = 24
NS = uuid.UUID("6b6f746f-6e2d-6465-6d6f-732d61726d70")	# (ids stables d'une generation a l'autre)
LETTERS = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}

# qualites (theory.cpp g_qualityNames)
MAJ, MIN, SUS4, MAJ7, MIN7, DOM7, M6, ADD9, DOM9, MAJ9, MIN9, D7B9, MAJ7S11, SEVSUS4 = 0, 1, 5, 6, 7, 8, 12, 13, 15, 16, 17, 18, 22, 23
# styles d'accords (gen_chords.cpp g_styleNames)
ST_HELD, ST_ARP_UPDOWN, ST_WALTZ, ST_BOSSA, ST_HABANERA, ST_BALLAD, ST_ARP_3_8, ST_HARP = 0, 4, 12, 16, 18, 19, 23, 27
# instruments General MIDI (programme)
PIANO, CELESTA, VIBES, MARIMBA, ACCORDION, NYLON, AC_BASS, STRINGS, CHOIR, OBOE, CLARINET, FLUTE, SHAKU, PAD_WARM, PAD_POLY = \
	0, 8, 11, 12, 21, 24, 32, 48, 52, 68, 71, 73, 77, 89, 90


class Song:
	def __init__ (self, name):
		self.name = name
		self.n = 0
		self.riffs = []

	def id (self, what):
		self.n += 1
		return str (uuid.uuid5 (NS, "%s/%s/%d" % (self.name, what, self.n)))


def note (s):
	"""'F#5' -> 0..95 (C0 = 0) ; 'Bb3'"""
	l, rest = s[0], s[1:]
	acc = 0
	while rest and rest[0] in "#b":
		acc += 1 if rest[0] == "#" else -1
		rest = rest[1:]
	return (int (rest) * 12) + LETTERS[l] + acc


def module (song, kind, **f):
	m = {"$type": kind, "Id": song.id (kind), "X": 0, "Y": 0, "WidthHint": 0, "Collapsed": False}
	m.update (f)
	return m


def item (m, silence = 0):
	return {"SilenceBefore": silence, "Module": m}


def riff (song, name, notes, beats):
	"""notes : [(nom, debut en temps, duree en temps)] -> l'id du riff"""
	rid = song.id ("riff")
	song.riffs.append ({"Id": rid, "Name": name, "SlicesPerQuarter": SPQ, "LengthSlices": int (round (beats * SPQ)),
	                    "Notes": [{"Note": note (n), "Start": int (round (s * SPQ)), "Length": max (1, int (round (d * SPQ)) - 2), "Voice": 0,
	                               "GlideFromNote": note (n), "GlideDurationSlices": 0, "Bend": None} for n, s, d in notes]})
	return rid


def seq (pairs, start = 0.0):
	"""[(nom ou None pour un silence, duree)] -> [(nom, debut, duree)]"""
	out, t = [], start
	for n, d in pairs:
		if n:
			out.append ((n, t, d))
		t += d
	return out


def play (song, rid):
	return module (song, "PlayRiff", RiffId = rid)


def plugin (pid, **params):
	return {"Id": pid, "Enabled": True, "State": json.dumps ({"v": 1, "params": params}, separators = (",", ":"))}


def generator (song, gid, beats, **params):
	st = json.dumps ({"v": 1, "params": params}, separators = (",", ":"))
	return module (song, "KotonGenerator", GeneratorId = gid, GeneratorState = base64.b64encode (st.encode ()).decode (), DurationBeats = beats)


def track (name, items, kind = 0, program = 0, volume = 1.0, pan = 0.0, reverb = 0, bus = 0.0, inst = None, inserts = None, fade = None):
	t = {"Name": name, "Type": kind, "Instrument": program, "DrumKit": 0, "Volume": volume, "Pan": pan, "Mute": False, "Solo": False,
	     "ReverbOffset": reverb, "Collapsed": False, "ReverbBusSend": bus, "VolumeAutomation": fade or [], "AutomationLanes": [], "Items": items}
	if inst:
		t["OnyxInstrument"] = inst
	if inserts:
		t["OnyxInserts"] = inserts
	return t


def chord_track (song, chords):
	"""[(fondamentale 'D', qualite, temps)] -> la piste d'accords (l'harmonie que lisent les autres)"""
	items = []
	for r, q, b in chords:
		root = (LETTERS[r[0]] + (1 if "#" in r else -1 if "b" in r[1:] else 0)) % 12
		items.append (item (module (song, "Pattern", Root = root, Octave = 4, Quality = q, Degree = -1, BeatsPerBar = b, Repeats = 1,
		                            VoiceLeadMode = 1, Style = ST_HELD)))
	return track ("Accords", items, kind = 2)


def articulation (song, style, beats, length, octave = 4, bass = False, **f):
	return module (song, "ChordArticulation", Beats = beats, LengthBeats = length, Style = style, Octave = octave, VoiceLeadMode = 1,
	               Bass = bass, **f)


def save (song, bpm, key, tracks, markers, num = 4, swing = 50, humanize = 20, reverb = 0.3):
	tonic, acc, mode = key
	doc = {"Format": "koton-onyx", "Version": 1,
	       "Project": {"Tempo": [{"Beat": 0, "Bpm": bpm}], "Tracks": tracks, "UserChordStyles": [], "UserMelodicLines": [], "UserDrumStyles": [],
	                   "Key": {"TonicLetter": "CDEFGAB".index (tonic), "Accidental": acc, "Mode": mode, "FullMode": -1},
	                   "TimeSigNum": num, "TimeSigDen": 4, "PickupBeats": 0, "TimeSigScale": 1, "MinBeats": 0,
	                   "SwingPercent": swing, "HumanizePercent": humanize, "AgogicPercent": 0,
	                   "Markers": [{"Beat": b, "Name": n} for b, n in markers], "OnyxReverbMix": reverb},
	       "Riffs": song.riffs}
	os.makedirs (OUT, exist_ok = True)
	p = os.path.join (OUT, song.name + ".kson")
	with open (p, "w", encoding = "utf-8", newline = "\n") as f:
		json.dump (doc, f, ensure_ascii = False, indent = 1)
		f.write ("\n")
	print (p)


# ---------------------------------------------------------------------------------------------------------------------
# 1. Aube sur le lac : re majeur, 72 a la noire. Une kora (greffon pluck) egrene les accords, des cloches s'allument par
#    anneaux euclidiens, une nappe chaude, puis la flute.
def aube ():
	s = Song ("aube-sur-le-lac")
	harm = [("D", MAJ7, 8), ("G", MAJ7S11, 8), ("B", MIN7, 8), ("A", ADD9, 8),
	        ("D", MAJ7, 8), ("G", MAJ7, 8), ("E", MIN9, 4), ("A", SEVSUS4, 4), ("D", MAJ7, 16)]
	total = sum (b for _, _, b in harm)	# 72
	flute = seq ([("F#5", 1.5), ("E5", .5), ("F#5", 1), ("A5", 1), ("C#6", 2), ("B5", 1), ("A5", 1),
	              ("B5", 1.5), ("A5", .5), ("G5", 1), ("F#5", 1), ("C#6", 1), ("B5", 1), ("F#5", 2),
	              ("G5", 1), ("F#5", 1), ("E5", 1), ("B5", 1), ("A5", 1.5), ("G5", .5), ("E5", 1), ("D5", 1),
	              ("F#5", 3), ("E5", 1), ("D5", 6)])
	tracks = [
		track ("Kora", [item (articulation (s, ST_ARP_3_8, 4, total, octave = 4))], program = NYLON, volume = .85, pan = -.25,
		       inst = plugin ("pluck", decay = 6, damping = .3, brightness = .62, pick = .17, release = .4, width = .7, volume = -4),
		       inserts = [plugin ("delay", time = 625, feedback = .35, tone = 4500, ping_pong = 1, width = 1, mix = .22),
		                  plugin ("reverb", room = .82, damping = .45, width = 1, predelay = 20, wet = .3, dry = 1)]),
		track ("Cloches", [item (module (s, "PolyChord", Octave = 5, CycleBeats = 4, Beats = 48, MonodicPick = True, MonodicSeed = 7,
		                                 MonodicAvoidRepeat = True, MonodicStrategy = 2,
		                                 Layers = [{"Hits": 3, "Steps": 8, "Rotation": 0, "Octave": 0, "ToneIndex": 0, "Contour": 0},
		                                           {"Hits": 5, "Steps": 12, "Rotation": 1, "Octave": 0, "ToneIndex": 1, "Contour": 0},
		                                           {"Hits": 2, "Steps": 16, "Rotation": 3, "Octave": 1, "ToneIndex": 2, "Contour": 0}]), 16)],
		       program = CELESTA, volume = .55, pan = .35, reverb = 30),
		track ("Nappe", [item (articulation (s, ST_HELD, 8, total, octave = 3))], program = PAD_WARM, volume = .6, reverb = 20,
		       fade = [{"Beat": 0, "Volume": 0}, {"Beat": 8, "Volume": .6}, {"Beat": 64, "Volume": .6}, {"Beat": 72, "Volume": 0}]),
		track ("Flute", [item (play (s, riff (s, "Flute", flute, 32)), 32)], program = FLUTE, volume = .9, pan = .1, reverb = 25),
		chord_track (s, harm)]
	save (s, 72, ("D", 0, 0), tracks, [(0, "Aube"), (32, "Flute"), (56, "Fin")], humanize = 25, reverb = .35)


# 2. Bossa du port : fa majeur, 132. Guitare et contrebasse jouent la grille, la flute dit le theme, le vibraphone
#    improvise le deuxieme chorus (une ligne melodique : le rythme est ecrit, Koton choisit les notes dans l'harmonie).
def bossa ():
	s = Song ("bossa-du-port")
	grid = [("F", MAJ7, 8), ("G", DOM9, 8), ("G", MIN7, 4), ("C", DOM9, 4), ("A", MIN7, 4), ("D", D7B9, 4),
	        ("G", MIN7, 8), ("Bb", M6, 8), ("A", MIN7, 4), ("D", D7B9, 4), ("G", MIN7, 4), ("C", DOM9, 4)]
	harm = grid + grid + [("F", MAJ9, 8)]
	total = sum (b for _, _, b in harm)	# 136
	theme = seq ([("C6", 1.5), ("A5", .5), ("G5", 1), ("A5", 1), ("E5", 3), (None, 1),
	              ("D6", 1.5), ("B5", .5), ("A5", 1), ("B5", 1), ("A5", 3), (None, 1),
	              ("Bb5", 1), ("A5", .5), ("G5", .5), ("F5", 1), ("D5", 1), ("E5", 1.5), ("D5", .5), ("E5", 1), ("G5", 1),
	              ("C6", 1.5), ("A5", .5), ("G5", 1), ("E5", 1), ("F#5", 1.5), ("Eb5", .5), ("F#5", 1), ("A5", 1),
	              ("Bb5", 3), ("A5", .5), ("G5", .5), ("F5", 2), ("D5", 2),
	              ("Db6", 2), ("C6", 1), ("Bb5", 1), ("G5", 3), (None, 1),
	              ("C6", 1), ("A5", 1), ("G5", 1), ("E5", 1), ("F#5", 1), ("A5", 1), ("C6", 1), ("Eb6", 1),
	              ("D6", 1.5), ("Bb5", .5), ("A5", 1), ("G5", 1), ("E5", 2), ("G5", 1), ("Bb5", 1)])
	# le rythme du chorus improvise : syncopes de bossa, deux mesures qui reviennent
	cell = [(0, 1), (1.5, .5), (2, 1.5), (4.5, .5), (5, 1), (6, 1.5)]
	rhythm = [{"Note": 0, "Start": int ((b + 8 * k) * 4), "Length": int (d * 4), "Voice": 0, "GlideFromNote": 0, "GlideDurationSlices": 0,
	           "Bend": None} for k in range (8) for b, d in cell]
	tracks = [
		track ("Guitare", [item (articulation (s, ST_BOSSA, 4, total, octave = 4))], program = NYLON, volume = .8, pan = -.3,
		       inserts = [plugin ("chorus", rate = .6, depth = 2.5, delay = 14, feedback = 0, voices = 2, spread = 1, mix = .35)]),
		track ("Contrebasse", [item (articulation (s, ST_HABANERA, 4, total, octave = 2))], program = AC_BASS, volume = .85, pan = .05),
		track ("Batterie", [item (module (s, "DrumKit", Style = 7, Density = 1, Kit = 0, FillLast = True, BeatsPerBar = 4, Repeats = 16)),
		                    item (module (s, "DrumKit", Style = 7, Density = 2, Kit = 0, FillLast = True, BeatsPerBar = 4, Repeats = 18))],
		       kind = 1, volume = .7),
		track ("Flute", [item (play (s, riff (s, "Theme", theme, 64)))], program = FLUTE, volume = .9, pan = .15, reverb = 15),
		track ("Vibraphone", [item (module (s, "MelodicLine", BeatsPerBar = 64, VoiceCount = 1, Preserve = False, SlicesPerQuarter = 4,
		                                    Notes = rhythm, Contour = 0, Anchor = 0, Continuity = 2, Variation = 0, Amplitude = 10,
		                                    Ornaments = 1, RegisterShift = 0), 64)],
		       program = VIBES, volume = .8, pan = .3, reverb = 20),
		chord_track (s, harm)]
	save (s, 132, ("F", 0, 0), tracks, [(0, "Theme"), (64, "Chorus (vibraphone)"), (128, "Fin")], swing = 50, humanize = 30)


# 3. Neons : la mineur, 112. Basse et lead au greffon subsynth, un arpegiateur (generateur arp) sur un FM, une cadence
#    vi-IV-I-V en nappe, la batterie disco.
def neons ():
	s = Song ("neons")
	loop = [("A", MIN, 8), ("F", MAJ, 8), ("C", MAJ, 8), ("G", MAJ, 8)]
	harm = loop * 4
	total = 128
	bass = []
	for r in ("A1", "F1", "C2", "G1"):
		up = r[0] + str (int (r[1]) + 1)
		for bar in range (2):
			bass += [(r, .5), (r, .5), (up, .5), (r, .5), (r, .5), (r, .5), (up, .5), (r, .5)]
	lead = seq ([("E5", 1), ("A5", 1), ("C6", 1.5), ("B5", .5), ("A5", 2), ("E5", 2),
	             ("F5", 1), ("A5", 1), ("C6", 1.5), ("D6", .5), ("C6", 3), ("A5", 1),
	             ("G5", 1), ("C6", 1), ("E6", 1.5), ("D6", .5), ("C6", 2), ("G5", 2),
	             ("B5", 1), ("D6", 1), ("G5", 1), ("B5", 1), ("D6", 2), ("B5", 2)])
	cad = module (s, "Cadence", Octave = 4, Style = ST_HELD, BeatsPerBar = 8, Measures = 4, ChordsPerMeasure = 1, VoiceLeadMode = 1,
	              CadenceStyle = 7, Chords = [{"Root": r, "Quality": q, "Inversion": 0, "OctaveShift": 0, "HeldVoice": -1, "Degree": -1}
	                                          for r, q in ((9, MIN), (5, MAJ), (0, MAJ), (7, MAJ))])
	bass_id = riff (s, "Basse", seq (bass), 32)
	lead_id = riff (s, "Lead", lead, 32)
	tracks = [
		track ("Basse", [item (play (s, bass_id)) for _ in range (4)], program = 38, volume = .85,
		       inst = plugin ("subsynth", osc1 = 0, osc2 = 1, osc2_semi = -12, osc2_fine = 5, mix = .45, cutoff = 650, resonance = 3,
		                      env_amt = 2.5, attack = .003, decay = .18, sustain = .55, release = .08, f_decay = .2, f_sustain = .15, volume = -7),
		       inserts = [plugin ("eq3", low_gain = 3, low_freq = 90, mid_gain = -2, mid_freq = 400, mid_q = .8, high_gain = 0,
		                          high_freq = 5000, output = 0)]),
		track ("Arpeges", [item (generator (s, "arp", total, pattern = 2, notes_per_beat = 4, extend = 1, articulation = 3, velocity = 92,
		                                    octave = 4, voice_leading = 1, spread = 0, rhythm_mode = 0))],
		       program = 81, volume = .6, pan = -.35,
		       inst = plugin ("fm2", ratio = 3.5, index = 3.5, feedback = .1, attack = .002, decay = .35, sustain = .2, release = .25,
		                      mod_decay = .2, mod_sustain = .1, detune = 4, vel_sens = .7, volume = -9),
		       inserts = [plugin ("delay", time = 402, feedback = .38, tone = 5000, ping_pong = 1, width = 1, mix = .3)]),
		track ("Nappe", [item (cad) for _ in range (4)], program = PAD_POLY, volume = .55, pan = .2,
		       fade = [{"Beat": 0, "Volume": 0}, {"Beat": 16, "Volume": .55}]),
		track ("Lead", [item (play (s, lead_id), 64), item (play (s, lead_id))], program = 81, volume = .8, pan = .1, reverb = 20,
		       inst = plugin ("subsynth", osc1 = 0, osc2 = 0, osc2_semi = 0, osc2_fine = 9, mix = .5, cutoff = 2800, resonance = 2,
		                      env_amt = 1.5, attack = .01, decay = .4, sustain = .7, release = .3, volume = -8),
		       inserts = [plugin ("drive", type = 2, drive = 9, bass_cut = 150, tone = .55, bias = .2, mix = .7, level = -3),
		                  plugin ("delay", time = 536, feedback = .3, tone = 4000, ping_pong = 1, width = 1, mix = .25)]),
		track ("Batterie", [item (module (s, "DrumKit", Style = 4, Density = 2, Kit = 0, FillLast = True, BeatsPerBar = 4, Repeats = 8))
		                    for _ in range (4)], kind = 1, volume = .75),
		chord_track (s, harm)]
	save (s, 112, ("A", 0, 1), tracks, [(0, "Intro"), (32, "Batterie"), (64, "Lead")], swing = 50, humanize = 10, reverb = .25)


# 4. Rituel : mi mineur (couleur phrygienne : fa majeur), 100. Tout tourne en anneaux : percussions (PolyDrum), marimba
#    (generateur euclid), un automate cellulaire sur le celesta, des cordes en anneaux (MelodicPoly), un shakuhachi.
def rituel ():
	s = Song ("rituel")
	harm = [("E", MIN, 8), ("F", MAJ, 8), ("E", MIN, 8), ("D", MAJ, 8)] * 3
	total = 96
	lay = lambda lane, k, n, rot = 0: {"Lane": lane, "AccentLane": -1, "Hits": k, "Steps": n, "Rotation": rot}
	rhythm = []
	for k in range (6):	# 8 temps : longues tenues et appels
		for b, d in ((0, 3), (3.5, .5), (4, 2), (6.5, 1.5)):
			rhythm.append ({"Note": 0, "Start": int ((b + 8 * k) * 4), "Length": int (d * 4), "Voice": 0, "GlideFromNote": 0,
			                "GlideDurationSlices": 0, "Bend": None})
	tracks = [
		track ("Percussions", [item (module (s, "PolyDrum", Kit = 0, Beats = 4, Repeats = 24, BeatsPerBar = 4,
		                                     Layers = [lay (12, 4, 16), lay (29, 3, 8), lay (28, 5, 16, 2), lay (40, 5, 12, 1),
		                                               lay (35, 7, 16, 3), lay (23, 2, 5)]))],
		       kind = 1, volume = .75,
		       inserts = [plugin ("eq3", low_gain = 2, low_freq = 120, mid_gain = -3, mid_freq = 900, mid_q = 1, high_gain = 1.5,
		                          high_freq = 7000, output = 0)]),
		track ("Marimba", [item (generator (s, "euclid", total, hits = 5, steps = 8, rotation = 0, notes_per_beat = 2, contour = 2,
		                                    tones = 2, octave = 4, range = 2, velocity = 96, accent = 1, articulation = 1, seed = 11))],
		       program = MARIMBA, volume = .8, pan = -.3,
		       inserts = [plugin ("reverb", room = .6, damping = .5, width = .9, predelay = 8, wet = .22, dry = 1)]),
		track ("Celesta", [item (generator (s, "automaton", 64, notes_per_beat = 2, rule = 90, width = 16, scale = 5, base_octave = 5,
		                                    oct_range = 2, seed = 3, seed_mode = 0, density = .3, velocity = 70, articulation = 2,
		                                    chord_aware = 1), 32)],
		       program = CELESTA, volume = .5, pan = .45, reverb = 35),
		track ("Cordes", [item (module (s, "MelodicPoly", Beats = 4, Repeats = 24, BeatsPerBar = 4,
		                                Layers = [{"Voice": 0, "Hits": 2, "Steps": 8, "Rotation": 0, "Octave": -1, "Legato": True},
		                                          {"Voice": 2, "Hits": 3, "Steps": 8, "Rotation": 2, "Octave": 0, "Legato": True}]))],
		       program = STRINGS, volume = .55, reverb = 20,
		       fade = [{"Beat": 0, "Volume": 0}, {"Beat": 16, "Volume": .55}, {"Beat": 88, "Volume": .55}, {"Beat": 96, "Volume": 0}]),
		track ("Shakuhachi", [item (module (s, "MelodicLine", BeatsPerBar = 48, VoiceCount = 1, Preserve = False, SlicesPerQuarter = 4,
		                                    Notes = rhythm, Contour = 8, Anchor = 0, Continuity = 1, Variation = 0, Amplitude = 9,
		                                    Ornaments = 2, RegisterShift = 0), 48)],
		       program = SHAKU, volume = .85, pan = .1, reverb = 40),
		chord_track (s, harm)]
	save (s, 100, ("E", 0, 1), tracks, [(0, "Anneaux"), (32, "Automate"), (48, "Shakuhachi")], swing = 50, humanize = 25, reverb = .3)


# 5. Petite valse musette : sol majeur, 3/4, 156. Le piano fait la valse, la clarinette dit l'air, l'accordeon le
#    reprend ; les cordes tiennent chaque accord par un module Pattern (un accord, une maniere de le jouer).
def valse ():
	s = Song ("petite-valse")
	form = [("G", MAJ), ("G", MAJ), ("E", MIN), ("E", MIN), ("A", MIN), ("D", DOM7), ("G", MAJ), ("D", DOM7),
	        ("G", MAJ), ("B", DOM7), ("E", MIN), ("C", MAJ), ("G", MAJ), ("D", DOM7), ("G", MAJ), ("G", MAJ)]
	harm = [(r, q, 3) for r, q in form] * 2 + [("G", MAJ, 6)]
	total = 102
	air = seq ([("D5", 1), ("G5", 1), ("B5", 1), ("A5", 2), ("G5", 1), ("E5", 1), ("G5", 1), ("B5", 1), ("B5", 1.5), ("A5", .5), ("G5", 1),
	            ("C6", 1), ("B5", 1), ("A5", 1), ("F#5", 1), ("A5", 1), ("C6", 1), ("B5", 2), ("A5", 1), ("A5", 3),
	            ("D5", 1), ("G5", 1), ("B5", 1), ("A5", 1), ("F#5", 1), ("D#5", 1), ("E5", 1), ("G5", 1), ("B5", 1), ("E6", 2), ("C6", 1),
	            ("D6", 1), ("B5", 1), ("G5", 1), ("F#5", 1), ("A5", 1), ("C6", 1), ("G5", 3), ("G5", 1), (None, 2)])
	air_id = riff (s, "Air", air, 48)
	roots = {"G": 7, "E": 4, "A": 9, "D": 2, "B": 11, "C": 0}
	strings = [item (module (s, "Pattern", Root = roots[r], Octave = 3, Quality = q, Degree = -1, BeatsPerBar = 3, Repeats = 1,
	                         VoiceLeadMode = 1, Style = ST_HELD, OpenVoicing = True), 48 if i == 0 else 0)
	           for i, (r, q) in enumerate (form)]
	tracks = [
		track ("Piano", [item (articulation (s, ST_WALTZ, 3, total, octave = 4, bass = True))], program = PIANO, volume = .75, pan = -.2),
		track ("Clarinette", [item (play (s, air_id))], program = CLARINET, volume = .9, pan = .2, reverb = 15),
		track ("Accordeon", [item (play (s, air_id), 48)], program = ACCORDION, volume = .8, pan = .25, reverb = 10),
		track ("Cordes", strings, program = STRINGS, volume = .45, reverb = 25),
		track ("Balais", [item (module (s, "DrumKit", Style = 12, Density = 1, Kit = 0, FillLast = True, BeatsPerBar = 3, Repeats = 34))],
		       kind = 1, volume = .5),
		chord_track (s, harm)]
	save (s, 156, ("G", 0, 0), tracks, [(0, "Air"), (48, "Accordeon"), (96, "Fin")], num = 3, swing = 50, humanize = 30, reverb = .3)


if __name__ == "__main__":
	aube ()
	bossa ()
	neons ()
	rituel ()
	valse ()
