/* mmsystem.c -- win16 : MMSYSTEM (sortie MIDI, minuteries multimédia, temps).
 *
 * Une sortie MIDI (« Korin MIDI ») : les messages vont à synth_midi (le synthétiseur, à venir : sans lui, ils sont
 * notés avec W16_TRACE=1). Pas d'entrée MIDI. Les minuteries de timeSetEvent sont appelées pendant l'attente des
 * messages (user.c) : sous Windows, elles le seraient pendant une interruption. */
#include <stdio.h>
#include <string.h>
#include "w16.h"
#include "be.h"
#include "user.h"

#define MMSYSERR_NOERROR 0
#define MMSYSERR_BADDEVICEID 2
#define MM_MOM_OPEN 0x3c7
#define MM_MOM_CLOSE 0x3c8
#define MM_MOM_DONE 0x3c9

/* le synthétiseur (pas encore là : une fonction faible) */
void synth_midi (uint32_t msg) __attribute__ ((weak));
void synth_sysex (const uint8_t *p, int n) __attribute__ ((weak));

static struct { int open; uint32_t cb, inst, flags; } mout;

static void mout_callback (uint16_t msg, uint32_t p1)
{
	uint32_t kind = mout.flags & 0x70000;
	if (kind == 0x10000)	/* CALLBACK_WINDOW */
		user_post ((uint16_t) mout.cb, msg, 1, p1);
	else if (kind == 0x30000 && mout.cb)	/* CALLBACK_FUNCTION : (handle, msg, instance, p1, p2) */
	{
		uint16_t a[8] = { 1, msg, (uint16_t) (mout.inst >> 16), (uint16_t) mout.inst, (uint16_t) (p1 >> 16), (uint16_t) p1, 0, 0 };
		call16 (mout.cb, a, 8, ne_ds_for (mout.cb));
	}
}

void api_midiOutGetNumDevs16 (struct w16call *c) { (void) c; c->ret = 1; }

/* MIDIOUTCAPS 16 bits : wMid, wPid, vDriverVersion, szPname[32], wTechnology, wVoices, wNotes, wChannelMask, dwSupport */
void api_midiOutGetDevCaps16 (struct w16call *c)
{
	if ((int16_t) c->a[0] > 0 && c->a[0] != 0xffff)
	{
		c->ret = MMSYSERR_BADDEVICEID;
		return;
	}
	uint8_t caps[52];
	memset (caps, 0, sizeof caps);
	caps[0] = 1;
	caps[4] = 0;
	caps[5] = 1;
	snprintf ((char *) caps + 6, 32, "Korin MIDI");
	caps[38] = 4;		/* MOD_SYNTH */
	caps[40] = 32;		/* voix */
	caps[42] = 32;		/* notes */
	caps[44] = 0xff;
	caps[45] = 0xff;	/* les 16 canaux */
	int n = (int) c->a[2] < (int) sizeof caps ? (int) c->a[2] : (int) sizeof caps;
	memcpy (far_ptr (c->a[1]), caps, (size_t) n);
	c->ret = MMSYSERR_NOERROR;
}

void api_midiOutOpen16 (struct w16call *c)
{
	if ((int16_t) c->a[1] > 0 && c->a[1] != 0xffff)
	{
		c->ret = MMSYSERR_BADDEVICEID;
		return;
	}
	if (c->a[0])
		wr16 (far_lin (c->a[0]), 1);
	mout.open = 1;
	mout.cb = c->a[2];
	mout.inst = c->a[3];
	mout.flags = c->a[4];
	mout_callback (MM_MOM_OPEN, 0);
	c->ret = MMSYSERR_NOERROR;
}

void api_midiOutClose16 (struct w16call *c)
{
	(void) c;
	if (mout.open)
		mout_callback (MM_MOM_CLOSE, 0);
	mout.open = 0;
	c->ret = MMSYSERR_NOERROR;
}

void api_midiOutShortMsg16 (struct w16call *c)
{
	uint32_t m = c->a[1];
	if (synth_midi)
		synth_midi (m);
	else
		w16_log ("   MIDI %02x %02x %02x\n", m & 0xff, (m >> 8) & 0xff, (m >> 16) & 0xff);
	c->ret = MMSYSERR_NOERROR;
}

