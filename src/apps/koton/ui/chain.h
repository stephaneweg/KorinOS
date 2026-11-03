//
// ui/chain.h -- a track's SOUND CHAIN (right of the editor's pane): its instrument (a SoundFont program
// or kit, or an instrument plugin -- a process of its own), its insert effects (plugins: each with
// its first parameters as knobs, on / off, its editor, removed), "Add an effect", the reverb send and
// the track's level. A plugin's editor (made from its parameters) opens in a floating panel of the
// window. The plugins' host is plug/plughost.h.
//
#ifndef _koton_chain_h
#define _koton_chain_h

#include "ui/dialogs.h"
#include "plug/plughost.h"

namespace kui {

extern PlugHost *g_plug;			// 0: no plugins (an old kernel, the PC simulator)
extern void (*g_openPluginEditor) (PlugInstance *p, const char *title);

// the track's plugins made again from the song (after an edit, an undo, a file opened)
static inline void syncPlugins ()
{
	if (!g_plug || !g_plug->available ()) return;
	for (int t = 0; t < g_doc.p.tracks.size () && t < ENGINE_MAX_TRACKS; t++) g_plug->syncTrack (t, g_doc.p.tracks[t]);
	g_plug->releaseTracks (g_doc.p.tracks.size ());
}
// the plugins' states into the song (before it is saved)
static inline void savePluginStates ()
{
	if (!g_plug || !g_plug->available ()) return;
	for (int t = 0; t < g_doc.p.tracks.size () && t < ENGINE_MAX_TRACKS; t++) g_plug->saveTrack (t, g_doc.p.tracks[t]);
}

class ChainPanel : public View
{
public:
	enum { W = 300, T_INSTR = 1, T_INSTR_EDIT, T_ADD, T_REVERB, T_FX = 100 };	// T_FX + slot * 10 + (0 on, 1 edit, 2 remove, 3 pick, 4.. knobs)
	int track;
	int open;				// the insert shown with its knobs (-1 none)
	long meter;
	unsigned m_states = 0;		// (plugStates when made)
	bool m_knobs = true;		// the open insert's knobs fit in the pane
	ChainPanel (int l, int t, int h) : View (l, t, W, h), track (-1), open (0), meter (0) { anchor = ANCHOR_TOP | ANCHOR_BOTTOM | ANCHOR_RIGHT; }
	unsigned bgColor () override { return PANEL; }
	void onControl (long w, int tag, int code) override { (void) code; control (tag, w); }
	void clearChildren ()
	{
		while (firstChild) { View *c = firstChild; removeChild (c); delete c; }
		ctlClear ();
		meter = 0;
	}
	long btn (int x, int y, int w, const char *s, int tag, const char *tip = 0)
	{
		long b = addCtl (ui_pushbutton (x, y, w, 24, (long) s, 0), tag, x, y, w, 24, UN_ACTIVATE);
		if (tip) ui_w_tip (b, (long) tip);
		return b;
	}
	void knob (int x, int y, int w, int h, int hi, int v, int tag, const char *name)
	{
		addCtl (ui_knob (x, y, w, h - 18, 0, hi, v), tag, x, y, w, h - 18, UN_CHANGE);
		addCtl (ui_label (x, y + h - 16, w, 16, (long) name, ULB_CENTER), 0, x, y + h - 16, w, 16, -1);
	}

