// cdmtest -- essai de l'hôte du module Widevine de bout en bout, hors de Jet (docs/etude/WIDEVINE.md,
// étape 3) : chargement du module, Initialize, session avec la pssh d'un flux d'essai, échange de licence
// avec un serveur de licences (HTTPS, OpenSSL), UpdateSession, puis Decrypt des échantillons du premier
// fragment ; sous Linux, les échantillons déchiffrés sont décodés par libavcodec (preuve du déchiffrement).
//
//   cdmtest MODULE.so VIDEO.mp4 URL_DE_LICENCE [AUDIO.mp4] [--storage DOSSIER] [-v]
//
// VIDEO.mp4, AUDIO.mp4 : le début d'un fichier MP4 fragmenté (init, sidx, le début en clair, au moins un
// fragment chiffré). La vidéo : Decrypt seul doit être refusé par le module (L3 : pas de vidéo compressée
// en clair), DecryptAndDecodeFrame doit rendre chaque image ; l'audio : Decrypt doit rendre l'AAC en clair. Le même source pour Linux (tools/linux/cdmtest.sh : dlopen, et --trace : les syscall
// directs du module par seccomp) et Korin (tools/linux/build.sh : liblinux, capture de syscall du noyau).
#include <errno.h>
#include <netdb.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <openssl/err.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>

#include <string>
#include <vector>

#include "cenc.h"
#include "host.h"

#if defined(__linux__)
#include <dlfcn.h>
#else
#include "linux.h"
#endif

#if defined(CDMTEST_AVCODEC)
extern "C" {
#include <libavcodec/avcodec.h>
}
#endif

static int s_fails;

static void say (const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));
static void say (const char *fmt, ...)
{
	va_list ap;
	va_start (ap, fmt);
	vfprintf (stdout, fmt, ap);
	va_end (ap);
	fflush (stdout);
}

