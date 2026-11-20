// cenc.h -- lecture minimale des MP4 chiffrés (ISO/IEC 23001-7, « Common Encryption ») pour l'hôte du
// module Widevine et ses essais (docs/etude/WIDEVINE.md, étape 3) : la boîte pssh de Widevine et la
// boîte tenc d'un segment d'initialisation, puis les échantillons d'un fragment (moof + mdat) avec leur
// IV et leurs sous-échantillons (senc). Dans Jet, ce travail revient au lecteur MSE (MediaKit) ; ici,
// il sert à essayer l'hôte sans navigateur.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include <vector>

struct CencTrack
{
	std::vector<uint8_t> pssh;	// la boîte pssh de Widevine entière (données d'initialisation « cenc » d'EME)
	uint8_t kid[16] = {};		// KID par défaut (tenc)
	int iv_size = 8;		// taille des IV par échantillon (tenc), 0 : IV constant
	std::vector<uint8_t> const_iv;	// IV constant (cbcs)
	uint32_t scheme = 0;		// 'cenc', 'cbcs'…
	uint8_t crypt_blocks = 0, skip_blocks = 0;	// motif (cbcs)
	uint32_t codec = 0;		// format d'origine (frma) : 'avc1', 'mp4a'…
	std::vector<uint8_t> avcc;	// la configuration avcC (H.264), pour décoder
	int width = 0, height = 0;	// (entrée vidéo de stsd)
	std::vector<uint8_t> asc;	// AudioSpecificConfig (esds) de l'AAC, pour décoder
	int channels = 0, sample_rate = 0;	// (entrée audio de stsd)
	uint32_t timescale = 0;
};

struct CencSample
{
	size_t offset = 0, size = 0;	// dans le fichier
	bool encrypted = false;		// (un fragment sans senc est en clair : « clear lead » du début)
	std::vector<uint8_t> iv;
	std::vector<uint32_t> clear, cipher;	// sous-échantillons
	int64_t pts = 0;		// en unités de timescale
};

// le segment d'initialisation (ftyp + moov) : false s'il n'a ni pssh de Widevine ni tenc
bool cenc_parse_init (const uint8_t *data, size_t size, CencTrack &t);

// le premier fragment (moof + mdat) à partir de « from » dans le fichier : ses échantillons ; rend la
// position qui suit le fragment, 0 s'il n'y en a pas
size_t cenc_parse_fragment (const uint8_t *data, size_t size, size_t from, const CencTrack &t,
			    std::vector<CencSample> &out);

// H.264 en Annex B, comme le fait Chromium avant le module (et avant tout décodeur) : les longueurs des NAL
// (avcC) deviennent des codes de début 00 00 00 01, et les SPS/PPS de avcC précèdent une image clé (IDR) ;
// les sous-échantillons sont ajustés (les octets ajoutés sont en clair). false si l'échantillon est mal formé.
bool cenc_to_annexb (const CencTrack &t, const uint8_t *in, size_t n, const CencSample &s, std::vector<uint8_t> &out,
		     std::vector<uint32_t> &clear, std::vector<uint32_t> &cipher);

// une chaîne de quatre lettres (affichage)
const char *cenc_fourcc (uint32_t v, char out[5]);
