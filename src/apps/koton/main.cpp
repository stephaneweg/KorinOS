//
// koton -- Onyx's studio: Koton Studio (the user's DAW for Windows) made again for Onyx. A song is
// thought in harmony: a silent chord track (degree-locked chords, the next-chord co-pilot, cadences)
// drives every other part -- accompaniments that play its chords in a style or a drawn grid, melodic
// lines whose pitches come from it, riffs drawn over its shaded tones, drums from a catalog or drawn,
// euclidean rings in polyrhythm. Koton's .sq files open (what Onyx lacks is dropped); Onyx saves
// .kson (the same JSON, plus its own fields).
//
// The sound (ARMPI): AudioKit's synthesizer (a kit in assembly, ak_synth_*) -- one per track --, mixed and
// soft-clipped by the engine on a thread of its own. See ui/audio.h. The AI and the plugins are Onyx's: not here.
//
// The pieces: engine/ (the model, the harmony, the generators, the compiler, the audio engine),
// ui/ (palette.h: the look; doc.h: the song, undo; audio.h: the sound; arrange.h:
// the arrangement; grid.h: the note grids; editor.h + ed_*.h: the block editors; dialogs.h; chrome.h:
// the transport bar, the browser, the editor's host, the status bar).
//
// Files: SD0:/home/koton/songs (the songs, .kson / .sq), SD0:/res/soundfonts (the .sf2: the package generaluser-gs, shared with Media Player), SD0:/home/koton/settings.json;
// "koton SD0:/home/koton/songs/a.kson" opens it.
//
#include "ui/view.h"
#include "ui/chrome.h"
#include "ui/ai_dialog.h"
#include "ui/chain.h"
#include "ui/pocket.h"		// the pocket mode on a small screen: it plays and mixes (g_pk)

using namespace kui;

// ---- the globals the ui headers declare -------------------------------------------------------------------------
namespace kui {
alignas (Doc) static char s_docMem[sizeof (Doc)];
Doc &g_doc = *(Doc *) s_docMem;
AudioHost g_audio;
SoundNames g_names;
DrumCatalog g_drumCatalog;
Editor *g_editor = 0;
void (*g_onEdited) () = 0;
bool g_rebuildEditor = false;
char g_status[160] = "Welcome to Koton.";
Commands g_cmd;
ArrangeView *g_arrange = 0;
bool g_metronome = false;
Face *g_bigFace = 0;
PlugHost *g_plug = 0;
void (*g_openPluginEditor) (PlugInstance *p, const char *title) = 0;
PluginWindow *g_pluginWindow = 0;
}

static TopBar *g_top;
static Browser *g_browser;
static EditorHost *g_host;
static StatusBar *g_statusBar;
static ChainPanel *g_chain;
static int g_editorH = 360;
// POCKET on a small screen (640 x 480, no pointer): Koton plays a project and mixes it -- ui/pocket.h's one widget over
// the window, the desktop's parts made as ever and hidden. Decided once in main; false on the desktop: nothing changes.
static bool g_pk = false;
static PocketKoton *g_pocket = 0;
static void pocket_layout (AppWindow *r);

// the main parts placed for the window's size (the editor's pane keeps its height, within reason)
static void layoutMain (View &root)
{
	int W = root.width, H = root.height;
	int bodyTop = TopBar::H, bodyH = H - TopBar::H - StatusBar::H;
	g_editorH = iclamp (g_editorH, 200, imax (200, bodyH - 180));
	g_top->left = 0; g_top->top = 0; g_top->resizeTo (W, TopBar::H);
	g_browser->left = W - Browser::W; g_browser->top = bodyTop; g_browser->resizeTo (Browser::W, bodyH);
	g_host->left = 0; g_host->top = bodyTop + bodyH - g_editorH; g_host->resizeTo (W - Browser::W - ChainPanel::W, g_editorH);
	g_chain->left = W - Browser::W - ChainPanel::W; g_chain->top = bodyTop + bodyH - g_editorH; g_chain->resizeTo (ChainPanel::W, g_editorH);
	g_arrange->left = 0; g_arrange->top = bodyTop; g_arrange->resizeTo (W - Browser::W, bodyH - g_editorH);
	g_statusBar->left = 0; g_statusBar->top = H - StatusBar::H; g_statusBar->resizeTo (W, StatusBar::H);
	g_top->placeChildren ();
	if (g_pk) pocket_layout ((AppWindow *) &root);
	root.invalidate (true);
}
static void onSplit (int dy)
{
	AppWindow *r = AppWindow::current ();
	int h = g_editorH - dy;
	if (!r || h == g_editorH) return;
	g_editorH = h;
	layoutMain (*r);
	g_rebuildEditor = true;
}

