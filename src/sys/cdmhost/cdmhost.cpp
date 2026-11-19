// cdmhost -- l'hôte du module Widevine de Google pour Jet (docs/etude/WIDEVINE.md § 3.3) : un process à
// part (un module qui plante n'emporte pas Jet), qui charge libwidevinecdm.so (liblinux dans Korin, dlopen
// sous Linux pour les essais) et le sert au client (cdmclient, dans le process web de Jet) par deux sockets :
//
//   cdmhost --control FD --media FD --module CHEMIN [--storage DOSSIER]
//
// Le contrôle porte les sessions et les événements du module, le média les requêtes synchrones des fils de
// décodage (proto.h). Un seul thread : le module est appelé et ses rappels servis ici (CdmHost::pump), entre
// deux messages. La fin du contrôle (le client est parti) arrête cdmhost.
#include <errno.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <string>
#include <vector>

#include "host.h"
#include "module.h"
#include "proto.h"

static int s_control = -1, s_media = -1;

// les événements du module, vers le client
struct IpcClient : CdmClient
{
	void send (CdmOut &m)
	{
		if (!cdm_send (s_control, m))
			exit (0);	// (le client est parti)
	}
	void initialized (bool ok) override { CdmOut m (CDM_EV_INITIALIZED); send (m.u32 (ok)); }
	void new_session (uint32_t p, const std::string &s) override { CdmOut m (CDM_EV_NEW_SESSION); send (m.u32 (p).str (s)); }
	void resolved (uint32_t p) override { CdmOut m (CDM_EV_RESOLVED); send (m.u32 (p)); }
	void rejected (uint32_t p, uint32_t e, uint32_t code, const std::string &msg) override
	{
		CdmOut m (CDM_EV_REJECTED);
		send (m.u32 (p).u32 (e).u32 (code).str (msg));
	}
	void key_status (uint32_t p, uint32_t s) override { CdmOut m (CDM_EV_RESOLVED); send (m.u32 (p)); (void) s; }
	void message (const std::string &s, uint32_t type, const uint8_t *d, size_t n) override
	{
		CdmOut m (CDM_EV_MESSAGE);
		send (m.str (s).u32 (type).blk (d, n));
	}
	void keys_change (const std::string &s, bool has_new, const std::vector<std::vector<uint8_t>> &kids,
			  const std::vector<uint32_t> &status) override
	{
		CdmOut m (CDM_EV_KEYS_CHANGE);
		m.str (s).u32 (has_new).u32 ((uint32_t) kids.size ());
		for (size_t i = 0; i < kids.size (); i++)
			m.blk (kids[i].data (), kids[i].size ()).u32 (status[i]);
		send (m);
	}
	void expiration (const std::string &s, double t) override { CdmOut m (CDM_EV_EXPIRATION); send (m.str (s).i64 ((int64_t) (t * 1000))); }
	void closed (const std::string &s) override { CdmOut m (CDM_EV_CLOSED); send (m.str (s)); }
};

static IpcClient s_client;
static CdmHost *s_host;
static std::string s_module_path;

static void *get_host (int version, void *)
{
	return version == cdm::Host_10::kVersion ? static_cast<cdm::Host_10 *> (s_host) : nullptr;
}

// un échantillon pour le module : ses octets et son chiffrement (proto.h)
struct Input
{
	CdmCrypt c;
	std::vector<uint8_t> data, iv;
	std::vector<cdm::SubsampleEntry> subs;
	cdm::InputBuffer_2 in = {};

