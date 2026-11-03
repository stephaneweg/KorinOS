//
// plug/plughost.h -- Koton's plugins as processes (the protocol: kplug_proto.h; a plugin's side:
// kplug.h): the catalogue (SD0:/apps/koton.app/plugins/<id>/{main, plugin.json}), the processes (started with a
// shared region each, ended with the app or when unused, a crash noticed), the engine's side of each
// (plugshm.h: an instrument rendered ahead, an effect with its latency compensated), the parameters,
// the states (strings in the project: kt::PluginSlot::state, GeneratorModule::state), a generator's
// requests (kt::g_generatorHook) and a plugin's editor, a kui::View of the app made from its parameters
// (Onyx's draws in the plugin, into a shared surface: an ARMPI plugin has no UI).
//
// The app, on its UI thread (the one that posts the engine's commands):
//
//     PlugHost host;
//     host.init (44100);                  once
//     host.scan ();                       the catalogue: count () / info (i) / find (id)
//     host.attach (&engine);              the engine the instances play in
//     host.installGeneratorHook ();       generator modules render through their plugins
//     -- a project opened (or its tracks moved): for each track t
//     host.syncTrack (t, project.tracks[t]);   its instrument plugin and inserts made, connected
//     -- every UI tick (~60 Hz)
//     host.poll ();                       the channels, the liveness, what the engine let go
//     -- an editor in the chain panel (a view; the app lays it out, closes it)
//     PlugEditorView *v = host.openEditor (host.trackInsert (t, 0), panel, x, y, w, h);
//     host.closeEditor (v);
//     -- saving the project
//     host.saveTrack (t, project.tracks[t]);   the plugins' states into its PluginSlots
//     -- the end: the engine stopped (or deleted: attach (0) first), then
//     host.shutdown ();
//
// Latency: an instrument plugin plays the song in time (its notes go lead () frames ahead: a play or
// a seek waits that long, ~93 ms); a LIVE note on its track (a MIDI keyboard: CMD_EXT_NOTE_ON) is
// heard that late. An effect plugin gives its block back latency () frames late (~93 ms): the engine
// delays every other track as much while one is connected (plugin delay compensation), so the song
// stays aligned and is heard that much later; the SoundFont tracks' live notes (the preview voice)
// are not delayed.
//
#ifndef _koton_plughost_h
#define _koton_plughost_h

#include "ui/view.h"			// (before the engine's headers: see kbase.h)
#include <pthread.h>
#include "../engine/engine.h"
#include "plugshm.h"
#include "plugctx.h"

