//
// style.h -- le style et les gammes d'Arabesque.
//
// Un style : dix parties, chacune son orchestration (canal, instrument, volume, panoramique) et sa composition (tessiture,
// unite, rythme, densite, et quelle composante de la couleur donne hauteur, duree, volume et articulation). Le fichier
// .stl est celui d'Art Song : [Pt_n] puis cle=valeur. Art Song n'y ecrit que ce qui differe de ses valeurs par defaut,
// que son manuel ne donne pas : celles d'ici sont deduites de ses exemples. Arabesque ecrit toutes les cles.
//
// Une gamme : un nom et un masque de douze bits, la note 0 etant do (fichier .sca : une ligne le nom, une ligne le masque).
//
#ifndef ARABESQUE_STYLE_H
#define ARABESQUE_STYLE_H

#include "ifs.h"

static const int NPART = 10, NSCALE_MAX = 64;
enum { MAP_R, MAP_G, MAP_B };			// composantes de la couleur
enum { ART_FIXED, ART_R, ART_G, ART_B };	// articulation : fixe, ou d'apres une composante
enum { CHORD_CLOSED, CHORD_OPEN1, CHORD_OPEN2 };

struct Part
{
	bool on, mute, tie;
	char name[32];
	int chan;				// 0..15, 9 : percussions
	int patch;				// General MIDI, 0..127
	int start;				// premiere note, 0 : au hasard
	int low, high;				// tessiture
	int voices, chord;			// notes ensemble, 1..4, et leur disposition
	int unit;				// duree de base, pulsations a 120 par noire (comme les .stl)
	char rhythm[24];			// coefficients de l'unite, un chiffre chacun
	int density;				// % de notes jouees
	int mapP, mapD, mapV, mapA;		// MAP_* (mapA : ART_*)
	int range;				// etendue des nuances, 10..80
	int vol, pan;				// 0..127
	int phase;				// iterations d'avance sur la partie d'avant
	int harm;				// la grille d'accords : HARM_*
};
enum { HARM_BEATS, HARM_CHORD, HARM_BASS, HARM_FREE };	// l'accord aux temps et des passages entre ; l'accord seul ; sa basse ; libre

struct Style
{
	char name[64];
	Part p[NPART];

	void reset ()
	{
		strcpy (name, "Sans titre");
		for (int i = 0; i < NPART; i++)
		{
			Part &q = p[i];
			memset (&q, 0, sizeof q);
			q.chan = i == 9 ? 10 : i; q.low = 48; q.high = 72; q.voices = 1; q.unit = 120; q.density = 100;
			q.range = 80; q.vol = 64; q.pan = 64; q.phase = 100;
		}
	}
	Style () { reset (); }