	void read (CdmIn &m)
	{
		c = m.crypt ();
		data = m.blk ();
		for (size_t i = 0; i < c.clear.size (); i++)
			subs.push_back ({ c.clear[i], c.cipher[i] });
		iv = c.iv;
		if (!iv.empty ())
			iv.resize (16, 0);	// (IV de 8 octets : complété de zéros, comme Chromium)
		in.data = data.empty () ? nullptr : data.data ();
		in.data_size = (uint32_t) data.size ();
		in.encryption_scheme = (cdm::EncryptionScheme) c.scheme;
		in.key_id = c.kid.empty () ? nullptr : c.kid.data ();
		in.key_id_size = (uint32_t) c.kid.size ();
		in.iv = iv.empty () ? nullptr : iv.data ();
		in.iv_size = (uint32_t) iv.size ();
		in.subsamples = subs.empty () ? nullptr : subs.data ();
		in.num_subsamples = (uint32_t) subs.size ();
		in.pattern = { c.crypt_blocks, c.skip_blocks };
		in.timestamp = c.timestamp;
	}
};

static void control (CdmIn &m)
{
	cdm::ContentDecryptionModule_10 *cdm = s_host->cdm;
	switch (m.type)
	{
	case CDM_INIT:
	{
		std::string ks = m.str ();
		uint32_t distinctive = m.u32 (), persistent = m.u32 ();
		if (!cdm && cdm_module_load (s_module_path.c_str ()))
			s_host->cdm = cdm = cdm_module_create (ks.c_str (), get_host, nullptr);
		if (!cdm)
		{
			s_client.initialized (false);
			return;
		}
		fprintf (stderr, "cdmhost : module %s, %s\n", cdm_module_version (), ks.c_str ());
		cdm->Initialize (distinctive != 0, persistent != 0, false);
		return;
	}
	}
	if (!cdm)
	{
		uint32_t promise = m.u32 ();
		s_client.rejected (promise, cdm::kExceptionInvalidStateError, 0, "cdmhost : pas de module");
		return;
	}
	switch (m.type)
	{
	case CDM_SET_SERVER_CERT:
	{
		uint32_t p = m.u32 ();
		std::vector<uint8_t> cert = m.blk ();
		cdm->SetServerCertificate (p, cert.data (), (uint32_t) cert.size ());
		break;
	}
	case CDM_CREATE_SESSION:
	{
		uint32_t p = m.u32 (), st = m.u32 (), it = m.u32 ();
		std::vector<uint8_t> d = m.blk ();
		cdm->CreateSessionAndGenerateRequest (p, (cdm::SessionType) st, (cdm::InitDataType) it, d.data (), (uint32_t) d.size ());
		break;
	}
	case CDM_UPDATE_SESSION:
	{
		uint32_t p = m.u32 ();
		std::string s = m.str ();
		std::vector<uint8_t> r = m.blk ();
		cdm->UpdateSession (p, s.c_str (), (uint32_t) s.size (), r.data (), (uint32_t) r.size ());
		break;
	}
	case CDM_CLOSE_SESSION:
	case CDM_REMOVE_SESSION:
	{
		uint32_t p = m.u32 ();
		std::string s = m.str ();
		if (m.type == CDM_CLOSE_SESSION)
			cdm->CloseSession (p, s.c_str (), (uint32_t) s.size ());
		else
			cdm->RemoveSession (p, s.c_str (), (uint32_t) s.size ());
		break;
	}
	default:
		fprintf (stderr, "cdmhost : message de contrôle inconnu %u\n", m.type);
	}
}

