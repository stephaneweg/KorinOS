// cenc.cpp -- lecture minimale des MP4 chiffrés (cenc.h).
#include "cenc.h"

#include <string.h>

static uint32_t be32 (const uint8_t *p) { return (uint32_t) p[0] << 24 | (uint32_t) p[1] << 16 | (uint32_t) p[2] << 8 | p[3]; }
static uint16_t be16 (const uint8_t *p) { return (uint16_t) (p[0] << 8 | p[1]); }
static uint64_t be64 (const uint8_t *p) { return (uint64_t) be32 (p) << 32 | be32 (p + 4); }

#define FOURCC(a, b, c, d) ((uint32_t) (a) << 24 | (uint32_t) (b) << 16 | (uint32_t) (c) << 8 | (uint32_t) (d))

static const uint8_t WIDEVINE_ID[16] = { 0xed, 0xef, 0x8b, 0xa9, 0x79, 0xd6, 0x4a, 0xce,
					 0xa3, 0xc8, 0x27, 0xdc, 0xd5, 0x1d, 0x21, 0xed };

const char *cenc_fourcc (uint32_t v, char out[5])
{
	for (int i = 0; i < 4; i++)
	{
		char c = (char) (v >> (24 - 8 * i));
		out[i] = c >= 32 && c < 127 ? c : '?';
	}
	out[4] = 0;
	return out;
}

// une boîte : [type, début du contenu, fin] ; false à la fin ou si elle déborde
struct Box
{
	uint32_t type;
	const uint8_t *body, *end;
	const uint8_t *start;
};

static bool next_box (const uint8_t *&p, const uint8_t *end, Box &b)
{
	if (end - p < 8)
		return false;
	uint64_t size = be32 (p);
	b.start = p;
	b.type = be32 (p + 4);
	const uint8_t *body = p + 8;
	if (size == 1)
	{
		if (end - p < 16)
			return false;
		size = be64 (p + 8);
		body = p + 16;
	}
	else if (size == 0)
		size = (uint64_t) (end - p);
	if (size < (uint64_t) (body - p) || size > (uint64_t) (end - p))
		return false;
	b.body = body;
	b.end = p + size;
	p = b.end;
	return true;
}

static void walk_init (const uint8_t *p, const uint8_t *end, CencTrack &t);

// les descripteurs MPEG-4 de esds (ES 3, DecoderConfig 4, DecoderSpecificInfo 5 = AudioSpecificConfig)
static void walk_descriptors (const uint8_t *p, const uint8_t *end, CencTrack &t)
{
	while (end - p >= 2)
	{
		int tag = *p++;
		size_t len = 0;
		for (int i = 0; i < 4 && p < end; i++)
		{
			uint8_t b = *p++;
			len = len << 7 | (b & 0x7f);
			if (!(b & 0x80))
				break;
		}
		if (len > (size_t) (end - p))
			return;
		const uint8_t *body = p, *next = p + len;
		if (tag == 3 && len >= 3)
		{
			uint8_t fl = body[2];
			const uint8_t *q = body + 3;
			if (fl & 0x80) q += 2;			// dependsOn_ES_ID
			if ((fl & 0x40) && q < next) q += 1 + *q;	// URL
			if (fl & 0x20) q += 2;			// OCR_ES_Id
			if (q < next)
				walk_descriptors (q, next, t);
		}
		else if (tag == 4 && len >= 13)
			walk_descriptors (body + 13, next, t);
		else if (tag == 5)
			t.asc.assign (body, next);
		p = next;
	}
}

// une entrée de stsd (encv/enca) : en-tête d'échantillon, puis des boîtes
static void walk_sample_entry (const Box &e, CencTrack &t)
{
	bool video = e.type == FOURCC ('e', 'n', 'c', 'v') || e.type == FOURCC ('a', 'v', 'c', '1');
	size_t head = video ? 78 : 28;	// (VisualSampleEntry, AudioSampleEntry)
	if ((size_t) (e.end - e.body) < head)
		return;
	if (video)
	{
		t.width = be16 (e.body + 24);
		t.height = be16 (e.body + 26);
	}
	else
	{
		t.channels = be16 (e.body + 16);
		t.sample_rate = be16 (e.body + 24);	// (16.16 : la partie entière)
	}
	walk_init (e.body + head, e.end, t);
}