// ---- settings (SD0:/home/koton/settings.json) -----------------------------------------------------------------------------
struct Settings
{
	char soundfont[256], lastDir[256], aiProvider[32], aiModel[64], aiKey[160];
	AiLast aiLast;			// Compose with AI's last request
	Settings () { soundfont[0] = 0; snprintf (lastDir, sizeof lastDir, "SD0:/home/koton/songs"); snprintf (aiProvider, sizeof aiProvider, "gemini"); snprintf (aiModel, sizeof aiModel, "gemini-flash-latest"); aiKey[0] = 0; }
	void load ()
	{
		unsigned got = 0;
		char *b = readFile ("SD0:/home/koton/settings.json", &got);
		if (!b) return;
		json::Doc d;
		if (got > 0 && d.parse (b, (unsigned long) got, json::TOLERANT))
		{
			const json::Value &r = d.root ();
			snprintf (soundfont, sizeof soundfont, "%s", r["soundfont"].asStr (""));
			snprintf (lastDir, sizeof lastDir, "%s", r["lastDir"].asStr ("SD0:/home/koton/songs"));
			snprintf (aiProvider, sizeof aiProvider, "%s", r["aiProvider"].asStr ("gemini"));
			snprintf (aiModel, sizeof aiModel, "%s", r["aiModel"].asStr ("gemini-flash-latest"));
			snprintf (aiKey, sizeof aiKey, "%s", r["aiKey"].asStr (""));
			const json::Value &a = r["aiLast"];
			AiLast &L = aiLast;
			L.kind = a["kind"].asInt (L.kind);
			L.style = a["style"].asStr (L.style.c ());
			L.intention = a["intention"].asStr (L.intention.c ());
			L.bars = a["bars"].asInt (L.bars);
			L.full = a["fullMelody"].asBool (L.full); L.drums = a["drums"].asBool (L.drums); L.voice = a["chordsVoice"].asBool (L.voice);
			L.pchords = a["polyChords"].asBool (L.pchords); L.pdrums = a["polyDrums"].asBool (L.pdrums);
		}
		free (b);
	}
	void save ()
	{
		json::Writer w (true);
		w.beginObj ();
		w.key ("soundfont"); w.str (soundfont);
		w.key ("lastDir"); w.str (lastDir);
		w.key ("aiProvider"); w.str (aiProvider);
		w.key ("aiModel"); w.str (aiModel);
		w.key ("aiKey"); w.str (aiKey);
		w.key ("aiLast"); w.beginObj ();
		w.key ("kind"); w.num (aiLast.kind);
		w.key ("style"); w.str (aiLast.style.c ());
		w.key ("intention"); w.str (aiLast.intention.c ());
		w.key ("bars"); w.num (aiLast.bars);
		w.key ("fullMelody"); w.boolean (aiLast.full); w.key ("drums"); w.boolean (aiLast.drums); w.key ("chordsVoice"); w.boolean (aiLast.voice);
		w.key ("polyChords"); w.boolean (aiLast.pchords); w.key ("polyDrums"); w.boolean (aiLast.pdrums);
		w.endObj ();
		w.endObj ();
		mkdir ("SD0:/home/koton", 0755);
		if (w.ok ()) writeFile ("SD0:/home/koton/settings.json", w.data (), (unsigned) w.size ());
	}
};
static Settings g_settings;

static void setStatus (const char *fmt, const char *a = "")
{
	snprintf (g_status, sizeof g_status, TR (fmt), a);
	if (g_statusBar) g_statusBar->invalidate (true);
}

// ---- the views follow the song -------------------------------------------------------------------------------------
static void refreshAll ()
{
	if (g_pocket) g_pocket->invalidate (true);
	if (g_arrange) g_arrange->invalidate (true);
	if (g_top) g_top->invalidate (true);
	if (g_statusBar) g_statusBar->invalidate (true);
	if (g_host) g_host->invalidate (true);
}
static void onEdited () { refreshAll (); }
static void onSelect () { g_rebuildEditor = true; refreshAll (); }

// ---- files -----------------------------------------------------------------------------------------------------------
static void titleUpdate ()
{
	char t[300];
	const char *f = g_doc.path[0] ? g_doc.path : TR ("untitled");
	const char *s = strrchr (f, '/'); if (s) f = s + 1;
	snprintf (t, sizeof t, "Koton - %s%s", f, g_doc.dirty ? " *" : "");
	(void) t;					// (the status bar shows the file and its state)
}
// false: the user cancelled (unsaved changes)
static bool saveAs ();
static bool askSave ()
{
	if (!g_doc.dirty) return true;
	int r = message ("Koton", "The song has unsaved changes. Save them?", UM_YESNOCANCEL);
	if (r == 0) return false;
	if (r == 1)
	{
		const char *ext = strrchr (g_doc.path, '.');
		if (!g_doc.path[0] || !ext || strcmp (ext, ".kson")) return saveAs ();
		return g_doc.save (g_doc.path);
	}
	return true;
}
static void afterLoad ()
{
	g_audio.stopPlay ();
	g_host->stopListening ();
	if (g_arrange) { g_arrange->scrollBeat = 0; g_arrange->scrollY = 0; g_arrange->cursorBeat = 0; g_arrange->loopOn = false; }
	g_rebuildEditor = true;
	refreshAll ();
}
static bool openFile (const char *path)
{
	char err[160];
	if (!g_doc.load (path, err, sizeof err)) { char m[300]; snprintf (m, sizeof m, TR ("Cannot open the song: %s"), TR (err)); message ("Koton", m, UM_OK); return false; }
	const char *ext = strrchr (path, '.');
	if (ext && !strcmp (ext, ".sq")) { g_doc.path[0] = 0; setStatus ("Opened a Koton Studio song (.sq): Save writes a .kson."); }
	else setStatus ("Opened %s", path);
	afterLoad ();
	return true;
}
static void cmdNew () { if (!askSave ()) return; g_doc.newSong (); afterLoad (); setStatus ("A new song."); }
static void cmdOpen ()
{
	if (!askSave ()) return;
	char p[256];
	mkdir ("SD0:/home/koton", 0755); mkdir ("SD0:/home/koton/songs", 0755);
	if (!fileOpen (p, sizeof p, g_settings.lastDir, "Koton songs|*.kson;*.sq|All files|*")) return;
	if (openFile (p))
	{
		char *s = strrchr (p, '/'); if (s) { *s = 0; snprintf (g_settings.lastDir, sizeof g_settings.lastDir, "%s", p); g_settings.save (); }
	}
}
static bool saveAs ()
{
	savePluginStates ();
	char p[256];
	mkdir ("SD0:/home/koton", 0755); mkdir ("SD0:/home/koton/songs", 0755);
	if (!fileSave (p, sizeof p, g_settings.lastDir, "song.kson", "Koton songs|*.kson|All files|*")) return false;
	int n = (int) strlen (p);
	if (n < 5 || strcmp (p + n - 5, ".kson")) { const char *d = strrchr (p, '.'); const char *sl = strrchr (p, '/'); if (d && (!sl || d > sl)) p[d - p] = 0; snprintf (p + strlen (p), sizeof p - strlen (p), ".kson"); }
	g_doc.pruneRiffs ();
	if (!g_doc.save (p)) { message ("Koton", "The song could not be saved.", UM_OK); return false; }
	setStatus ("Saved %s", p);
	refreshAll ();
	return true;
}
static void cmdSave ()
{
	savePluginStates ();
	if (!g_doc.path[0]) { saveAs (); return; }
	g_doc.pruneRiffs ();
	if (g_doc.save (g_doc.path)) setStatus ("Saved %s", g_doc.path); else message ("Koton", "The song could not be saved.", UM_OK);
	refreshAll ();
}
static void cmdSaveAs () { saveAs (); }
static void exportProgress (int pct)
{
	snprintf (g_status, sizeof g_status, TR ("Exporting the song... %d %%"), pct);
	AppWindow *r = AppWindow::current ();
	if (r && g_statusBar) { g_statusBar->invalidate (true); r->draw (); }
}
static void cmdExport ()
{
	char p[256];
	if (!fileSave (p, sizeof p, g_settings.lastDir, "song.wav", "WAV audio|*.wav|All files|*")) return;
	g_audio.sync (g_doc);
	if (g_audio.exportWav (g_doc, p, exportProgress)) setStatus ("Exported %s", p);
	else setStatus ("The export failed (no SoundFont, or the file could not be written).");
}