	void parse (const char *s)
	{
		reset ();
		int cur = -1;
		while (*s)
		{
			const char *e = s;
			while (*e && *e != '\n') e++;
			char line[96]; int n = (int) (e - s) < 95 ? (int) (e - s) : 95;
			memcpy (line, s, (size_t) n); line[n] = 0;
			while (n > 0 && (line[n - 1] == '\r' || line[n - 1] == ' ')) line[--n] = 0;
			s = *e ? e + 1 : e;
			if (!strncmp (line, "[Pt_", 4)) { cur = atoi (line + 4) - 1; if (cur >= NPART) cur = -1; continue; }
			char *eq = strchr (line, '=');
			if (cur < 0 || !eq) continue;
			*eq = 0;
			set (p[cur], line, eq + 1);
		}
		for (int i = 0; i < NPART; i++)
		{
			Part &q = p[i];
			if (q.high > 127) q.high = 127;
			if (q.low > q.high) q.low = q.high;
			if (q.voices < 1) q.voices = 1;
			if (q.voices > 4) q.voices = 4;
			if (q.unit < 1) q.unit = 120;
			if (q.range < 10) q.range = 10;
		}
	}
	static void set (Part &q, const char *k, const char *v)
	{
		int i = atoi (v);
		#define K(s) (!strcmp (k, s))
		if (K ("On")) q.on = i != 0;
		else if (K ("Name")) { strncpy (q.name, v, sizeof q.name - 1); q.name[sizeof q.name - 1] = 0; }
		else if (K ("Chan")) q.chan = i & 15;
		else if (K ("Pat")) q.patch = i & 127;
		else if (K ("Start")) q.start = i & 127;
		else if (K ("High")) q.high = i;
		else if (K ("Low")) q.low = i;
		else if (K ("Vct")) q.voices = i;
		else if (K ("Chd")) q.chord = i >= 0 && i <= 2 ? i : 0;
		else if (K ("Tie")) q.tie = i != 0;
		else if (K ("MP")) q.mapP = i & 3;
		else if (K ("MD")) q.mapD = i & 3;
		else if (K ("DR")) { strncpy (q.rhythm, v, sizeof q.rhythm - 1); q.rhythm[sizeof q.rhythm - 1] = 0; }
		else if (K ("MV")) q.mapV = i & 3;
		else if (K ("VR")) q.range = i;
		else if (K ("MA")) q.mapA = i & 3;
		else if (K ("Vol")) q.vol = i & 127;
		else if (K ("Pan")) q.pan = i & 127;
		else if (K ("Unit")) q.unit = i;
		else if (K ("Den")) q.density = i;
		else if (K ("Pha")) q.phase = i;
		else if (K ("Mute")) q.mute = i != 0;
		else if (K ("Har")) q.harm = i >= 0 && i <= 3 ? i : 0;
		#undef K
	}
	int format (char *o, int cap) const
	{
		int w = 0;
		for (int i = 0; i < NPART; i++)
		{
			const Part &q = p[i];
			w += snprintf (o + w, cap - w, "[Pt_%d]\r\nOn=%d\r\nName=%s\r\nChan=%d\r\nPat=%d\r\nStart=%d\r\nHigh=%d\r\nLow=%d\r\nVct=%d\r\nChd=%d\r\nTie=%d\r\n"
				"MP=%d\r\nMD=%d\r\nDR=%s\r\nMV=%d\r\nVR=%d\r\nMA=%d\r\nVol=%d\r\nPan=%d\r\nUnit=%d\r\nDen=%d\r\nPha=%d\r\nMute=%d\r\n",
				i + 1, q.on, q.name, q.chan, q.patch, q.start, q.high, q.low, q.voices, q.chord, q.tie, q.mapP, q.mapD, q.rhythm, q.mapV, q.range,
				q.mapA, q.vol, q.pan, q.unit, q.density, q.phase, q.mute);
			if (q.harm) w += snprintf (o + w, cap - w, "Har=%d\r\n", q.harm);	// (absent des fichiers d'Art Song)
			w += snprintf (o + w, cap - w, "\r\n");
		}
		return w;
	}
	bool load (const char *path)
	{
		char *s = read_all (path);
		if (!s) return false;
		parse (s);
		free (s);
		return true;
	}
	bool save (const char *path) const
	{
		static char b[NPART * 300];
		return write_all (path, b, format (b, sizeof b));
	}
};

