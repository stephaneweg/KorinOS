//
// ui/chrome.h -- the studio's frame around the arrangement: the transport bar (save, undo / redo, the
// transport, the position display, the song's tempo, key, meter, swing, the snap, the engine's load,
// "Compose with AI", the master level), the browser on the right (what can be put in the song: the
// harmony, the rhythm, the melody generators, the AI's actions, the plugins), the editor's host at the
// bottom (its title strip -- the block's kind, its track, Listen -- and the editor) and the status bar.
// All hand-drawn in the studio's colours; the few uikit controls (the snap's drop-down, Listen) are
// children.
//
#ifndef _koton_chrome_h
#define _koton_chrome_h

#include "ui/dialogs.h"

namespace kui {

// ---- the commands the chrome calls (main.cpp) ----------------------------------------------------------------
struct Commands
{
	void (*save) (); void (*undo) (); void (*redo) (); void (*rewind) (); void (*playStop) (); void (*stop) ();
	void (*loop) (); void (*metronome) (); void (*song) (); void (*compose) ();
	void (*browse) (int action);			// a browser item
};
extern Commands g_cmd;
extern ArrangeView *g_arrange;
extern bool g_metronome;
extern Face *g_bigFace;			// the position display's digits (0: the text face)

// the key, for the chip: "F# minor (Aeolian)" with its words in the chosen language (the plugins get keyName as it is)
static inline void keyLabel (const Key &k, char *b, int cap)
{
	char n[64]; keyName (k, n, sizeof n);
	char *sp = strchr (n, ' ');
	if (!sp) { snprintf (b, cap, "%s", n); return; }
	*sp = 0;
	snprintf (b, cap, "%s %s", n, TR (sp + 1));
}

// the position: bar.beat.sixteenth and the time
static inline void positionText (double beat, char *pos, int pcap, char *tim, int tcap)
{
	const Project &p = g_doc.p;
	int bb = imax (1, p.barBeats ());
	if (beat < 0) beat = 0;
	int bar = (int) (beat / bb), b = (int) (beat - bar * bb), six = (int) ((beat - floor (beat)) * 4);
	snprintf (pos, pcap, "%d.%d.%d", bar + 1, b + 1, six + 1);
	double s = AudioHost::beatToSeconds (p, beat);
	int ms = (int) (s * 1000);
	snprintf (tim, tcap, "%d:%02d.%02d", ms / 60000, (ms / 1000) % 60, (ms / 10) % 100);
}

// ---- the transport bar ---------------------------------------------------------------------------------------------
class TopBar : public View
{
public:
	enum { H = 58, BTN = 34 };
	enum { B_SAVE, B_UNDO, B_REDO, B_REW, B_PLAY, B_STOP, B_LOOP, B_METRO, B_COUNT };
	long snapDd;
	float lvL, lvR;			// the master level shown (decaying)
	char lastPos[32];
	TopBar (int l, int t, int w) : View (l, t, w, H), snapDd (0), lvL (0), lvR (0), m_hot (-1), m_down (-1) { anchor = ANCHOR_LEFT | ANCHOR_TOP | ANCHOR_RIGHT; lastPos[0] = 0; }
	unsigned bgColor () override { return PANEL; }
	int btnX (int i) const { return 12 + i * (BTN + 4) + (i >= B_REW ? 14 : 0); }
	int lcdX () const { return btnX (B_COUNT - 1) + BTN + 18; }
	enum { CHIP_Y = 13, CHIP_H = 32 };
	// (ARMPI: the bar was laid out for a wide window; below that the same parts in less room: a narrower position,
	//  narrower chips without their words, the snap without its box; the AI's button, the DSP's load and the master
	//  go first -- the AI is in its menu, the load in the status bar)
	bool narrow () const { return width < 1400; }		// 1024 x 768, 800 x 600
	bool tiny () const { return width < 900; }		// 800 x 600: the meter and the swing are in the Song dialog
	int lcdW () const { return narrow () ? 150 : 196; }
	int chips () const { return tiny () ? 2 : 4; }
	// the chips: bpm, key, meter, swing (their x, w)
	int chipX (int i) const { int x = lcdX () + lcdW () + (narrow () ? 8 : 14); for (int k = 0; k < i; k++) x += chipW (k) + (narrow () ? 6 : 8); return x; }
	int chipW (int i) const { static const int w[4] = { 96, 226, 92, 104 }, n[4] = { 76, 104, 52, 62 }; return narrow () ? n[i] : w[i]; }
	int snapX () const { return chipX (chips () - 1) + chipW (chips () - 1) + (narrow () ? 6 : 8); }
	int aiX () const { return width - 12 - 190 - 12 - 200; }
	int masterX () const { return narrow () ? snapX () + (tiny () ? 88 : 104) + 8 : width - 116 - 190; }
	void placeChildren ()
	{
		if (!snapDd) return;
		Ctl *c = ctl (snapDd);
		c->x = snapX () + (narrow () ? 0 : 46); c->y = CHIP_Y + 4;
		if (tiny ()) c->w = 88;
		invalidate (true);
	}
	void layout () override { View::layout (); placeChildren (); }
	void (*onSnap) (int choice) = 0;
	void onControl (long w, int tag, int code) override { (void) tag; (void) code; if (onSnap) onSnap ((int) ui_w_get_value (w)); }