// ---- edits ---------------------------------------------------------------------------------------------------------
static void pushPluginStates ();
static void cmdUndo () { savePluginStates (); if (g_doc.undo ()) { pushPluginStates (); g_rebuildEditor = true; setStatus ("Undone."); refreshAll (); } }
static void cmdRedo () { savePluginStates (); if (g_doc.redo ()) { pushPluginStates (); g_rebuildEditor = true; setStatus ("Redone."); refreshAll (); } }
static void cmdDuplicate () { if (g_arrange) g_arrange->duplicateSelected (); }
static void cmdDelete () { if (g_arrange) g_arrange->deleteSelected (); }

// ---- the transport ---------------------------------------------------------------------------------------------------
static void playFrom (double beat)
{
	g_host->stopListening ();
	g_audio.play (g_doc, beat);
	if (g_arrange) g_audio.loop (g_doc, g_arrange->loopOn, g_arrange->loopA, g_arrange->loopB);
	refreshAll ();
}
static void cmdPlayStop ()
{
	if (g_audio.isPlaying ()) { g_audio.stopPlay (); refreshAll (); return; }
	if (!g_audio.sf) { setStatus ("The SoundFont is still loading."); return; }
	playFrom (g_arrange ? g_arrange->cursorBeat : 0);
}
static void cmdStop ()
{
	if (g_audio.isPlaying ()) g_audio.stopPlay ();
	else if (g_arrange) g_arrange->cursorBeat = 0;
	g_host->stopListening ();
	refreshAll ();
}
static void cmdRewind ()
{
	if (g_arrange) { g_arrange->cursorBeat = 0; g_arrange->ensureVisible (0); }
	if (g_audio.isPlaying ()) g_audio.seek (g_doc, 0);
	refreshAll ();
}
static void cmdLoop ()
{
	if (!g_arrange) return;
	if (!g_arrange->loopOn && g_arrange->loopB <= g_arrange->loopA)
	{
		// the selected block's span, else the first 4 bars
		int bb = imax (1, g_doc.p.barBeats ());
		if (g_doc.sel.valid ()) { g_arrange->loopA = g_doc.itemStart (g_doc.sel.track, g_doc.sel.item); g_arrange->loopB = g_arrange->loopA + g_doc.itemLen (g_doc.sel.track, g_doc.sel.item); }
		else { g_arrange->loopA = 0; g_arrange->loopB = 4 * bb; }
	}
	g_arrange->loopOn = !g_arrange->loopOn;
	refreshAll ();
}
static void cmdMetronome () { g_metronome = !g_metronome; g_audio.metronome (g_metronome); refreshAll (); }
static void cmdSong ()
{
	SongDialog *d = new SongDialog ();
	if (d->run () == 1) { g_doc.checkpoint (); d->apply (); g_doc.changed (); g_rebuildEditor = true; refreshAll (); }
	delete d;
}
static void cmdCadence () { cadenceOnChordTrack (); refreshAll (); }
static void cmdSoundFont ()
{
	char p[256];
	mkdir ("SD0:/res", 0755); mkdir ("SD0:/res/soundfonts", 0755);
	if (!fileOpen (p, sizeof p, "SD0:/res/soundfonts", "SoundFonts|*.sf2|All files|*")) return;
	int n = (int) strlen (p);
	if (n < 4 || strcasecmp (p + n - 4, ".sf2")) { message ("SoundFont", "Choose a .sf2 file.", UM_OK); return; }
	snprintf (g_settings.soundfont, sizeof g_settings.soundfont, "%s", p);
	g_settings.save ();
	message ("SoundFont", "Koton will use it from its next start.", UM_OK);
}
// (Onyx fetches GeneralUser GS with SD:/bin/llm, a TLS download: not here)
static void cmdGetSoundFont ()
{
	message ("SoundFont", "Not available on Korin OS: copy a .sf2 file into SD0:/res/soundfonts.", UM_OK);
}
static void cmdZoomIn () { if (g_arrange) { g_arrange->ppb = dmin (160, g_arrange->ppb * 1.25); g_arrange->invalidate (true); } }
static void cmdZoomOut () { if (g_arrange) { g_arrange->ppb = dmax (2, g_arrange->ppb * 0.8); g_arrange->invalidate (true); } }