struct Scale { char name[32]; unsigned mask; };
struct ScaleList
{
	Scale s[NSCALE_MAX];
	int n;
	ScaleList () : n (0) {}
	void add (const char *name, unsigned mask)
	{
		if (n >= NSCALE_MAX) return;
		strncpy (s[n].name, name, sizeof s[n].name - 1); s[n].name[sizeof s[n].name - 1] = 0;
		s[n].mask = mask & 0xFFF;
		n++;
	}
	void defaults ()
	{
		n = 0;
		add ("Majeure", 0xAB5); add ("Pentatonique majeure", 0x295); add ("Pentatonique mineure", 0x4A9); add ("Blues", 0x4E9);
		add ("Mineure naturelle", 0x5AD); add ("Mineure harmonique", 0x9AD); add ("Mineure mélodique", 0xAAD);
		add ("Dorien", 0x6AD); add ("Phrygien", 0x5AB); add ("Lydien", 0xAD5); add ("Mixolydien", 0x6B5);
		add ("Par tons", 0x555); add ("Diminuée", 0xB6D); add ("Hirajoshi", 0x18D); add ("Hijaz", 0x5B3);
	}
	void parse (const char *t)
	{
		n = 0;
		char name[96];
		while (*t)
		{
			const char *e = t;
			while (*e && *e != '\n') e++;
			int l = (int) (e - t) < 95 ? (int) (e - t) : 95;
			memcpy (name, t, (size_t) l); name[l] = 0;
			while (l > 0 && name[l - 1] == '\r') name[--l] = 0;
			if (!*e) break;
			t = e + 1;
			unsigned m = (unsigned) strtoul (t, 0, 10);
			while (*t && *t != '\n') t++;
			if (*t) t++;
			if (l) add (name, m);
		}
	}
	bool load (const char *path)
	{
		char *b = read_all (path);
		if (!b) return false;
		parse (b);
		free (b);
		return n > 0;
	}
	bool save (const char *path) const
	{
		static char b[NSCALE_MAX * 48];
		int w = 0;
		for (int i = 0; i < n; i++) w += snprintf (b + w, sizeof b - (size_t) w, "%s\r\n%u\r\n", s[i].name, s[i].mask);
		return write_all (path, b, w);
	}
};

static const char *const GM_NAMES[128] = {
	"Acoustic Grand Piano", "Bright Piano", "Electric Grand", "Honky-tonk Piano", "Electric Piano 1", "Electric Piano 2", "Harpsichord", "Clavinet",
	"Celesta", "Glockenspiel", "Music Box", "Vibraphone", "Marimba", "Xylophone", "Tubular Bells", "Dulcimer",
	"Drawbar Organ", "Percussive Organ", "Rock Organ", "Church Organ", "Reed Organ", "Accordion", "Harmonica", "Tango Accordion",
	"Nylon Guitar", "Steel Guitar", "Jazz Guitar", "Clean Guitar", "Muted Guitar", "Overdriven Guitar", "Distortion Guitar", "Guitar Harmonics",
	"Acoustic Bass", "Finger Bass", "Pick Bass", "Fretless Bass", "Slap Bass 1", "Slap Bass 2", "Synth Bass 1", "Synth Bass 2",
	"Violin", "Viola", "Cello", "Contrabass", "Tremolo Strings", "Pizzicato Strings", "Harp", "Timpani",
	"Strings", "Slow Strings", "Synth Strings 1", "Synth Strings 2", "Choir Aahs", "Voice Oohs", "Synth Voice", "Orchestra Hit",
	"Trumpet", "Trombone", "Tuba", "Muted Trumpet", "French Horn", "Brass Section", "Synth Brass 1", "Synth Brass 2",
	"Soprano Sax", "Alto Sax", "Tenor Sax", "Baritone Sax", "Oboe", "English Horn", "Bassoon", "Clarinet",
	"Piccolo", "Flute", "Recorder", "Pan Flute", "Blown Bottle", "Shakuhachi", "Whistle", "Ocarina",
	"Square Lead", "Saw Lead", "Calliope", "Chiff Lead", "Charang", "Voice Lead", "Fifths Lead", "Bass + Lead",
	"New Age Pad", "Warm Pad", "Polysynth", "Choir Pad", "Bowed Pad", "Metallic Pad", "Halo Pad", "Sweep Pad",
	"Rain", "Soundtrack", "Crystal", "Atmosphere", "Brightness", "Goblins", "Echoes", "Sci-fi",
	"Sitar", "Banjo", "Shamisen", "Koto", "Kalimba", "Bagpipe", "Fiddle", "Shanai",
	"Tinkle Bell", "Agogo", "Steel Drums", "Woodblock", "Taiko Drum", "Melodic Tom", "Synth Drum", "Reverse Cymbal",
	"Guitar Fret Noise", "Breath Noise", "Seashore", "Bird Tweet", "Telephone Ring", "Helicopter", "Applause", "Gunshot" };

#endif