/* MIDIHDR 16 bits : lpData, dwBufferLength, dwBytesRecorded, dwUser, dwFlags (+16) */
void api_midiOutPrepareHeader16 (struct w16call *c)
{
	uint32_t h = far_lin (c->a[1]);
	wr32 (h + 16, rd32 (h + 16) | 2);	/* MHDR_PREPARED */
	c->ret = MMSYSERR_NOERROR;
}

void api_midiOutUnprepareHeader16 (struct w16call *c)
{
	uint32_t h = far_lin (c->a[1]);
	wr32 (h + 16, rd32 (h + 16) & ~2u);
	c->ret = MMSYSERR_NOERROR;
}

void api_midiOutLongMsg16 (struct w16call *c)
{
	uint32_t h = far_lin (c->a[1]);
	uint32_t data = rd32 (h), len = rd32 (h + 4);
	if (synth_sysex && data && len < 65536)
		synth_sysex (far_ptr (data), (int) len);
	wr32 (h + 16, rd32 (h + 16) | 1);	/* MHDR_DONE */
	mout_callback (MM_MOM_DONE, c->a[1]);
	c->ret = MMSYSERR_NOERROR;
}

void api_midiOutReset16 (struct w16call *c)
{
	(void) c;
	/* (toutes les notes coupées : All Notes Off sur chaque canal) */
	for (uint32_t ch = 0; ch < 16; ch++)
	{
		if (synth_midi)
		{
			synth_midi (0xb0 | ch | 123u << 8);
			synth_midi (0xb0 | ch | 121u << 8);
		}
	}
	c->ret = MMSYSERR_NOERROR;
}

void api_midiOutGetVolume16 (struct w16call *c) { if (c->a[1]) wr32 (far_lin (c->a[1]), 0xffffffff); c->ret = 0; }
void api_midiOutSetVolume16 (struct w16call *c) { (void) c; c->ret = 0; }
void api_midiOutGetID16 (struct w16call *c) { if (c->a[1]) wr16 (far_lin (c->a[1]), 0); c->ret = 0; }
void api_midiOutCachePatches16 (struct w16call *c) { (void) c; c->ret = 0; }
void api_midiOutCacheDrumPatches16 (struct w16call *c) { (void) c; c->ret = 0; }
void api_midiOutMessage16 (struct w16call *c) { (void) c; c->ret = 0; }

void api_midiOutGetErrorText16 (struct w16call *c)
{
	static const char *texts[] = { "Pas d'erreur.", "Erreur MIDI.", "Périphérique MIDI absent." };
	int e = (int) c->a[0];
	const char *t = e >= 0 && e < 3 ? texts[e] : "Erreur MIDI.";
	char raw[64];
	w16_from_utf8 (t, (uint8_t *) raw, sizeof raw);
	int n = (int) c->a[2];
	if (n > 0)
	{
		int l = (int) strlen (raw);
		if (l > n - 1)
			l = n - 1;
		memcpy (far_ptr (c->a[1]), raw, (size_t) l);
		((char *) far_ptr (c->a[1]))[l] = 0;
	}
	c->ret = 0;
}

/* entrée MIDI : aucune */
void api_midiInGetNumDevs16 (struct w16call *c) { (void) c; c->ret = 0; }
void api_midiInGetDevCaps16 (struct w16call *c) { (void) c; c->ret = MMSYSERR_BADDEVICEID; }
void api_midiInOpen16 (struct w16call *c) { (void) c; c->ret = MMSYSERR_BADDEVICEID; }
void api_midiInClose16 (struct w16call *c) { (void) c; c->ret = 0; }
void api_midiInPrepareHeader16 (struct w16call *c) { (void) c; c->ret = 0; }
void api_midiInUnprepareHeader16 (struct w16call *c) { (void) c; c->ret = 0; }
void api_midiInAddBuffer16 (struct w16call *c) { (void) c; c->ret = 0; }
void api_midiInStart16 (struct w16call *c) { (void) c; c->ret = 0; }
void api_midiInStop16 (struct w16call *c) { (void) c; c->ret = 0; }
void api_midiInReset16 (struct w16call *c) { (void) c; c->ret = 0; }

