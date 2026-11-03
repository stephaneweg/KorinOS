//
// plug/plughost.cpp -- Koton's plugins as processes: the host (plughost.h), on POSIX: a region is a
// memfd, a process comes from posix_spawn, a channel is a socketpair.
//
#include "plughost.h"
#include "ui/json.hpp"
#include <dirent.h>
#include <errno.h>
#include <signal.h>
#include <spawn.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/wait.h>

extern char **environ;

namespace kt {

using namespace kui;

// ---- small helpers ---------------------------------------------------------------------------------------
static void setErr (char *e, const char *s) { snprintf (e, 96, "%s", s); }

// ---- PlugInstance ------------------------------------------------------------------------------------------
PlugInstance::PlugInstance () : user (0), m_number (0), m_state (PLUG_STARTING), m_pid (0), m_sock (-1), m_shmFd (-1), m_shm (0), m_pool (-1),
	m_src (0), m_fx (0), m_track (-1), m_slot (-1), m_wasTrack (-1), m_wasSlot (-1), m_t0 (0), m_beat (0), m_beatT (0), m_dspBeat (0), m_dspT (0), m_dspWant (0),
	m_busy (0), m_pending (0), m_editor (0), m_dying (false), m_generator (false), m_dieT (0), m_usedT (0)
{
	m_err[0] = 0;
	pthread_mutex_init (&m_xlock, 0);
	for (int i = 0; i < KP_MAX_PARAMS; i++) m_pendingVal[i] = 0;
}

PlugInstance::~PlugInstance () { pthread_mutex_destroy (&m_xlock); }

int PlugInstance::paramIndex (const char *id) const
{
	for (int i = 0; i < m_info.params.size (); i++) if (m_info.params[i].id == id) return i;
	return -1;
}

float PlugInstance::param (int i) const
{
	if (i < 0 || i >= m_info.params.size ()) return 0;
	if (m_pending & (1ull << i)) return m_pendingVal[i];
	return m_shm && m_state == PLUG_READY ? m_shm->param[i] : m_info.params[i].def;
}

// ---- PlugHost: setting up, the catalogue --------------------------------------------------------------------
PlugHost::PlugHost () : onCrash (0), onParam (0), onDirty (0), ctx (0), m_ok (false), m_rate (44100),
	m_lead (KP_LEAD_DEFAULT), m_latency (KP_LEAD_DEFAULT), m_next (1), m_engine (0), m_lastProcs (0)
{
	pthread_mutex_init (&m_lock, 0);
}

PlugHost::~PlugHost () { shutdown (); pthread_mutex_destroy (&m_lock); }

bool PlugHost::init (int sampleRate)
{
	m_rate = sampleRate > 0 ? sampleRate : 44100;
	if (m_ok) return true;
	m_inst.reserve (MAX_INSTANCES);			// (never moved: the generator hook's thread reads it too)
	m_ok = true;
	return true;
}

void PlugHost::setLead (int f) { m_lead = f < KP_LEAD_MIN ? KP_LEAD_MIN : f > KP_LEAD_MAX ? KP_LEAD_MAX : f; }
void PlugHost::setLatency (int f) { m_latency = f < KP_LEAD_MIN ? KP_LEAD_MIN : f > KP_LEAD_MAX ? KP_LEAD_MAX : f; }

int PlugHost::scan (const char *dir)
{
	m_infos.clear ();
	DIR *d = opendir (dir);
	if (!d) return 0;
	struct dirent *e;
	while ((e = readdir (d)))
	{
		if (e->d_type != DT_DIR || e->d_name[0] == '.') continue;
		char path[256]; snprintf (path, sizeof path, "%s/%s", dir, e->d_name);
		char mf[300]; snprintf (mf, sizeof mf, "%s/plugin.json", path);
		char exe[300]; snprintf (exe, sizeof exe, "%s/main", path);
		unsigned len = 0;
		char *text = readFile (mf, &len);
		if (!text) continue;
		if (access (exe, F_OK)) { free (text); continue; }
		json::Doc doc;
		if (doc.parse (text, len, json::TOLERANT) && doc.root ().isObj ())
		{
			const json::Value &r = doc.root ();
			PlugInfo &pi = m_infos.add ();
			pi.id = e->d_name; pi.exe = exe;
			pi.name = r["name"].asStr (e->d_name); pi.text = r["text"].asStr (""); pi.vendor = r["vendor"].asStr ("");
			const char *k = r["kind"].asStr ("");
			pi.kind = json::seqi (k, "instrument") ? KP_INSTRUMENT : json::seqi (k, "effect") ? KP_EFFECT : json::seqi (k, "generator") ? KP_GENERATOR : 0;
			for (const json::Value *a = r["aliases"].first (); a; a = a->next) if (a->isStr ()) pi.aliases.push (Str (a->asStr ()));
			pi.editorW = r["editor"]["w"].asInt (0); pi.editorH = r["editor"]["h"].asInt (0);
			for (const json::Value *p = r["params"].first (); p && pi.params.size () < KP_MAX_PARAMS; p = p->next)
			{
				PlugParam &q = pi.params.add ();
				q.id = (*p)["id"].asStr (""); q.name = (*p)["name"].asStr (q.id.c ()); q.unit = (*p)["unit"].asStr ("");
				q.min = (*p)["min"].asFloat (0); q.max = (*p)["max"].asFloat (1); q.def = (*p)["default"].asFloat (q.min); q.step = (*p)["step"].asFloat (0);
				for (const json::Value *c = (*p)["choices"].first (); c; c = c->next) q.choices.push (Str (c->asStr ("")));
			}
			if (!pi.kind) m_infos.pop ();
		}
		free (text);
	}
	closedir (d);
	m_infos.sort ([] (const PlugInfo &a, const PlugInfo &b) { return strcmp (a.name.c (), b.name.c ()) < 0; });
	return m_infos.size ();
}

const PlugInfo *PlugHost::find (const char *id) const
{
	if (!id || !id[0]) return 0;
	for (int i = 0; i < m_infos.size (); i++) if (json::seqi (m_infos[i].id.c (), id)) return &m_infos[i];
	for (int i = 0; i < m_infos.size (); i++)
		for (int a = 0; a < m_infos[i].aliases.size (); a++) if (json::seqi (m_infos[i].aliases[a].c (), id)) return &m_infos[i];
	return 0;
}

int PlugHost::countKind (int kind) const { int n = 0; for (int i = 0; i < m_infos.size (); i++) if (m_infos[i].kind == kind) n++; return n; }

// ---- the processes ------------------------------------------------------------------------------------------
int PlugHost::takeRegion ()
{
	for (int i = 0; i < m_regions.size (); i++) if (!m_regions[i].used) { m_regions[i].used = true; return i; }
	int fd = memfd_create ("koton-plug", MFD_CLOEXEC);
	if (fd < 0) return -1;
	void *s = ftruncate (fd, KP_SHM_BYTES) ? MAP_FAILED : mmap (0, KP_SHM_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (s == MAP_FAILED) { close (fd); return -1; }
	Region r = { fd, (KpShm *) s, true };
	m_regions.push (r);
	return m_regions.size () - 1;
}

// the process of p on its region, with that initial state. fresh: the region made new; else (a
// restart) the engine's side of it -- the clock, the rings' counts -- is left as it is (it plays on)
bool PlugHost::start (PlugInstance *p, const char *stateJson, bool fresh)
{
	KpShm *s = p->m_shm;
	if (fresh) kp_shm_init (s, p->m_info.kind, m_rate, m_lead, m_latency, p->m_number);
	else
	{
		s->ready = KP_STARTING; s->pluginVersion = 0; s->nparams = 0; s->paramsLen = 0; s->beat = s->dspBeat = 0;
		s->dspUs = s->dspMaxUs = 0; s->lateFrames = 0; s->initLen = 0;
		s->reqSeq = s->repSeq = 0; s->reqType = s->reqLen = s->repStatus = s->repLen = 0;
	}
	unsigned len = stateJson ? (unsigned) strlen (stateJson) : 0;
	if (len && len <= KP_DATA_BYTES) { memcpy (s->data, stateJson, len); s->initLen = len; }
	p->m_pid = 0; p->m_state = PLUG_STARTING; p->m_t0 = p->m_beatT = p->m_dspT = ticks (); p->m_beat = p->m_dspBeat = 0;
	p->m_err[0] = 0;
	int sv[2];
	if (socketpair (AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sv)) { p->m_state = PLUG_FAILED; setErr (p->m_err, "no channel to it"); return false; }
	// (every descriptor of Koton's is CLOEXEC: the plugin inherits its region and its end, not its neighbours')
	char a1[16], a2[16];
	snprintf (a1, sizeof a1, "%d", p->m_shmFd); snprintf (a2, sizeof a2, "%d", sv[1]);
	char *const argv[] = { (char *) p->m_info.exe.c (), (char *) "--kplug", a1, a2, 0 };
	posix_spawn_file_actions_t fa;
	posix_spawn_file_actions_init (&fa);
	posix_spawn_file_actions_adddup2 (&fa, p->m_shmFd, p->m_shmFd);
	posix_spawn_file_actions_adddup2 (&fa, sv[1], sv[1]);
	pid_t pid = 0;
	int e = posix_spawn (&pid, p->m_info.exe.c (), &fa, 0, argv, environ);
	posix_spawn_file_actions_destroy (&fa);
	close (sv[1]);
	if (e) { close (sv[0]); p->m_state = PLUG_FAILED; setErr (p->m_err, "its program could not be started"); return false; }
	p->m_sock = sv[0]; p->m_pid = (int) pid;
	return true;
}

// a process gone? (reaped then, its channel closed)
bool PlugHost::ended (PlugInstance *p)
{
	if (p->m_pid <= 0) return true;
	if (waitpid (p->m_pid, 0, WNOHANG) == 0) return false;
	p->m_pid = 0;
	if (p->m_sock >= 0) { close (p->m_sock); p->m_sock = -1; }
	return true;
}

void PlugHost::kill9 (PlugInstance *p)
{
	if (p->m_pid > 0) { kill (p->m_pid, SIGKILL); waitpid (p->m_pid, 0, 0); p->m_pid = 0; }
	if (p->m_sock >= 0) { close (p->m_sock); p->m_sock = -1; }
}

bool PlugHost::sendTo (PlugInstance *p, int type, const void *data, int len)
{
	if (p->m_sock < 0) return false;
	KpMsg m;
	m.type = type;
	if (len > 0) memcpy (m.data, data, (size_t) (len < (int) sizeof m.data ? len : (int) sizeof m.data));
	return send (p->m_sock, &m, sizeof m.type + (size_t) (len > 0 ? len : 0), MSG_DONTWAIT | MSG_NOSIGNAL) > 0;
}

PlugInstance *PlugHost::create (const char *id, const char *stateJson, bool wait)
{
	if (!m_ok || m_inst.size () >= MAX_INSTANCES) return 0;
	const PlugInfo *info = find (id);
	if (!info) return 0;
	PlugInstance *p = new PlugInstance;
	p->m_info = *info;
	pthread_mutex_lock (&m_lock);
	int r = takeRegion ();
	if (r < 0) { pthread_mutex_unlock (&m_lock); delete p; return 0; }
	p->m_pool = r; p->m_shmFd = m_regions[r].fd; p->m_shm = m_regions[r].shm;
	p->m_number = m_next++;
	p->m_lastState = stateJson ? stateJson : "";
	m_inst.push (p);
	pthread_mutex_unlock (&m_lock);
	start (p, stateJson, true);
	if (wait) waitReady (p, 3000);
	return p;
}

bool PlugHost::restart (PlugInstance *p)
{
	if (!p || p->m_dying || p->m_state == PLUG_READY || p->m_state == PLUG_STARTING) return false;
	kill9 (p);
	Str st (p->m_lastState);			// (the region is kept: its engine side goes on reading it)
	if (!start (p, st.c (), false)) return false;
	if (p->m_wasTrack >= 0 && !p->m_fx) connectEffect (p, p->m_wasTrack, p->m_wasSlot);	// (taken out at its crash)
	p->m_wasTrack = p->m_wasSlot = -1;
	return true;
}

// up (its region says so)
bool PlugHost::waitReady (PlugInstance *p, int timeoutMs)
{
	if (!p || !p->m_shm) return false;
	unsigned t0 = ticks ();
	for (;;)
	{
		unsigned r = kp_ld32 (&p->m_shm->ready);
		if (r == KP_FAILED) { if (p->m_state == PLUG_STARTING) { p->m_state = PLUG_FAILED; setErr (p->m_err, "it refused to start (another kind, protocol)"); } return false; }
		if (r == KP_READY)
		{
			if (p->m_state == PLUG_STARTING) p->m_state = PLUG_READY;
			return p->m_state == PLUG_READY;
		}
		if (p->m_state != PLUG_STARTING && p->m_state != PLUG_READY) return false;
		if ((int) (ticks () - t0) * 10 >= timeoutMs) return false;
		usleep (1000);
	}
}

void PlugHost::destroy (PlugInstance *p)
{
	if (!p || p->m_dying) return;
	if (p->m_editor) closeEditor (p->m_editor);
	disconnect (p);
	for (int t = 0; t < m_tracks.size (); t++)
	{
		if (m_tracks[t].instr == p) m_tracks[t].instr = 0;
		for (int s = 0; s < 4; s++) if (m_tracks[t].fx[s] == p) m_tracks[t].fx[s] = 0;
	}
	sendTo (p, KP_BYE, 0, 0);
	p->m_dying = true; p->m_dieT = ticks ();
	if (p->m_state == PLUG_READY || p->m_state == PLUG_STARTING) p->m_state = PLUG_ENDING;
}

void PlugHost::lose (PlugInstance *p, const char *why)
{
	if (p->m_dying || p->m_state == PLUG_CRASHED || p->m_state == PLUG_FAILED) return;
	bool wasStarting = p->m_state == PLUG_STARTING;
	p->m_state = wasStarting ? PLUG_FAILED : PLUG_CRASHED;
	setErr (p->m_err, why);
	kill9 (p);
	if (p->m_editor) p->m_editor->sync ();
	// an effect taken out of its track: the track goes on dry (restart puts it back); an instrument
	// stays (silent: its track has nothing else to play)
	if (p->m_fx) { int t = p->m_track, sl = p->m_slot; disconnect (p); p->m_wasTrack = t; p->m_wasSlot = sl; }
	if (onCrash) onCrash (p, ctx);
	if (p->m_generator && !p->m_editor) { p->m_dying = true; p->m_dieT = ticks (); }	// (the next request starts another)
}

// ---- the engine ---------------------------------------------------------------------------------------------
void PlugHost::attach (Engine *e)
{
	if (!e && m_engine)			// (the engine was deleted: its sides are no one's any more)
		for (int i = 0; i < m_retired.size (); i++) { delete m_retired[i].src; delete m_retired[i].fx; }
	if (!e) m_retired.clear ();
	m_engine = e;
}

void PlugHost::retire (ShmSource *s, ShmEffect *f)
{
	if (!s && !f) return;
	if (!m_engine) { delete s; delete f; return; }
	Retired r = { s, f, __atomic_load_n (&m_engine->renders, __ATOMIC_ACQUIRE) };
	m_retired.push (r);
}

bool PlugHost::connectInstrument (PlugInstance *p, int track)
{
	if (!p || !m_engine || p->m_dying || p->m_info.kind != KP_INSTRUMENT || track < 0 || track >= ENGINE_MAX_TRACKS) return false;
	disconnect (p);
	while (m_tracks.size () <= track) { TrackPlugs t = { 0, { 0, 0, 0, 0 } }; m_tracks.push (t); }
	// the track's other instrument plugin, if any, gives its place
	for (int i = 0; i < m_inst.size (); i++) if (m_inst[i] != p && m_inst[i]->m_src && m_inst[i]->m_track == track) disconnect (m_inst[i]);
	ShmSource *src = new ShmSource (p->m_shm, (int) p->m_shm->lead);
	if (!m_engine->post (CMD_EXTERNAL, 0, 0, track, 0, src)) { delete src; return false; }
	p->m_src = src; p->m_track = track; p->m_slot = -1;
	p->m_shm->active = 1;
	return true;
}

bool PlugHost::connectEffect (PlugInstance *p, int track, int slot)
{
	if (!p || !m_engine || p->m_dying || p->m_info.kind != KP_EFFECT || track < 0 || track >= ENGINE_MAX_TRACKS || slot < 0 || slot >= 4) return false;
	disconnect (p);
	for (int i = 0; i < m_inst.size (); i++) if (m_inst[i] != p && m_inst[i]->m_fx && m_inst[i]->m_track == track && m_inst[i]->m_slot == slot) disconnect (m_inst[i]);
	ShmEffect *fx = new ShmEffect (p->m_shm, (int) p->m_shm->latency);
	m_engine->setInsert (track, slot, fx);
	p->m_wasTrack = p->m_wasSlot = -1;
	p->m_fx = fx; p->m_track = track; p->m_slot = slot;
	p->m_shm->active = 1;
	return true;
}

void PlugHost::disconnect (PlugInstance *p)
{
	if (!p) return;
	if (p->m_src)
	{
		if (m_engine && !m_engine->post (CMD_EXTERNAL, 0, 0, p->m_track, 0, (void *) 0))
		{
			// (the command ring is full: try again at the next tick -- the source stays connected meanwhile)
			return;
		}
		retire (p->m_src, 0); p->m_src = 0;
	}
	if (p->m_fx)
	{
		if (m_engine) m_engine->setInsert (p->m_track, p->m_slot, 0);
		retire (0, p->m_fx); p->m_fx = 0;
	}
	p->m_track = p->m_slot = -1;
	if (p->m_shm) p->m_shm->active = 0;
}

// ---- parameters, states, requests ---------------------------------------------------------------------------------
void PlugHost::setParam (PlugInstance *p, int i, float v)
{
	if (!p || i < 0 || i >= p->m_info.params.size () || i >= KP_MAX_PARAMS) return;
	p->m_pendingVal[i] = v;
	p->m_pending |= 1ull << i;
	if (p->m_state == PLUG_READY)
	{
		KpParamMsg m = { i, v };
		if (sendTo (p, KP_SET_PARAM, &m, sizeof m)) p->m_pending &= ~(1ull << i);
	}
}

// A request through the region's channel (the payload and the reply in data[]); the channel's message
// is the doorbell. Its lock: one request at a time.
bool PlugHost::request (PlugInstance *p, unsigned type, const char *payload, unsigned len, Str *reply, int timeoutMs)
{
	if (!p || p->m_dying || len > KP_DATA_BYTES || !waitReady (p, timeoutMs)) return false;
	pthread_mutex_lock (&p->m_xlock);
	p->m_busy = 1;
	KpShm *s = p->m_shm;
	unsigned seq = kp_req_post (s, type, payload, len);
	KpReq r = { seq };
	sendTo (p, (int) type, &r, sizeof r);
	unsigned t0 = ticks ();
	bool ok = false;
	for (;;)
	{
		unsigned rs = kp_ld32 (&s->repSeq);
		if (rs == seq)
		{
			ok = s->repStatus == KP_OK;
			if (ok && reply) reply->set (s->data, (int) (s->repLen <= KP_DATA_BYTES ? s->repLen : 0));
			break;
		}
		if ((int) (ticks () - t0) * 10 >= timeoutMs || kp_ld32 (&s->ready) != KP_READY) break;
		usleep (500);
	}
	p->m_busy = 0;
	pthread_mutex_unlock (&p->m_xlock);
	return ok;
}

// the parameters not sent yet (a full channel, a plugin starting)
void PlugHost::flushParams (PlugInstance *p)
{
	if (!p->m_pending || p->m_state != PLUG_READY) return;
	for (int k = 0; k < KP_MAX_PARAMS; k++)
		if (p->m_pending & (1ull << k))
		{
			KpParamMsg m = { k, p->m_pendingVal[k] };
			if (sendTo (p, KP_SET_PARAM, &m, sizeof m)) p->m_pending &= ~(1ull << k);
			else break;
		}
}

bool PlugHost::getState (PlugInstance *p, Str &json, int timeoutMs)
{
	if (!p) return false;
	waitReady (p, timeoutMs);
	for (int k = 0; k < 50 && p->m_pending && p->m_state == PLUG_READY; k++) { flushParams (p); if (p->m_pending) usleep (2000); }
	if (!request (p, KP_STATE_GET, 0, 0, &json, timeoutMs)) return false;
	p->m_lastState = json;
	return true;
}

bool PlugHost::setState (PlugInstance *p, const char *json, int timeoutMs)
{
	if (!p || !json) return false;
	p->m_pending = 0;
	if (!request (p, KP_STATE_SET, json, (unsigned) strlen (json), 0, timeoutMs)) return false;
	p->m_lastState = json;
	return true;
}

// ---- a project's tracks --------------------------------------------------------------------------------------
PlugInstance *PlugHost::trackInstrument (int t) const { return t >= 0 && t < m_tracks.size () ? m_tracks[t].instr : 0; }
PlugInstance *PlugHost::trackInsert (int t, int s) const { return t >= 0 && t < m_tracks.size () && s >= 0 && s < 4 ? m_tracks[t].fx[s] : 0; }

bool PlugHost::syncTrack (int track, const Track &t)
{
	if (!m_ok || track < 0 || track >= ENGINE_MAX_TRACKS) return false;
	while (m_tracks.size () <= track) { TrackPlugs tp = { 0, { 0, 0, 0, 0 } }; m_tracks.push (tp); }
	TrackPlugs &tp = m_tracks[track];
	bool ok = true;
	// the instrument
	const PluginSlot &is = t.instrumentPlugin;
	bool want = !is.id.empty () && t.type != TRACK_CHORD;
	const PlugInfo *wi = want ? find (is.id) : 0;
	if (tp.instr && (!want || !wi || !(tp.instr->m_info.id == wi->id))) { PlugInstance *o = tp.instr; tp.instr = 0; destroy (o); }
	if (want && wi && !tp.instr)
	{
		tp.instr = create (is.id, is.state.c (), false);
		if (!tp.instr) ok = false;
	}
	if (want && !wi) ok = false;
	if (tp.instr)
	{
		bool up = tp.instr->m_state == PLUG_READY || tp.instr->m_state == PLUG_STARTING;	// (a crashed one: restart ())
		if (is.enabled && up && tp.instr->m_track != track) ok = connectInstrument (tp.instr, track) && ok;
		else if (!is.enabled && tp.instr->m_src) disconnect (tp.instr);
	}
	// the inserts, slots 0..3
	for (int s = 0; s < 4; s++)
	{
		const PluginSlot *ps = s < t.inserts.size () ? &t.inserts[s] : 0;
		const PlugInfo *fi = ps && !ps->id.empty () ? find (ps->id) : 0;
		if (fi && fi->kind != KP_EFFECT) fi = 0;
		if (tp.fx[s] && (!fi || !(tp.fx[s]->m_info.id == fi->id))) { PlugInstance *o = tp.fx[s]; tp.fx[s] = 0; destroy (o); }
		if (fi && !tp.fx[s])
		{
			tp.fx[s] = create (ps->id, ps->state.c (), false);
			if (!tp.fx[s]) ok = false;
		}
		if (ps && !ps->id.empty () && !fi) ok = false;
		if (tp.fx[s])
		{
			bool up = tp.fx[s]->m_state == PLUG_READY || tp.fx[s]->m_state == PLUG_STARTING;
			if (ps->enabled && up && (tp.fx[s]->m_track != track || tp.fx[s]->m_slot != s)) ok = connectEffect (tp.fx[s], track, s) && ok;
			else if (!ps->enabled && tp.fx[s]->m_fx) disconnect (tp.fx[s]);
		}
	}
	return ok;
}

void PlugHost::saveTrack (int track, Track &t)
{
	if (track < 0 || track >= m_tracks.size ()) return;
	TrackPlugs &tp = m_tracks[track];
	Str st;
	if (tp.instr && tp.instr->m_info.id == t.instrumentPlugin.id && getState (tp.instr, st)) t.instrumentPlugin.state = st;
	for (int s = 0; s < 4 && s < t.inserts.size (); s++)
		if (tp.fx[s] && tp.fx[s]->m_info.id == t.inserts[s].id && getState (tp.fx[s], st)) t.inserts[s].state = st;
}

void PlugHost::releaseTracks (int from)
{
	for (int t = from < 0 ? 0 : from; t < m_tracks.size (); t++)
	{
		TrackPlugs &tp = m_tracks[t];
		if (tp.instr) { PlugInstance *o = tp.instr; tp.instr = 0; destroy (o); }
		for (int s = 0; s < 4; s++) if (tp.fx[s]) { PlugInstance *o = tp.fx[s]; tp.fx[s] = 0; destroy (o); }
	}
	if (from >= 0 && from < m_tracks.size ()) m_tracks.resize (from);
}

// ---- generators ------------------------------------------------------------------------------------------------
static PlugHost *s_hookHost;
static bool generatorHook (const GeneratorModule &m, const Project &p, double startBeat, Riff &out)
{
	return s_hookHost && s_hookHost->generate (m, p, startBeat, out);
}
void PlugHost::installGeneratorHook () { s_hookHost = this; g_generatorHook = generatorHook; }

PlugInstance *PlugHost::generator (const char *id)
{
	const PlugInfo *info = find (id);
	if (!m_ok || !info || info->kind != KP_GENERATOR) return 0;
	PlugInstance *g = 0;
	pthread_mutex_lock (&m_lock);
	for (int i = 0; i < m_inst.size () && !g; i++)
	{
		PlugInstance *p = m_inst[i];
		if (p->m_generator && !p->m_dying && p->m_info.id == info->id && (p->m_state == PLUG_READY || p->m_state == PLUG_STARTING)) g = p;
	}
	pthread_mutex_unlock (&m_lock);
	if (g) { g->m_usedT = ticks (); return g; }
	g = create (info->id, 0, true);
	if (g) { g->m_generator = true; g->m_usedT = ticks (); }
	return g;
}

// The song is compiled again at every edit: a module whose request (its state, the chords under it, the
// key...) did not change gets the notes its process gave last time, without asking again.
static unsigned fnv (const char *s, unsigned long n, unsigned h = 2166136261u)
{
	for (unsigned long i = 0; i < n; i++) { h ^= (unsigned char) s[i]; h *= 16777619u; }
	return h;
}

bool PlugHost::generate (const GeneratorModule &m, const Project &p, double startBeat, Riff &out, int timeoutMs)
{
	PlugInstance *g = generator (m.generatorId);
	if (!g) return false;
	Str st; moduleStateJson (m, st);
	json::Writer w (false);
	genContext (m, p, startBeat, st.c (), w);
	if (!w.ok ()) return false;
	unsigned h = fnv (w.data (), w.size (), fnv (g->m_info.id.c (), g->m_info.id.len ()));
	pthread_mutex_lock (&m_lock);
	for (int i = 0; i < m_genCache.size (); i++)
		if (m_genCache[i].hash == h && m_genCache[i].len == (unsigned) w.size () && m_genCache[i].number == g->m_number)
		{
			out.notes = m_genCache[i].notes;
			pthread_mutex_unlock (&m_lock);
			g->m_usedT = ticks ();
			return true;
		}
	pthread_mutex_unlock (&m_lock);
	Str reply;
	g->m_usedT = ticks ();
	if (!request (g, KP_GENERATE, w.data (), (unsigned) w.size (), &reply, timeoutMs)) return false;
	g->m_usedT = ticks ();
	if (!genReply (reply.c (), (unsigned long) reply.len (), out)) return false;
	pthread_mutex_lock (&m_lock);
	if (m_genCache.size () >= GEN_CACHE) m_genCache.removeAt (0);
	GenCached &c = m_genCache.add ();
	c.hash = h; c.len = (unsigned) w.size (); c.number = g->m_number; c.notes = out.notes;
	pthread_mutex_unlock (&m_lock);
	return true;
}

PlugEditorView *PlugHost::openGeneratorEditor (const GeneratorModule &m, View &parent, int x, int y, int w, int h)
{
	PlugInstance *g = generator (m.generatorId);
	if (!g) return 0;
	Str st; moduleStateJson (m, st);
	setState (g, st.empty () ? "{}" : st.c ());
	g->m_editModule = m.id;
	return openEditor (g, parent, x, y, w, h);
}

bool PlugHost::pullGeneratorState (PlugInstance *p, Project &pr)
{
	if (!p || !p->m_generator || p->m_editModule.empty ()) return false;
	for (int t = 0; t < pr.tracks.size (); t++)
		for (int i = 0; i < pr.tracks[t].items.size (); i++)
		{
			Module *m = pr.tracks[t].items[i].module;
			if (m && m->kind == M_GENERATOR && m->id == p->m_editModule) return pullGeneratorState (p, *(GeneratorModule *) m);
		}
	return false;
}

bool PlugHost::pullGeneratorState (PlugInstance *p, GeneratorModule &m)
{
	Str st, old;
	if (!getState (p, st)) return false;
	moduleStateJson (m, old);
	if (old == st) return false;
	setModuleStateJson (m, st.c ());
	return true;
}

// ---- editors ------------------------------------------------------------------------------------------------------
// a parameter shown as a check box: its choices are Off / On
static bool isToggle (const PlugParam &q) { return q.choices.size () == 2 && q.choices[0] == "Off" && q.choices[1] == "On"; }

enum { ED_ROW = 600, ED_MARGIN = 10, ED_GAP = 8, ED_DIAL = 58, ED_KNOB = 96, ED_CHOICE = 170, ED_TOGGLE = 150 };
static int edKnobH () { return 2 * th () + ED_DIAL + 8; }
static int edCellW (const PlugParam &q) { return q.choices.size () && !isToggle (q) ? ED_CHOICE : isToggle (q) ? ED_TOGGLE : ED_KNOB; }

void PlugHost::editorSize (const PlugInstance *p, int *w, int *h) const { editorSize (p ? &p->m_info : 0, w, h); }
void PlugHost::editorSize (const PlugInfo *info, int *w, int *h) const
{
	int W = 480, H = 240;
	if (info && info->editorW > 0 && info->editorH > 0) { W = info->editorW; H = info->editorH; }
	else if (info)
	{
		AppWindow *r = AppWindow::current ();
		int rowW = r && r->width - 80 < ED_ROW ? r->width - 80 : ED_ROW;	// (as the editor lays its controls out)
		int x = ED_MARGIN, rows = 1;
		for (int i = 0; i < info->params.size (); i++)
		{
			int cw = edCellW (info->params[i]);
			if (x + cw > rowW - 6 && x > ED_MARGIN) { x = ED_MARGIN; rows++; }
			x += cw;
		}
		W = rowW; H = ED_MARGIN + rows * (edKnobH () + ED_GAP) + 6;
	}
	if (w) *w = W;
	if (h) *h = H;
}

PlugEditorView *PlugHost::openEditor (PlugInstance *p, View &parent, int x, int y, int w, int h)
{
	if (!p || p->m_dying || w < 32 || h < 32) return 0;
	if (p->m_editor) closeEditor (p->m_editor);
	if (!waitReady (p, 3000)) return 0;
	PlugEditorView *v = new PlugEditorView (this, p, x, y, w, h);
	p->m_editor = v;
	parent.addChild (v);
	return v;
}

void PlugHost::closeEditor (PlugEditorView *v)
{
	if (!v) return;
	if (v->parent) v->parent->removeChild (v);
	delete v;					// (its destructor: editorGone)
}

void PlugHost::editorGone (PlugEditorView *v)
{
	PlugInstance *p = v->m_inst;
	if (!p || p->m_editor != v) return;
	p->m_editor = 0;
	p->m_editModule = "";
}

// a value on a control: 0..1000 for a continuous parameter, its steps for a stepped one
static int ctlHi (const PlugParam &q) { return q.step > 0 ? (int) ((q.max - q.min) / q.step + 0.5f) : 1000; }
static int ctlOf (const PlugParam &q, float v)
{
	float f = q.max > q.min ? (v - q.min) / (q.max - q.min) : 0;
	return (int) (f * (float) ctlHi (q) + 0.5f);
}

PlugEditorView::PlugEditorView (PlugHost *h, PlugInstance *p, int x, int y, int w, int hh)
	: View (x, y, w, hh), m_host (h), m_inst (p), m_touchT (0), m_live (true)
{
	const Vec<PlugParam> &ps = p->m_info.params;
	int cx = ED_MARGIN, cy = ED_MARGIN, KH = edKnobH (), fh = th ();
	for (int i = 0; i < ps.size () && i < KP_MAX_PARAMS; i++)
	{
		const PlugParam &q = ps[i];
		float v = p->param (i);
		int cw = edCellW (q), cv = ctlOf (q, v);
		if (cx + cw > w - 6 && cx > ED_MARGIN) { cx = ED_MARGIN; cy += KH + ED_GAP; }
		m_x[i] = (short) cx; m_y[i] = (short) cy; m_val[i] = m_seen[i] = v;
		if (isToggle (q))
		{
			m_kind[i] = 2;
			m_ctl[i] = addCtl (ui_checkbox (cx, cy + 4, cw - 8, 24, (long) TR (q.name.c ()), v >= 0.5f), 1 + i, cx, cy + 4, cw - 8, 24, UN_CHANGE);
		}
		else if (q.choices.size ())
		{
			m_kind[i] = 1;
			const char *names[32]; int n = 0;
			for (; n < q.choices.size () && n < 32; n++) names[n] = q.choices[n].c ();
			m_ctl[i] = addCtl (ui_dropdown (cx, cy + fh + 4, cw - 8, 28, (long) joinLabels (names, n)), 1 + i, cx, cy + fh + 4, cw - 8, 28, UN_CHANGE);
			ui_w_set_value (m_ctl[i], cv);
		}
		else
		{
			m_kind[i] = 0;
			m_ctl[i] = addCtl (ui_knob (cx + 8, cy + fh + 4, cw - 20, ED_DIAL, 0, ctlHi (q), cv), 1 + i, cx + 8, cy + fh + 4, cw - 20, ED_DIAL, UN_CHANGE);
		}
		cx += cw;
	}
}

PlugEditorView::~PlugEditorView () { if (m_host) m_host->editorGone (this); }

// the value of a parameter as text ("440 Hz", "0.35")
static void formatValue (const PlugParam &q, float v, char *out, int cap)
{
	float a = v < 0 ? -v : v;
	int dec = q.step >= 1 ? 0 : a >= 100 ? 0 : a >= 10 ? 1 : 2;
	snprintf (out, (size_t) cap, "%.*f%s%s", dec, (double) v, q.unit.empty () ? "" : " ", q.unit.c ());
}

void PlugEditorView::onDraw ()
{
	PlugInstance *p = m_inst;
	ui_fill (0, 0, width, height, C_FACE);		// (the face of UIKit's knobs)
	if (!p || !m_live)
	{
		const char *s = !p ? "No plugin" : p->m_err[0] ? p->m_err : "The plugin stopped.";
		ui_text ((long) TR (s), ED_MARGIN, ED_MARGIN + textPx (), C_TEXT, textPx (), textFont (0), 0);
		return;
	}
	int KH = edKnobH (), fh = th ();
	for (int i = 0; i < p->m_info.params.size () && i < KP_MAX_PARAMS; i++)
	{
		const PlugParam &q = p->m_info.params[i];
		if (m_kind[i] == 2) continue;
		int x = m_x[i], w = edCellW (q) - 8;
		ui_text_fit ((long) TR (q.name.c ()), x, m_y[i] + textPx (), C_TEXT, textPx (), textFont (0), w);
		if (m_kind[i]) continue;
		char t[48]; formatValue (q, m_val[i], t, sizeof t);
		int tw_ = tw (t);
		ui_text ((long) t, x + (w - tw_) / 2, m_y[i] + KH - fh + textPx (), C_TEXT, textPx (), textFont (0), 0);
	}
}

void PlugEditorView::onControl (long w, int tag, int code)
{
	(void) code;
	PlugInstance *p = m_inst;
	int i = tag - 1;
	if (!p || !m_live || i < 0 || i >= p->m_info.params.size () || i >= KP_MAX_PARAMS) return;
	const PlugParam &q = p->m_info.params[i];
	int c = (int) ui_w_get_value (w);
	float v = q.min + (q.max - q.min) * (float) c / (float) ctlHi (q);
	m_val[i] = v;
	m_touchT = ticks ();
	m_host->setParam (p, i, v);
	if (m_kind[i] == 0) invalidate (true);
	if (m_host->onParam) m_host->onParam (p, i, v, m_host->ctx);
}

void PlugEditorView::sync ()
{
	PlugInstance *p = m_inst;
	if (!p) return;
	bool live = p->m_state == PLUG_READY;
	if (live != m_live)
	{
		m_live = live;
		for (int i = 0; i < p->m_info.params.size () && i < KP_MAX_PARAMS; i++) ctlShow (m_ctl[i], live);
		invalidate (true);
	}
	if (!live || ticks () - m_touchT < 40) return;		// (a control in the hand: not moved under it)
	for (int i = 0; i < p->m_info.params.size () && i < KP_MAX_PARAMS; i++)
	{
		float v = p->m_shm->param[i];
		if (v == m_seen[i]) continue;
		m_seen[i] = v;
		const PlugParam &q = p->m_info.params[i];
		if (ctlOf (q, v) == ctlOf (q, m_val[i])) { m_val[i] = v; continue; }
		m_val[i] = v;
		ui_w_set_value (m_ctl[i], ctlOf (q, v));
		invalidate (true);
	}
}

// ---- the loop ---------------------------------------------------------------------------------------------------
void PlugHost::handleMessage (PlugInstance *p, int type, const void *data, int len)
{
	(void) data; (void) len;
	switch (type)
	{
	case KP_HELLO: if (p->m_state == PLUG_STARTING && kp_ld32 (&p->m_shm->ready) == KP_READY) p->m_state = PLUG_READY; break;
	case KP_BYE: if (!p->m_dying) lose (p, "The plugin ended."); break;
	default: break;
	}
}

void PlugHost::poll ()
{
	if (!m_ok) return;
	for (int i = 0; i < m_inst.size (); i++)
	{
		PlugInstance *p = m_inst[i];
		KpMsg m;
		int n = 1;
		while (p->m_sock >= 0 && (n = (int) recv (p->m_sock, &m, sizeof m, MSG_DONTWAIT)) > 0)
			handleMessage (p, m.type, m.data, n - (int) sizeof m.type);
		// its end closed: the process died (a KP_BYE came first if it ended by itself)
		if (p->m_sock >= 0 && n == 0 && !p->m_dying) lose (p, "The plugin stopped (it crashed?).");
	}
	tick ();
}

void PlugHost::tick ()
{
	if (!m_ok) return;
	unsigned now = ticks ();
	// what the engine let go (two blocks after: it is no longer inside them)
	for (int i = 0; i < m_retired.size (); )
	{
		if (!m_engine || m_engine->renders - m_retired[i].renders >= 2) { delete m_retired[i].src; delete m_retired[i].fx; m_retired.removeAt (i); }
		else i++;
	}
	// the parameters not sent yet (a full channel, a plugin starting)
	for (int i = 0; i < m_inst.size (); i++) flushParams (m_inst[i]);
	// the editors follow the plugins
	for (int i = 0; i < m_inst.size (); i++) if (m_inst[i]->m_editor) m_inst[i]->m_editor->sync ();
	// the source of an instrument taken out while the command ring was full: again
	for (int i = 0; i < m_inst.size (); i++) if (m_inst[i]->m_dying && m_inst[i]->m_src) disconnect (m_inst[i]);
	// twice a second: which processes still run
	if (now - m_lastProcs < 50) return;
	m_lastProcs = now;
	pthread_mutex_lock (&m_lock);
	for (int i = 0; i < m_inst.size (); i++)
	{
		PlugInstance *p = m_inst[i];
		KpShm *s = p->m_shm;
		if (p->m_dying) continue;
		if (p->m_state == PLUG_STARTING)
		{
			unsigned r = kp_ld32 (&s->ready);
			if (r == KP_READY) p->m_state = PLUG_READY;
			else if (r == KP_FAILED) { p->m_state = PLUG_FAILED; setErr (p->m_err, "It refused to start."); if (onCrash) onCrash (p, ctx); }
			else if (now - p->m_t0 > 800 || ended (p)) { pthread_mutex_unlock (&m_lock); lose (p, "It did not start."); pthread_mutex_lock (&m_lock); }
			continue;
		}
		if (p->m_state != PLUG_READY) continue;
		if (ended (p)) { pthread_mutex_unlock (&m_lock); lose (p, "The plugin stopped (it crashed?)."); pthread_mutex_lock (&m_lock); continue; }
		// hung: its main thread stopped beating for 3 s, or its render thread while it has frames to render
		if (s->beat != p->m_beat) { p->m_beat = s->beat; p->m_beatT = now; }
		unsigned long long want = s->want;
		if (s->dspBeat != p->m_dspBeat || want == p->m_dspWant || s->done >= want) { p->m_dspBeat = s->dspBeat; p->m_dspT = now; }
		p->m_dspWant = want;
		if (p->m_busy) p->m_beatT = now;		// (a request in flight has its own timeout)
		if (now - p->m_beatT > 300 || (p->m_info.kind != KP_GENERATOR && now - p->m_dspT > 300))
		{
			pthread_mutex_unlock (&m_lock); lose (p, "The plugin stopped answering: ended."); pthread_mutex_lock (&m_lock);
			continue;
		}
		// a generator unused for a minute (no editor open, no request): ended
		if (p->m_generator && !p->m_editor && !p->m_busy && now - p->m_usedT > 6000) { p->m_dying = true; p->m_dieT = now; sendTo (p, KP_BYE, 0, 0); }
	}
	// the ending ones: gone (or killed a second after their KP_BYE), their region free again once the
	// engine let their side go
	for (int i = 0; i < m_inst.size (); )
	{
		PlugInstance *p = m_inst[i];
		if (!p->m_dying) { i++; continue; }
		bool gone = ended (p);
		if (!gone && now - p->m_dieT > 100) { kill9 (p); gone = true; }
		bool held = p->m_src != 0;
		for (int k = 0; k < m_retired.size () && !held; k++)
			if ((m_retired[k].src && m_retired[k].src->shm () == p->m_shm) || (m_retired[k].fx && m_retired[k].fx->shm () == p->m_shm)) held = true;
		if (gone && !held)
		{
			if (p->m_pool >= 0 && p->m_pool < m_regions.size ()) m_regions[p->m_pool].used = false;
			m_inst.removeAt (i);
			delete p;
		}
		else i++;
	}
	pthread_mutex_unlock (&m_lock);
}

void PlugHost::shutdown ()
{
	if (!m_ok) return;
	if (s_hookHost == this) { g_generatorHook = 0; s_hookHost = 0; }
	for (int i = 0; i < m_inst.size (); i++)
	{
		PlugInstance *p = m_inst[i];
		if (p->m_editor) { p->m_editor->m_host = 0; p->m_editor->m_inst = 0; p->m_editor = 0; }
		disconnect (p);
		for (int k = 0; k < 50 && p->m_src; k++) { usleep (2000); disconnect (p); }	// (the engine's command ring was full)
		if (p->m_src) { retire (p->m_src, 0); p->m_src = 0; }
		sendTo (p, KP_BYE, 0, 0);
		p->m_dying = true;
	}
	// a second for them to end by themselves, then killed
	unsigned t0 = ticks ();
	for (;;)
	{
		bool any = false;
		for (int i = 0; i < m_inst.size (); i++) if (!ended (m_inst[i])) any = true;
		if (!any || ticks () - t0 > 100) break;
		usleep (20000);
	}
	for (int i = 0; i < m_inst.size (); i++)
	{
		PlugInstance *p = m_inst[i];
		kill9 (p);
		delete p;
	}
	m_inst.clear ();
	m_tracks.clear ();
	m_genCache.clear ();
	// the engine's sides: once it rendered two blocks since (it has let them go), or 0.2 s went by
	// without a block (it is not rendering: stopped, as it must be by now)
	if (m_engine) { unsigned r = m_engine->renders; for (int k = 0; k < 20 && m_engine->renders - r < 2; k++) usleep (10000); }
	for (int i = 0; i < m_retired.size (); i++) { delete m_retired[i].src; delete m_retired[i].fx; }
	m_retired.clear ();
	for (int i = 0; i < m_regions.size (); i++) { munmap (m_regions[i].shm, KP_SHM_BYTES); close (m_regions[i].fd); }	// (no one maps them now)
	m_regions.clear ();
	m_ok = false;
}

} // namespace kt