	void rebuild ()
	{
		clearChildren ();
		const Project &p = g_doc.p;
		track = iclamp (g_doc.sel.valid () ? g_doc.sel.track : g_doc.selTrack, -1, p.tracks.size () - 1);
		m_states = plugStates ();
		if (track < 0) { invalidate (true); return; }
		const Track &tr = p.tracks[track];
		int y = 38, meterY = 40;
		if (tr.type != TRACK_CHORD)
		{
			// a short pane: the open insert's knobs give way, the reverb moves above the meter
			int n = imin (4, tr.inserts.size ()), rows = 102 + n * 36 + (n < 4 ? 36 : 0);
			m_knobs = rows + 68 <= height;
			bool tight = rows + (m_knobs ? 68 : 0) + 84 > height;
			btn (width - 50 - 84, y + 16, 76, "Change", T_INSTR, "The track's instrument (SoundFont or plugin)");
			if (!tr.instrumentPlugin.id.empty ()) btn (width - 50 - 136, y + 16, 48, "Edit", T_INSTR_EDIT, "The plugin's own editor");
			y += 64;
			for (int s = 0; s < tr.inserts.size () && s < 4; s++)
			{
				const PluginSlot &ps = tr.inserts[s];
				int base = T_FX + s * 10;
				ui_w_tip (addCtl (ui_checkbox (16, y + 5, 22, 22, (long) "", ps.enabled), base, 16, y + 5, 22, 22, UN_CHANGE), (long) "On / off");
				PlugInstance *pi0 = g_plug ? g_plug->trackInsert (track, s) : 0;
				bool dead = pi0 && (pi0->state () == PLUG_CRASHED || pi0->state () == PLUG_FAILED);
				btn (width - 50 - (dead ? 104 : 86), y + 4, dead ? 62 : 44, dead ? "Restart" : "Edit", base + 1);
				btn (width - 50 - 38, y + 4, 30, "x", base + 2, "Remove it");
				PlugInstance *pi = g_plug ? g_plug->trackInsert (track, s) : 0;
				if (showKnobs (s, pi))
				{
					int nk = imin (4, pi->paramCount ()), kw = (width - 66) / 4;
					for (int k = 0; k < nk; k++)
					{
						const PlugParam &pp = pi->info ().params[k];
						float v = pi->param (k);
						int iv = pp.max > pp.min ? (int) ((v - pp.min) / (pp.max - pp.min) * 1000 + 0.5f) : 0;
						knob (16 + k * kw, y + 34, kw - 4, 62, 1000, iv, base + 4 + k, TR (pp.name.c ()));
					}
					y += 104;
				}
				else y += 36;
			}
			if (tr.inserts.size () < 4) { btn (8, y + 4, width - 58, "+ Add an effect", T_ADD); y += 36; }
			// the reverb send
			if (tight) { knob (width - 46, 38, 44, 62, 127, tr.reverbSend (), T_REVERB, "Reverb"); meterY = 104; }
			else knob (16, height - 84, 70, 72, 127, tr.reverbSend (), T_REVERB, "Reverb");
		}
		meter = addCtl (ui_vumeter (width - 34, meterY, 14, height - meterY - 16, 65536), 0, width - 34, meterY, 14, height - meterY - 16, -1);
		invalidate (true);
	}
	bool showKnobs (int s, PlugInstance *pi) const { return s == open && m_knobs && pi && pi->ready (); }
	void onDraw () override
	{
		clearView (PANEL);
		vline (0, 0, height, LINE);
		if (track < 0 || track >= g_doc.p.tracks.size ()) { textL (16, 8, 22, TR ("SOUND CHAIN"), DIM, 2); return; }
		const Track &tr = g_doc.p.tracks[track];
		char b[96]; snprintf (b, sizeof b, TR ("%s - SOUND CHAIN"), tr.name.c ());
		for (char *q = b; *q; q++) if (*q >= 'a' && *q <= 'z') *q -= 32;
		textFit (16, 8, width - 60, 22, b, DIM, 2);
		if (tr.type == TRACK_CHORD)
		{
			wrapText (16, 44, width - 64, TR ("The chord track is silent: the accompaniments, the melodic lines and the rings of the other tracks play its chords."), FAINT, 18);
			return;
		}
		int y = 38;
		box (8, y, width - 50, 56, 6, PANEL2);
		textL (16, y + 4, 16, TR ("INSTRUMENT"), FAINT);
		const char *src = TR (!tr.instrumentPlugin.id.empty () ? "Plugin" : tr.type == TRACK_DRUM ? "Kit" : "SF2");
		snprintf (b, sizeof b, "%s - %s", src, instrumentName (tr));
		textFit (16, y + 22, width - 50 - (tr.instrumentPlugin.id.empty () ? 100 : 150), 30, b, TEXT, 2);
		y += 64;
		for (int s = 0; s < tr.inserts.size () && s < 4; s++)
		{
			const PluginSlot &ps = tr.inserts[s];
			PlugInstance *pi = g_plug ? g_plug->trackInsert (track, s) : 0;
			bool big = showKnobs (s, pi);
			int h = big ? 100 : 32;
			box (8, y, width - 50, h, 6, PANEL2);
			const PlugInfo *inf = g_plug ? g_plug->find (ps.id) : 0;
			const char *st = !pi ? (g_plug ? "not running" : "no plugins here") : pi->state () == PLUG_READY ? "" : pi->state () == PLUG_STARTING ? "starting" : pi->error ();
			snprintf (b, sizeof b, "%s%s%s", TR (inf ? inf->name.c () : ps.id.c ()), st[0] ? " - " : "", TR (st));
			textFit (44, y, width - 190, 32, b, ps.enabled ? TEXT : FAINT, big ? 2 : 0);
			y += h + 4;
		}
		if (!g_plug || !g_plug->available ())
			wrapText (16, height - 150, width - 64, TR ("Plugins (instruments, effects, generators) run as processes of their own: not available here."), FAINT, 17);
	}
	// the states of the track's plugins (a plugin still starting when the panel was made: made again
	// once it runs -- or crashed)
	unsigned plugStates () const
	{
		if (!g_plug || track < 0) return 0;
		unsigned h = 0;
		PlugInstance *pi = g_plug->trackInstrument (track);
		h = h * 31 + (pi ? (unsigned) pi->state () + 1 : 0);
		for (int s = 0; s < 4; s++) { PlugInstance *fx = g_plug->trackInsert (track, s); h = h * 31 + (fx ? (unsigned) fx->state () + 1 : 0); }
		return h;
	}
	void tick ()
	{
		if (plugStates () != m_states) rebuild ();
		if (meter && track >= 0 && track < ENGINE_MAX_TRACKS)
			{
			float l = fabsf (g_audio.engine.peakL[track]), r = fabsf (g_audio.engine.peakR[track]), v = l > r ? l : r;
			ui_vumeter_level (meter, v > 16.f ? 1 << 20 : (int) (v * 65536.f));
		}
	}
	void control (int tag, long w)
	{
		if (track < 0) return;
		Track &tr = g_doc.p.tracks[track];
		if (tag == T_INSTR) { if (g_arrange_chooseSound) g_arrange_chooseSound (track); return; }
		if (tag == T_INSTR_EDIT) { PlugInstance *pi = g_plug ? g_plug->trackInstrument (track) : 0; if (pi && pi->state () == PLUG_CRASHED) { g_plug->restart (pi); rebuild (); return; } if (pi && g_openPluginEditor) g_openPluginEditor (pi, tr.instrumentPlugin.id); return; }
		if (tag == T_REVERB) { tr.reverbOffset = (int) ui_w_get_value (w) - DEFAULT_REVERB; g_doc.dirty = true; return; }
		if (tag == T_ADD) { addEffect (); return; }
		if (tag >= T_FX)
		{
			int s = (tag - T_FX) / 10, k = (tag - T_FX) % 10;
			if (s >= tr.inserts.size ()) return;
			PlugInstance *pi = g_plug ? g_plug->trackInsert (track, s) : 0;
			switch (k)
			{
			case 0: g_doc.checkpoint (); tr.inserts[s].enabled = (ui_w_get_value (w) != 0); g_doc.changed (); syncPlugins (); rebuild (); break;
			case 1:
				if (pi && (pi->state () == PLUG_CRASHED || pi->state () == PLUG_FAILED)) { g_plug->restart (pi); rebuild (); break; }	// (its Edit: Restart)
				if (open != s) { open = s; rebuild (); } else if (pi && g_openPluginEditor) g_openPluginEditor (pi, tr.inserts[s].id);
				break;
			case 2: savePluginStates (); g_doc.checkpoint (); tr.inserts.removeAt (s); g_doc.changed (); syncPlugins (); open = 0; rebuild (); break;
			default:
				if (pi && pi->ready () && k - 4 < pi->paramCount ())
				{
					const PlugParam &pp = pi->info ().params[k - 4];
					g_plug->setParam (pi, k - 4, pp.min + (pp.max - pp.min) * (int) ui_w_get_value (w) / 1000.0f);
					g_doc.dirty = true;
				}
			}
		}
	}
	void addEffect ()
	{
		if (!g_plug || !g_plug->available () || !g_plug->countKind (KP_EFFECT)) { { char m[160]; snprintf (m, sizeof m, TR ("No effect plugin here (they live in %s)."), KP_DIR); message ("Effects", m, UM_OK); } return; }
		Track &tr = g_doc.p.tracks[track];
		Popup *m = new Popup (absX (this) + 16, absY (this) + height / 2);
		int ids[16], n = 0;
		for (int i = 0; i < g_plug->count () && n < 16; i++) if (g_plug->info (i).kind == KP_EFFECT) { m->add (g_plug->info (i).name, 1 + i); ids[n++] = i; }
		int r = m->run ();
		delete m;
		if (r < 1) return;
		savePluginStates ();
		g_doc.checkpoint ();
		PluginSlot ps; ps.id = g_plug->info (r - 1).id; ps.enabled = true;
		tr.inserts.push (ps);
		g_doc.changed ();
		syncPlugins ();
		open = tr.inserts.size () - 1;
		rebuild ();
		(void) ids;
	}
	static void (*g_arrange_chooseSound) (int track);
};
void (*ChainPanel::g_arrange_chooseSound) (int) = 0;

// ---- a plugin's own editor, floating over the window --------------------------------------------------
class PluginWindow : public View
{
public:
	enum { TITLE = 30 };
	PlugEditorView *view;
	char title[64];
	PluginWindow (int x, int y, int w, int h, const char *t) : View (x, y, w, h), view (0) { snprintf (title, sizeof title, "%s", t); floating = true; }
	void onDraw () override
	{
		clearView (PANEL);
		box (0, 0, width, TITLE, 0, PANEL2);
		textL (12, 0, TITLE, title, TEXT, 2);
		box (width - 30, 5, 22, 20, 4, FACE);
		textC (width - 30, 5, 22, 20, "x", TEXT);
		frame (0, 0, width, height, 0, ACC2);
	}
	bool onMouse (int mx, int my, int bl, int br, int bm, int wheel) override
	{
		(void) br; (void) bm; (void) wheel;
		int e = m_b.edge (bl, 0);
		if (e == 1 && my < TITLE && mx >= width - 32) { closing = true; hidden = true; if (parent) parent->invalidate (true); return true; }
		if (e == 1 && my < TITLE) { m_drag = true; m_dx = mx; m_dy = my; catchOutside = true; return true; }
		if (m_drag && bl) { left += mx - m_dx; top += my - m_dy; if (parent) parent->invalidate (true); return true; }
		if (m_drag && !bl) { m_drag = false; catchOutside = false; }
		return true;
	}
	void closeMe ();
	// (its x asks: closed by the app's next tick -- deleted here, it would still be in the mouse's
	// dispatch, the parent keeping it as the last one that took the mouse: a crash on the next move)
	bool closing = false;
private:
	Buttons m_b; bool m_drag = false; int m_dx = 0, m_dy = 0;
};
extern PluginWindow *g_pluginWindow;
void PluginWindow::closeMe ()
{
	if (view && g_plug) { g_plug->closeEditor (view); view = 0; }
	View *p = parent;
	if (p) { p->removeChild (this); p->invalidate (true); }
	g_pluginWindow = 0;
	delete this;
}

} // namespace kui

#endif