/* ---- minuteries multimédia ---- */
#define MAX_MMT 16
static struct { int used, periodic; uint16_t id; uint32_t proc, user, delay, next; } mmt[MAX_MMT];
static int in_poll;

void api_timeGetTime16 (struct w16call *c) { (void) c; c->ret = be_ticks (); }

void api_timeGetSystemTime16 (struct w16call *c)
{
	uint32_t p = far_lin (c->a[0]);
	wr16 (p, 2);	/* TIME_MS */
	wr32 (p + 2, be_ticks ());
	c->ret = 0;
}

void api_timeGetDevCaps16 (struct w16call *c)
{
	uint32_t p = far_lin (c->a[0]);
	wr16 (p, 1);
	wr16 (p + 2, 65535);
	c->ret = 0;
}

void api_timeBeginPeriod16 (struct w16call *c) { (void) c; c->ret = 0; }
void api_timeEndPeriod16 (struct w16call *c) { (void) c; c->ret = 0; }

void api_timeSetEvent16 (struct w16call *c)
{
	for (int i = 0; i < MAX_MMT; i++)
		if (!mmt[i].used)
		{
			mmt[i].used = 1;
			mmt[i].id = (uint16_t) (i + 1);
			mmt[i].delay = c->a[0] ? c->a[0] : 1;
			mmt[i].proc = c->a[2];
			mmt[i].user = c->a[3];
			mmt[i].periodic = (c->a[4] & 1) != 0;
			mmt[i].next = be_ticks () + mmt[i].delay;
			c->ret = mmt[i].id;
			return;
		}
	c->ret = 0;
}

void api_timeKillEvent16 (struct w16call *c)
{
	int i = (int) c->a[0] - 1;
	if (i >= 0 && i < MAX_MMT && mmt[i].used)
	{
		mmt[i].used = 0;
		c->ret = 0;
	}
	else
		c->ret = 97;	/* TIMERR_NOCANDO */
}

void mm_poll (void)
{
	if (in_poll)
		return;
	in_poll = 1;
	uint32_t now = be_ticks ();
	for (int i = 0; i < MAX_MMT; i++)
	{
		if (!mmt[i].used || (int32_t) (now - mmt[i].next) < 0)
			continue;
		uint32_t proc = mmt[i].proc, user = mmt[i].user;
		uint16_t id = mmt[i].id;
		if (mmt[i].periodic)
		{
			mmt[i].next += mmt[i].delay;
			if ((int32_t) (now - mmt[i].next) > 200)	/* (trop de retard : on ne rattrape pas) */
				mmt[i].next = now + mmt[i].delay;
		}
		else
			mmt[i].used = 0;
		/* (TIMECALLBACK : uID, uMsg, dwUser, dw1, dw2) */
		uint16_t a[8] = { id, 0, (uint16_t) (user >> 16), (uint16_t) user, 0, 0, 0, 0 };
		call16 (proc, a, 8, ne_ds_for (proc));
		now = be_ticks ();
	}
	in_poll = 0;
}

int mm_next_ms (void)
{
	uint32_t now = be_ticks ();
	int best = -1;
	for (int i = 0; i < MAX_MMT; i++)
		if (mmt[i].used)
		{
			int d = (int) (int32_t) (mmt[i].next - now);
			if (d < 0)
				d = 0;
			if (best < 0 || d < best)
				best = d;
		}
	return best;
}

/* ---- son : pas encore de sortie WAVE ---- */
void api_sndPlaySound16 (struct w16call *c) { (void) c; c->ret = 1; }
void api_waveOutGetNumDevs16 (struct w16call *c) { (void) c; c->ret = 0; }
void api_mciSendCommand16 (struct w16call *c) { (void) c; c->ret = 274; }	/* MCIERR_UNSUPPORTED_FUNCTION */
void api_mciSendString16 (struct w16call *c) { (void) c; c->ret = 274; }