namespace kt {

struct PlugParam { Str id, name, unit; float min, max, def, step; Vec<Str> choices; };

struct PlugInfo
{
	Str id;					// the folder's name: a project's PluginSlot::id
	Str name, text, vendor;
	int kind;				// KP_INSTRUMENT / KP_EFFECT / KP_GENERATOR
	Str exe;				// SD0:/apps/koton.app/plugins/<id>/main
	Vec<PlugParam> params;
	Vec<Str> aliases;			// other ids it answers to (Koton's: "koton.arpeggiator")
	int editorW, editorH;			// the editor's size it asks for (0: made from its parameters)
	PlugInfo () : kind (0), editorW (0), editorH (0) {}
};

enum PlugState { PLUG_STARTING, PLUG_READY, PLUG_FAILED, PLUG_CRASHED, PLUG_ENDING };

class PlugHost;
class PlugEditorView;

// A plugin process.
class PlugInstance
{
public:
	const PlugInfo &info () const { return m_info; }
	int state () const { return m_state; }			// PlugState
	bool ready () const { return m_state == PLUG_READY; }
	const char *error () const { return m_err; }		// why it failed / crashed
	int pid () const { return m_pid; }
	int paramCount () const { return m_info.params.size (); }
	int paramIndex (const char *id) const;
	float param (int i) const;				// its value now (the plugin's)
	unsigned underruns () const { return m_shm ? m_shm->underruns : 0; }	// blocks the engine played silent
	unsigned dspUs () const { return m_shm ? m_shm->dspMaxUs : 0; }	// its render thread's longest pass lately
	bool isGenerator () const { return m_generator; }	// a generator's process (PlugHost::generator)
	const char *editedModule () const { return m_editModule.c (); }	// its editor's module (its id), "" none
	KpShm *shm () const { return m_shm; }			// (its region: tests drive the engine's side by hand)
	int track () const { return m_track; }			// where it is connected (-1 none)
	int slot () const { return m_slot; }			// an effect's insert slot (-1: the instrument)
	void *user;						// the app's
private:
	friend class PlugHost;
	friend class PlugEditorView;
	PlugInstance ();
	~PlugInstance ();
	PlugInfo m_info;
	int m_number, m_state, m_pid, m_sock;			// m_sock: its channel, -1 none
	char m_err[96];
	int m_shmFd; KpShm *m_shm; int m_pool;			// its region (the host's pool)
	ShmSource *m_src; ShmEffect *m_fx;			// its side in the engine, when connected
	int m_track, m_slot;
	int m_wasTrack, m_wasSlot;				// an effect taken out at its crash (restart: back)
	unsigned m_t0, m_beat, m_beatT, m_dspBeat, m_dspT;
	unsigned long long m_dspWant;
	pthread_mutex_t m_xlock;				// the request channel
	volatile int m_busy;					// a request in flight (its own timeout)
	unsigned long long m_pending; float m_pendingVal[KP_MAX_PARAMS];	// parameters to send
	Str m_lastState;					// the last state known (a restart after a crash)
	Str m_editModule;					// a generator: the module its editor shows (its id)
	PlugEditorView *m_editor;
	bool m_dying, m_generator;
	unsigned m_dieT, m_usedT;
};

// A plugin's editor: a view made from its parameters (knobs, drop-downs, check boxes: UIKit's controls).
// Made by PlugHost::openEditor, a child of the view given; PlugHost::closeEditor (or deleting its
// parent) ends it.
class PlugEditorView : public kui::View
{
public:
	PlugEditorView (PlugHost *h, PlugInstance *p, int x, int y, int w, int hh);
	~PlugEditorView ();
	PlugInstance *instance () const { return m_inst; }
	void onDraw () override;
	void onControl (long w, int tag, int code) override;
private:
	friend class PlugHost;
	void sync ();					// the values the plugin holds (a state, the chain's knobs)
	void show (int i, float v);
	PlugHost *m_host;
	PlugInstance *m_inst;
	long m_ctl[KP_MAX_PARAMS];
	short m_x[KP_MAX_PARAMS], m_y[KP_MAX_PARAMS];	// a knob's place (its name above, its value under it)
	unsigned char m_kind[KP_MAX_PARAMS];		// 0 knob, 1 drop-down, 2 check box
	float m_val[KP_MAX_PARAMS], m_seen[KP_MAX_PARAMS];
	unsigned m_touchT;
	bool m_live;
};

class PlugHost
{
public:
	PlugHost ();
	~PlugHost ();

	// ---- setting up
	bool init (int sampleRate);
	bool available () const { return m_ok; }
	void setLead (int frames);			// an instrument's render-ahead (KP_LEAD_DEFAULT = 4096); new instances
	void setLatency (int frames);			// an effect's (the same by default)
	int lead () const { return m_lead; }
	int latency () const { return m_latency; }

	// ---- the catalogue
	int scan (const char *dir = KP_DIR);		// -> how many
	int count () const { return m_infos.size (); }
	const PlugInfo &info (int i) const { return m_infos[i]; }
	const PlugInfo *find (const char *id) const;	// by its id or an alias
	int countKind (int kind) const;

	// ---- instances (the UI thread)
	// A process started with that state (JSON, 0: the defaults); wait: until it is up (<= 3 s) --
	// else it starts meanwhile (connected, it is silent until then). 0: no such plugin, no memory.
	PlugInstance *create (const char *id, const char *stateJson = 0, bool wait = false);
	void destroy (PlugInstance *p);			// disconnected, asked to end, killed a second later
	bool restart (PlugInstance *p);			// a crashed one again (its last state, the same connection)
	int instances () const { return m_inst.size (); }
	PlugInstance *instance (int i) const { return m_inst[i]; }

	// ---- the engine (the UI thread: it posts the engine's commands)
	void attach (Engine *e);			// 0: no engine any more (it was deleted): nothing posted
	bool connectInstrument (PlugInstance *p, int track);	// the track's notes to it (CMD_EXTERNAL)
	bool connectEffect (PlugInstance *p, int track, int slot);	// an insert of the track (0..3)
	void disconnect (PlugInstance *p);

	// ---- parameters, states
	void setParam (PlugInstance *p, int index, float value);	// (sent at once, or at the next tick)
	bool getState (PlugInstance *p, Str &json, int timeoutMs = 1000);
	bool setState (PlugInstance *p, const char *json, int timeoutMs = 1000);