static void check (bool ok, const char *what)
{
	say ("  [%s] %s\n", ok ? "ok" : "ÉCHEC", what);
	if (!ok)
		s_fails++;
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

static std::string hex (const uint8_t *p, size_t n)
{
	std::string s;
	char b[3];
	for (size_t i = 0; i < n; i++)
	{
		snprintf (b, sizeof b, "%02x", p[i]);
		s += b;
	}
	return s;
}

// ---- HTTPS POST (OpenSSL, bloquant) : le corps de la réponse, statut HTTP dans *status ----
static bool https_post (const std::string &url, const std::vector<uint8_t> &body, std::vector<uint8_t> &out, int *status)
{
	if (url.compare (0, 8, "https://"))
	{
		say ("cdmtest : https seulement : %s\n", url.c_str ());
		return false;
	}
	std::string rest = url.substr (8), host = rest, path = "/";
	size_t sl = rest.find ('/');
	if (sl != std::string::npos)
	{
		host = rest.substr (0, sl);
		path = rest.substr (sl);
	}
	struct addrinfo hints = {}, *res = nullptr;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	if (getaddrinfo (host.c_str (), "443", &hints, &res) || !res)
	{
		say ("cdmtest : %s introuvable (réseau ?)\n", host.c_str ());
		return false;
	}
	int fd = socket (res->ai_family, res->ai_socktype, res->ai_protocol);
	if (fd < 0 || connect (fd, res->ai_addr, res->ai_addrlen))
	{
		say ("cdmtest : %s ne répond pas\n", host.c_str ());
		freeaddrinfo (res);
		if (fd >= 0)
			close (fd);
		return false;
	}
	freeaddrinfo (res);
	SSL_CTX *ctx = SSL_CTX_new (TLS_client_method ());
	SSL_CTX_set_min_proto_version (ctx, TLS1_2_VERSION);
	SSL_CTX_set_verify (ctx, SSL_VERIFY_PEER, nullptr);
	SSL_CTX_set_default_verify_paths (ctx);
	SSL *ssl = SSL_new (ctx);
	SSL_set_fd (ssl, fd);
	SSL_set_tlsext_host_name (ssl, host.c_str ());
	SSL_set1_host (ssl, host.c_str ());
	bool ok = false;
	if (SSL_connect (ssl) != 1)
		say ("cdmtest : TLS avec %s impossible (vérification : %s)\n", host.c_str (),
		     X509_verify_cert_error_string (SSL_get_verify_result (ssl)));
	else
	{
		char head[512];
		int n = snprintf (head, sizeof head,
				  "POST %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: KorinOS-cdmtest/1\r\n"
				  "Content-Type: application/octet-stream\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n",
				  path.c_str (), host.c_str (), body.size ());
		std::vector<uint8_t> req (head, head + n);
		req.insert (req.end (), body.begin (), body.end ());
		if (SSL_write (ssl, req.data (), (int) req.size ()) == (int) req.size ())
		{
			std::vector<uint8_t> raw;
			char b[8192];
			int k;
			while ((k = SSL_read (ssl, b, sizeof b)) > 0)
				raw.insert (raw.end (), b, b + k);
			std::string s (raw.begin (), raw.end ());
			size_t he = s.find ("\r\n\r\n");
			if (he != std::string::npos && s.compare (0, 5, "HTTP/") == 0)
			{
				*status = atoi (s.c_str () + s.find (' ') + 1);
				out.assign (raw.begin () + (long) he + 4, raw.end ());
				ok = true;
			}
		}
	}
	SSL_free (ssl);
	SSL_CTX_free (ctx);
	close (fd);
	return ok;
}

// ---- le client de l'hôte : ce que le module annonce ----
struct TestClient : CdmClient
{
	bool init_done = false, init_ok = false;
	std::string session;
	std::vector<std::vector<uint8_t>> messages;
	std::vector<uint32_t> message_types;
	int keys_events = 0, usable = 0;
	int resolved_n = 0, rejected_n = 0;

	void initialized (bool ok) override { init_done = true; init_ok = ok; say ("hôte : OnInitialized %d\n", ok); }
	void new_session (uint32_t p, const std::string &s) override { session = s; say ("hôte : session %s (promesse %u)\n", s.c_str (), p); }
	void resolved (uint32_t p) override { resolved_n++; say ("hôte : promesse %u tenue\n", p); }
	void rejected (uint32_t p, uint32_t e, uint32_t code, const std::string &m) override
	{
		rejected_n++;
		say ("hôte : promesse %u rejetée (exception %u, code %u) %s\n", p, e, code, m.c_str ());
	}
	void key_status (uint32_t p, uint32_t s) override { say ("hôte : statut de clé %u (promesse %u)\n", s, p); }
	void message (const std::string &s, uint32_t type, const uint8_t *d, size_t n) override
	{
		messages.emplace_back (d, d + n);
		message_types.push_back (type);
		say ("hôte : message de %s, type %u, %zu octets\n", s.c_str (), type, n);
	}
	void keys_change (const std::string &, bool has_new, const std::vector<std::vector<uint8_t>> &kids,
			  const std::vector<uint32_t> &status) override
	{
		keys_events++;
		for (size_t i = 0; i < kids.size (); i++)
		{
			say ("hôte : clé %s statut %u%s\n", hex (kids[i].data (), kids[i].size ()).c_str (), status[i],
			     status[i] == cdm::kUsable ? " (utilisable)" : "");
			if (status[i] == cdm::kUsable)
				usable++;
		}
		(void) has_new;
	}
	void expiration (const std::string &, double t) override { say ("hôte : expiration %.0f\n", t); }
	void closed (const std::string &s) override { say ("hôte : session fermée %s\n", s.c_str ()); }
};

// ---- le module ----
static void *s_module;

#if defined(__linux__)
static bool load_module (const char *path)
{
	s_module = dlopen (path, RTLD_NOW);
	if (!s_module)
		say ("cdmtest : dlopen : %s\n", dlerror ());
	return s_module != nullptr;
}
static void *module_sym (const char *n) { return dlsym (s_module, n); }
#else
static bool load_module (const char *path)
{
	std::vector<uint8_t> img;
	if (!read_file (path, img))
	{
		say ("cdmtest : %s illisible\n", path);
		return false;
	}
	char err[200];
	s_module = linux_load (img.data (), img.size (), path, 0, err, sizeof err);
	if (!s_module)
		say ("cdmtest : linux_load : %s\n", err);
	return s_module != nullptr;
}
static void *module_sym (const char *n) { return linux_sym ((struct linux_module *) s_module, n); }
#endif

// un échantillon prêt pour le module : H.264 en Annex B (comme Chromium), sous-échantillons, IV de 16 octets
struct Prepared
{
	std::vector<uint8_t> data, iv;
	std::vector<cdm::SubsampleEntry> subs;
	cdm::InputBuffer_2 in = {};
	bool ok = true;
};

static void prepare (const CencTrack &t, const std::vector<uint8_t> &media, const CencSample &s, Prepared &p)
{
	std::vector<uint32_t> clear = s.clear, cipher = s.cipher;
	if (t.codec == 0x61766331)	// 'avc1'
		p.ok = cenc_to_annexb (t, media.data () + s.offset, s.size, s, p.data, clear, cipher);
	else
		p.data.assign (media.begin () + (long) s.offset, media.begin () + (long) (s.offset + s.size));
	for (size_t j = 0; j < clear.size (); j++)
		p.subs.push_back ({ clear[j], cipher[j] });
	p.iv = s.iv;
	p.iv.resize (16, 0);	// (IV de 8 octets : complété de zéros, comme Chromium)
	p.in.data = p.data.data ();
	p.in.data_size = (uint32_t) p.data.size ();
	p.in.encryption_scheme = !s.encrypted ? cdm::EncryptionScheme::kUnencrypted
				 : t.scheme == 0x63626373 ? cdm::EncryptionScheme::kCbcs : cdm::EncryptionScheme::kCenc;
	p.in.key_id = t.kid;
	p.in.key_id_size = 16;
	p.in.iv = p.iv.data ();
	p.in.iv_size = 16;
	p.in.subsamples = p.subs.empty () ? nullptr : p.subs.data ();
	p.in.num_subsamples = (uint32_t) p.subs.size ();
	p.in.pattern = { t.crypt_blocks, t.skip_blocks };
	p.in.timestamp = t.timescale ? s.pts * 1000000 / t.timescale : s.pts;	// (en microsecondes)
}

static CdmHost *s_host;

static void *get_host (int version, void *)
{
	return version == cdm::Host_10::kVersion ? static_cast<cdm::Host_10 *> (s_host) : nullptr;
}

// un flux de l'essai : son segment d'initialisation et ses échantillons, du début en clair au premier
// fragment chiffré compris
struct Stream
{
	const char *what;
	CencTrack t;
	std::vector<uint8_t> data;
	std::vector<CencSample> samples;
	int clear_lead = 0;
};

static bool load_stream (const char *path, const char *what, Stream &s)
{
	s.what = what;
	if (!read_file (path, s.data))
	{
		say ("cdmtest : %s illisible\n", path);
		return false;
	}
	char fc[5], fc2[5];
	bool ok = cenc_parse_init (s.data.data (), s.data.size (), s.t);
	say ("cdmtest : %s : schéma %s, codec %s, KID %s, IV de %d octets, pssh de %zu octets\n", what,
	     cenc_fourcc (s.t.scheme, fc), cenc_fourcc (s.t.codec, fc2), hex (s.t.kid, 16).c_str (), s.t.iv_size, s.t.pssh.size ());
	std::vector<CencSample> frag;
	size_t from = 0;
	while (ok && (from = cenc_parse_fragment (s.data.data (), s.data.size (), from, s.t, frag)) != 0)
	{
		bool enc = !frag.empty () && frag[0].encrypted;
		if (!enc)
			s.clear_lead += (int) frag.size ();
		s.samples.insert (s.samples.end (), frag.begin (), frag.end ());
		frag.clear ();
		if (enc)
			break;
	}
	say ("cdmtest : %s : %zu échantillons, dont %d en clair au début\n", what, s.samples.size (), s.clear_lead);
	return ok && !s.samples.empty () && s.samples.back ().encrypted;
}

// Decrypt sur tous les échantillons ; décodés ensuite par libavcodec (Linux) quand c'est demandé
static void decrypt_pass (CdmHost &host, Stream &st, bool expect_refusal, bool verbose)
{
	int ok_n = 0, nokey = 0, errors = 0, enc_ok = 0, enc_n = 0;
#if defined(CDMTEST_AVCODEC)
	bool aac = st.t.codec == 0x6d703461;	// 'mp4a'
	const AVCodec *codec = avcodec_find_decoder (aac ? AV_CODEC_ID_AAC : AV_CODEC_ID_H264);
	AVCodecContext *dec = avcodec_alloc_context3 (codec);
	if (aac && !st.t.asc.empty ())
	{
		dec->extradata = (uint8_t *) av_mallocz (st.t.asc.size () + AV_INPUT_BUFFER_PADDING_SIZE);
		memcpy (dec->extradata, st.t.asc.data (), st.t.asc.size ());
		dec->extradata_size = (int) st.t.asc.size ();
	}
	dec->err_recognition = AV_EF_EXPLODE | AV_EF_CRCCHECK | AV_EF_BITSTREAM;	// (H.264 : Annex B, SPS/PPS dans le flux)
	bool dec_ok = avcodec_open2 (dec, codec, nullptr) == 0;
	AVPacket *pkt = av_packet_alloc ();
	AVFrame *frame = av_frame_alloc ();
	int frames = 0, enc_frames = 0, dec_errors = 0;
#endif
	for (size_t i = 0; i < st.samples.size (); i++)
	{
		const CencSample &s = st.samples[i];
		Prepared pr;
		prepare (st.t, st.data, s, pr);
		if (!pr.ok)
		{
			errors++;
			continue;
		}
		if (s.encrypted)
			enc_n++;
		CdmDecryptedBlock out;
		host.pump ();
		cdm::Status r = host.cdm->Decrypt (pr.in, &out);
		if (verbose && s.encrypted)
			say ("  %s %zu : %u octets, statut %u\n", st.what, i, pr.in.data_size, r);
		if (r == cdm::kSuccess && out.DecryptedBuffer ())
		{
			ok_n++;
			if (s.encrypted)
				enc_ok++;
#if defined(CDMTEST_AVCODEC)
			if (dec_ok)
			{
				av_new_packet (pkt, (int) out.DecryptedBuffer ()->Size ());
				memcpy (pkt->data, out.DecryptedBuffer ()->Data (), out.DecryptedBuffer ()->Size ());
				if (avcodec_send_packet (dec, pkt) < 0)
					dec_errors++;
				av_packet_unref (pkt);
				while (avcodec_receive_frame (dec, frame) == 0)
				{
					frames++;
					if (s.encrypted)
						enc_frames++;
				}
			}
#endif
		}
		else if (r == cdm::kNoKey)
			nokey++;
		else
			errors++;
	}
	say ("cdmtest : %s : Decrypt : %d rendus en clair (%d chiffrés sur %d), %d refusés (kNoKey), %d erreurs\n", st.what,
	     ok_n, enc_ok, enc_n, nokey, errors);
	if (expect_refusal)
		check (enc_ok == 0 && nokey == enc_n && !errors,
		       "vidéo : Decrypt seul refusé par le module (la vidéo passe par DecryptAndDecodeFrame)");
	else
		check (enc_n > 0 && enc_ok == enc_n && !nokey && !errors, "audio : Decrypt rend les échantillons chiffrés en clair");
#if defined(CDMTEST_AVCODEC)
	if (dec_ok && !expect_refusal)
	{
		avcodec_send_packet (dec, nullptr);
		while (avcodec_receive_frame (dec, frame) == 0)
			frames++;
		say ("cdmtest : %s : libavcodec : %d trames décodées (dont %d d'échantillons chiffrés), %d erreurs\n", st.what, frames,
		     enc_frames, dec_errors);
		check (enc_frames > 0 && !dec_errors, "audio : les échantillons déchiffrés se décodent (libavcodec)");
	}
	av_frame_free (&frame);
	av_packet_free (&pkt);
	avcodec_free_context (&dec);
#endif
}

// la vidéo comme Chrome : le module déchiffre et décode lui-même (DecryptAndDecodeFrame), rend des images I420
static void decode_pass (CdmHost &host, Stream &st, bool verbose)
{
	cdm::VideoDecoderConfig_2 vc = {};
	vc.codec = cdm::kCodecH264;
	int prof = st.t.avcc.size () > 1 ? st.t.avcc[1] : 100;
	vc.profile = prof == 66 ? cdm::kH264ProfileBaseline : prof == 77 ? cdm::kH264ProfileMain
		     : prof == 88 ? cdm::kH264ProfileExtended : cdm::kH264ProfileHigh;
	vc.format = cdm::kI420;
	vc.coded_size = { st.t.width, st.t.height };
	vc.extra_data = st.t.avcc.data ();
	vc.extra_data_size = (uint32_t) st.t.avcc.size ();
	vc.encryption_scheme = cdm::EncryptionScheme::kCenc;
	cdm::Status init = host.cdm->InitializeVideoDecoder (vc);
	say ("cdmtest : %s : InitializeVideoDecoder (H.264 %dx%d) : statut %u\n", st.what, st.t.width, st.t.height, init);
	int images = 0, need = 0, bad = 0, w = 0, h = 0;
	uint32_t fmt = 0;
	for (size_t i = 0; init == cdm::kSuccess && i < st.samples.size (); i++)
	{
		Prepared pr;
		prepare (st.t, st.data, st.samples[i], pr);
		CdmVideoFrame f;
		host.pump ();
		cdm::Status r = pr.ok ? host.cdm->DecryptAndDecodeFrame (pr.in, &f) : cdm::kDecodeError;
		if (r == cdm::kSuccess)
		{
			images++;
			w = f.Size ().width, h = f.Size ().height, fmt = f.Format ();
		}
		else if (r == cdm::kNeedMoreData)
			need++;
		else if (bad++ < 3 || verbose)
			say ("  image %zu (%s) : statut %u\n", i, st.samples[i].encrypted ? "chiffrée" : "en clair", r);
	}
	for (int k = 0; init == cdm::kSuccess && k < 64; k++)	// (la fin : des entrées vides rendent les images en retard)
	{
		cdm::InputBuffer_2 in = {};
		CdmVideoFrame f;
		if (host.cdm->DecryptAndDecodeFrame (in, &f) != cdm::kSuccess)
			break;
		images++;
	}
	say ("cdmtest : %s : DecryptAndDecodeFrame : %d images %dx%d (format %u), %d « encore des données », %d erreurs\n",
	     st.what, images, w, h, fmt, need, bad);
	check (init == cdm::kSuccess && images == (int) st.samples.size () && !bad,
	       "vidéo : chaque échantillon (en clair et chiffré) déchiffré et décodé par le module");
	host.cdm->DeinitializeDecoder (cdm::kStreamTypeVideo);
}

int main (int argc, char **argv)
{
	const char *args[5] = {};
	int na = 0;
	bool verbose = false;
	std::string storage = "/tmp/cdmtest-storage";
	for (int i = 1; i < argc; i++)
	{
		if (!strcmp (argv[i], "-v"))
			verbose = true;
		else if (!strcmp (argv[i], "--storage") && i + 1 < argc)
			storage = argv[++i];
		else if (na < 5)
			args[na++] = argv[i];
	}
	if (na < 3)
	{
		say ("cdmtest MODULE.so VIDEO.mp4 URL_DE_LICENCE [AUDIO.mp4] [--storage DOSSIER] [-v]\n");
		return 2;
	}
	Stream video, audio;
	check (load_stream (args[1], "vidéo", video), "vidéo : pssh de Widevine, tenc, un fragment chiffré");
	bool have_audio = na >= 4;
	if (have_audio)
		check (load_stream (args[3], "audio", audio), "audio : tenc, un fragment chiffré");

	TestClient client;
	CdmHost host (&client, storage);
	s_host = &host;
	if (!load_module (args[0]))
		return 1;
	((void (*) ()) module_sym ("InitializeCdmModule_4")) ();
	say ("cdmtest : module %s\n", ((const char *(*) ()) module_sym ("GetCdmVersion")) ());
	const char *ks = "com.widevine.alpha";
	void *p = ((void *(*) (int, const char *, uint32_t, void *(*) (int, void *), void *))
		   module_sym ("CreateCdmInstance")) (10, ks, (uint32_t) strlen (ks), get_host, nullptr);
	check (p != nullptr, "CreateCdmInstance (interface 10)");
	if (!p)
		return 1;
	host.cdm = static_cast<cdm::ContentDecryptionModule_10 *> (p);
	host.cdm->Initialize (false, true, false);	// (identifiant distinctif non, état persistant oui, matériel non)
	host.run (3000, [&] { return client.init_done; });
	check (client.init_ok, "Initialize");

	host.cdm->CreateSessionAndGenerateRequest (1, cdm::kTemporary, cdm::kCenc, video.t.pssh.data (),
						   (uint32_t) video.t.pssh.size ());
	host.run (5000, [&] { return !client.messages.empty (); });
	check (!client.session.empty () && !client.messages.empty (), "session créée, requête de licence");

	// l'échange de licence : chaque message part au serveur, sa réponse revient au module
	uint32_t promise = 2;
	for (int round = 0; round < 3 && !client.messages.empty () && !client.usable; round++)
	{
		std::vector<uint8_t> msg = client.messages.back ();
		client.messages.clear ();
		std::vector<uint8_t> resp;
		int status = 0;
		bool ok = https_post (args[2], msg, resp, &status);
		say ("cdmtest : serveur de licences : HTTP %d, %zu octets\n", status, resp.size ());
		check (ok && status == 200, "réponse du serveur de licences");
		if (!ok || status != 200)
		{
			if (!resp.empty ())
				say ("  %.*s\n", (int) (resp.size () < 300 ? resp.size () : 300), (const char *) resp.data ());
			break;
		}
		host.cdm->UpdateSession (promise++, client.session.c_str (), (uint32_t) client.session.size (), resp.data (),
					 (uint32_t) resp.size ());
		host.run (5000, [&] { return client.usable || client.rejected_n || !client.messages.empty (); });
	}
	check (client.usable > 0, "clés utilisables après la licence");

	decrypt_pass (host, video, true, verbose);
	if (video.t.codec == 0x61766331)
		decode_pass (host, video, verbose);
	if (have_audio)
		decrypt_pass (host, audio, false, verbose);

	host.cdm->CloseSession (promise++, client.session.c_str (), (uint32_t) client.session.size ());
	host.run (500);
	host.cdm->Destroy ();
	((void (*) ()) module_sym ("DeinitializeCdmModule")) ();
#if !defined(__linux__)
	say ("cdmtest : syscall directs capturés par le noyau : %lu\n", linux_capture_count);
#endif
	say ("cdmtest : %s (%d échec(s))\n", s_fails ? "ÉCHEC" : "RÉUSSI", s_fails);
	return s_fails ? 1 : 0;
}
