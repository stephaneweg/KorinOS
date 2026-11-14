//
// ui/ai_dialog.h -- "Compose with AI": Koton's AI dialog, Onyx's on UIKit. What to ask (a whole piece, a development
// of the theme, a new instrument over the song, drums, a polyrhythmic piece), the style and the intention in words,
// the length and the options (melody as riffs, drums, the AI voicing the chords, poly chords / poly drums), the
// provider, its model and the API key. Generate runs the helper SD0:/bin/llm (src/bin/llm: the request on its
// stdin, the model's text on its stdout, its progress on its stderr) with the dialog left open, followed at each
// turn of the dialog's loop (onIdle), its progress at the bottom (Cancel stops it); the reply is checked, its
// summary shown, and "Apply as a new song" places it by engine/ai (aiApplyReply) on a new song -- the piece alone,
// or a copy of the song with the addition. Without a key: "Copy the prompt" (to paste in any chat) and "Paste a
// reply" (the chat's answer, checked the same way).
//
#ifndef _koton_ai_dialog_h
#define _koton_ai_dialog_h

#include "ui/dialogs.h"
#include "engine/ai.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdarg.h>
#include <unistd.h>
#include <sys/wait.h>

extern char **environ;

namespace kui {

// the dialog's last request (what, style, intention, bars, the options): the next opening starts from it
// (kept in SD0:/home/koton/settings.json with the provider)
struct AiLast
{
	int kind; Str style, intention; int bars; bool full, drums, voice, pchords, pdrums;
	AiLast () : kind (AI_COMPOSE), style ("bossa nova, warm, acoustic"), intention ("A calm intro, a verse, a brighter chorus, an ending."),
		bars (32), full (false), drums (true), voice (false), pchords (false), pdrums (false) {}
};
struct AiSettings { char *provider; int provCap; char *model; int modelCap; char *key; int keyCap; AiLast *last; void (*save) ();
	bool (*askSave) (); };		// (askSave: the song's unsaved changes before a new song -- false: cancelled)

static const int s_aiKinds[5] = { AI_COMPOSE, AI_DEVELOP, AI_ADD_TRACK, AI_ADD_DRUMS, AI_POLYRHYTHM };
static const char *const s_aiKindNames[5] = { "Compose a piece", "Develop the theme (after the end)", "Add an instrument over the song",
	"Add drums over the song", "A polyrhythmic piece" };

static const char *const LLM_PATH = "/bin/llm";

// The helper, run without blocking: the request written to its stdin as it takes it, its stdout and stderr read
// into `out` as they come (one pipe: the progress lines, then the result line). poll () at each turn of the loop.
class LlmRun
{
public:
	Vec<char> out;
	LlmRun () : m_pid (-1), m_in (-1), m_out (-1), m_req (0), m_len (0), m_off (0) {}
	~LlmRun () { cancel (); }
	bool running () const { return m_pid > 0; }
	bool start (const char *req, unsigned len)
	{
		cancel ();
		out.clear ();
		if (access (LLM_PATH, F_OK) != 0) return false;		// (FAT: no execute bit to ask for)
		signal (SIGPIPE, SIG_IGN);		// (a helper that stops reading: write says EPIPE, Koton lives on)
		int in[2], o[2];
		if (pipe2 (in, O_CLOEXEC)) return false;
		if (pipe2 (o, O_CLOEXEC)) { close (in[0]); close (in[1]); return false; }
		posix_spawn_file_actions_t fa;
		posix_spawn_file_actions_init (&fa);
		posix_spawn_file_actions_adddup2 (&fa, in[0], 0);
		posix_spawn_file_actions_adddup2 (&fa, o[1], 1);
		posix_spawn_file_actions_adddup2 (&fa, o[1], 2);
		char *const argv[] = { (char *) "llm", 0 };
		pid_t pid = 0;
		int e = posix_spawn (&pid, LLM_PATH, &fa, 0, argv, environ);
		posix_spawn_file_actions_destroy (&fa);
		close (in[0]); close (o[1]);
		if (e) { close (in[1]); close (o[0]); return false; }
		fcntl (in[1], F_SETFL, fcntl (in[1], F_GETFL, 0) | O_NONBLOCK);
		fcntl (o[0], F_SETFL, fcntl (o[0], F_GETFL, 0) | O_NONBLOCK);
		m_pid = pid; m_in = in[1]; m_out = o[0];
		m_req = (char *) malloc (len ? len : 1);
		if (m_req) memcpy (m_req, req, len);
		m_len = m_req ? len : 0; m_off = 0;
		if (!m_len) closeIn ();
		return true;
	}
	// the request fed, the output read; true when the helper has ended (all its output in `out`)
	bool poll ()
	{
		if (m_pid <= 0) return true;
		while (m_in >= 0 && m_off < m_len)
		{
			ssize_t w = write (m_in, m_req + m_off, m_len - m_off);
			if (w > 0) { m_off += (unsigned) w; continue; }
			if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
			closeIn (); break;		// (it does not read any more: its answer will say why)
		}
		if (m_in >= 0 && m_off >= m_len) closeIn ();
		drain ();
		int st = 0;
		if (waitpid (m_pid, &st, WNOHANG) == 0) return false;
		m_pid = -1;
		drain ();
		closeAll ();
		return true;
	}
	void cancel ()
	{
		if (m_pid > 0) { kill (m_pid, SIGKILL); waitpid (m_pid, 0, 0); m_pid = -1; }
		closeAll ();
	}
	// its last progress line ("llm: ...", a line of its own)
	bool lastProgress (char *line, int cap) const
	{
		int i = out.size () - 1;
		while (i >= 0 && (out[i] == '\n' || out[i] == '\r')) i--;
		int e2 = i + 1;
		while (i >= 0 && out[i] != '\n') i--;
		if (e2 - (i + 1) > 5 && out[i + 1] == 'l' && out[i + 2] == 'l' && out[i + 3] == 'm')
		{ snprintf (line, cap, "%.*s", imin (cap - 1, e2 - (i + 1)), out.data () + i + 1); return true; }
		return false;
	}
private:
	pid_t m_pid; int m_in, m_out; char *m_req; unsigned m_len, m_off;
	void drain ()
	{
		char buf[4096];
		for (;;)
		{
			if (m_out < 0) return;
			ssize_t n = read (m_out, buf, sizeof buf);
			if (n > 0) { for (int i = 0; i < (int) n; i++) out.push (buf[i]); continue; }
			if (n == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) { close (m_out); m_out = -1; }
			return;
		}
	}
	void closeIn ()
	{
		if (m_in >= 0) { close (m_in); m_in = -1; }
		if (m_req) { memset (m_req, 0, m_len); free (m_req); m_req = 0; }	// (the key does not stay in memory)
	}
	void closeAll () { closeIn (); if (m_out >= 0) { close (m_out); m_out = -1; } }
};

// The dialog stays open while the model composes: its progress at the bottom, Cancel stops the request.
// The reply is checked and its summary shown with "Apply as a new song" -- or Generate again. "Copy the
// prompt" / "Paste a reply" (a chat's answer, through the clipboard) keep it open and end the same way.
class AiDialog : public KDialog
{
public:
	enum { W = 660, H = 560, B_GEN = 1, B_CANCEL = 2, B_COPY = 10, B_PASTE, B_APPLY, T_PROV = 20 };
	AiSettings &S;
	long kind, style, intent, bars, full, drums, voice, pchords, pdrums, prov, model, key;
	long genB, cancelB, copyB, pasteB, applyB;
	const char *provNames[AI_PROVIDER_COUNT];
	LlmRun llm; bool running, cancelReq;
	char status[400]; unsigned statusCol;
	Str reply; AiRequest replyReq;		// the reply waiting for Apply, and the request it answers
	Project result;				// the new song (Apply)
	AiDialog (AiSettings &s, int initialKind) : KDialog (W, H, "Compose with AI"), S (s), running (false), cancelReq (false), statusCol (C_TEXT)
	{
		status[0] = 0;
		// (KDialog's OK / Cancel: Generate / Close, found again by their place)
		genB = cancelB = 0;
		for (Ctl *c = ctls; c; c = c->next) { if (c->tag == 1) genB = c->h; else if (c->tag == 2) cancelB = c->h; }
		if (genB) ui_w_set_text (genB, (long) TR ("Generate"));
		if (cancelB) ui_w_set_text (cancelB, (long) TR ("Close"));
		int y = top0 (), x = 150, w = width - x - 16;
		static const AiLast s_def;
		const AiLast &L = s.last ? *s.last : s_def;
		if (initialKind < 0) initialKind = L.kind;	// (the menu: the last kind; the browser's entries: theirs)
		int ks = 0; for (int i = 0; i < 5; i++) if (s_aiKinds[i] == initialKind) ks = i;
		lab (16, y + 1, 130, "What"); kind = dropd (x, y, w, s_aiKindNames, 5, ks);
		y += 32;
		lab (16, y + 1, 130, "Style"); style = field (x, y, w, 255, L.style.c (), 0);
		y += 34;
		lab (16, y + 1, 130, "Intention");
		intent = addCtl (ui_textarea (x, y, w, 70, UTA_VSCROLL | UTA_FORMTAB), 0, x, y, w, 70, -1);
		ui_w_set_text (intent, (long) L.intention.c ());
		y += 78;
		lab (16, y + 1, 130, "Bars (about)"); bars = numb (x, y, 90, 4, 128, iclamp (L.bars, 4, 128));
		y += 34;
		full = check (x, y, 240, "The melody as notes (riffs)", L.full); drums = check (x + 250, y, 200, "Drums", L.drums);
		y += 26;
		voice = check (x, y, 240, "The AI voices the chords", L.voice); pchords = check (x + 250, y, 200, "Poly chords", L.pchords);
		y += 26;
		pdrums = check (x + 250, y, 200, "Poly drums", L.pdrums);
		y += 36;
		int ps = 0;
		for (int i = 0; i < AI_PROVIDER_COUNT; i++) { provNames[i] = aiProviderLabel (g_aiProviders[i]); if (!strcmp (g_aiProviders[i], s.provider)) ps = i; }
		lab (16, y + 1, 130, "Provider");
		prov = addCtl (ui_dropdown (x, y, 170, 24, (long) joinLabels (provNames, AI_PROVIDER_COUNT)), T_PROV, x, y, 170, 24, UN_CHANGE);
		ui_w_set_value (prov, ps);
		model = field (x + 180, y, w - 180, s.modelCap - 1, s.model, 0);
		ui_w_tip (model, (long) TR ("The model's name at this provider"));
		y += 34;
		lab (16, y + 1, 130, "API key"); key = field (x, y, w, s.keyCap - 1, s.key, UTF_PASSWORD);
		ui_w_tip (key, (long) TR ("Kept in SD0:/home/koton/settings.json, in plain text"));
		copyB = button (16, height - 40, 150, "Copy the prompt", B_COPY);
		pasteB = button (174, height - 40, 140, "Paste a reply", B_PASTE);
		applyB = button (width - 216, height - 86, 200, "Apply as a new song", B_APPLY);
		ctlShow (applyB, false);
		ui_w_focus (style);
	}
	long field (int x, int y, int w, int maxLen, const char *text, int flags)
	{
		long h = addCtl (ui_textbox (x, y, w, 26, flags), 0, x, y, w, 26, -1);
		ui_w_set_range (h, 0, maxLen, 0, 0); ui_w_set_text (h, (long) text);
		return h;
	}
	long check (int x, int y, int w, const char *label, bool on) { return addCtl (ui_checkbox (x, y, w, 22, (long) TR (label), on), 0, x, y, w, 22, -1); }
	long button (int x, int y, int w, const char *label, int tag)
	{
		return addCtl (ui_pushbutton (x, y, w, 28, (long) TR (label), 0), tag, x, y, w, 28, UN_ACTIVATE);
	}
	static bool on (long w) { return ui_w_get_value (w) != 0; }
	static const char *text (long w) { const char *t = (const char *) ui_w_get_text (w); return t ? t : ""; }
	int provSel () { return iclamp ((int) ui_w_get_value (prov), 0, AI_PROVIDER_COUNT - 1); }
	void onControl (long w, int tag, int code) override
	{
		if (tag == T_PROV && code == UN_CHANGE) { ui_w_set_text (model, (long) aiProviderDefaultModel (g_aiProviders[provSel ()])); return; }
		KDialog::onControl (w, tag, code);
	}
	void setStatus (unsigned col, const char *fmt, ...) __attribute__ ((format (printf, 3, 4)))
	{
		va_list ap; va_start (ap, fmt); vsnprintf (status, sizeof status, fmt, ap); va_end (ap);
		statusCol = col; invalidate (true);
	}
	void request (AiRequest &r)
	{
		r.kind = s_aiKinds[iclamp ((int) ui_w_get_value (kind), 0, 4)];
		r.style = text (style); r.intention = text (intent);
		r.measures = iclamp ((int) ui_w_get_value (bars), 4, 128);
		r.fullMelody = on (full); r.drums = on (drums); r.chordsVoice = on (voice);
		r.polyChords = on (pchords); r.polyDrums = on (pdrums);
		r.english = true;
	}
	// the provider, model, key and this request kept for the next time (SD0:/home/koton/settings.json)
	void remember (const AiRequest &r)
	{
		snprintf (S.provider, S.provCap, "%s", g_aiProviders[provSel ()]);
		snprintf (S.model, S.modelCap, "%s", text (model));
		snprintf (S.key, S.keyCap, "%s", text (key));
		if (S.last)
		{
			AiLast &L = *S.last;
			L.kind = r.kind; L.style = r.style; L.intention = r.intention; L.bars = r.measures;
			L.full = r.fullMelody; L.drums = r.drums; L.voice = r.chordsVoice; L.pchords = r.polyChords; L.pdrums = r.polyDrums;
		}
		if (S.save) S.save ();
	}
	bool prompt (AiRequest &r, Str &sys, Str &usr)
	{
		request (r); remember (r);
		char err[200] = "";
		if (!aiBuildPrompt (g_doc.p, r, sys, usr, err, sizeof err)) { setStatus (REC, "%s", TR (err)); return false; }
		return true;
	}
	// a reply received (or pasted): checked, then waiting for Apply
	void gotReply (const AiRequest &r, const char *t)
	{
		char sum[200];
		if (!aiCheckReply (r, t, sum, sizeof sum)) { ctlShow (applyB, false); setStatus (REC, "%s %s", TR ("The reply cannot be used:"), sum); return; }
		reply = t; replyReq = r;
		ctlShow (applyB, true);
		setStatus (GREEN, "%s %s", TR ("Reply received --"), sum);
	}
	void busy (bool b)
	{
		running = b;
		ctlShow (genB, !b); ctlShow (copyB, !b); ctlShow (pasteB, !b);
		if (b) ctlShow (applyB, false);
		if (cancelB) ui_w_set_text (cancelB, (long) TR (b ? "Cancel" : "Close"));
		invalidate (true);
	}
	void generate ()
	{
		AiRequest r; Str sys, usr;
		if (!prompt (r, sys, usr)) return;
		bool keyless = !strcmp (S.provider, "openai-compatible");
		if (!S.key[0] && !keyless) { setStatus (REC, "%s", TR ("An API key is needed (Gemini: aistudio.google.com, free). Or Copy the prompt / Paste a reply.")); return; }
		json::Writer w (false);
		aiBuildRequestJson (S.provider, S.model, S.key, sys, usr, 0.7, -1, w);
		if (!w.ok ()) { setStatus (REC, "%s", TR ("The request could not be built.")); return; }
		if (!llm.start (w.data (), (unsigned) w.size ()))
		{
			setStatus (REC, "%s", TR ("SD0:/bin/llm cannot be started (is the system up to date?)."));
			return;
		}
		pending = r;
		cancelReq = false; busy (true);
		setStatus (C_TEXT, "%s", TR ("Starting SD0:/bin/llm..."));
	}
	// the helper followed: its progress, then its answer
	void onIdle () override
	{
		if (!running) return;
		if (cancelReq) { llm.cancel (); busy (false); setStatus (REC, "%s", TR ("Cancelled.")); return; }
		bool ended = llm.poll ();
		char line[200];
		if (!ended)
		{
			if (llm.lastProgress (line, sizeof line) && strcmp (line, status)) setStatus (C_TEXT, "%s", line);
			unsigned t = ticks () / 8;
			if (t != m_anim) { m_anim = t; invalidate (true); }
			return;
		}
		busy (false);
		llm.out.push (0);
		Str t, error;
		if (!aiParseLlmOutput (llm.out.data (), (unsigned long) (llm.out.size () - 1), t, error))
		{ setStatus (REC, "%s %s", TR ("The AI did not answer:"), error.c ()); return; }
		gotReply (pending, t.c ());
	}
	void copyPrompt ()
	{
		AiRequest r; Str sys, usr;
		if (!prompt (r, sys, usr)) return;
		Str all; aiFullPrompt (sys, usr, all);
		ui_clip_set ((long) all.c (), -1);
		setStatus (C_TEXT, "%s", TR ("The prompt is on the clipboard: paste it in a chat with any AI, copy its whole answer, then Paste a reply."));
	}
	void pasteReply ()
	{
		AiRequest r; request (r); remember (r);
		int cap = 1 << 20; char *b = (char *) malloc ((size_t) cap);
		int n = b ? (int) ui_clip_get ((long) b, cap) : 0;
		if (n <= 0) { free (b); setStatus (REC, "%s", TR ("The clipboard holds no text.")); return; }
		b[imin (n, cap - 1)] = 0;
		gotReply (r, b);
		free (b);
	}
	// Apply: the reply placed on a new song (a piece: the reply alone; an addition: a copy of the song
	// with it); the current song's unsaved changes are asked about first
	void apply ()
	{
		Project np;
		if (replyReq.kind != AI_COMPOSE && replyReq.kind != AI_POLYRHYTHM) np = g_doc.p;
		char err[200] = "";
		if (!aiApplyReply (np, replyReq, reply.c (), err, sizeof err)) { ctlShow (applyB, false); setStatus (REC, "%s", err); return; }
		if (S.askSave && !S.askSave ()) return;
		result = np;
		close (1);
	}
	void onButton (int tag) override
	{
		if (running) { if (tag == B_CANCEL) { cancelReq = true; setStatus (C_TEXT, "%s", TR ("Cancelling...")); } return; }
		switch (tag)
		{
		case B_GEN: generate (); break;
		case B_COPY: copyPrompt (); break;
		case B_PASTE: pasteReply (); break;
		case B_APPLY: apply (); break;
		default: close (0); break;
		}
	}
	bool onKey (long k) override
	{
		if (k == 27) { if (running) cancelReq = true; else close (0); return true; }
		return false;				// (Enter: a new line of the intention)
	}
	void drawMore () override
	{
		// the status: two lines at most, above the buttons (left of Apply when it shows)
		bool ap = !running && reply.c ()[0] && !ctl (applyB)->hidden;
		int y = height - 90, maxW = ap ? width - 216 - 28 : width - 32;
		const char *s = status;
		for (int l = 0; l < 2 && *s; l++)
		{
			char ln[400]; int n = 0, lastSp = -1;
			while (s[n] && n < (int) sizeof ln - 1)
			{
				ln[n] = s[n]; ln[n + 1] = 0;
				if (tw (ln) > maxW) break;
				if (s[n] == ' ') lastSp = n;
				n++;
			}
			if (s[n] && l == 0 && lastSp > 0) n = lastSp;
			ln[n] = 0;
			textL (16, y + l * 20, 20, ln, statusCol);
			s += n; while (*s == ' ') s++;
		}
		if (running)
		{
			int t = (int) (ticks () / 8) % 24;
			for (int i = 0; i < 24; i++) box (16 + i * 17, height - 30, 12, 8, 3, i == t ? ACC : mixc (ACC, C_FACE, 190));
		}
	}
private:
	AiRequest pending;			// the request being answered
	unsigned m_anim = 0;
};

// the whole "Compose with AI": -> true when a new song was made of the reply
static bool aiCompose (AiSettings &s, int initialKind)
{
	AiDialog *d = new AiDialog (s, initialKind);
	int ok = d->run ();
	if (ok != 1) { delete d; return false; }
	char sum[200]; if (!aiCheckReply (d->replyReq, d->reply.c (), sum, sizeof sum)) sum[0] = 0;
	g_doc.adopt (d->result);
	delete d;
	snprintf (g_status, sizeof g_status, "%s %s", TR ("AI: a new song --"), sum);
	return true;
}

} // namespace kui

#endif
