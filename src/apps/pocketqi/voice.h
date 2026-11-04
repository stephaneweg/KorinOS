//
// voice.h -- PocketQI : la voix de Pocket, par la synthèse vocale de Mistral (Voxtral TTS), reprise de voice.h du
// PocketQI d'Onyx (user/Apps/pocketqi/voice.h). POST https://api.mistral.ai/v1/audio/speech {"model", "voice_id",
// "response_format": "pcm", "stream": true, "input"} : la réponse est un flux SSE d'événements dont "audio_data" est du
// PCM float32 little-endian, 24 kHz mono, en base64. Il est lu à mesure (http_request et son progress : les octets bruts,
// en-têtes et morceaux compris, décodés ici), porté à 44,1 kHz stéréo et joué tout de suite par AudioKit (ak_open,
// le service sound) : Pocket parle avant que toute la phrase soit faite.
//
// Les voix : celle du français et celle de l'anglais (config.ini : voice_fr, voice_en ; défaut : Léa, Paul), dans la
// langue du système. Une « famille » (fr_marie, en_paul, gb_oliver, gb_jane) suit l'humeur du visage. La clé : la clé
// Mistral des réglages. Seul ce qui est dit part chez Mistral.
//
// Speaker : un fil à lui, qui dit les phrases dans l'ordre ; stop () coupe la phrase en cours et oublie les autres.
//
// MIT License -- Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. Permission is hereby
// granted, free of charge, to any person obtaining a copy of this software and associated
// documentation files (the "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions: The above copyright notice and this permission notice shall
// be included in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS
// IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
//
#ifndef POCKETQI_VOICE_H
#define POCKETQI_VOICE_H

#include <pthread.h>
#include <audiokit_abi.h>
#include <deque>
#include <vector>
#include "config.h"
#include "http.h"
#include "json.h"
#include "core/pocket_humeur.h"

