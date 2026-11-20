// ipctest -- essai de cdmhost par son client (cdmclient), comme Jet s'en servira (docs/etude/WIDEVINE.md § 3.3) :
// cdmhost lancé à part, le module Widevine chez lui ; ici : la session, la licence (HTTPS), l'audio déchiffré
// (Decrypt) et la vidéo décodée (DecryptAndDecodeFrame), à travers les deux sockets. Les empreintes des sorties
// sont celles de cdmtest (même flux, même module) : vidéo fabf757bb7928937, audio 85bba6b1d46b3472.
//
//   ipctest CDMHOST MODULE.so VIDEO.mp4 URL_DE_LICENCE AUDIO.mp4 [--storage DOSSIER]
#include <poll.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <string>
#include <vector>

#include "cdmclient.h"
#include "cenc.h"
#include "content_decryption_module.h"
#include "net.h"

#if !defined(__linux__)
#include <armpi/syscall.h>
#include <unistd.h>

#include "korin_sys_nr.h"
#endif

static int s_fails;

// la sortie ; dans Korin, aussi dans le journal du noyau (l'essai sous QEMU, lancé comme un service)
static void say (const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));
static void say (const char *fmt, ...)
{
	char b[1024];
	va_list ap;
	va_start (ap, fmt);
	int n = vsnprintf (b, sizeof b, fmt, ap);
	va_end (ap);
	if (n > (int) sizeof b - 1)
		n = sizeof b - 1;
	fwrite (b, 1, (size_t) n, stdout);
	fflush (stdout);
#if !defined(__linux__)
	armpi_syscall (SYS_PRINT, (long) b, n, 0, 0, 0, 0);
#endif
}

static void check (bool ok, const char *what)
{
	say ("  [%s] %s\n", ok ? "ok" : "ÉCHEC", what);
	fflush (stdout);
	if (!ok)
		s_fails++;
}

static void fnv (uint64_t &h, const uint8_t *p, size_t n)
{
	for (size_t i = 0; i < n; i++)
		h = (h ^ p[i]) * 0x100000001b3ULL;
}

static bool read_file (const char *path, std::vector<uint8_t> &out)
{
	FILE *f = fopen (path, "rb");
	if (!f)
		return false;
	uint8_t b[65536];
	size_t k;
	while ((k = fread (b, 1, sizeof b, f)) > 0)
		out.insert (out.end (), b, b + k);
	fclose (f);
	return true;
}

// le début en clair puis le premier fragment chiffré (comme cdmtest)
static bool load (const char *path, CencTrack &t, std::vector<uint8_t> &data, std::vector<CencSample> &samples)
{
	if (!read_file (path, data) || !cenc_parse_init (data.data (), data.size (), t))
		return false;
	std::vector<CencSample> frag;
	size_t from = 0;
	while ((from = cenc_parse_fragment (data.data (), data.size (), from, t, frag)) != 0)
	{
		bool enc = !frag.empty () && frag[0].encrypted;
		samples.insert (samples.end (), frag.begin (), frag.end ());
		frag.clear ();
		if (enc)
			break;
	}
	return !samples.empty () && samples.back ().encrypted;
}

// le chiffrement d'un échantillon pour le protocole ; H.264 en Annex B (comme Chromium)
static void crypt_of (const CencTrack &t, const std::vector<uint8_t> &data, const CencSample &s, CdmCrypt &c, std::vector<uint8_t> &bytes)
{
	std::vector<uint32_t> clear = s.clear, cipher = s.cipher;
	if (t.codec == 0x61766331)
		cenc_to_annexb (t, data.data () + s.offset, s.size, s, bytes, clear, cipher);
	else
		bytes.assign (data.begin () + (long) s.offset, data.begin () + (long) (s.offset + s.size));
	c.scheme = s.encrypted ? (uint32_t) cdm::EncryptionScheme::kCenc : (uint32_t) cdm::EncryptionScheme::kUnencrypted;
	c.kid.assign (t.kid, t.kid + 16);
	c.iv = s.iv;
	c.clear = clear;
	c.cipher = cipher;
	c.timestamp = t.timescale ? s.pts * 1000000 / t.timescale : s.pts;
}