static void walk_init (const uint8_t *p, const uint8_t *end, CencTrack &t)
{
	Box b;
	while (next_box (p, end, b))
	{
		switch (b.type)
		{
		case FOURCC ('m', 'o', 'o', 'v'): case FOURCC ('t', 'r', 'a', 'k'): case FOURCC ('m', 'd', 'i', 'a'):
		case FOURCC ('m', 'i', 'n', 'f'): case FOURCC ('s', 't', 'b', 'l'): case FOURCC ('s', 'i', 'n', 'f'):
		case FOURCC ('s', 'c', 'h', 'i'):
			walk_init (b.body, b.end, t);
			break;
		case FOURCC ('m', 'd', 'h', 'd'):
			if (b.end - b.body >= 24)
				t.timescale = b.body[0] == 1 ? be32 (b.body + 20) : be32 (b.body + 12);
			break;
		case FOURCC ('s', 't', 's', 'd'):
		{
			const uint8_t *q = b.body + 8;	// (version, drapeaux, nombre)
			Box e;
			while (q < b.end && next_box (q, b.end, e))
				walk_sample_entry (e, t);
			break;
		}
		case FOURCC ('p', 's', 's', 'h'):
			if (b.end - b.body >= 20 && !memcmp (b.body + 4, WIDEVINE_ID, 16) && t.pssh.empty ())
				t.pssh.assign (b.start, b.end);
			break;
		case FOURCC ('f', 'r', 'm', 'a'):
			if (b.end - b.body >= 4)
				t.codec = be32 (b.body);
			break;
		case FOURCC ('s', 'c', 'h', 'm'):
			if (b.end - b.body >= 8)
				t.scheme = be32 (b.body + 4);
			break;
		case FOURCC ('a', 'v', 'c', 'C'):
			t.avcc.assign (b.body, b.end);
			break;
		case FOURCC ('e', 's', 'd', 's'):
			if (b.end - b.body > 4)
				walk_descriptors (b.body + 4, b.end, t);
			break;
		case FOURCC ('t', 'e', 'n', 'c'):
		{
			const uint8_t *q = b.body;
			if (b.end - q < 24)
				break;
			int version = q[0];
			if (version >= 1)
			{
				t.crypt_blocks = q[5] >> 4;
				t.skip_blocks = q[5] & 15;
			}
			t.iv_size = q[7];
			memcpy (t.kid, q + 8, 16);
			if (q[6] && t.iv_size == 0 && b.end - q >= 25)
			{
				int n = q[24];
				if (b.end - q >= 25 + n)
					t.const_iv.assign (q + 25, q + 25 + n);
			}
			break;
		}
		}
	}
}

bool cenc_parse_init (const uint8_t *data, size_t size, CencTrack &t)
{
	walk_init (data, data + size, t);
	return !t.pssh.empty () && t.scheme;
}

bool cenc_to_annexb (const CencTrack &t, const uint8_t *in, size_t n, const CencSample &s, std::vector<uint8_t> &out,
		     std::vector<uint32_t> &clear, std::vector<uint32_t> &cipher)
{
	static const uint8_t SC[4] = { 0, 0, 0, 1 };
	if (t.avcc.size () < 7)
		return false;
	int lsize = (t.avcc[4] & 3) + 1;
	// les SPS et PPS de avcC, en Annex B
	std::vector<uint8_t> params;
	const uint8_t *a = t.avcc.data (), *ae = a + t.avcc.size ();
	const uint8_t *q = a + 5;
	for (int pass = 0; pass < 2 && q < ae; pass++)
	{
		int count = pass == 0 ? (*q++ & 31) : *q++;
		for (int i = 0; i < count && q + 2 <= ae; i++)
		{
			size_t len = be16 (q);
			q += 2;
			if (q + len > ae)
				return false;
			params.insert (params.end (), SC, SC + 4);
			params.insert (params.end (), q, q + len);
			q += len;
		}
	}
	// les NAL de l'échantillon : longueur -> code de début ; une IDR (type 5) demande les paramètres
	out.clear ();
	bool idr = false;
	for (size_t p = 0; p + (size_t) lsize <= n;)
	{
		size_t len = 0;
		for (int i = 0; i < lsize; i++)
			len = len << 8 | in[p + i];
		if (p + (size_t) lsize + len > n || !len)
			return false;
		if ((in[p + lsize] & 31) == 5)
			idr = true;
		out.insert (out.end (), SC, SC + 4);
		out.insert (out.end (), in + p + lsize, in + p + lsize + len);
		p += (size_t) lsize + len;
	}
	size_t grow = out.size () - n;	// (longueurs de 1 ou 2 octets : les codes de début sont plus longs)
	if (idr)
	{
		out.insert (out.begin (), params.begin (), params.end ());
		grow += params.size ();
	}
	clear = s.clear;
	cipher = s.cipher;
	if (!s.encrypted)
		return true;
	// (H.264 chiffré : toujours en sous-échantillons, les longueurs des NAL en clair ; des longueurs de moins de
	// 4 octets changeraient les tailles des parties en clair : pas servi)
	if (clear.empty () || lsize != 4)
		return false;
	clear[0] += (uint32_t) (idr ? params.size () : 0);
	return true;
}