// the instrument of the selected track
static int selectedTrack () { int t = g_doc.sel.valid () ? g_doc.sel.track : g_doc.selTrack; return iclamp (t, 0, imax (0, g_doc.p.tracks.size () - 1)); }
static void cmdInstrument () { if (g_arrange && g_doc.p.tracks.size ()) g_arrange->chooseSound (selectedTrack ()); }
static void cmdRenameTrack () { if (g_arrange && g_doc.p.tracks.size ()) g_arrange->renameTrack (selectedTrack ()); }
static void cmdAddTrack ()
{
	g_doc.checkpoint ();
	char nm[32]; snprintf (nm, sizeof nm, TR ("Track %d"), g_doc.p.tracks.size ());
	g_doc.selTrack = g_doc.addTrack (nm, TRACK_INSTRUMENT, 0);
	g_doc.changed (); refreshAll ();
}
static void cmdAddDrums () { g_doc.checkpoint (); g_doc.selTrack = g_doc.addTrack (TR ("Drums"), TRACK_DRUM, 0); g_doc.changed (); refreshAll (); }

// ---- the AI (ai.h + SD0:/bin/llm): see ui/ai_dialog.h when present ------------------------------------------------------
static void cmdCompose ();
static void composeKind (int kind);

// ---- the browser -------------------------------------------------------------------------------------------------------
// the track a generator goes on: the selected one when it fits, else the first that does
static int trackFor (int kind)
{
	const Project &p = g_doc.p;
	int want = kind == M_PATTERN || kind == M_POLYCHORD ? TRACK_CHORD : (kind == M_DRUMKIT || kind == M_POLYDRUM) ? TRACK_DRUM : TRACK_INSTRUMENT;
	int s = selectedTrack ();
	if (kind == M_POLYCHORD && s < p.tracks.size () && p.tracks[s].type == TRACK_INSTRUMENT) return s;
	if (s < p.tracks.size () && p.tracks[s].type == want) return s;
	for (int t = 0; t < p.tracks.size (); t++) if (p.tracks[t].type == want) return t;
	return -1;
}
static void browseInsert (int kind)
{
	int t = trackFor (kind);
	if (t < 0)
	{
		if (kind == M_DRUMKIT || kind == M_POLYDRUM) { g_doc.checkpoint (); t = g_doc.addTrack (TR ("Drums"), TRACK_DRUM, 0); g_doc.changed (); }
		else { setStatus ("No track for it: add a track first."); return; }
	}
	// at the playhead's cursor when that place is free, else after the track's last block
	double at = g_arrange ? g_arrange->cursorBeat : 0;
	if (g_doc.itemAt (t, at) >= 0 || at < g_doc.p.trackEnd (g_doc.p.tracks[t]) - 1e-9) at = -1;
	insertBlock (*g_arrange, t, kind, at);
	refreshAll ();
}
// ---- the plugins -------------------------------------------------------------------------------------------------
static Vec<int> g_browser_pending;			// (the catalogue, for the browser)
static void onPluginCrash (PlugInstance *p, void *ctx) { (void) ctx; char m[160]; snprintf (m, sizeof m, TR ("The plugin %s stopped (%s): its track is silent."), TR (p->info ().name.c ()), TR (p->error ())); setStatus ("%s", m); if (g_chain) g_chain->invalidate (true); }
// a plugin's editor changed it: a generator's state goes back into its modules (the song recompiled)
static void onPluginDirty (PlugInstance *p, void *ctx)
{
	(void) ctx;
	if (p->isGenerator () && g_plug->pullGeneratorState (p, g_doc.p)) { g_doc.changed (); g_rebuildEditor = true; refreshAll (); } else g_doc.dirty = true;
}
static void onPluginParam (PlugInstance *p, int index, float value, void *ctx) { (void) index; (void) value; onPluginDirty (p, ctx); }
// after an undo / redo: the running plugins take the states the song holds again
static void pushPluginStates ()
{
	if (!g_plug || !g_plug->available ()) return;
	syncPlugins ();
	for (int t = 0; t < g_doc.p.tracks.size () && t < ENGINE_MAX_TRACKS; t++)
	{
		const Track &tr = g_doc.p.tracks[t];
		PlugInstance *pi = g_plug->trackInstrument (t);
		if (pi && pi->ready () && !tr.instrumentPlugin.state.empty ()) g_plug->setState (pi, tr.instrumentPlugin.state, 300);
		for (int s = 0; s < tr.inserts.size () && s < 4; s++)
		{
			PlugInstance *fx = g_plug->trackInsert (t, s);
			if (fx && fx->ready () && !tr.inserts[s].state.empty ()) g_plug->setState (fx, tr.inserts[s].state, 300);
		}
	}
}
static void pluginInstruments (Vec<Str> &ids, Vec<Str> &names)
{
	if (!g_plug || !g_plug->available ()) return;
	for (int i = 0; i < g_plug->count (); i++) if (g_plug->info (i).kind == KP_INSTRUMENT) { ids.push (g_plug->info (i).id); names.push (g_plug->info (i).name); }
}
static void chooseSoundOf (int t) { if (g_arrange) g_arrange->chooseSound (t); }
static void showPluginWindow (PluginWindow *w, PlugEditorView *v)
{
	AppWindow *r = AppWindow::current ();
	w->view = v;
	r->addChild (w);
	g_pluginWindow = w;
	r->invalidate (true);
}
static void openPluginEditor (PlugInstance *p, const char *title)
{
	if (!g_plug || !p) return;
	if (g_pluginWindow) g_pluginWindow->closeMe ();
	AppWindow *r = AppWindow::current ();
	int w, h; g_plug->editorSize (p, &w, &h);
	w = imax (240, imin (w, r->width - 80)); h = imax (160, imin (h, r->height - 120));
	PluginWindow *pw = new PluginWindow ((r->width - w) / 2, (r->height - h) / 2, w + 2, h + PluginWindow::TITLE + 1, title);
	PlugEditorView *v = g_plug->openEditor (p, *pw, 1, PluginWindow::TITLE, w, h);
	showPluginWindow (pw, v);
}
static void openGeneratorEditor (int t, int i)
{
	Module *m = g_doc.module (t, i);
	if (!g_plug || !g_plug->available () || !m || m->kind != M_GENERATOR) { message ("Generator", "Plugins are not available here.", UM_OK); return; }
	if (g_pluginWindow) g_pluginWindow->closeMe ();
	GeneratorModule *gm = (GeneratorModule *) m;
	AppWindow *r = AppWindow::current ();
	const PlugInfo *inf = g_plug->find (gm->generatorId);
	int w, h; g_plug->editorSize (inf, &w, &h);			// (as an instrument's or an effect's: its knobs' rows)
	w = imax (240, imin (w, r->width - 80)); h = imax (160, imin (h, r->height - 120));
	PluginWindow *pw = new PluginWindow ((r->width - w) / 2, (r->height - h) / 2, w + 2, h + PluginWindow::TITLE + 1, inf ? inf->name.c () : gm->generatorId.c ());
	PlugEditorView *v = g_plug->openGeneratorEditor (*gm, *pw, 1, PluginWindow::TITLE, w, h);
	showPluginWindow (pw, v);
}
// a plugin of the browser: an instrument for the selected track, an effect on it, a generator block on it
static void browsePlugin (int i)
{
	if (!g_plug || i < 0 || i >= g_plug->count ()) return;
	const PlugInfo &pi = g_plug->info (i);
	int t = selectedTrack ();
	if (t >= g_doc.p.tracks.size ()) return;
	Track &tr = g_doc.p.tracks[t];
	if (tr.type == TRACK_CHORD) { setStatus ("Choose an instrument track first."); return; }
	savePluginStates ();
	g_doc.checkpoint ();
	if (pi.kind == KP_INSTRUMENT) { tr.instrumentPlugin.id = pi.id; tr.instrumentPlugin.state = ""; tr.instrumentPlugin.enabled = true; setStatus ("%s plays the track.", TR (pi.name.c ())); }
	else if (pi.kind == KP_EFFECT)
	{
		if (tr.inserts.size () >= 4) { setStatus ("A track has four effects at most."); return; }
		PluginSlot ps; ps.id = pi.id; tr.inserts.push (ps);
	}
	else
	{
		GeneratorModule *gm = new GeneratorModule; gm->id = newId (); gm->generatorId = pi.id; gm->durationBeats = 4 * imax (1, g_doc.p.barBeats ());
		double at = g_arrange ? g_arrange->cursorBeat : 0;
		if (g_doc.itemAt (t, at) >= 0 || at < g_doc.p.trackEnd (tr) - 1e-9) at = -1;
		int it = at < 0 ? g_doc.append (t, gm) : g_doc.place (t, gm, at);
		g_arrange->select (t, it);
	}
	g_doc.changed ();
	syncPlugins ();
	if (g_chain) g_chain->rebuild ();
	refreshAll ();
}