	// ---- a project's tracks: the instrument plugin (Track::instrumentPlugin) and the inserts
	// (Track::inserts -> the engine's slots 0..3) made, kept when unchanged, replaced, connected
	// (a disabled slot: kept, disconnected). After tracks were deleted / moved: saveTrack them all,
	// releaseTracks (0), syncTrack them again.
	bool syncTrack (int track, const Track &t);
	void saveTrack (int track, Track &t);		// the live states into its PluginSlots
	void releaseTracks (int from = 0);		// the tracks' instances from `from` on, ended
	PlugInstance *trackInstrument (int track) const;
	PlugInstance *trackInsert (int track, int slot) const;

	// ---- generators: one process per generator plugin, serving every module that uses it (each
	// request carries the module's state); started when first asked, ended when unused for a minute
	void installGeneratorHook ();			// kt::g_generatorHook -> generate ()
	bool generate (const GeneratorModule &m, const Project &p, double startBeat, Riff &out, int timeoutMs = 2000);
	PlugInstance *generator (const char *id);	// its process (started if needed)
	// a module's editor: the process takes the module's state, then its editor opens; when it
	// reports a change (onParam / onDirty) the app calls pullGeneratorState: the state goes back into
	// the module the editor shows (found by its id in the project: an undo's copy too) -> true: changed
	PlugEditorView *openGeneratorEditor (const GeneratorModule &m, kui::View &parent, int x, int y, int w, int h);
	bool pullGeneratorState (PlugInstance *p, Project &project);
	bool pullGeneratorState (PlugInstance *p, GeneratorModule &m);

	// ---- editors
	PlugEditorView *openEditor (PlugInstance *p, kui::View &parent, int x, int y, int w, int h);
	void closeEditor (PlugEditorView *v);		// removed from its parent, deleted
	void editorSize (const PlugInstance *p, int *w, int *h) const;	// what it would like
	void editorSize (const PlugInfo *info, int *w, int *h) const;	// (from its description: a generator's before it runs)

	// ---- the loop (the UI thread)
	void poll ();					// the channels, then tick ()
	void tick ();
	void (*onCrash) (PlugInstance *p, void *ctx);	// a plugin died / hung (its track goes silent)
	void (*onParam) (PlugInstance *p, int index, float value, void *ctx);	// its editor moved a parameter
	void (*onDirty) (PlugInstance *p, void *ctx);	// its state changed (save it again)
	void *ctx;

	// ---- the end: every plugin ended, their regions freed (the engine stopped or deleted first)
	void shutdown ();

private:
	friend class PlugEditorView;
	PlugHost (const PlugHost &);
	PlugHost &operator= (const PlugHost &);
	struct Region { int fd; KpShm *shm; bool used; };
	struct Retired { ShmSource *src; ShmEffect *fx; unsigned renders; };
	struct TrackPlugs { PlugInstance *instr, *fx[4]; };
	struct GenCached { unsigned hash, len; int number; Vec<RiffNote> notes; };
	enum { GEN_CACHE = 64 };
	Vec<GenCached> m_genCache;			// the generators' last replies (by their request)
	enum { MAX_INSTANCES = 128 };
	bool start (PlugInstance *p, const char *stateJson, bool fresh);
	void flushParams (PlugInstance *p);
	int takeRegion ();
	bool waitReady (PlugInstance *p, int timeoutMs);
	bool request (PlugInstance *p, unsigned type, const char *payload, unsigned len, Str *reply, int timeoutMs);
	bool sendTo (PlugInstance *p, int type, const void *data, int len);
	void handleMessage (PlugInstance *p, int type, const void *data, int len);
	void retire (ShmSource *s, ShmEffect *f);
	void lose (PlugInstance *p, const char *why);
	bool ended (PlugInstance *p);			// its process is gone (then reaped)
	void kill9 (PlugInstance *p);			// its process killed, reaped, its channel closed
	void editorGone (PlugEditorView *v);

	bool m_ok;
	int m_rate, m_lead, m_latency, m_next;
	Engine *m_engine;
	Vec<PlugInfo> m_infos;
	Vec<PlugInstance *> m_inst;
	Vec<Region> m_regions;
	Vec<Retired> m_retired;
	Vec<TrackPlugs> m_tracks;
	pthread_mutex_t m_lock;				// m_inst (the generator hook may run on another thread)
	unsigned m_lastProcs;
};

} // namespace kt

#endif
