//
// json.h -- PocketQI : juste assez de JSON pour parler à un LLM compatible OpenAI (chat/completions) : écrire une
// chaîne échappée, et lire une réponse en arbre (objets, tableaux, chaînes décodées en UTF-8, nombres, vrai/faux/nul).
//
#ifndef POCKETQI_JSON_H
#define POCKETQI_JSON_H

#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>

namespace pq {

// s en chaîne JSON, guillemets compris
static std::string json_str (const std::string &s)
{
	static const char hex[] = "0123456789abcdef";
	std::string o = "\"";
	for (unsigned char c : s)
	{
		if (c == '"') o += "\\\"";
		else if (c == '\\') o += "\\\\";
		else if (c == '\n') o += "\\n";
		else if (c == '\r') o += "\\r";
		else if (c == '\t') o += "\\t";
		else if (c < 0x20) { o += "\\u00"; o += hex[c >> 4]; o += hex[c & 15]; }
		else o += (char) c;
	}
	return o + "\"";
}

struct Json
{
	enum Kind { NUL, BOOL, NUM, STR, ARR, OBJ } kind = NUL;
	std::string s;				// STR : le texte ; NUM : le nombre écrit
	bool b = false;
	std::vector<Json> items;		// ARR, OBJ
	std::vector<std::string> keys;		// OBJ

	const Json *get (const char *k) const
	{
		if (kind != OBJ) return 0;
		for (size_t i = 0; i < keys.size (); i++) if (keys[i] == k) return &items[i];
		return 0;
	}
	const Json *at (size_t i) const { return kind == ARR && i < items.size () ? &items[i] : 0; }
	std::string text () const { return kind == STR || kind == NUM ? s : kind == BOOL ? (b ? "true" : "false") : ""; }
};

struct JsonReader
{
	const char *p, *end;
	int depth = 0;
	void ws () { while (p < end && (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t')) p++; }
	static void put_utf8 (std::string &o, unsigned u)
	{
		if (u < 0x80) o += (char) u;
		else if (u < 0x800) { o += (char) (0xC0 | (u >> 6)); o += (char) (0x80 | (u & 63)); }
		else if (u < 0x10000) { o += (char) (0xE0 | (u >> 12)); o += (char) (0x80 | ((u >> 6) & 63)); o += (char) (0x80 | (u & 63)); }
		else { o += (char) (0xF0 | (u >> 18)); o += (char) (0x80 | ((u >> 12) & 63)); o += (char) (0x80 | ((u >> 6) & 63)); o += (char) (0x80 | (u & 63)); }
	}
	unsigned hex4 ()
	{
		unsigned u = 0;
		for (int i = 0; i < 4 && p < end; i++, p++)
		{
			char c = *p;
			u = u * 16 + (unsigned) (c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 0);
		}
		return u;
	}
	bool str (std::string &o)
	{
		if (p >= end || *p != '"') return false;
		p++;
		while (p < end && *p != '"')
		{
			if (*p != '\\') { o += *p++; continue; }
			if (++p >= end) return false;
			char c = *p++;
			switch (c)
			{
			case 'n': o += '\n'; break;
			case 'r': o += '\r'; break;
			case 't': o += '\t'; break;
			case 'b': o += '\b'; break;
			case 'f': o += '\f'; break;
			case 'u':
			{
				unsigned u = hex4 ();
				if (u >= 0xD800 && u < 0xDC00 && p + 6 <= end && p[0] == '\\' && p[1] == 'u')
				{
					p += 2;
					unsigned lo = hex4 ();
					u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
				}
				put_utf8 (o, u);
				break;
			}
			default: o += c;
			}
		}
		if (p >= end) return false;
		p++;
		return true;
	}
	bool value (Json &v)
	{
		ws ();
		if (p >= end || ++depth > 64) return false;
		bool ok = true;
		if (*p == '{')
		{
			v.kind = Json::OBJ; p++; ws ();
			if (p < end && *p == '}') p++;
			else for (;;)
			{
				ws ();
				std::string k;
				if (!str (k)) { ok = false; break; }
				ws ();
				if (p >= end || *p != ':') { ok = false; break; }
				p++;
				v.keys.push_back (k);
				v.items.emplace_back ();
				if (!value (v.items.back ())) { ok = false; break; }
				ws ();
				if (p < end && *p == ',') { p++; continue; }
				if (p < end && *p == '}') { p++; break; }
				ok = false; break;
			}
		}
		else if (*p == '[')
		{
			v.kind = Json::ARR; p++; ws ();
			if (p < end && *p == ']') p++;
			else for (;;)
			{
				v.items.emplace_back ();
				if (!value (v.items.back ())) { ok = false; break; }
				ws ();
				if (p < end && *p == ',') { p++; continue; }
				if (p < end && *p == ']') { p++; break; }
				ok = false; break;
			}
		}
		else if (*p == '"') { v.kind = Json::STR; ok = str (v.s); }
		else if (end - p >= 4 && !strncmp (p, "true", 4)) { v.kind = Json::BOOL; v.b = true; p += 4; }
		else if (end - p >= 5 && !strncmp (p, "false", 5)) { v.kind = Json::BOOL; p += 5; }
		else if (end - p >= 4 && !strncmp (p, "null", 4)) { v.kind = Json::NUL; p += 4; }
		else
		{
			const char *b = p;
			while (p < end && (strchr ("+-.eE", *p) || (*p >= '0' && *p <= '9'))) p++;
			v.kind = Json::NUM; v.s.assign (b, p);
			ok = p > b;
		}
		depth--;
		return ok;
	}
};

static bool json_parse (const std::string &text, Json &out)
{
	JsonReader r;
	r.p = text.data (); r.end = r.p + text.size ();
	return r.value (out);
}

} // namespace pq

#endif