static void browse (int a)
{
	switch (a)
	{
	case BR_CHORD: browseInsert (M_PATTERN); break;
	case BR_CADENCE: cmdCadence (); break;
	case BR_CHAIN:
	{
		int ci = g_doc.p.chordTrackIndex ();
		if (ci < 0) break;
		if (!g_doc.p.tracks[ci].items.size ()) { browseInsert (M_PATTERN); break; }
		int from = g_doc.sel.valid () && g_doc.sel.track == ci ? g_doc.sel.item : g_doc.p.tracks[ci].items.size () - 1;
		g_doc.checkpoint ();
		int last = chainProgression (g_doc, ci, from, 4);
		g_doc.changed ();
		g_arrange->select (ci, last);
		refreshAll ();
	} break;
	case BR_POLYCHORD: browseInsert (M_POLYCHORD); break;
	case BR_ACCOMP: browseInsert (M_ARTICULATION); break;
	case BR_DRUMS: browseInsert (M_DRUMKIT); break;
	case BR_POLYDRUM: browseInsert (M_POLYDRUM); break;
	case BR_RIFF: browseInsert (M_PLAYRIFF); break;
	case BR_LINE: browseInsert (M_MELODICLINE); break;
	case BR_RINGS: browseInsert (M_MELODICPOLY); break;
	case BR_AI_COMPOSE: composeKind (AI_COMPOSE); break;
	case BR_AI_TRACK: composeKind (AI_ADD_TRACK); break;
	case BR_AI_DRUMS: composeKind (AI_ADD_DRUMS); break;
	case BR_AI_DEVELOP: composeKind (AI_DEVELOP); break;
	case BR_ADD_TRACK: cmdAddTrack (); break;
	case BR_ADD_DRUMS: cmdAddDrums (); break;
	default: if (a >= BR_PLUGIN) browsePlugin (a - BR_PLUGIN); break;
	}
}