size_t cenc_parse_fragment (const uint8_t *data, size_t size, size_t from, const CencTrack &t,
			    std::vector<CencSample> &out)
{
	const uint8_t *p = data + from, *end = data + size;
	Box b, moof = {};
	bool have_moof = false;
	while (next_box (p, end, b))
	{
		if (b.type == FOURCC ('m', 'o', 'o', 'f'))
		{
			moof = b;
			have_moof = true;
		}
		else if (b.type == FOURCC ('m', 'd', 'a', 't') && have_moof)
			break;
	}
	if (!have_moof)
		return 0;
	size_t moof_off = (size_t) (moof.start - data);
	uint32_t default_size = 0, default_duration = 0;
	int64_t base_time = 0;
	std::vector<uint32_t> sizes, durations;
	int64_t data_offset = 0;
	std::vector<CencSample> senc;
	const uint8_t *q = moof.body;
	Box traf;
	while (next_box (q, moof.end, traf))
	{
		if (traf.type != FOURCC ('t', 'r', 'a', 'f'))
			continue;
		const uint8_t *r = traf.body;
		Box c;
		while (next_box (r, traf.end, c))
		{
			const uint8_t *s = c.body;
			uint32_t flags = c.end - s >= 4 ? be32 (s) & 0xffffff : 0;
			if (c.type == FOURCC ('t', 'f', 'h', 'd'))
			{
				const uint8_t *x = s + 8;	// (version/drapeaux, track_ID)
				if (flags & 1) x += 8;		// base_data_offset
				if (flags & 2) x += 4;		// sample_description_index
				if ((flags & 8) && x + 4 <= c.end)
				{
					default_duration = be32 (x);
					x += 4;
				}
				if ((flags & 0x10) && x + 4 <= c.end)
					default_size = be32 (x);
			}
			else if (c.type == FOURCC ('t', 'f', 'd', 't'))
				base_time = s[0] == 1 ? (int64_t) be64 (s + 4) : be32 (s + 4);
			else if (c.type == FOURCC ('t', 'r', 'u', 'n'))
			{
				uint32_t n = be32 (s + 4);
				const uint8_t *x = s + 8;
				if (flags & 1) { data_offset = (int32_t) be32 (x); x += 4; }
				if (flags & 4) x += 4;		// first_sample_flags
				for (uint32_t i = 0; i < n && x <= c.end; i++)
				{
					uint32_t du = default_duration;
					if (flags & 0x100) { du = be32 (x); x += 4; }
					durations.push_back (du);
					uint32_t sz = default_size;
					if (flags & 0x200) { sz = be32 (x); x += 4; }
					if (flags & 0x400) x += 4;	// drapeaux
					if (flags & 0x800) x += 4;	// décalage de composition
					sizes.push_back (sz);
				}
			}
			else if (c.type == FOURCC ('s', 'e', 'n', 'c'))
			{
				uint32_t n = be32 (s + 4);
				const uint8_t *x = s + 8;
				for (uint32_t i = 0; i < n && x < c.end; i++)
				{
					CencSample e;
					if (t.iv_size)
					{
						e.iv.assign (x, x + t.iv_size);
						x += t.iv_size;
					}
					else
						e.iv = t.const_iv;
					if (flags & 2)
					{
						uint16_t k = be16 (x);
						x += 2;
						for (uint16_t j = 0; j < k && x + 6 <= c.end; j++)
						{
							e.clear.push_back (be16 (x));
							e.cipher.push_back (be32 (x + 2));
							x += 6;
						}
					}
					senc.push_back (e);
				}
			}
		}
	}
	size_t off = moof_off + (size_t) data_offset;
	for (size_t i = 0; i < sizes.size (); i++)
	{
		CencSample e = i < senc.size () ? senc[i] : CencSample ();
		e.encrypted = i < senc.size ();
		e.offset = off;
		e.size = sizes[i];
		e.pts = base_time;
		base_time += durations[i];
		off += sizes[i];
		if (e.offset + e.size > size)
			break;
		out.push_back (e);
	}
	return (size_t) (p - data);
}