namespace pq {

#define TTS_URL		"https://api.mistral.ai/v1/audio/speech"
#define TTS_MODEL	"voxtral-mini-tts-3-0"
#define TTS_RATE	24000
#define OUT_RATE	44100

// ---- les voix (GET /v1/audio/voices, 26/10/2026, comme Onyx) ------------------------------------------------------
struct VoiceChoice { const char *id, *name; bool moods; };
// l'id d'une famille est son préfixe : l'émotion de l'humeur s'y ajoute (fr_marie_happy)
static const VoiceChoice VOICES_FR[] = {
	{ "voice_589", "L\xC3\xA9" "a", false }, { "fr_marie", "Marie", true }, { "voice_3137", "Manon", false },
	{ "voice_3141", "Camille", false }, { "voice_592", "Marie (2)", false } };
static const VoiceChoice VOICES_EN[] = {
	{ "en_paul", "Paul", true }, { "gb_oliver", "Oliver (GB)", true }, { "gb_jane", "Jane (GB)", true },
	{ "voice_3150", "James", false }, { "voice_478", "Sheila", false }, { "voice_447", "Margaret", false },
	{ "voice_485", "Rob", false }, { "voice_590", "Harriet (GB)", false }, { "voice_591", "Fred (GB)", false },
	{ "voice_1787", "Mary (GB)", false }, { "voice_1788", "David (GB)", false }, { "voice_1786", "Sky (GB)", false } };
#define VOICE_FR_DEFAULT "voice_589"
#define VOICE_EN_DEFAULT "en_paul"

// les émotions d'une famille (celles de Mistral), la première que l'humeur demande et qu'elle a
static std::string voice_emotion (const std::string &family, int h)
{
	const char *have = family == "fr_marie" ? " neutral happy sad excited curious angry "
			 : family == "en_paul" ? " neutral happy sad frustrated excited confident cheerful angry "
			 : family == "gb_oliver" ? " neutral sad excited curious confident cheerful angry "
			 : " neutral sad confused curious confident frustrated ";		// (gb_jane)
	const char *want[3] = { 0, 0, 0 };
	switch (h)
	{
	case HUMEUR_JOYEUX: case HUMEUR_RIEUR: case HUMEUR_AMOUREUX: case HUMEUR_CLIN: want[0] = "happy"; want[1] = "cheerful"; break;
	case HUMEUR_FIER: want[0] = "confident"; want[1] = "happy"; break;
	case HUMEUR_TRISTE: case HUMEUR_FATIGUE: case HUMEUR_ENNUI: case HUMEUR_TIMIDE: want[0] = "sad"; break;
	case HUMEUR_SURPRIS: case HUMEUR_EXCITE: want[0] = "excited"; want[1] = "cheerful"; break;
	case HUMEUR_CURIEUX: want[0] = "curious"; break;
	case HUMEUR_PERPLEXE: case HUMEUR_MEFIANT: want[0] = "confused"; want[1] = "curious"; break;
	case HUMEUR_COLERE: case HUMEUR_DEGOUT: want[0] = "angry"; want[1] = "frustrated"; break;
	default: break;
	}
	for (int i = 0; i < 3 && want[i]; i++) if (strstr (have, (std::string (" ") + want[i] + " ").c_str ())) return want[i];
	return "neutral";
}
// la voix d'une phrase : celle de la langue (les réglages), l'émotion de l'humeur pour une famille
static std::string voice_id (bool en, const std::string &fr, const std::string &eng, int mood)
{
	std::string v = en ? (eng.empty () ? VOICE_EN_DEFAULT : eng) : (fr.empty () ? VOICE_FR_DEFAULT : fr);
	for (auto &c : VOICES_FR) if (v == c.id && c.moods) return v + "_" + voice_emotion (v, mood);
	for (auto &c : VOICES_EN) if (v == c.id && c.moods) return v + "_" + voice_emotion (v, mood);
	return v;
}

// ---- le texte à dire -----------------------------------------------------------------------------------------------
// ce qui se lit mal : les cases de la liste de courses, les puces, °C, km/h, les heures « 14:30 » (« 14 h 30 »),
// les lignes (des pauses)
static std::string speech_text (const std::string &t, bool en)
{
	std::string s;
	size_t p = 0;
	while (p <= t.size ())
	{
		size_t e = t.find ('\n', p);
		if (e == std::string::npos) e = t.size ();
		std::string l = trim (t.substr (p, e - p));
		p = e + 1;
		if (l.compare (0, 3, "[ ]") == 0 || l.compare (0, 3, "[x]") == 0) l = trim (l.substr (3));
		if (!l.empty () && (l[0] == '-' || l[0] == '*')) l = trim (l.substr (1));
		if (l.empty ()) continue;
		char last = s.empty () ? '.' : s.back ();
		if (!s.empty ()) s += last == '.' || last == ':' || last == '?' || last == '!' || last == ',' ? " " : ". ";
		s += l;
	}
	auto repl = [&] (const char *a, const char *b) { size_t q = 0; while ((q = s.find (a, q)) != std::string::npos) { s.replace (q, strlen (a), b); q += strlen (b); } };
	repl ("\xC2\xB0" "C", en ? " degrees" : " degrés");
	repl ("km/h", en ? "kilometres per hour" : "kilomètres heure");
	repl (" %", en ? " percent" : " pour cent");
	for (size_t q = 3; q + 1 < s.size (); q++)		// « 10:00-11:00 » -> « 10:00 à 11:00 »
		if (s[q] == '-' && isdigit ((unsigned char) s[q - 1]) && isdigit ((unsigned char) s[q + 1]) && s[q - 3] == ':')
			s.replace (q, 1, en ? " to " : " à ");
	if (!en)
		for (size_t q = 1; q + 3 < s.size (); q++)	// « 14:30 » -> « 14 h 30 », « 9:00 » -> « 9 h »
			if (s[q] == ':' && isdigit ((unsigned char) s[q - 1]) && isdigit ((unsigned char) s[q + 1]) && isdigit ((unsigned char) s[q + 2]))
			{
				if (s[q + 1] == '0' && s[q + 2] == '0') s.replace (q, 3, " h");
				else s.replace (q, 1, " h ");
			}
	return s;
}

// ---- le flux -------------------------------------------------------------------------------------------------------
struct TtsStream
{
	size_t done = 0;		// les octets de la réponse déjà vus
	long body = -1;			// le début du corps (-1 : en-têtes pas finis)
	int status = 0;
	bool chunked = false;
	long chunkLeft = -1;		// ce qui reste du morceau (-1 : on lit sa taille)
	std::string size, line;
	int skip = 0;			// le CRLF après un morceau
	unsigned acc = 0; int bits = 0; unsigned char f4[4]; int nf4 = 0;
	long out = -1;			// le flux d'AudioKit (ouvert au premier son)
	double pos = 0;			// la position dans le 24 kHz, pour le passage à 44,1 kHz
	short prev = 0;			// le dernier échantillon de l'appel d'avant
	unsigned samples = 0;
	volatile int *cancel = 0;
	volatile bool *talking = 0;
	std::string err;
	std::vector<short> mono, st;
};

// des échantillons 24 kHz -> 44,1 kHz stéréo (interpolation linéaire : on monte en fréquence), joués
static void tts_play (TtsStream &t)
{
	if (t.mono.empty ()) return;
	if (t.out < 0)
	{
		t.out = ak_open (OUT_RATE, 2, 16, 0);
		if (t.out < 0) { t.err = "la sortie du son ne s'ouvre pas"; t.mono.clear (); return; }
		if (t.talking) *t.talking = true;
	}
	const double step = (double) TTS_RATE / OUT_RATE;
	t.st.clear ();
	// (l'échantillon -1 est le dernier de l'appel d'avant : pos va de -1 à mono.size () - 1)
	while (t.pos < (double) t.mono.size () - 1)
	{
		int i = (int) floor (t.pos);
		double f = t.pos - i;
		double a = i < 0 ? t.prev : t.mono[(size_t) i], b = t.mono[(size_t) (i + 1)];
		short v = (short) lround (a + (b - a) * f);
		t.st.push_back (v);
		t.st.push_back (v);
		t.pos += step;
	}
	t.pos -= (double) t.mono.size ();
	t.prev = t.mono.back ();
	t.mono.clear ();
	if (!t.st.empty ()) ((long (*) (long, long, long)) ak_write_all) (t.out, (long) t.st.data (), (long) (t.st.size () * 2));
}

static int b64v (int c)
{
	return c >= 'A' && c <= 'Z' ? c - 'A' : c >= 'a' && c <= 'z' ? c - 'a' + 26 : c >= '0' && c <= '9' ? c - '0' + 52
	     : c == '+' ? 62 : c == '/' ? 63 : -1;
}

// une ligne SSE entière : son "audio_data" décodé en échantillons
static void tts_line (TtsStream &t)
{
	const char *l = t.line.c_str ();
	const char *a = strstr (l, "\"audio_data\"");
	if (!a)
	{
		if (strstr (l, "\"error\"") || strstr (l, "\"message\"")) t.err = t.line.substr (0, 200);
		return;
	}
	if (!(a = strchr (a + 12, ':')) || !(a = strchr (a, '"'))) return;
	t.acc = 0; t.bits = 0; t.nf4 = 0;
	for (a++; *a && *a != '"'; a++)
	{
		int v = b64v ((unsigned char) *a);
		if (v < 0) continue;
		t.acc = (t.acc << 6) | (unsigned) v; t.bits += 6;
		if (t.bits < 8) continue;
		t.bits -= 8;
		t.f4[t.nf4++] = (unsigned char) (t.acc >> t.bits);
		if (t.nf4 < 4) continue;
		t.nf4 = 0;
		float x;
		memcpy (&x, t.f4, 4);
		int s = (int) (x * 32767.0f);
		t.mono.push_back ((short) (s > 32767 ? 32767 : s < -32767 ? -32767 : s));
	}
	t.samples += (unsigned) t.mono.size ();
	tts_play (t);
}

static void tts_byte (TtsStream &t, char c)
{
	if (c == '\n') { tts_line (t); t.line.clear (); return; }
	if (c != '\r') t.line += c;
}

// la réponse brute arrivée jusqu'ici (http_request) : en-têtes, morceaux, lignes SSE
static bool tts_progress (void *ctx, const std::string &all)
{
	TtsStream &t = *(TtsStream *) ctx;
	for (; t.done < all.size (); t.done++)
	{
		char c = all[t.done];
		if (t.body < 0)
		{
			if (t.done >= 3 && all.compare (t.done - 3, 4, "\r\n\r\n") == 0)
			{
				t.body = (long) t.done + 1;
				std::string head = fold (all.substr (0, (size_t) t.body));
				size_t sp = head.find (' ');
				t.status = sp != std::string::npos ? atoi (head.c_str () + sp + 1) : 0;
				t.chunked = head.find ("transfer-encoding: chunked") != std::string::npos;
			}
			continue;
		}
		if (t.status != 200) continue;			// (les mots de l'erreur : lus en entier)
		if (!t.chunked) { tts_byte (t, c); continue; }
		if (t.skip > 0) { t.skip--; continue; }
		if (t.chunkLeft < 0)
		{
			if (c == '\n')
			{
				t.chunkLeft = strtol (t.size.c_str (), 0, 16);
				t.size.clear ();
				if (t.chunkLeft == 0) return false;		// (le dernier morceau : fini)
			}
			else if (c != '\r') t.size += c;
			continue;
		}
		tts_byte (t, c);
		if (--t.chunkLeft == 0) { t.chunkLeft = -1; t.skip = 2; }
	}
	return !t.err.size () && !(t.cancel && *t.cancel);
}

// dit le texte avec cette voix ; revient quand il est entendu (ou coupé) ; false : err dit pourquoi
static bool voice_say (const std::string &url, const std::string &key, const std::string &voice, const std::string &text,
		       volatile int *cancel, volatile bool *talking, std::string &err)
{
	if (key.empty ()) { err = "pas de clé Mistral"; return false; }
	std::string body = "{\"model\": \"" TTS_MODEL "\", \"voice_id\": " + json_str (voice) +
			   ", \"response_format\": \"pcm\", \"stream\": true, \"input\": " + json_str (text) + "}";
	std::string head = "Authorization: Bearer " + key + "\r\nAccept: text/event-stream\r\n", resp;
	TtsStream t;
	t.cancel = cancel;
	t.talking = talking;
	int code = http_request (false, url, head, body, resp, err, cancel, 30000, tts_progress, &t);
	std::fill (head.begin (), head.end (), 0);
	if (!t.line.empty ()) tts_line (t);
	if (t.out >= 0)
	{
		// (la fin de la lecture : attendue par petits pas, pour que stop () coupe tout de suite)
		for (int i = 0; i < 3000 && ak_pending (t.out) > 0 && !(cancel && *cancel); i++) usleep (10000);
		ak_close (t.out);
	}
	if (t.samples > 0 && t.err.empty ()) return true;
	if (cancel && *cancel) { err = "coupé"; return false; }
	if (!t.err.empty ()) err = t.err;
	else if (code == 0) { if (err.empty ()) err = "pas de réponse"; }
	else if (t.status != 200 && t.body >= 0) err = "HTTP " + std::to_string (t.status) + " : " + resp.substr (0, 200);
	else err = "aucun son reçu";
	return false;
}

// ---- le fil de la voix ---------------------------------------------------------------------------------------------
class Speaker
{
public:
	// une phrase à dire, après celles d'avant (la clé et la voix : lues dans config.ini à ce moment)
	void say (const std::string &text, bool en, int mood)
	{
		std::string s = speech_text (text, en);
		if (s.empty ()) return;
		pthread_mutex_lock (&mu);
		queue.push_back (Item { s, en, mood });
		if (!running)
		{
			running = true;
			cancel = 0;
			pthread_t t;
			if (pthread_create (&t, 0, worker, this) == 0) pthread_detach (t);
			else running = false;
		}
		pthread_mutex_unlock (&mu);
	}
	// tout se tait : la phrase en cours est coupée, les autres oubliées
	void stop ()
	{
		pthread_mutex_lock (&mu);
		queue.clear ();
		cancel = 1;
		pthread_mutex_unlock (&mu);
	}
	bool talking () const { return talk; }
	// ce qui n'a pas marché depuis le dernier appel ("" rien)
	std::string error ()
	{
		pthread_mutex_lock (&mu);
		std::string e;
		e.swap (lastErr);
		pthread_mutex_unlock (&mu);
		return e;
	}

private:
	struct Item { std::string text; bool en; int mood; };
	pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;
	std::deque<Item> queue;
	bool running = false;
	volatile int cancel = 0;
	volatile bool talk = false;
	std::string lastErr;

	static void *worker (void *self)
	{
		Speaker *sp = (Speaker *) self;
		for (;;)
		{
			pthread_mutex_lock (&sp->mu);
			if (sp->queue.empty ()) { sp->running = false; sp->talk = false; pthread_mutex_unlock (&sp->mu); return 0; }
			Item it = sp->queue.front ();
			sp->queue.pop_front ();
			sp->cancel = 0;
			pthread_mutex_unlock (&sp->mu);
			Config c;
			c.load ();
			std::string url = c.ttsUrl.empty () ? std::string (TTS_URL) : c.ttsUrl, err;
			bool ok = voice_say (url, c.mistralKey, voice_id (it.en, c.voiceFr, c.voiceEn, it.mood), it.text, &sp->cancel, &sp->talk, err);
			std::fill (c.mistralKey.begin (), c.mistralKey.end (), 0);
			sp->talk = false;
			if (!ok && !sp->cancel)
			{
				pthread_mutex_lock (&sp->mu);
				sp->lastErr = err;
				sp->queue.clear ();			// (la suite ne marcherait pas mieux)
				pthread_mutex_unlock (&sp->mu);
			}
		}
	}
};

} // namespace pq

#endif