// les événements jusqu'à ce que cond soit vrai (ms au plus)
template <class F>
static void events (CdmHostClient &h, int ms, F handle)
{
	for (int waited = 0; waited < ms; waited += 50)
	{
		struct pollfd pf = { h.control_fd (), POLLIN, 0 };
		if (poll (&pf, 1, 50) > 0)
		{
			CdmIn ev;
			if (!h.read_event (ev))
				return;
			if (handle (ev))
				return;
		}
	}
}

static int run (int argc, char **argv);

int main (int argc, char **argv)
{
#if !defined(__linux__)
	if (argc == 1)
	{
		// sans argument (lancé comme un service par init2, essai sous QEMU) : le dossier /widevine ; à la fin,
		// il reste (un service qui s'arrête serait relancé)
		static char *def[] = { argv[0], (char *) "/widevine/cdmhost", (char *) "/widevine/libwidevinecdm.so",
				       (char *) "/widevine/video.mp4", (char *) "https://cwip-shaka-proxy.appspot.com/no_auth",
				       (char *) "/widevine/audio.mp4", (char *) "--storage", (char *) "/widevine/storage", nullptr };
		run (8, def);
		for (;;)
			pause ();
	}
#endif
	return run (argc, argv);
}

static int run (int argc, char **argv)
{
	if (argc < 6)
	{
		say ("ipctest CDMHOST MODULE.so VIDEO.mp4 URL_DE_LICENCE AUDIO.mp4 [--storage DOSSIER]\n");
		return 2;
	}
	std::string storage = argc > 7 && !strcmp (argv[6], "--storage") ? argv[7] : "/tmp/ipctest-storage";
	CencTrack vt, at;
	std::vector<uint8_t> vdata, adata;
	std::vector<CencSample> vs, as;
	check (load (argv[3], vt, vdata, vs) && load (argv[5], at, adata, as), "flux d'essai lus (vidéo et audio)");

	auto host = CdmHostClient::spawn (argv[1], argv[2], storage);
	check (host != nullptr, "cdmhost lancé");
	if (!host)
		return 1;
	host->init ("com.widevine.alpha", false, true);
	bool inited = false;
	events (*host, 10000, [&] (CdmIn &ev) {
		if (ev.type == CDM_EV_INITIALIZED)
			inited = ev.u32 () != 0;
		return ev.type == CDM_EV_INITIALIZED;
	});
	check (inited, "INIT : le module est prêt chez cdmhost");

	std::string session;
	std::vector<uint8_t> request;
	host->create_session (1, cdm::kTemporary, cdm::kCenc, vt.pssh);
	events (*host, 10000, [&] (CdmIn &ev) {
		if (ev.type == CDM_EV_NEW_SESSION)
		{
			ev.u32 ();
			session = ev.str ();
		}
		else if (ev.type == CDM_EV_MESSAGE)
		{
			ev.str ();
			ev.u32 ();
			request = ev.blk ();
		}
		return !request.empty ();
	});
	say ("ipctest : session %s, requête de licence de %zu octets\n", session.c_str (), request.size ());
	check (!session.empty () && !request.empty (), "CREATE_SESSION : session et requête de licence");

	std::vector<uint8_t> resp;
	int status = 0;
	check (https_post (argv[4], request, resp, &status) && status == 200, "licence du serveur");
	host->update_session (2, session, resp);
	int usable = 0;
	events (*host, 10000, [&] (CdmIn &ev) {
		if (ev.type == CDM_EV_KEYS_CHANGE)
		{
			ev.str ();
			ev.u32 ();
			uint32_t n = ev.u32 ();
			for (uint32_t i = 0; i < n; i++)
			{
				ev.blk ();
				if (ev.u32 () == cdm::kUsable)
					usable++;
			}
		}
		return usable > 0;
	});
	say ("ipctest : %d clés utilisables\n", usable);
	check (usable > 0, "UPDATE_SESSION : clés utilisables");

	// l'audio : Decrypt par le socket du média
	uint64_t adigest = 0xcbf29ce484222325ULL;
	int aok = 0, aenc = 0;
	for (const CencSample &s : as)
	{
		CdmCrypt c;
		std::vector<uint8_t> bytes, out;
		crypt_of (at, adata, s, c, bytes);
		int st = host->decrypt (c, bytes.data (), bytes.size (), out);
		if (st == cdm::kSuccess && s.encrypted)
		{
			aok++;
			fnv (adigest, out.data (), out.size ());
		}
		aenc += s.encrypted;
	}
	say ("ipctest : audio : %d échantillons chiffrés sur %d rendus en clair, empreinte %016llx\n", aok, aenc,
		(unsigned long long) adigest);
	check (aenc > 0 && aok == aenc, "DECRYPT : l'audio chiffré rendu en clair");

	// la vidéo : décodée par le module chez cdmhost
	int prof = vt.avcc.size () > 1 ? vt.avcc[1] : 100;
	int vst = host->init_video (cdm::kCodecH264, prof == 66 ? cdm::kH264ProfileBaseline : prof == 77 ? cdm::kH264ProfileMain : cdm::kH264ProfileHigh,
				    (uint32_t) vt.width, (uint32_t) vt.height, vt.avcc);
	uint64_t vdigest = 0xcbf29ce484222325ULL;
	int images = 0, errors = 0;
	auto take = [&] (const CdmFrame &f) {
		if (!images)
			say ("ipctest : première image %ux%u, format %u, pas %u/%u/%u, décalages %u/%u/%u, %zu octets\n", f.width, f.height,
				f.format, f.stride[0], f.stride[1], f.stride[2], f.offset[0], f.offset[1], f.offset[2], f.pixels.size ());
		images++;
		uint64_t one = 0xcbf29ce484222325ULL;
		for (int p = 0; p < 3; p++)
		{
			int pw = p ? ((int) f.width + 1) / 2 : (int) f.width, ph = p ? ((int) f.height + 1) / 2 : (int) f.height;
			for (int y = 0; y < ph; y++)
				if (f.offset[p] + (size_t) y * f.stride[p] + (size_t) pw <= f.pixels.size ())
				{
					fnv (vdigest, f.pixels.data () + f.offset[p] + (size_t) y * f.stride[p], (size_t) pw);
					fnv (one, f.pixels.data () + f.offset[p] + (size_t) y * f.stride[p], (size_t) pw);
				}
		}
		if (getenv ("IPCTEST_FRAMES"))
			say ("image %d t=%lld %016llx\n", images, (long long) f.timestamp, (unsigned long long) one);
	};
	for (size_t i = 0; vst == cdm::kSuccess && i < vs.size (); i++)
	{
		CdmCrypt c;
		std::vector<uint8_t> bytes;
		crypt_of (vt, vdata, vs[i], c, bytes);
		CdmFrame f;
		int st = host->decode_video (c, bytes.data (), bytes.size (), f);
		if (st == cdm::kSuccess)
			take (f);
		else if (st != cdm::kNeedMoreData)
			errors++;
	}
	for (int k = 0; vst == cdm::kSuccess && k < 64; k++)
	{
		// (les images en retard, rendues par des entrées vides : dans l'empreinte, comme cdmtest)
		CdmCrypt c;
		CdmFrame f;
		if (host->decode_video (c, nullptr, 0, f) != cdm::kSuccess)
			break;
		take (f);
	}
	say ("ipctest : vidéo : %d images sur %zu échantillons, %d erreurs, empreinte %016llx\n", images, vs.size (), errors,
		(unsigned long long) vdigest);
	check (vst == cdm::kSuccess && images == (int) vs.size () && !errors, "DECODE_VIDEO : chaque échantillon décodé par le module");

	host->close_session (3, session);
	host.reset ();	// (la fin du contrôle arrête cdmhost)
	say ("ipctest : %s (%d échec(s))\n", s_fails ? "ÉCHEC" : "RÉUSSI", s_fails);
	return s_fails ? 1 : 0;
}