	static void icon (int i, int x, int y, int s, unsigned c)
	{
		int cx = x + s / 2, cy = y + s / 2;
		switch (i)
		{
		case B_SAVE:
			frame (cx - 8, cy - 8, 16, 16, 2, c);
			ui_fill (cx - 5, cy - 8, 10, 5, c);
			ui_fill (cx - 4, cy + 2, 8, 5, c);
			break;
		case B_UNDO: case B_REDO:
		{
			VPath p;
			if (i == B_UNDO) { p.arc (V (cx), V (cy + 2), V (7), 190, 360 + 20, V (2)); p.arrowHead (V (cx - 7), V (cy + 1), 90, V (5), V (4)); }
			else { p.arc (V (cx), V (cy + 2), V (7), 160, 350, V (2)); p.arrowHead (V (cx + 7), V (cy + 1), 90, V (5), V (4)); }
			p.fill (c);
		} break;
		case B_REW: ui_fill (cx - 8, cy - 7, 2, 14, c); tri (cx - 1, cy, 7, 2, c); tri (cx + 6, cy, 7, 2, c); break;
		case B_PLAY: tri (cx + 2, cy, 9, 0, c); break;
		case B_STOP: box (cx - 7, cy - 7, 14, 14, 2, c); break;
		case B_LOOP:
		{
			VPath p; p.arc (V (cx), V (cy), V (8), 20, 160, V (2)); p.arc (V (cx), V (cy), V (8), 200, 340, V (2));
			p.arrowHead (V (cx + 8), V (cy - 2), 270, V (5), V (4)); p.arrowHead (V (cx - 8), V (cy + 2), 90, V (5), V (4));
			p.fill (c);
		} break;
		case B_METRO:
		{
			int xy[8] = { V (cx - 7), V (cy + 8), V (cx - 3), V (cy - 9), V (cx + 3), V (cy - 9), V (cx + 7), V (cy + 8) };
			VPath p; p.polyline (xy, 4, V (2), true); p.line (V (cx), V (cy + 5), V (cx + 6), V (cy - 7), V (2)); p.fill (c);
		} break;
		}
	}
	bool active (int i) const
	{
		if (i == B_PLAY) return g_audio.isPlaying ();
		if (i == B_LOOP) return g_arrange && g_arrange->loopOn;
		if (i == B_METRO) return g_metronome;
		if (i == B_UNDO) return g_doc.canUndo ();
		if (i == B_REDO) return g_doc.canRedo ();
		if (i == B_SAVE) return g_doc.dirty;
		return false;
	}
	void onDraw () override
	{
		const Project &p = g_doc.p;
		clearView (PANEL);
		hline (0, width, height - 1, LINE);
		for (int i = 0; i < B_COUNT; i++)
		{
			int x = btnX (i), y = (H - BTN) / 2;
			bool on = active (i);
			unsigned face = i == m_down ? darker (FACE, 50) : i == m_hot ? lighter (FACE, 20) : FACE;
			if (i == B_PLAY && on) face = ACC2;
			if ((i == B_LOOP || i == B_METRO) && on) face = ACC2;
			gbox (x, y, BTN, BTN, 6, lighter (face, 10), face);
			unsigned ic = (i == B_UNDO || i == B_REDO || i == B_SAVE) && !on ? FAINT : (i == B_PLAY ? (on ? 0xFFFFFF : ACC) : TEXT);
			icon (i, x, y, BTN, ic);
		}
		vline (btnX (B_REW) - 10, 14, H - 14, LINE);
		// the position
		int lx = lcdX ();
		box (lx, 8, lcdW (), H - 16, 6, FIELD);
		frame (lx, 8, lcdW (), H - 16, 6, LINE);
		double beat = g_audio.isPlaying () ? g_audio.playheadBeat () : (g_arrange ? g_arrange->cursorBeat : 0);
		char pos[32], tim[32]; positionText (beat, pos, sizeof pos, tim, sizeof tim);
		snprintf (lastPos, sizeof lastPos, "%s", pos);
		{ FaceScope sc (g_bigFace); textL (lx + 14, 8, H - 16, pos, ACC, g_bigFace ? 0 : 2); }
		if (narrow ()) textR (lx + lcdW () - 8, 12, H - 24, tim, DIM);
		else { textR (lx + lcdW () - 12, 12, 18, TR ("BAR.BEAT.16"), FAINT); textR (lx + lcdW () - 12, 30, 18, tim, DIM); }
		// the song's chips
		char b[64];
		for (int i = 0; i < chips (); i++)
		{
			int x = chipX (i), w = chipW (i);
			box (x, CHIP_Y, w, CHIP_H, 5, PANEL2);
			frame (x, CHIP_Y, w, CHIP_H, 5, LINE);
			const char *cap = i == 0 ? "BPM" : TR (i == 1 ? "Key" : i == 2 ? "Meter" : "Swing");
			if (narrow ()) cap = i == 0 ? "BPM" : "";
			textL (x + 8, CHIP_Y, CHIP_H, cap, DIM);
			int vx = x + (cap[0] ? 10 + tw (cap) : 8);
			if (i == 0) snprintf (b, sizeof b, "%d", iround (p.mainBpm ()));
			else if (i == 1) keyLabel (p.key, b, sizeof b);
			else if (i == 2) snprintf (b, sizeof b, "%d/%d", p.timeSigNum, p.timeSigDen);
			else snprintf (b, sizeof b, "%d %%", p.swingPercent > 50 ? iround (p.swingPercent) : 50);
			textFit (vx, CHIP_Y, w - (vx - x) - 8, CHIP_H, b, TEXT, 2);
		}
		int sx = snapX ();
		if (!narrow ()) { box (sx, CHIP_Y, 160, CHIP_H, 5, PANEL2); textL (sx + 8, CHIP_Y, CHIP_H, TR ("Snap"), DIM); }
		// the engine's load
		int ex = aiX () - 150;
		if (!narrow () && ex > sx + 170)
		{
			snprintf (b, sizeof b, "%s", TR (g_audio.running ? "DSP THREAD" : "NO SOUND"));
			textL (ex, 8, 18, b, DIM, 2);
			int load = g_audio.loadPercent ();
			snprintf (b, sizeof b, "%d %%", load);
			textL (ex, 28, 20, b, TEXT);
			box (ex + 44, 36, 90, 6, 3, FIELD);
			box (ex + 44, 36, imax (4, imin (90, load * 90 / 100)), 6, 3, load > 80 ? REC : load > 50 ? PLAY : GREEN);
		}
		// Compose with AI
		int ax = aiX ();
		if (!narrow ())
		{
			gbox (ax, 11, 200, H - 22, 6, lighter (ACC2, 30), ACC2);
			textC (ax, 11, 200, H - 22, TR ("Compose with AI..."), 0xFFFFFF, 2);
		}
		// the master level
		int mx = masterX ();
		int leds = imin (40, (width - 112 - mx) / 4);	// (left of the window's buttons)
		if (leds < 15) return;
		textL (mx, 6, 16, TR ("MASTER"), DIM, 2);
		for (int ch = 0; ch < 2; ch++)
		{
			float v = ch ? lvR : lvL;
			float db = v > 1e-5f ? 20.0f * log10f (v) : -90;
			int n = iclamp ((int) ((db + 48) / 48 * leds), 0, leds);
			for (int k = 0; k < leds; k++)
			{
				unsigned c = k < leds * 7 / 10 ? GREEN : k < leds * 9 / 10 ? PLAY : REC;
				ui_fill (mx + k * 4, 26 + ch * 10, 3, 8, k < n ? c : mixc (c, PANEL, 200));
			}
		}
	}
	int hitTest (int mx, int my)
	{
		for (int i = 0; i < B_COUNT; i++) { int x = btnX (i), y = (H - BTN) / 2; if (mx >= x && mx < x + BTN && my >= y && my < y + BTN) return i; }
		for (int i = 0; i < chips (); i++) if (mx >= chipX (i) && mx < chipX (i) + chipW (i) && my >= CHIP_Y && my < CHIP_Y + CHIP_H) return 100 + i;
		if (!narrow () && mx >= aiX () && mx < aiX () + 200 && my >= 11 && my < H - 11) return 200;
		if (mx >= lcdX () && mx < lcdX () + lcdW ()) return 300;
		return -1;
	}
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override
	{
		(void) br; (void) bm; (void) wheel;
		int e = m_b.edge (bl, 0);
		int h = hitTest (mx, my);
		int hot = h >= 0 && h < B_COUNT ? h : -1;
		if (hot != m_hot) { m_hot = hot; invalidate (true); }
		if (e == 1) { m_down = hot; invalidate (true); }
		if (e == -1)
		{
			int d = m_down; m_down = -1; invalidate (true);
			if (d >= 0 && d == hot)
				switch (d)
				{
				case B_SAVE: g_cmd.save (); break;
				case B_UNDO: g_cmd.undo (); break;
				case B_REDO: g_cmd.redo (); break;
				case B_REW: g_cmd.rewind (); break;
				case B_PLAY: g_cmd.playStop (); break;
				case B_STOP: g_cmd.stop (); break;
				case B_LOOP: g_cmd.loop (); break;
				case B_METRO: g_cmd.metronome (); break;
				}
			else if (h >= 100 && h < 104) g_cmd.song ();
			else if (h == 200) g_cmd.compose ();
		}
		return true;
	}
	void tick ()
	{
		float l = g_audio.engine.masterPeakL, r = g_audio.engine.masterPeakR;
		float nl = l > lvL * 0.86f ? l : lvL * 0.86f, nr = r > lvR * 0.86f ? r : lvR * 0.86f;
		if (fabsf (nl - lvL) > 0.002f || fabsf (nr - lvR) > 0.002f) { lvL = nl; lvR = nr; invalidate (true); }
		if (g_audio.isPlaying ()) invalidate (true);
	}
private:
	Buttons m_b; int m_hot, m_down;
};

// ---- the browser -------------------------------------------------------------------------------------------------------
enum
{
	BR_CHORD = 1, BR_CADENCE, BR_CHAIN, BR_POLYCHORD, BR_ACCOMP,
	BR_DRUMS, BR_POLYDRUM, BR_EUCLID_LINE,
	BR_RIFF, BR_LINE, BR_RINGS,
	BR_AI_COMPOSE, BR_AI_TRACK, BR_AI_DRUMS, BR_AI_DEVELOP,
	BR_ADD_TRACK, BR_ADD_DRUMS,
	BR_PLUGIN = 1000			// + the plugin's index
};
struct BrowserItem { int action; const char *label, *hint; unsigned colour; };

class Browser : public View
{
public:
	enum { W = 262, ROW = 24 };
	Vec<BrowserItem> items;			// action 0: a heading
	Vec<Str> pluginNames; Vec<int> pluginKinds;
	Browser (int l, int t, int h) : View (l, t, W, h), m_hot (-1), m_scroll (0) { anchor = ANCHOR_TOP | ANCHOR_BOTTOM | ANCHOR_RIGHT; build (); }
	unsigned bgColor () override { return SIDE; }
	void heading (const char *s) { BrowserItem i = { 0, TR (s), 0, 0 }; items.push (i); }
	void add (int a, const char *s, const char *hint, unsigned c) { BrowserItem i = { a, TR (s), hint ? TR (hint) : 0, c }; items.push (i); }
	void build ()
	{
		items.clear ();
		heading ("HARMONY");
		add (BR_CHORD, "Chord", "the next one the co-pilot suggests", FUNC_T);
		add (BR_CADENCE, "Cadence (30 styles)", "chords on the chord track", FUNC_T);
		add (BR_CHAIN, "Chain 4 bars", "the co-pilot's progression", FUNC_T);
		add (BR_POLYCHORD, "Poly chords", "chords in rings", FUNC_T);
		add (BR_ACCOMP, "Accompaniment", "plays the chord track", FUNC_S);
		heading ("RHYTHM");
		add (BR_DRUMS, "Drum pattern", "a groove or the catalog", 0xE28844);
		add (BR_POLYDRUM, "Polyrhythm (rings)", "euclidean drum rings", 0xE28844);
		heading ("MELODY");
		add (BR_RIFF, "Riff", "notes you draw or play", 0xE2A454);
		add (BR_LINE, "Melodic line", "a rhythm; the harmony picks", 0xE2A454);
		add (BR_RINGS, "Melodic rings", "a line in polyrhythm", 0xE2A454);
		heading ("AI");
		add (BR_AI_COMPOSE, "Compose a piece...", "Gemini writes a whole song", 0xA884DE);
		add (BR_AI_TRACK, "Add an instrument...", "a new part over the song", 0xA884DE);
		add (BR_AI_DRUMS, "Add drums...", "a groove for the song", 0xA884DE);
		add (BR_AI_DEVELOP, "Develop the end...", "the next bars", 0xA884DE);
		heading ("TRACKS");
		add (BR_ADD_TRACK, "Add an instrument track", 0, DIM);
		add (BR_ADD_DRUMS, "Add a drum track", 0, DIM);
		if (pluginNames.size ())
		{
			heading ("PLUGINS (IPC)");
			for (int i = 0; i < pluginNames.size (); i++)
			{
				int k = i < pluginKinds.size () ? pluginKinds[i] : 0;
				add (BR_PLUGIN + i, pluginNames[i], k == 1 ? "instrument: plays the track" : k == 2 ? "effect: on the track" : "generator: a block of notes", k == 1 ? GREEN : k == 2 ? ACC : 0xE2A454);
			}
		}
		invalidate (true);
	}
	void onDraw () override
	{
		clearView (SIDE);
		vline (0, 0, height, LINE);
		textL (14, 6, 22, TR ("GENERATORS"), ACC, 2);
		textL (14, 26, 18, TR ("click: put it on the selected track"), FAINT);
		int y = 50 - m_scroll;
		for (int i = 0; i < items.size (); i++)
		{
			const BrowserItem &it = items[i];
			if (!it.action)
			{
				y += 6;
				if (y > 40 && y < height) { textL (14, y, 20, it.label, DIM, 2); hline (14 + tw (it.label, 2) + 8, width - 12, y + 10, LINE); }
				y += 20;
				continue;
			}
			if (y + ROW > 44 && y < height)
			{
				if (i == m_hot) box (8, y, width - 16, ROW + (it.hint ? 14 : 0), 4, PANEL2);
				box (14, y + 6, 12, 12, 3, it.colour);
				textL (34, y, ROW, it.label, TEXT);
				if (it.hint) textFit (34, y + 18, width - 44, 16, it.hint, FAINT);
			}
			y += ROW + (it.hint ? 16 : 0);
		}
		m_contentH = y + m_scroll;
	}
	int itemAt (int my)
	{
		int y = 50 - m_scroll;
		for (int i = 0; i < items.size (); i++)
		{
			const BrowserItem &it = items[i];
			if (!it.action) { y += 26; continue; }
			int h = ROW + (it.hint ? 16 : 0);
			if (my >= y && my < y + h) return i;
			y += h;
		}
		return -1;
	}
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override
	{
		(void) mx; (void) br; (void) bm;
		if (wheel) { m_scroll = iclamp (m_scroll - wheel * 40, 0, imax (0, m_contentH - height + 20)); invalidate (true); return true; }
		int i = itemAt (my);
		if (i != m_hot) { m_hot = i; invalidate (true); }
		if (m_b.edge (bl, 0) == -1 && i >= 0 && g_cmd.browse) g_cmd.browse (items[i].action);
		return true;
	}
private:
	Buttons m_b; int m_hot, m_scroll, m_contentH = 0;
};

// ---- the editor's host ------------------------------------------------------------------------------------------------------
class EditorHost : public View
{
public:
	enum { TITLE_H = 34 };
	long listen;
	bool listening;
	unsigned listenRev;
	EditorHost (int l, int t, int w, int h) : View (l, t, w, h), listen (0), listening (false), listenRev (0), onSplit (0)
	{
		anchor = ANCHOR_LEFT | ANCHOR_RIGHT | ANCHOR_BOTTOM;
		listen = addCtl (ui_pushbutton (w - 130, 4, 118, 26, (long) "Listen", 0), 0, w - 130, 4, 118, 26, UN_ACTIVATE);
		ctlAnchor (listen, ANCHOR_TOP | ANCHOR_RIGHT);
		ui_w_tip (listen, (long) "Hear this block alone, looping");
	}
	unsigned bgColor () override { return PANEL; }
	void (*onSplit) (int dy);		// the top edge dragged: the pane resized by dy
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override
	{
		(void) mx; (void) br; (void) bm; (void) wheel;
		int e = m_b.edge (bl, 0);
		if (e == 1 && my < 6) { m_drag = true; m_y0 = my; catchOutside = true; return true; }
		if (m_drag && bl) { if (my != m_y0 && onSplit) onSplit (my - m_y0); return true; }
		if (m_drag && !bl) { m_drag = false; catchOutside = false; return true; }
		return false;
	}
	void onDraw () override
	{
		clearView (PANEL);
		hline (0, width, 0, LINE);
		ui_fill (0, 1, width, TITLE_H - 1, PANEL2);
		box (width / 2 - 22, 3, 44, 3, 1, FAINT);		// (the grip: drag to resize the pane)
		hline (0, width, TITLE_H, LINE);
		if (g_editor)
		{
			const Track &t = g_doc.p.tracks[g_editor->track];
			char b[160];
			const char *ttl = TR (g_editor->title ());
			textL (16, 1, TITLE_H - 1, ttl, TEXT, 2);
			int x = 22 + tw (ttl, 2);
			char lab_[64] = ""; Module *m = g_editor->module ();
			if (m) moduleLabel (g_doc.p, *m, lab_, sizeof lab_);
			double s = g_editor->startBeat (), len = g_editor->lenBeats ();
			int bb = imax (1, g_doc.p.barBeats ());
			snprintf (b, sizeof b, TR ("%s   -   %s   -   bars %d-%d"), lab_, t.name.c (), (int) (s / bb) + 1, (int) ((s + len - 0.001) / bb) + 1);
			textFit (x, 1, width - x - 150, TITLE_H - 1, b, DIM);
		}
		else
		{
			textL (16, 1, TITLE_H - 1, TR ("Editor"), TEXT, 2);
			wrapText (24, TITLE_H + 30, width - 48, TR ("Select a block of the arrangement (a chord, an accompaniment, a riff, drums, a melodic line, rings...) to edit it here. Double-click an empty place of a lane to put a block there; right-click for more."), FAINT, 20);
		}
	}
	void setEditor (Editor *e)
	{
		if (g_editor) { removeChild (g_editor); delete g_editor; g_editor = 0; }
		stopListening ();
		g_editor = e;
		if (e) addChild (e);
		ctlShow (listen, e != 0);
		invalidate (true);
	}
	// the editor of the selected block, made again
	void rebuild ()
	{
		Selection s = g_doc.sel;
		Editor *e = s.valid () && g_doc.module (s.track, s.item) ? makeEditor (0, TITLE_H + 1, width, height - TITLE_H - 1, s.track, s.item) : 0;
		bool was = listening;
		setEditor (e);
		if (was && e) startListening ();
	}
	void startListening ()
	{
		if (!g_editor) return;
		CompiledSong *cs = g_editor->previewSong ();
		if (!cs) return;
		if (g_audio.isPlaying ()) g_audio.stopPlay ();
		g_audio.preview (cs);
		listening = true; listenRev = g_doc.revision;
		ui_w_set_text (listen, (long) TR ("Stop"));
	}
	void stopListening ()
	{
		if (listening) g_audio.stopPreview ();
		listening = false;
		if (listen) ui_w_set_text (listen, (long) TR ("Listen"));
	}
	void onControl (long w, int tag, int code) override { (void) w; (void) tag; (void) code; if (listening) stopListening (); else startListening (); }
	void tick ()
	{
		if (listening && listenRev != g_doc.revision && g_editor) { CompiledSong *cs = g_editor->previewSong (); if (cs) g_audio.preview (cs); listenRev = g_doc.revision; }
		if (g_editor) g_editor->tick ();
	}
private:
	Buttons m_b; bool m_drag = false; int m_y0 = 0;
};

// ---- the status bar ---------------------------------------------------------------------------------------------------------------
class StatusBar : public View
{
public:
	enum { H = 26 };
	StatusBar (int l, int t, int w) : View (l, t, w, H) { anchor = ANCHOR_LEFT | ANCHOR_RIGHT | ANCHOR_BOTTOM; }
	void onDraw () override
	{
		clearView (PANEL);
		hline (0, width, 0, LINE);
		char b[200];
		disc (16, H / 2, 4, g_audio.running ? GREEN : REC);
		if (g_audio.running)
			snprintf (b, sizeof b, TR ("%d.%d kHz - %d ms - %d voices - DSP %d %% - %u underruns"),
				  SOUND_RATE / 1000, (SOUND_RATE / 100) % 10, g_audio.latencyMs (), (int) g_audio.engine.activeVoices, g_audio.loadPercent (), g_audio.underruns);
		else snprintf (b, sizeof b, "%s", TR (g_audio.status));
		textL (28, 1, H - 1, b, DIM);
		int x = 28 + tw (b) + 30;
		vline (x - 15, 6, H - 6, LINE);
		snprintf (b, sizeof b, "%s", g_audio.sfName[0] ? g_audio.sfName : TR ("no SoundFont"));
		textL (x, 1, H - 1, b, DIM);
		x += tw (b) + 30;
		vline (x - 15, 6, H - 6, LINE);
		textFit (x, 1, width - x - 160, H - 1, g_status, TEXT);
		const char *f = g_doc.path[0] ? g_doc.path : TR ("untitled");
		{ const char *sl = strrchr (f, '/'); if (sl) f = sl + 1; }
		snprintf (b, sizeof b, "%s%s%s", f, g_doc.dirty ? "  " : "", g_doc.dirty ? TR ("(modified)") : "");
		textR (width - 12, 1, H - 1, b, DIM);
	}
};

} // namespace kui

#endif
