//
// markdown.cpp -- le Markdown du lecteur d'aide (markdown.h) : blocs, puis morceaux de style.
//
#include "markdown.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

namespace md {

// ---- les lignes --------------------------------------------------------------------------------------------------

// espaces de tete (une tabulation compte pour quatre)
static int lead (const std::string &l)
{
	int n = 0;
	for (char c : l)
	{
		if (c == ' ') n++;
		else if (c == '\t') n += 4;
		else break;
	}
	return n;
}
static std::string ltrim (const std::string &l)
{
	size_t i = 0;
	while (i < l.size () && (l[i] == ' ' || l[i] == '\t')) i++;
	return l.substr (i);
}
static std::string trim (const std::string &l)
{
	std::string t = ltrim (l);
	while (!t.empty () && (t.back () == ' ' || t.back () == '\t' || t.back () == '\r')) t.pop_back ();
	return t;
}
// retire au plus n espaces de tete
static std::string unindent (const std::string &l, int n)
{
	size_t i = 0;
	int k = 0;
	while (i < l.size () && k < n && (l[i] == ' ' || l[i] == '\t')) { k += l[i] == '\t' ? 4 : 1; i++; }
	return l.substr (i);
}
static bool is_fence (const std::string &t) { return t.compare (0, 3, "```") == 0 || t.compare (0, 3, "~~~") == 0; }
static bool is_hr (const std::string &t)
{
	if (t.size () < 3) return false;
	char c = t[0];
	if (c != '-' && c != '*' && c != '_') return false;
	int n = 0;
	for (char d : t)
	{
		if (d == c) n++;
		else if (d != ' ') return false;
	}
	return n >= 3;
}
// titre : -> niveau (0 : pas un titre)
static int head_level (const std::string &t)
{
	int n = 0;
	while (n < (int) t.size () && t[n] == '#') n++;
	if (n < 1 || n > 6) return 0;
	if (n < (int) t.size () && t[n] != ' ') return 0;
	return n;
}
// element de liste : -> longueur de sa marque (0 : pas un element), *num = numero (0 : puce)
static int item_mark (const std::string &t, int *num)
{
	*num = 0;
	if (t.size () >= 2 && (t[0] == '-' || t[0] == '*' || t[0] == '+') && t[1] == ' ') return 2;
	size_t i = 0;
	while (i < t.size () && isdigit ((unsigned char) t[i])) i++;
	if (i > 0 && i < 10 && i + 1 < t.size () && (t[i] == '.' || t[i] == ')') && t[i + 1] == ' ')
	{
		*num = atoi (t.substr (0, i).c_str ());
		if (*num == 0) *num = 1;
		return (int) i + 2;
	}
	return 0;
}
static bool is_image_line (const std::string &t, std::string *alt, std::string *src)
{
	if (t.compare (0, 2, "![") != 0) return false;
	size_t e = t.find ("](");
	if (e == std::string::npos || t.back () != ')') return false;
	*alt = t.substr (2, e - 2);
	*src = t.substr (e + 2, t.size () - e - 3);
	size_t sp = src->find (' ');				// (titre facultatif apres la cible)
	if (sp != std::string::npos) src->resize (sp);
	return true;
}
// une ligne qui commence un autre bloc (et termine un paragraphe)
static bool starts_block (const std::string &t)
{
	int num;
	return t.empty () || is_fence (t) || head_level (t) || is_hr (t) || t[0] == '>' || t[0] == '|' || item_mark (t, &num);
}
// cellules d'une ligne de tableau « | a | b | »
static std::vector<std::string> cells (const std::string &t)
{
	std::vector<std::string> v;
	std::string cur;
	size_t i = 0;
	if (i < t.size () && t[i] == '|') i++;
	for (; i < t.size (); i++)
	{
		if (t[i] == '\\' && i + 1 < t.size () && t[i + 1] == '|') { cur += '|'; i++; continue; }
		if (t[i] == '|') { v.push_back (trim (cur)); cur.clear (); continue; }
		cur += t[i];
	}
	if (!trim (cur).empty ()) v.push_back (trim (cur));
	return v;
}
static bool is_table_sep (const std::string &t)
{
	if (t.empty () || t[0] != '|') return false;
	for (char c : t)
		if (c != '|' && c != '-' && c != ':' && c != ' ') return false;
	return t.find ('-') != std::string::npos;
}

// ---- les blocs ---------------------------------------------------------------------------------------------------

std::vector<Block> parse (const std::string &src)
{
	std::vector<std::string> L;
	{
		size_t a = 0;
		while (a <= src.size ())
		{
			size_t b = src.find ('\n', a);
			if (b == std::string::npos) b = src.size ();
			std::string l = src.substr (a, b - a);
			if (!l.empty () && l.back () == '\r') l.pop_back ();
			L.push_back (l);
			a = b + 1;
		}
	}
	std::vector<Block> out;
	int listDepth = -1;					// profondeur du dernier element de liste (-1 : hors liste)
	int listLead[8] = { 0 };				// retrait de la marque a chaque profondeur
	size_t n = L.size (), i = 0;
	while (i < n)
	{
		std::string t = trim (L[i]);
		int ind = lead (L[i]);
		if (t.empty ()) { i++; continue; }
		// (hors retrait : la liste est finie)
		if (listDepth >= 0 && ind < 2)
		{
			int num;
			if (!item_mark (t, &num)) listDepth = -1;
		}
		Block b;
		if (is_fence (t))
		{
			std::string fence = t.substr (0, 3);
			i++;
			while (i < n && trim (L[i]).compare (0, 3, fence) != 0)
			{
				b.code += unindent (L[i], ind);
				b.code += '\n';
				i++;
			}
			i++;
			if (!b.code.empty ()) b.code.pop_back ();
			b.kind = CODE;
			b.indent = listDepth >= 0 && ind >= 2 ? listDepth + 1 : 0;
			out.push_back (b);
			continue;
		}
		if (int h = head_level (t))
		{
			b.kind = HEAD;
			b.level = h;
			b.text = trim (t.substr ((size_t) h));
			while (!b.text.empty () && b.text.back () == '#') b.text.pop_back ();	// (« ## Titre ## »)
			b.text = trim (b.text);
			out.push_back (b);
			listDepth = -1;
			i++;
			continue;
		}
		if (is_hr (t)) { b.kind = HR; out.push_back (b); listDepth = -1; i++; continue; }
		if (t[0] == '>')
		{
			// les lignes « > » qui suivent ; une ligne « > » vide separe deux paragraphes
			while (i < n)
			{
				std::string q = trim (L[i]);
				if (q.empty () || q[0] != '>') break;
				q = trim (q.substr (1));
				if (q.empty ())
				{
					if (!b.text.empty ()) { b.kind = QUOTE; out.push_back (b); b = Block (); }
				}
				else
				{
					if (!b.text.empty () && b.text.back () != '\n') b.text += ' ';
					bool brk = L[i].size () >= 2 && L[i].compare (L[i].size () - 2, 2, "  ") == 0;
					b.text += q;
					if (brk || q.back () == '\\') { if (q.back () == '\\') b.text.pop_back (); b.text += '\n'; }
				}
				i++;
			}
			if (!b.text.empty ()) { b.kind = QUOTE; out.push_back (b); }
			listDepth = -1;
			continue;
		}
		if (t[0] == '|')
		{
			b.kind = TABLE;
			while (i < n)
			{
				std::string r = trim (L[i]);
				if (r.empty () || r[0] != '|') break;
				if (is_table_sep (r)) { if (b.rows.size () == 1) b.header = true; }
				else b.rows.push_back (cells (r));
				i++;
			}
			out.push_back (b);
			listDepth = -1;
			continue;
		}
		std::string alt, img;
		if (is_image_line (t, &alt, &img))
		{
			b.kind = IMAGE;
			b.text = alt;
			b.code = img;
			b.indent = listDepth >= 0 && ind >= 2 ? listDepth + 1 : 0;
			out.push_back (b);
			i++;
			continue;
		}
		int num;
		int mk = item_mark (t, &num);
		if (mk)
		{
			// profondeur : d'apres le retrait de la marque, par rapport aux elements d'avant
			int d = 0;
			if (listDepth >= 0)
			{
				d = listDepth + 1;
				while (d > 0 && ind < listLead[d - 1] + 2) d--;
				if (d > 7) d = 7;
			}
			listLead[d] = ind;
			listDepth = d;
			b.kind = ITEM;
			b.level = d;
			b.ordered = num > 0;
			b.number = num;
			b.text = trim (t.substr ((size_t) mk));
			i++;
			// les lignes qui suivent, sans ligne vide : la suite de l'element
			while (i < n)
			{
				std::string c = trim (L[i]);
				if (c.empty () || starts_block (c)) break;
				bool brk = b.text.size () >= 2 && b.text.compare (b.text.size () - 2, 2, "  ") == 0;
				b.text = trim (b.text);
				b.text += brk ? '\n' : ' ';
				b.text += c;
				i++;
			}
			out.push_back (b);
			continue;
		}
		// un paragraphe : jusqu'a une ligne vide ou un autre bloc
		b.kind = PARA;
		b.indent = listDepth >= 0 && ind >= 2 ? listDepth + 1 : 0;
		if (!b.indent) listDepth = -1;
		while (i < n)
		{
			std::string raw = L[i];
			std::string c = trim (raw);
			if (c.empty ()) break;
			if (!b.text.empty () && starts_block (c)) break;
			bool brk = raw.size () >= 2 && raw.compare (raw.size () - 2, 2, "  ") == 0;
			if (!c.empty () && c.back () == '\\') { c.pop_back (); brk = true; }
			if (!b.text.empty () && b.text.back () != '\n') b.text += ' ';
			b.text += c;
			if (brk) b.text += '\n';
			i++;
		}
		while (!b.text.empty () && b.text.back () == '\n') b.text.pop_back ();
		out.push_back (b);
	}
	return out;
}

// ---- les morceaux de style ---------------------------------------------------------------------------------------

static void push (std::vector<Run> &v, const std::string &t, int style, int link)
{
	if (t.empty ()) return;
	if (!v.empty () && v.back ().style == style && v.back ().link == link && !(style & S_BREAK)) { v.back ().text += t; return; }
	Run r;
	r.text = t;
	r.style = style;
	r.link = link;
	v.push_back (r);
}

static void scan (const std::string &s, size_t a, size_t e, int style, int link, std::vector<Run> &v, std::vector<std::string> &links)
{
	std::string cur;
	auto flush = [&] () { push (v, cur, style, link); cur.clear (); };
	for (size_t i = a; i < e; i++)
	{
		char c = s[i];
		if (c == '\\' && i + 1 < e && ispunct ((unsigned char) s[i + 1])) { cur += s[++i]; continue; }
		if (c == '\n')
		{
			flush ();
			Run r;
			r.style = S_BREAK;
			v.push_back (r);
			continue;
		}
		if (c == '`')
		{
			size_t k = i;
			while (k < e && s[k] == '`') k++;
			std::string tick = s.substr (i, k - i);
			size_t z = s.find (tick, k);
			if (z != std::string::npos && z < e)
			{
				flush ();
				std::string code = s.substr (k, z - k);
				if (code.size () >= 2 && code.front () == ' ' && code.back () == ' ') code = code.substr (1, code.size () - 2);
				push (v, code, style | S_CODE, link);
				i = z + tick.size () - 1;
				continue;
			}
		}
		if (c == '*' && i + 1 < e && s[i + 1] == '*')
		{
			size_t z = s.find ("**", i + 2);
			if (z != std::string::npos && z < e && z > i + 2)
			{
				flush ();
				scan (s, i + 2, z, style | S_BOLD, link, v, links);
				i = z + 1;
				continue;
			}
		}
		if (c == '*' && i + 1 < e && s[i + 1] != ' ' && s[i + 1] != '*')
		{
			size_t z = i + 1;
			while (z < e && !(s[z] == '*' && s[z - 1] != ' ' && (z + 1 >= e || s[z + 1] != '*'))) z++;
			if (z < e)
			{
				flush ();
				scan (s, i + 1, z, style | S_ITALIC, link, v, links);
				i = z;
				continue;
			}
		}
		if (c == '!' && i + 1 < e && s[i + 1] == '[')
		{
			// image au milieu d'un texte : son texte en italique
			size_t z = s.find ("](", i);
			size_t p = z == std::string::npos ? z : s.find (')', z);
			if (z != std::string::npos && p != std::string::npos && p < e)
			{
				flush ();
				push (v, s.substr (i + 2, z - i - 2), style | S_ITALIC, link);
				i = p;
				continue;
			}
		}
		if (c == '[' && link < 0)
		{
			// [texte](cible) : le ] qui ferme, au meme niveau de crochets
			int depth = 0;
			size_t z = i;
			for (; z < e; z++)
			{
				if (s[z] == '\\') { z++; continue; }
				if (s[z] == '[') depth++;
				else if (s[z] == ']' && --depth == 0) break;
			}
			if (z + 1 < e && s[z + 1] == '(')
			{
				size_t p = s.find (')', z + 2);
				if (p != std::string::npos && p < e)
				{
					flush ();
					std::string target = s.substr (z + 2, p - z - 2);
					size_t sp = target.find (' ');
					if (sp != std::string::npos) target.resize (sp);
					links.push_back (target);
					scan (s, i + 1, z, style | S_LINK, (int) links.size () - 1, v, links);
					i = p;
					continue;
				}
			}
		}
		if (c == '<' && link < 0 && (s.compare (i + 1, 7, "http://") == 0 || s.compare (i + 1, 8, "https://") == 0))
		{
			size_t z = s.find ('>', i);
			if (z != std::string::npos && z < e)
			{
				flush ();
				std::string url = s.substr (i + 1, z - i - 1);
				links.push_back (url);
				push (v, url, style | S_LINK, (int) links.size () - 1);
				i = z;
				continue;
			}
		}
		cur += c;
	}
	flush ();
}

std::vector<Run> inlines (const std::string &s, std::vector<std::string> &links)
{
	std::vector<Run> v;
	scan (s, 0, s.size (), 0, -1, v, links);
	return v;
}

std::string plain (const std::string &s)
{
	std::vector<std::string> links;
	std::string o;
	for (const Run &r : inlines (s, links)) o += r.style & S_BREAK ? std::string (" ") : r.text;
	return o;
}

std::string slug (const std::string &s)
{
	std::string p = plain (s), o;
	for (unsigned char c : p)
	{
		if (c >= 0x80) { o += (char) c; continue; }		// (lettres accentuees gardees, comme GitHub)
		if (isalnum (c)) o += (char) tolower (c);
		else if (c == ' ' || c == '-') o += '-';
		else if (c == '_') o += '_';
	}
	return o;
}

}