static void saveSettings () { g_settings.save (); }
static void composeKind (int kind)
{
	AiSettings s = { g_settings.aiProvider, (int) sizeof g_settings.aiProvider, g_settings.aiModel, (int) sizeof g_settings.aiModel,
		g_settings.aiKey, (int) sizeof g_settings.aiKey, &g_settings.aiLast, saveSettings, askSave };
	g_audio.stopPlay ();
	if (aiCompose (s, kind)) afterLoad ();
	refreshAll ();
}
static void cmdCompose () { composeKind (-1); }		// (the kind last asked)

// ---- pocket: the view over the window, its hint line, the Menu's "not in pocket" ---------------------------------------
static void pocket_hints (AppWindow *r)
{
	if (!g_pocket) return;
	char b[256];
	if (g_pocket->card) snprintf (b, sizeof b, "-\t%s\nA\t%s\nB\t%s", TR ("choose"), TR ("OK"), TR ("back to the song"));
	else if (g_pocket->view == 0) snprintf (b, sizeof b, "|\t%s\n-\t%s\n[Space]\t%s\nA\t%s\n[S]\t%s", TR ("track"), TR ("the bar"), TR ("play"), TR ("mute"), TR ("solo"));
	else snprintf (b, sizeof b, "-\t%s\n|\t%s\nA\t%s\n[S]\t%s\n[Space]\t%s", TR ("track"), TR ("level"), TR ("mute"), TR ("solo"), TR ("play"));
	int was = hintLineHeight ();
	if (hintLine (r, b) != was) pocket_layout (r);
}
static void pocket_layout (AppWindow *r)
{
	if (!g_pocket || !r) return;
	g_pocket->left = 0; g_pocket->top = 0;
	g_pocket->resizeTo (r->width, r->height - hintLineHeight ());
	g_pocket->invalidate (true);
}
static void cmdPocketCard () { if (g_pocket) g_pocket->showCard (); }
static void cmdPocketSong () { if (g_pocket) { g_pocket->view = 0; g_pocket->card = false; g_pocket->setFocus (); g_pocket->invalidate (true); } }
static void cmdPocketMixer () { if (g_pocket) { g_pocket->view = 1; g_pocket->card = false; g_pocket->setFocus (); g_pocket->invalidate (true); } }

// ---- the window ----------------------------------------------------------------------------------------------------------
class KotonRoot : public AppWindow
{
public:
	unsigned lastRev, lastRevTick, lastSyncTick;
	bool lastPlaying; bool lastLoopOn; double lastLoopA, lastLoopB;
	int lastSel[2];
	int lastW, lastH;
	unsigned plugRev = 0; int chainTrack = -2; bool chainRebuild = true;
	KotonRoot (int w, int h) : AppWindow (w, h, "Koton"), lastRev (0), lastRevTick (0), lastSyncTick (0), lastPlaying (false), lastLoopOn (false),
		lastLoopA (0), lastLoopB (0), lastW (w), lastH (h) { bg = BG; lastSel[0] = lastSel[1] = -2; }
	void onTick () override
	{
		unsigned now = ticks ();
		if (g_pluginWindow && g_pluginWindow->closing) g_pluginWindow->closeMe ();
		// the song compiled again once the edits pause (a drag recompiles a few times a second, not every move)
		if (g_doc.revision != lastRev) { lastRev = g_doc.revision; lastRevTick = now; }
		if (g_audio.compiledRev != g_doc.revision && (now - lastRevTick >= 6 || now - lastSyncTick >= 25)) { g_audio.sync (g_doc); lastSyncTick = now; }
		else g_audio.sync (g_doc);			// (the mixer: volume, pan, mute, solo -- no compile when unchanged)
		g_audio.tick ();
		// the loop follows the ruler
		if (g_arrange && (g_arrange->loopOn != lastLoopOn || g_arrange->loopA != lastLoopA || g_arrange->loopB != lastLoopB))
		{
			lastLoopOn = g_arrange->loopOn; lastLoopA = g_arrange->loopA; lastLoopB = g_arrange->loopB;
			g_audio.loop (g_doc, lastLoopOn, lastLoopA, lastLoopB);
			if (g_top) g_top->invalidate (true);
		}
		// the selection -> its editor
		if (g_doc.sel.track != lastSel[0] || g_doc.sel.item != lastSel[1]) { lastSel[0] = g_doc.sel.track; lastSel[1] = g_doc.sel.item; g_rebuildEditor = true; }
		if (g_rebuildEditor) { g_rebuildEditor = false; g_host->rebuild (); lastSel[0] = g_doc.sel.track; lastSel[1] = g_doc.sel.item; chainRebuild = true; }
		g_host->tick ();
		g_top->tick ();
		if (g_pocket) { g_pocket->tick (); pocket_hints (this); }
		if (g_chain) g_chain->tick ();
		if (g_plug && g_plug->available ())
		{
			g_plug->poll ();
			if (g_doc.revision != plugRev) { plugRev = g_doc.revision; syncPlugins (); }
		}
		int ct = g_doc.sel.valid () ? g_doc.sel.track : g_doc.selTrack;
		if (ct != chainTrack || chainRebuild) { chainTrack = ct; chainRebuild = false; g_chain->rebuild (); }
		// the playhead
		bool playing = g_audio.isPlaying ();
		if (playing || playing != lastPlaying)
		{
			if (playing && g_arrange) g_arrange->ensureVisible (g_audio.playheadBeat ());
			if (g_arrange) g_arrange->invalidate (true);
			g_top->invalidate (true);
			lastPlaying = playing;
		}
		static unsigned lastStatus;
		if (now - lastStatus >= 50) { lastStatus = now; g_statusBar->invalidate (true); titleUpdate (); }
	}
	void onResized () override
	{
		if (width != lastW || height != lastH)
		{
			if (height > lastH) g_editorH = g_editorH + (height - lastH) * 2 / 5;	// (the pane takes its share of the growth)
			lastW = width; lastH = height;
			layoutMain (*this);
			g_rebuildEditor = true;
		}
	}
	bool onKey (long k) override
	{
		if (g_pocket) { if (!g_pocket->hasFocus) g_pocket->setFocus (); return g_pocket->onKey (k); }	// (pocket: the view's keys, whatever had them)
		switch (k)
		{
		case ' ': cmdPlayStop (); return true;
		case K_HOME: cmdRewind (); return true;
		case 27: cmdStop (); return true;
		case K_DEL: cmdDelete (); return true;
		}
		return false;
	}
	void onDrop (int x, int y, bool files, const char *data, int len) override
	{
		(void) x; (void) y;
		if (!files || len <= 0) return;
		char p[256]; int n = 0;
		while (n < len && n < 255 && data[n] && data[n] != '\n') { p[n] = data[n]; n++; }
		p[n] = 0;
		if (askSave ()) openFile (p);
	}
};

