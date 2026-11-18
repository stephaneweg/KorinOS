// weather.c -- la météo d'Open-Meteo par HTTP en clair (NetKit), avec un petit lecteur JSON
//
// Géocodage : http://geocoding-api.open-meteo.com/v1/search?name=...
// Prévisions : http://api.open-meteo.com/v1/forecast?latitude=...&longitude=...
// (pas de TLS : OpenSSL n'est pas dans le GCC de la carte ; Open-Meteo répond aussi en HTTP)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netkit_abi.h>
#include "weather.h"

#define GEO_HOST	"geocoding-api.open-meteo.com"
#define API_HOST	"api.open-meteo.com"
#define RESP_MAX	(96 * 1024)		// une réponse tient largement là-dedans
#define NET_WAIT	15000			// délai de réception (ms)

// ---- HTTP ------------------------------------------------------------------------------------------------------------

// minuscules ASCII, pour lire les en-têtes
static int lower (int c) { return c >= 'A' && c <= 'Z' ? c + 32 : c; }

// cherche l'en-tête « name: » dans head (len octets) -> sa valeur, ou 0
static const char *header (const char *head, int len, const char *name)
{
	int n = (int) strlen (name);
	for (int i = 0; i + n < len; i++)
	{
		if (i > 0 && head[i - 1] != '\n') continue;
		int k = 0;
		while (k < n && lower ((unsigned char) head[i + k]) == lower ((unsigned char) name[k])) k++;
		if (k == n && head[i + n] == ':')
		{
			const char *v = head + i + n + 1;
			while (*v == ' ') v++;
			return v;
		}
	}
	return 0;
}

// recolle un corps en « Transfer-Encoding: chunked » sur place -> nouvelle longueur
static int dechunk (char *b, int len)
{
	int in = 0, out = 0;
	while (in < len)
	{
		char *e;
		long sz = strtol (b + in, &e, 16);
		if (e == b + in) break;
		char *nl = memchr (e, '\n', (size_t) (len - (e - b)));
		if (!nl) break;
		in = (int) (nl - b) + 1;
		if (sz <= 0 || in + sz > len) break;
		memmove (b + out, b + in, (size_t) sz);
		out += (int) sz;
		in += (int) sz;
		if (in < len && b[in] == '\r') in++;
		if (in < len && b[in] == '\n') in++;
	}
	return out;
}

// GET http://host/path -> longueur du corps (dans buf, terminé par 0) ou WX_ERR_*
static int http_get (const char *host, const char *path, char *buf, int max)
{
	unsigned char ni[32];
	memset (ni, 0, sizeof ni);
	net_info ((long) ni);
	if (*(unsigned *) (ni + NI_IP) == 0) return WX_ERR_NONET;

	long ip = net_resolve ((long) host);
	if (ip <= 0) return WX_ERR_DNS;
	long sd = net_socket (SOCK_TCP);
	if (sd < 0) return WX_ERR_CONNECT;
	if (net_connect (sd, ip, 80) != 0) { net_close (sd); return WX_ERR_CONNECT; }

	// HTTP/1.0 : pas de morceaux d'ordinaire, et la fin du corps est la fin de la connexion
	char req[1024];
	int n = snprintf (req, sizeof req,
		"GET %s HTTP/1.0\r\nHost: %s\r\nUser-Agent: KorinWeather/1.0\r\nAccept: application/json\r\n"
		"Connection: close\r\n\r\n", path, host);
	if (n <= 0 || n >= (int) sizeof req) { net_close (sd); return WX_ERR_HTTP; }
	for (int sent = 0; sent < n; )
	{
		long r = net_send (sd, (long) (req + sent), n - sent);
		if (r <= 0) { net_close (sd); return WX_ERR_CONNECT; }
		sent += (int) r;
	}

	int total = 0;
	long r = 0;
	while (total < max - 1)
	{
		long want = max - 1 - total;
		if (want > 8192) want = 8192;		// net_recv rend 8 Ko au plus
		r = net_recv (sd, (long) (buf + total), want, NET_WAIT);
		if (r <= 0) break;
		total += (int) r;
	}
	net_close (sd);
	buf[total] = 0;
	if (total == 0) return r < 0 ? WX_ERR_CONNECT : WX_ERR_HTTP;

	// « HTTP/1.1 200 OK »
	if (strncmp (buf, "HTTP/", 5) != 0) return WX_ERR_HTTP;
	const char *sp = strchr (buf, ' ');
	if (!sp || atoi (sp + 1) != 200) return WX_ERR_HTTP;
	char *body = strstr (buf, "\r\n\r\n");
	if (!body) return WX_ERR_HTTP;
	body += 4;
	int hlen = (int) (body - buf);
	int blen = total - hlen;
	const char *te = header (buf, hlen, "Transfer-Encoding");
	int chunked = te && lower ((unsigned char) te[0]) == 'c';
	memmove (buf, body, (size_t) blen);
	if (chunked) blen = dechunk (buf, blen);
	buf[blen] = 0;
	return blen;
}