static void media (CdmIn &m)
{
	cdm::ContentDecryptionModule_10 *cdm = s_host->cdm;
	CdmOut out (CDM_STATUS);
	switch (m.type)
	{
	case CDM_DECRYPT:
	{
		Input in;
		in.read (m);
		CdmDecryptedBlock b;
		s_host->pump ();
		cdm::Status st = cdm ? cdm->Decrypt (in.in, &b) : cdm::kDecryptError;
		CdmOut r (CDM_DECRYPTED);
		r.u32 (st);
		if (st == cdm::kSuccess && b.DecryptedBuffer ())
			r.blk (b.DecryptedBuffer ()->Data (), b.DecryptedBuffer ()->Size ());
		else
			r.blk (nullptr, 0);
		cdm_send (s_media, r);
		return;
	}
	case CDM_INIT_VIDEO:
	{
		cdm::VideoDecoderConfig_2 vc = {};
		vc.codec = (cdm::VideoCodec) m.u32 ();
		vc.profile = (cdm::VideoCodecProfile) m.u32 ();
		vc.coded_size.width = (int32_t) m.u32 ();
		vc.coded_size.height = (int32_t) m.u32 ();
		std::vector<uint8_t> extra = m.blk ();
		vc.format = cdm::kI420;
		vc.extra_data = extra.empty () ? nullptr : extra.data ();
		vc.extra_data_size = (uint32_t) extra.size ();
		vc.encryption_scheme = cdm::EncryptionScheme::kCenc;
		out.u32 (cdm ? cdm->InitializeVideoDecoder (vc) : cdm::kInitializationError);
		break;
	}
	case CDM_DECODE_VIDEO:
	{
		Input in;
		in.read (m);
		CdmVideoFrame f;
		s_host->pump ();
		cdm::Status st = cdm ? cdm->DecryptAndDecodeFrame (in.in, &f) : cdm::kDecodeError;
		CdmOut r (CDM_FRAME);
		r.u32 (st).u32 (f.Format ()).u32 ((uint32_t) f.Size ().width).u32 ((uint32_t) f.Size ().height);
		for (int p = 0; p < 3; p++)
			r.u32 (f.Stride ((cdm::VideoPlane) p));
		for (int p = 0; p < 3; p++)
			r.u32 (f.PlaneOffset ((cdm::VideoPlane) p));
		r.i64 (f.Timestamp ());
		if (st == cdm::kSuccess && f.FrameBuffer ())
			r.blk (f.FrameBuffer ()->Data (), f.FrameBuffer ()->Size ());
		else
			r.blk (nullptr, 0);
		cdm_send (s_media, r);
		return;
	}
	case CDM_RESET_VIDEO:
		if (cdm)
			cdm->ResetDecoder (cdm::kStreamTypeVideo);
		out.u32 (cdm::kSuccess);
		break;
	case CDM_DEINIT_VIDEO:
		if (cdm)
			cdm->DeinitializeDecoder (cdm::kStreamTypeVideo);
		out.u32 (cdm::kSuccess);
		break;
	default:
		fprintf (stderr, "cdmhost : requête de média inconnue %u\n", m.type);
		out.u32 (cdm::kDecryptError);
	}
	cdm_send (s_media, out);
}

int main (int argc, char **argv)
{
	std::string storage;
	for (int i = 1; i + 1 < argc; i += 2)
	{
		if (!strcmp (argv[i], "--control"))
			s_control = atoi (argv[i + 1]);
		else if (!strcmp (argv[i], "--media"))
			s_media = atoi (argv[i + 1]);
		else if (!strcmp (argv[i], "--module"))
			s_module_path = argv[i + 1];
		else if (!strcmp (argv[i], "--storage"))
			storage = argv[i + 1];
	}
	if (s_control < 0 || s_media < 0 || s_module_path.empty ())
	{
		fprintf (stderr, "cdmhost --control FD --media FD --module CHEMIN [--storage DOSSIER]\n");
		return 2;
	}
	CdmHost host (&s_client, storage);
	s_host = &host;
	for (;;)
	{
		int next = host.pump ();
		struct pollfd pf[2] = { { s_control, POLLIN, 0 }, { s_media, POLLIN, 0 } };
		int r = poll (pf, 2, next < 0 || next > 100 ? 100 : next);
		if (r < 0 && errno != EINTR)
			break;
		if (pf[1].revents & (POLLIN | POLLHUP))
		{
			CdmIn m;
			if (cdm_recv (s_media, m))
				media (m);
			else
				pf[1].fd = -1, s_media = -1;
		}
		if (pf[0].revents & (POLLIN | POLLHUP))
		{
			CdmIn m;
			if (!cdm_recv (s_control, m))
				break;	// (le client est parti)
			control (m);
		}
	}
	if (host.cdm)
		host.cdm->Destroy ();
	cdm_module_unload ();
	return 0;
}