static void cmdQuit () { if (askSave ()) { g_audio.stopPlay (); quitApp (); } }

int main (int argc, char **argv)
{
	const char *arg = argc > 1 ? argv[1] : "";

	new (s_docMem) Doc ();
	seedIds (ticks () ^ 0x5A17u);
	g_settings.load ();
	// (ARMPI: a window smaller than the work area, for the frame's button to bring back; it opens maximised)
	ScreenInfo si; screenInfo (&si);
	int sw = si.workW > 0 ? si.workW : 1024, sh = si.workH > 0 ? si.workH : 768;
	static Face big = { 26, true }, card = { 17, false };	// the position display, the pocket card's title
	g_bigFace = &big;
	g_pk = si.w > 0 && si.w < 800;
	KotonRoot root (sw * 85 / 100, sh * 85 / 100);
	applyTheme ();
	if (g_pk) PocketKoton::g_cardFace = &card;
	root.setBg (BG);

	// the song, the sound
	snprintf (g_status, sizeof g_status, "%s", TR ("Welcome to Koton."));
	g_doc.newSong ();
	g_audio.start (g_settings.soundfont);
	g_drumCatalog.load ();
	g_plug = new PlugHost;
	if (g_plug->init (SOUND_RATE))
	{
		g_plug->scan ();
		g_plug->attach (&g_audio.engine);
		g_plug->installGeneratorHook ();
		g_plug->onCrash = onPluginCrash; g_plug->onDirty = onPluginDirty; g_plug->onParam = onPluginParam;
		for (int i = 0; i < g_plug->count (); i++) g_browser_pending.push (i);
	}
	g_openPluginEditor = openPluginEditor;
	SoundDialog::s_plugins = pluginInstruments;
	ChainPanel::g_arrange_chooseSound = chooseSoundOf;
	GeneratorEditor::s_openGenerator = openGeneratorEditor;

	g_cmd.save = cmdSave; g_cmd.undo = cmdUndo; g_cmd.redo = cmdRedo; g_cmd.rewind = cmdRewind; g_cmd.playStop = cmdPlayStop;
	g_cmd.stop = cmdStop; g_cmd.loop = cmdLoop; g_cmd.metronome = cmdMetronome; g_cmd.song = cmdSong; g_cmd.compose = cmdCompose;
	g_cmd.browse = browse;
	g_onEdited = onEdited;
	ChordEditor::s_cadenceDialog = cmdCadence;
	ArticulationEditor::s_prompt = promptText;

	// the layout: the bar on top, the browser on the right, the arrangement, the editor below it, the status bar
	int W = root.width, H = root.height;
	g_top = new TopBar (0, 0, W);
	root.addChild (g_top);
	static const char *const snaps[5] = { "Bar", "Beat", "1/2 beat", "1/4 beat", "Off" };
	g_top->snapDd = g_top->addCtl (ui_dropdown (0, 0, 104, 24, (long) joinLabels (snaps, 5)), 0, 0, 0, 104, 24, UN_CHANGE);
	ui_w_set_value (g_top->snapDd, 1);
	g_top->onSnap = [] (int s) {
		static const double v[5] = { -1, 1, 0.5, 0.25, 0 };
		if (g_arrange) g_arrange->snap = v[s] < 0 ? imax (1, g_doc.p.barBeats ()) : v[s];
	};
	g_top->placeChildren ();
	int bodyTop = TopBar::H, bodyH = H - TopBar::H - StatusBar::H;
	g_editorH = imin (380, imax (bodyH < 500 ? 200 : 260, bodyH * 38 / 100));	// (ARMPI: 800 x 600, the arrangement keeps its room)
	g_browser = new Browser (W - Browser::W, bodyTop, bodyH);
	for (int k = 0; k < g_browser_pending.size (); k++) { const PlugInfo &pi = g_plug->info (g_browser_pending[k]); g_browser->pluginNames.push (pi.name); g_browser->pluginKinds.push (pi.kind); }
	g_browser->build ();
	root.addChild (g_browser);
	g_host = new EditorHost (0, bodyTop + bodyH - g_editorH, W - Browser::W - ChainPanel::W, g_editorH);
	root.addChild (g_host);
	g_chain = new ChainPanel (W - Browser::W - ChainPanel::W, bodyTop + bodyH - g_editorH, g_editorH);
	root.addChild (g_chain);
	g_statusBar = new StatusBar (0, H - StatusBar::H, W);
	root.addChild (g_statusBar);
	g_arrange = new ArrangeView (0, bodyTop, W - Browser::W, bodyH - g_editorH);
	g_arrange->onSelect = onSelect; g_arrange->onEdited = onEdited; g_arrange->onPlayFrom = playFrom;
	root.addChild (g_arrange);			// (last: a drag ending over another part still reaches it)
	g_arrange->setFocus ();
	g_host->onSplit = onSplit;
	if (g_pk)					// pocket: the desktop's parts hidden under the one view that plays and mixes
	{
		g_top->hidden = true; g_browser->hidden = true; g_host->hidden = true; g_chain->hidden = true;
		g_statusBar->hidden = true; g_arrange->hidden = true;
		g_pocket = new PocketKoton (W, H);
		root.addChild (g_pocket);
		g_pocket->setFocus ();
	}

	static MenuBar menu;
	if (g_pk)					// pocket: what plays and mixes; composing answers with the card
	{
		menu.menu (TR ("File"));
		menu.item (TR ("Open..."), "^O", KEY_CTRL ('O'), cmdOpen);
		menu.item (TR ("Save"), "^S", KEY_CTRL ('S'), cmdSave);
		menu.item (TR ("Export as WAV..."), "", 0, cmdExport);
		menu.menu (TR ("Song"));
		menu.item (TR ("Song"), "", 0, cmdPocketSong);
		menu.item (TR ("Mixer"), "", 0, cmdPocketMixer);
		menu.separator ();
		menu.item (TR ("Tempo, key, meter..."), "^K", KEY_CTRL ('K'), cmdSong);
		menu.menu (TR ("Transport"));
		menu.item (TR ("Play / Pause"), "Space", 0, cmdPlayStop);
		menu.item (TR ("Stop"), "Esc", 0, cmdStop);
		menu.item (TR ("Back to the start"), "Home", 0, cmdRewind);
		menu.item (TR ("Metronome"), "", 0, cmdMetronome);
		menu.menu (TR ("Compose"));
		menu.item (TR ("Edit a block..."), "", 0, cmdPocketCard);
		menu.item (TR ("Sound chain and effects..."), "", 0, cmdPocketCard);
		menu.item (TR ("Generators..."), "", 0, cmdPocketCard);
		menu.item (TR ("Compose with AI..."), "", 0, cmdPocketCard);
		menu.publish ();
	}
	else {
	menu.menu ("File");
	menu.item ("New song", "^N", KEY_CTRL ('N'), cmdNew);
	menu.item ("Open...", "^O", KEY_CTRL ('O'), cmdOpen);
	menu.separator ();
	menu.item ("Save", "^S", KEY_CTRL ('S'), cmdSave);
	menu.item ("Save As...", "", 0, cmdSaveAs);
	menu.item ("Export as WAV...", "", 0, cmdExport);
	menu.separator ();
	menu.item ("SoundFont...", "", 0, cmdSoundFont);
	menu.item ("Get a SoundFont...", "", 0, cmdGetSoundFont);
	menu.menu ("Edit");
	menu.item ("Undo", "^Z", KEY_CTRL ('Z'), cmdUndo);
	menu.item ("Redo", "^Y", KEY_CTRL ('Y'), cmdRedo);
	menu.separator ();
	menu.item ("Duplicate the block", "^D", KEY_CTRL ('D'), cmdDuplicate);
	menu.item ("Delete the block", "Del", 0, cmdDelete);
	menu.menu ("Song");
	menu.item ("Key, meter, tempo...", "^K", KEY_CTRL ('K'), cmdSong);
	menu.item ("Cadence on the chord track...", "", 0, cmdCadence);
	menu.menu ("Track");
	menu.item ("Add an instrument track", "", 0, cmdAddTrack);
	menu.item ("Add a drum track", "", 0, cmdAddDrums);
	menu.separator ();
	menu.item ("Instrument...", "^I", 0, cmdInstrument);
	menu.item ("Rename...", "", 0, cmdRenameTrack);
	menu.menu ("Transport");
	menu.item ("Play / Stop", "Space", 0, cmdPlayStop);
	menu.item ("Stop", "Esc", 0, cmdStop);
	menu.item ("Back to the start", "Home", 0, cmdRewind);
	menu.item ("Loop", "^L", KEY_CTRL ('L'), cmdLoop);
	menu.item ("Metronome", "^M", 0, cmdMetronome);
	menu.menu ("View");
	menu.item ("Zoom in", "^+", 0, cmdZoomIn);
	menu.item ("Zoom out", "^-", 0, cmdZoomOut);
	menu.menu ("AI");
	menu.item ("Compose with AI...", "", 0, cmdCompose);
	menu.publish ();
	}
	if (g_pk) { pocket_hints (&root); pocket_layout (&root); }

	ui_maximize ();
	if (arg[0]) openFile (arg);
	root.run ();
	// the end: the engine stopped before the song and the SoundFont are freed
	g_host->setEditor (0);
	if (g_pluginWindow) g_pluginWindow->closeMe ();
	g_audio.shutdown ();
	if (g_plug) { g_plug->attach (0); g_plug->shutdown (); delete g_plug; g_plug = 0; }
	g_audio.freeSoundFont ();
	g_doc.~Doc ();
	return 0;
}
