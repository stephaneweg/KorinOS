// proto.h -- le protocole entre Jet (cdmclient) et cdmhost, l'hôte du module Widevine
// (docs/etude/WIDEVINE.md § 3.3).
//
// Deux sockets de flux (socketpair à la création de cdmhost par le client) :
//   - le contrôle : les sessions d'EME (création, mise à jour, fermeture), et les événements du module
//     (promesses, messages de licence, statuts de clés) qui arrivent quand ils veulent ;
//   - le média : des requêtes synchrones des fils de décodage (Decrypt pour l'audio, décodage de la vidéo),
//     une réponse à chacune, dans l'ordre.
// Un message : u32 longueur (de ce qui suit), u32 type, puis ses champs, petit-boutiste. Champs : u32, i64,
// et des blocs (u32 longueur + octets). Les échantillons et les images vont dans les messages (pas encore de
// mémoire partagée).
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <string>
#include <vector>

enum CdmMsg : uint32_t
{
	// contrôle, client -> hôte
	CDM_INIT = 1,			// blocs : système de clés ; u32 : identifiant distinctif, état persistant
	CDM_SET_SERVER_CERT = 2,	// u32 promesse, bloc certificat
	CDM_CREATE_SESSION = 3,		// u32 promesse, u32 type de session, u32 type de données, bloc données
	CDM_UPDATE_SESSION = 4,		// u32 promesse, bloc session, bloc réponse
	CDM_CLOSE_SESSION = 5,		// u32 promesse, bloc session
	CDM_REMOVE_SESSION = 6,		// u32 promesse, bloc session
	// contrôle, hôte -> client (événements)
	CDM_EV_INITIALIZED = 100,	// u32 ok
	CDM_EV_RESOLVED = 101,		// u32 promesse
	CDM_EV_REJECTED = 102,		// u32 promesse, u32 exception, u32 code, bloc message
	CDM_EV_NEW_SESSION = 103,	// u32 promesse, bloc session
	CDM_EV_MESSAGE = 104,		// bloc session, u32 type, bloc message
	CDM_EV_KEYS_CHANGE = 105,	// bloc session, u32 nouvelle clé, u32 n, n x (bloc KID, u32 statut)
	CDM_EV_EXPIRATION = 106,	// bloc session, i64 temps (ms depuis 1970)
	CDM_EV_CLOSED = 107,		// bloc session
	// média, client -> hôte
	CDM_DECRYPT = 200,		// le chiffrement (voir CdmCrypt), bloc données -> CDM_DECRYPTED
	CDM_INIT_VIDEO = 201,		// u32 codec, u32 profil, u32 largeur, u32 hauteur, bloc extradata -> CDM_STATUS
	CDM_DECODE_VIDEO = 202,		// le chiffrement, bloc données (vide : la fin) -> CDM_FRAME
	CDM_RESET_VIDEO = 203,		// -> CDM_STATUS
	CDM_DEINIT_VIDEO = 204,		// -> CDM_STATUS
	// média, hôte -> client (réponses)
	CDM_DECRYPTED = 300,		// u32 statut (cdm::Status), bloc données
	CDM_STATUS = 301,		// u32 statut
	CDM_FRAME = 302,		// u32 statut, u32 format, u32 largeur, u32 hauteur, 3 x u32 pas, 3 x u32 décalage,
					// i64 horodatage, bloc pixels
};

// le chiffrement d'un échantillon (CDM_DECRYPT, CDM_DECODE_VIDEO) : u32 schéma (cdm::EncryptionScheme),
// bloc KID, bloc IV, u32 motif chiffré, u32 motif en clair, u32 n, n x (u32 en clair, u32 chiffrés), i64 horodatage
struct CdmCrypt
{
	uint32_t scheme = 0;
	std::vector<uint8_t> kid, iv;
	uint32_t crypt_blocks = 0, skip_blocks = 0;
	std::vector<uint32_t> clear, cipher;
	int64_t timestamp = 0;
};

// un message en écriture
struct CdmOut
{
	std::vector<uint8_t> b;
	explicit CdmOut (uint32_t type) { b.resize (8); memcpy (&b[4], &type, 4); }
	CdmOut &u32 (uint32_t v) { const uint8_t *p = (const uint8_t *) &v; b.insert (b.end (), p, p + 4); return *this; }
	CdmOut &i64 (int64_t v) { const uint8_t *p = (const uint8_t *) &v; b.insert (b.end (), p, p + 8); return *this; }
	CdmOut &blk (const void *d, size_t n)
	{
		u32 ((uint32_t) n);
		if (n)
			b.insert (b.end (), (const uint8_t *) d, (const uint8_t *) d + n);
		return *this;
	}
	CdmOut &str (const std::string &s) { return blk (s.data (), s.size ()); }
	CdmOut &crypt (const CdmCrypt &c)
	{
		u32 (c.scheme).blk (c.kid.data (), c.kid.size ()).blk (c.iv.data (), c.iv.size ());
		u32 (c.crypt_blocks).u32 (c.skip_blocks).u32 ((uint32_t) c.clear.size ());
		for (size_t i = 0; i < c.clear.size (); i++)
			u32 (c.clear[i]).u32 (c.cipher[i]);
		return i64 (c.timestamp);
	}
	// la longueur en tête, avant l'envoi
	const std::vector<uint8_t> &done () { uint32_t n = (uint32_t) (b.size () - 4); memcpy (&b[0], &n, 4); return b; }
};

// un message en lecture (ok devient faux à la première lecture hors du message)
struct CdmIn
{
	uint32_t type = 0;
	std::vector<uint8_t> b;
	size_t at = 0;
	bool ok = true;
	uint32_t u32 ()
	{
		uint32_t v = 0;
		if (at + 4 > b.size ()) { ok = false; return 0; }
		memcpy (&v, &b[at], 4);
		at += 4;
		return v;
	}
	int64_t i64 ()
	{
		int64_t v = 0;
		if (at + 8 > b.size ()) { ok = false; return 0; }
		memcpy (&v, &b[at], 8);
		at += 8;
		return v;
	}
	std::vector<uint8_t> blk ()
	{
		uint32_t n = u32 ();
		if (!ok || at + n > b.size ()) { ok = false; return {}; }
		std::vector<uint8_t> v (b.begin () + (long) at, b.begin () + (long) (at + n));
		at += n;
		return v;
	}
	std::string str () { std::vector<uint8_t> v = blk (); return std::string (v.begin (), v.end ()); }
	CdmCrypt crypt ()
	{
		CdmCrypt c;
		c.scheme = u32 ();
		c.kid = blk ();
		c.iv = blk ();
		c.crypt_blocks = u32 ();
		c.skip_blocks = u32 ();
		uint32_t n = u32 ();
		for (uint32_t i = 0; ok && i < n && i < 65536; i++)
		{
			c.clear.push_back (u32 ());
			c.cipher.push_back (u32 ());
		}
		c.timestamp = i64 ();
		return c;
	}
};

// l'envoi et la réception d'un message entier sur un descripteur bloquant (false : le pair est parti)
bool cdm_send (int fd, CdmOut &m);
bool cdm_recv (int fd, CdmIn &m);