// ---- JSON (juste ce qu'il faut : objets, tableaux, nombres, chaînes) ---------------------------------------------------

static const char *ws (const char *p, const char *e)
{
	while (p < e && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n')) p++;
	return p;
}

// fin d'une chaîne qui commence à p ('"') -> juste après le guillemet fermant
static const char *str_end (const char *p, const char *e)
{
	for (p++; p < e; p++)
	{
		if (*p == '\\') { p++; continue; }
		if (*p == '"') return p + 1;
	}
	return e;
}

// fin de la valeur qui commence à p
static const char *value_end (const char *p, const char *e)
{
	p = ws (p, e);
	if (p >= e) return e;
	if (*p == '"') return str_end (p, e);
	if (*p == '{' || *p == '[')
	{
		int depth = 0;
		for (; p < e; p++)
		{
			if (*p == '"') { p = str_end (p, e) - 1; continue; }
			if (*p == '{' || *p == '[') depth++;
			else if (*p == '}' || *p == ']') { if (--depth == 0) return p + 1; }
		}
		return e;
	}
	while (p < e && *p != ',' && *p != '}' && *p != ']') p++;	// nombre, true, false, null
	return p;
}

// dans l'objet [p, e) (p sur '{'), la valeur du membre key -> son début (0 : absent), sa fin dans *ve
static const char *obj_get (const char *p, const char *e, const char *key, const char **ve)
{
	p = ws (p, e);
	if (p >= e || *p != '{') return 0;
	size_t kl = strlen (key);
	p++;
	for (;;)
	{
		p = ws (p, e);
		if (p >= e || *p != '"') return 0;
		const char *ks = p + 1, *kend = str_end (p, e);
		int same = (size_t) (kend - 1 - ks) == kl && memcmp (ks, key, kl) == 0;
		p = ws (kend, e);
		if (p >= e || *p != ':') return 0;
		const char *v = ws (p + 1, e);
		const char *vend = value_end (v, e);
		if (same) { if (ve) *ve = vend; return v; }
		p = ws (vend, e);
		if (p >= e || *p != ',') return 0;
		p++;
	}
}

// le premier élément du tableau [p, e) (p sur '['), ou 0
static const char *arr_first (const char *p, const char *e)
{
	p = ws (p, e);
	if (p >= e || *p != '[') return 0;
	p = ws (p + 1, e);
	return p < e && *p != ']' ? p : 0;
}

// l'élément après celui qui commence à p, ou 0
static const char *arr_next (const char *p, const char *e)
{
	p = ws (value_end (p, e), e);
	if (p >= e || *p != ',') return 0;
	p = ws (p + 1, e);
	return p < e ? p : 0;
}

// le i-ième élément du tableau, ou 0
static const char *arr_at (const char *p, const char *e, int i)
{
	for (p = arr_first (p, e); p && i > 0; i--) p = arr_next (p, e);
	return p;
}

static double num (const char *p, const char *e, double def)
{
	if (!p || p >= e || *p == 'n') return def;	// null
	char *x;
	double v = strtod (p, &x);
	return x == p ? def : v;
}

// une chaîne JSON vers out (UTF-8, échappements décodés)
static void str (const char *p, const char *e, char *out, int size)
{
	int n = 0;
	out[0] = 0;
	if (!p || p >= e || *p != '"') return;
	for (p++; p < e && *p != '"' && n < size - 1; p++)
	{
		unsigned c = (unsigned char) *p;
		if (c == '\\' && p + 1 < e)
		{
			p++;
			switch (*p)
			{
			case 'n': c = '\n'; break;
			case 't': c = '\t'; break;
			case 'r': c = '\r'; break;
			case 'b': c = '\b'; break;
			case 'f': c = '\f'; break;
			case 'u':
				if (p + 4 < e) { char h[5] = { p[1], p[2], p[3], p[4], 0 }; c = (unsigned) strtoul (h, 0, 16); p += 4; }
				break;
			default: c = (unsigned char) *p; break;
			}
			if (c >= 0x80)		// \uXXXX hors ASCII (sans les paires de substitution : rares ici)
			{
				if (n + 3 >= size) break;
				if (c < 0x800) { out[n++] = (char) (0xC0 | c >> 6); out[n++] = (char) (0x80 | (c & 63)); }
				else { out[n++] = (char) (0xE0 | c >> 12); out[n++] = (char) (0x80 | (c >> 6 & 63)); out[n++] = (char) (0x80 | (c & 63)); }
				continue;
			}
		}
		out[n++] = (char) c;
	}
	out[n] = 0;
}

// ---- comparer des noms écrits à la main (« etats-unis » = « États-Unis ») -------------------------------------------

// les lettres latines accentuées (C3 80 à C3 BF) ramenées à leur lettre de base, en minuscules
static const char FOLD[64 + 1] = "aaaaaaaceeeeiiiidnooooo-ouuuuyts" "aaaaaaaceeeeiiiidnooooo-ouuuuyty";

static void fold (const char *s, char *out, int size)
{
	int n = 0;
	for (const unsigned char *p = (const unsigned char *) s; *p && n < size - 1; p++)
	{
		unsigned c = *p;
		if (c == 0xC3 && p[1] >= 0x80 && p[1] <= 0xBF) { c = (unsigned char) FOLD[p[1] - 0x80]; p++; }
		else if (c >= 0x80) continue;		// autre caractère non ASCII : ignoré
		c = (unsigned) lower ((int) c);
		if (c == ' ' || c == '-' || c == '\'' || c == '.') continue;
		out[n++] = (char) c;
	}
	out[n] = 0;
}

static int same_name (const char *a, const char *b)
{
	char fa[128], fb[128];
	fold (a, fa, sizeof fa);
	fold (b, fb, sizeof fb);
	return fa[0] && strcmp (fa, fb) == 0;
}

// encode s pour une adresse (« Liège » -> « Li%C3%A8ge »)
static void url_encode (const char *s, char *out, int size)
{
	static const char HEX[] = "0123456789ABCDEF";
	int n = 0;
	for (const unsigned char *p = (const unsigned char *) s; *p && n < size - 4; p++)
	{
		unsigned c = *p;
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')
			out[n++] = (char) c;
		else { out[n++] = '%'; out[n++] = HEX[c >> 4]; out[n++] = HEX[c & 15]; }
	}
	out[n] = 0;
}

// enlève les espaces au début et à la fin
static void trim (const char *s, char *out, int size)
{
	while (*s == ' ' || *s == '\t') s++;
	int n = (int) strlen (s);
	while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t')) n--;
	if (n > size - 1) n = size - 1;
	memcpy (out, s, (size_t) n);
	out[n] = 0;
}

// ---- l'API ------------------------------------------------------------------------------------------------------------

int wx_geocode (struct wx_place *place, const char *lang)
{
	char city[96], country[64], q[300], path[512];
	trim (place->city, city, sizeof city);
	trim (place->country, country, sizeof country);
	place->found = 0;
	if (!city[0]) return WX_ERR_NOTFOUND;
	url_encode (city, q, sizeof q);

	// un code de pays à deux lettres : le serveur filtre lui-même
	int is_code = strlen (country) == 2 && (unsigned char) country[0] < 0x80 && (unsigned char) country[1] < 0x80;
	char cc[4] = "";
	if (is_code) { cc[0] = (char) (lower (country[0]) - 32); cc[1] = (char) (lower (country[1]) - 32); cc[2] = 0; }
	snprintf (path, sizeof path, "/v1/search?name=%s&count=20&language=%s&format=json%s%s",
		q, lang && *lang ? lang : "fr", is_code ? "&countryCode=" : "", cc);

	char *buf = malloc (RESP_MAX);
	if (!buf) return WX_ERR_HTTP;
	int len = http_get (GEO_HOST, path, buf, RESP_MAX);
	if (len < 0) { free (buf); return len; }
	const char *e = buf + len, *ae;
	const char *arr = obj_get (buf, e, "results", &ae);
	int rc = WX_ERR_NOTFOUND;
	for (const char *it = arr ? arr_first (arr, ae) : 0; it; it = arr_next (it, ae))
	{
		const char *oe = value_end (it, ae), *ve;
		char cname[64], ccode[8], admin1[96];
		str (obj_get (it, oe, "country", 0), oe, cname, sizeof cname);
		str (obj_get (it, oe, "country_code", 0), oe, ccode, sizeof ccode);
		str (obj_get (it, oe, "admin1", 0), oe, admin1, sizeof admin1);
		// sans pays : le premier (le plus peuplé) ; sinon celui dont le pays, son code ou la région correspond
		if (country[0] && !is_code && !same_name (country, cname) && !same_name (country, ccode) && !same_name (country, admin1))
			continue;
		const char *v = obj_get (it, oe, "latitude", &ve);
		place->lat = num (v, ve, 0);
		v = obj_get (it, oe, "longitude", &ve);
		place->lon = num (v, ve, 0);
		str (obj_get (it, oe, "name", 0), oe, place->name, sizeof place->name);
		snprintf (place->country_name, sizeof place->country_name, "%s", cname);
		snprintf (place->code, sizeof place->code, "%s", ccode);
		place->found = 1;
		rc = WX_OK;
		break;
	}
	free (buf);
	return rc;
}

int wx_forecast (const struct wx_place *place, struct wx_data *out)
{
	char path[512];
	memset (out, 0, sizeof *out);
	snprintf (path, sizeof path,
		"/v1/forecast?latitude=%.4f&longitude=%.4f"
		"&current=temperature_2m,relative_humidity_2m,apparent_temperature,is_day,weather_code,wind_speed_10m"
		"&daily=weather_code,temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=%d",
		place->lat, place->lon, WX_DAYS);
	char *buf = malloc (RESP_MAX);
	if (!buf) return WX_ERR_HTTP;
	int len = http_get (API_HOST, path, buf, RESP_MAX);
	if (len < 0) { free (buf); return len; }
	const char *e = buf + len, *ce, *de, *ve;

	const char *cur = obj_get (buf, e, "current", &ce);
	if (!cur) { free (buf); return WX_ERR_HTTP; }
	str (obj_get (cur, ce, "time", 0), ce, out->time, sizeof out->time);
	const char *v;
	v = obj_get (cur, ce, "temperature_2m", &ve);		out->temp = num (v, ve, 0);
	v = obj_get (cur, ce, "apparent_temperature", &ve);	out->feels = num (v, ve, out->temp);
	v = obj_get (cur, ce, "wind_speed_10m", &ve);		out->wind = num (v, ve, 0);
	v = obj_get (cur, ce, "relative_humidity_2m", &ve);	out->humidity = (int) num (v, ve, 0);
	v = obj_get (cur, ce, "weather_code", &ve);		out->code = (int) num (v, ve, 0);
	v = obj_get (cur, ce, "is_day", &ve);			out->is_day = (int) num (v, ve, 1);

	const char *daily = obj_get (buf, e, "daily", &de);
	if (daily)
	{
		const char *te, *ke, *xe, *ne;
		const char *times = obj_get (daily, de, "time", &te);
		const char *codes = obj_get (daily, de, "weather_code", &ke);
		const char *tmax = obj_get (daily, de, "temperature_2m_max", &xe);
		const char *tmin = obj_get (daily, de, "temperature_2m_min", &ne);
		for (int i = 0; times && i < WX_DAYS; i++)
		{
			const char *t = arr_at (times, te, i);
			if (!t) break;
			str (t, te, out->day[i], sizeof out->day[i]);
			const char *x;
			x = codes ? arr_at (codes, ke, i) : 0;	out->dcode[i] = (int) num (x, ke, 0);
			x = tmax ? arr_at (tmax, xe, i) : 0;	out->tmax[i] = num (x, xe, 0);
			x = tmin ? arr_at (tmin, ne, i) : 0;	out->tmin[i] = num (x, ne, 0);
			out->ndays = i + 1;
		}
	}
	out->valid = 1;
	free (buf);
	return WX_OK;
}

int wx_weekday (const char *date)
{
	int y, m, d;
	if (!date || sscanf (date, "%d-%d-%d", &y, &m, &d) != 3 || m < 1 || m > 12 || d < 1 || d > 31) return -1;
	// jours depuis le 1er janvier 1970 (algorithme « days from civil »)
	y -= m <= 2;
	long era = (y >= 0 ? y : y - 399) / 400;
	long yoe = y - era * 400;
	long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
	long days = era * 146097 + doe - 719468;
	long w = (days + 3) % 7;			// le 1er janvier 1970 était un jeudi (3, lundi = 0)
	return (int) (w < 0 ? w + 7 : w);
}
