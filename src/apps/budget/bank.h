//
// bank.h -- Budget mensuel : la liaison bancaire par Enable Banking (open banking PSD2, accès en lecture), comme
// C:\Projects\gestion\BudgetApp\Bank (EnableBankingClient.cs, BankStore.cs, BankImportForm.ApplyTo) : sans interface,
// pour se construire aussi sur le PC (tools/budget/banktest.cpp, contre le faux serveur des essais).
//
//   banque.json, à côté de budget.json : le même fichier que l'app Windows (ApplicationId, KeyPath, RedirectUrl,
//   BankName, Country, SessionId, ValidUntil, AccountUid, AccountLabel, LastSync, Rules, Seen). Un accès relié sous
//   Windows sert tel quel ici, et inversement ; la clé privée (.pem) se cherche aussi à côté de banque.json quand son
//   chemin (de Windows) n'existe pas ici.
//
//   L'API : https://api.enablebanking.com, un JWT RS256 signé par la clé privée de l'application (OpenSSL), les
//   requêtes HTTPS de PocketQI (../pocketqi/http.h : OpenSSL, certificats du système). Aucune clé, aucun jeton, aucun
//   code n'est écrit nulle part hors de banque.json.
//
//   Avec le coffre à secrets de Korin (bankui.h, le PC) : la clé privée y est importée (Settings::keyPem) et
//   SessionId y est gardé ; bank.h ne connaît que keyPem, pour se construire aussi hors de Korin (banktest).
//
#ifndef BUDGET_BANK_H
#define BUDGET_BANK_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <sys/stat.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <algorithm>
#include <string>
#include <vector>
#include "model.h"
#include "../pocketqi/http.h"

namespace bank {

// ---- les réglages et l'état (banque.json) ----------------------------------------------------------------------
struct Rule { std::string keyword, target; };
struct SeenTx { std::string key; int day; };

struct Settings
{
	std::string appId, keyPath, redirect = "http://localhost:8765/banque", bankName = "BNP Paribas Fortis", country = "BE";
	std::string sessionId, accountUid, accountLabel;	// vides : pas d'accès
	std::string keyPem;					// la clé privée (PEM) du coffre à secrets ; pas dans banque.json
	long long validUntil = 0, lastSync = 0;			// secondes UTC (0 : pas de valeur)
	std::vector<Rule> rules;
	std::vector<SeenTx> seen;				// opérations déjà classées ou ignorées
	bool has_seen (const std::string &k) const { for (auto &s : seen) if (s.key == k) return true; return false; }
	void see (const std::string &k, int day) { for (auto &s : seen) if (s.key == k) { s.day = day; return; } seen.push_back (SeenTx { k, day }); }
};

static bool file_exists (const std::string &p) { struct stat st; return !p.empty () && stat (p.c_str (), &st) == 0 && !S_ISDIR (st.st_mode); }

// le .pem : le chemin noté, sinon le même nom à côté de banque.json (chemin de Windows « C:\…\cle.pem »)
static std::string key_file (const Settings &s, const std::string &dir)
{
	std::string k = s.keyPath;
	if (file_exists (k)) return k;
	size_t sl = k.find_last_of ("/\\");
	std::string name = sl == std::string::npos ? k : k.substr (sl + 1);
	if (name.empty ()) return "";
	std::string near = dir + "/" + name;
	return file_exists (near) ? near : "";
}
static bool configured (const Settings &s, const std::string &dir) { return !s.appId.empty () && (!s.keyPem.empty () || !key_file (s, dir).empty ()); }
static long long utc_now () { return (long long) time (0); }
static bool linked (const Settings &s) { return !s.sessionId.empty () && !s.accountUid.empty () && (s.validUntil == 0 || s.validUntil > utc_now ()); }

// ---- les dates de C# ----------------------------------------------------------------------------------------------
// « 2027-05-09T00:00:00Z », « 2026-11-14T16:18:03.1234567+01:00 », « 2026-11-03T00:00:00 » (heure locale) -> UTC
static long long parse_time (const std::string &t, long tzLocal)
{
	int y, mo, d, h = 0, mi = 0, s = 0;
	if (sscanf (t.c_str (), "%d-%d-%d", &y, &mo, &d) != 3) return 0;
	const char *p = t.c_str () + 10;
	if (*p == 'T' || *p == ' ') sscanf (p + 1, "%d:%d:%d", &h, &mi, &s);
	long long v = (long long) day_number (y, mo, d) * 86400 + h * 3600 + mi * 60 + s;
	// la zone : Z, +hh:mm, -hh:mm ; sans zone : l'heure locale
	const char *z = p;
	while (*z && *z != 'Z' && *z != '+' && !(*z == '-' && z > p)) z++;
	if (*z == 'Z') return v;
	if (*z == '+' || *z == '-')
	{
		int zh = 0, zm = 0; sscanf (z + 1, "%d:%d", &zh, &zm);
		long off = (zh * 60 + zm) * 60;
		return *z == '+' ? v - off : v + off;
	}
	return v - tzLocal;
}
// en heure locale avec sa zone, comme le DateTime.Now que l'app Windows écrit
static std::string format_time (long long utc, long tzLocal)
{
	long long l = utc + tzLocal;
	int y, mo, d; ymd_of ((int) (l / 86400), &y, &mo, &d);
	long sec = (long) (l % 86400);
	long a = tzLocal < 0 ? -tzLocal : tzLocal;
	char b[48];
	snprintf (b, sizeof b, "%04d-%02d-%02dT%02ld:%02ld:%02ld%c%02ld:%02ld", y, mo, d, sec / 3600, sec / 60 % 60, sec % 60,
		tzLocal < 0 ? '-' : '+', a / 3600, a / 60 % 60);
	return b;
}
static std::string format_day (int day) { int y, mo, d; ymd_of (day, &y, &mo, &d); char b[32]; snprintf (b, sizeof b, "%04d-%02d-%02dT00:00:00", y, mo, d); return b; }

static bool parse_settings (const std::string &text, Settings &s, long tz, std::string *err)
{
	s = Settings ();
	JParser jp; jp.p = text.data (); jp.end = text.data () + text.size ();
	if (text.size () >= 3 && (unsigned char) text[0] == 0xEF && (unsigned char) text[1] == 0xBB && (unsigned char) text[2] == 0xBF) jp.p += 3;
	JVal r;
	if (!jp.val (r) || r.type != JVal::OBJ) { if (err) *err = jp.err.empty () ? "objet attendu" : jp.err; return false; }
	auto str = [&] (const char *k, std::string &out) { if (const JVal *v = r.get (k)) if (v->type == JVal::STR) out = v->s; };
	str ("ApplicationId", s.appId); str ("KeyPath", s.keyPath); str ("RedirectUrl", s.redirect);
	str ("BankName", s.bankName); str ("Country", s.country);
	str ("SessionId", s.sessionId); str ("AccountUid", s.accountUid); str ("AccountLabel", s.accountLabel);
	if (const JVal *v = r.get ("ValidUntil")) if (v->type == JVal::STR) s.validUntil = parse_time (v->s, tz);
	if (const JVal *v = r.get ("LastSync")) if (v->type == JVal::STR) s.lastSync = parse_time (v->s, tz);
	if (const JVal *v = r.get ("Rules")) if (v->type == JVal::ARR)
		for (auto &e : v->a) if (e.type == JVal::OBJ) s.rules.push_back (Rule { j_str (e.get ("Keyword")), j_str (e.get ("Target")) });
	if (const JVal *v = r.get ("Seen")) if (v->type == JVal::OBJ)
		for (auto &kv : v->o) s.seen.push_back (SeenTx { kv.first, j_date (&kv.second) });
	return true;
}

static std::string settings_json (const Settings &s, long tz)
{
	JWriter w;
	w.o += '{'; w.ind = 1;
	auto field = [&] (const char *k, const std::string &v, bool first = false) { w.key (k, first); w.str (v); };
	auto opt = [&] (const char *k, const std::string &v) { w.key (k, false); if (v.empty ()) w.o += "null"; else w.str (v); };
	field ("ApplicationId", s.appId, true); field ("KeyPath", s.keyPath); field ("RedirectUrl", s.redirect);
	field ("BankName", s.bankName); field ("Country", s.country);
	opt ("SessionId", s.sessionId);
	opt ("ValidUntil", s.validUntil ? format_time (s.validUntil, tz) : "");
	opt ("AccountUid", s.accountUid); opt ("AccountLabel", s.accountLabel);
	opt ("LastSync", s.lastSync ? format_time (s.lastSync, tz) : "");
	w.key ("Rules", false);
	if (s.rules.empty ()) w.o += "[]";
	else
	{
		w.o += '['; w.ind++;
		for (size_t i = 0; i < s.rules.size (); i++)
		{
			if (i) w.o += ',';
			w.nl (); w.o += '{'; w.ind++;
			field ("Keyword", s.rules[i].keyword, true); field ("Target", s.rules[i].target);
			w.ind--; w.nl (); w.o += '}';
		}
		w.ind--; w.nl (); w.o += ']';
	}
	w.key ("Seen", false);
	if (s.seen.empty ()) w.o += "{}";
	else
	{
		w.o += '{'; w.ind++;
		for (size_t i = 0; i < s.seen.size (); i++) { w.key (s.seen[i].key.c_str (), i == 0); w.str (format_day (s.seen[i].day)); }
		w.ind--; w.nl (); w.o += '}';
	}
	w.ind--; w.nl (); w.o += '}';
	return w.o;
}

static bool read_file (const std::string &p, std::string &out)
{
	FILE *f = fopen (p.c_str (), "rb");
	if (!f) return false;
	out.clear (); char b[4096]; size_t n;
	while ((n = fread (b, 1, sizeof b, f)) > 0) out.append (b, n);
	fclose (f);
	return true;
}

// -> true ; un fichier absent donne les réglages par défaut
static bool load (const std::string &path, Settings &s, long tz, std::string *err)
{
	std::string t;
	if (!read_file (path, t)) { s = Settings (); return true; }
	return parse_settings (t, s, tz, err);
}
// écrit dans un .tmp puis renomme ; oublie les opérations traitées il y a plus de 400 jours
static bool save (const std::string &path, Settings &s, long tz, int today)
{
	s.seen.erase (std::remove_if (s.seen.begin (), s.seen.end (), [&] (const SeenTx &x) { return x.day != NODATE && x.day < today - 400; }), s.seen.end ());
	std::string j = settings_json (s, tz), tmp = path + ".tmp";
	FILE *f = fopen (tmp.c_str (), "wb");
	if (!f) return false;
	bool ok = fwrite (j.data (), 1, j.size (), f) == j.size ();
	ok = fclose (f) == 0 && ok;
	if (ok) { remove (path.c_str ()); ok = rename (tmp.c_str (), path.c_str ()) == 0; }
	return ok;
}

// ---- les opérations -----------------------------------------------------------------------------------------------
struct Tx
{
	std::string key;		// « id:… » ou « h:… » : la même clé que l'app Windows
	int day = NODATE;
	Money amount = 0;		// signé : négatif pour une sortie d'argent
	std::string counterparty, remittance;
	bool booked = true;
	std::string display () const { return counterparty.empty () ? remittance : remittance.empty () ? counterparty : counterparty + " \xE2\x80\x94 " + remittance; }
};

// ---- la reconnaissance (Classifier de BankStore.cs) ---------------------------------------------------------------
static const char *const IGNORE = "Ignorer";
static const char *const LATER = "(\xC3\xA0 classer plus tard)";
static const char *const LINE_PREFIX = "Poste fixe : ";
static const char *const CAT_PREFIX = "D\xC3\xA9pense : ";

// une lettre de Latin-1 sans accent, en majuscule (0 : pas une lettre)
static const char *latin1_base (unsigned cp)
{
	static const char *const t[64] = {
		"A", "A", "A", "A", "A", "A", "AE", "C", "E", "E", "E", "E", "I", "I", "I", "I",
		"D", "N", "O", "O", "O", "O", "O", 0, "O", "U", "U", "U", "U", "Y", "TH", "SS",
		"A", "A", "A", "A", "A", "A", "AE", "C", "E", "E", "E", "E", "I", "I", "I", "I",
		"D", "N", "O", "O", "O", "O", "O", 0, "O", "U", "U", "U", "U", "Y", "TH", "Y" };
	return cp >= 0xC0 && cp <= 0xFF ? t[cp - 0xC0] : 0;
}
static unsigned next_cp (const std::string &s, size_t &i)
{
	unsigned char c = (unsigned char) s[i++];
	if (c < 0x80) return c;
	int n = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
	unsigned cp = c & (0x3F >> n);
	while (n-- > 0 && i < s.size () && ((unsigned char) s[i] & 0xC0) == 0x80) cp = cp << 6 | ((unsigned char) s[i++] & 63);
	return cp;
}
// majuscules sans accents, tout ce qui n'est ni lettre ni chiffre en espace, puis seulement les mots qui ont une lettre
// (« Q8 » reste, les dates, numéros de carte et références partent)
static std::string normalize (const std::string &s)
{
	std::string o;
	for (size_t i = 0; i < s.size (); )
	{
		unsigned cp = next_cp (s, i);
		if (cp < 0x80) { o += isalnum ((int) cp) ? (char) toupper ((int) cp) : ' '; continue; }
		if (const char *b = latin1_base (cp)) { o += b; continue; }
		if (cp == 0x152 || cp == 0x153) { o += "OE"; continue; }
		// au-delà de Latin-1 : gardé tel quel s'il ne s'agit pas d'une ponctuation ou d'un symbole courant
		if (cp < 0xC0 || (cp >= 0x2000 && cp < 0x2BFF) || cp == 0xFEFF) { o += ' '; continue; }
		char b[5]; int n = 0;
		if (cp < 0x800) { b[n++] = (char) (0xC0 | cp >> 6); b[n++] = (char) (0x80 | (cp & 63)); }
		else if (cp < 0x10000) { b[n++] = (char) (0xE0 | cp >> 12); b[n++] = (char) (0x80 | (cp >> 6 & 63)); b[n++] = (char) (0x80 | (cp & 63)); }
		else { b[n++] = (char) (0xF0 | cp >> 18); b[n++] = (char) (0x80 | (cp >> 12 & 63)); b[n++] = (char) (0x80 | (cp >> 6 & 63)); b[n++] = (char) (0x80 | (cp & 63)); }
		o.append (b, (size_t) n);
	}
	std::string r;
	size_t i = 0;
	while (i < o.size ())
	{
		while (i < o.size () && o[i] == ' ') i++;
		size_t j = i;
		while (j < o.size () && o[j] != ' ') j++;
		if (j > i)
		{
			bool letter = false;
			for (size_t k = i; k < j; k++) if (!isdigit ((unsigned char) o[k])) letter = true;
			if (letter) { if (!r.empty ()) r += ' '; r.append (o, i, j - i); }
		}
		i = j;
	}
	return r;
}
static std::vector<std::string> words (const std::string &norm)
{
	std::vector<std::string> w;
	size_t i = 0;
	while (i < norm.size ()) { size_t j = norm.find (' ', i); if (j == std::string::npos) j = norm.size (); if (j > i) w.push_back (norm.substr (i, j - i)); i = j + 1; }
	return w;
}
static std::string match_text (const Tx &t) { return normalize (t.counterparty + " " + t.remittance); }

// les mots trop généraux pour reconnaître un commerçant (paiements par carte, domiciliations, formes juridiques)
static bool noise (const std::string &w)
{
	static const char *const N[] = {
		"PAIEMENT", "PAIEMENTS", "AVEC", "LA", "LE", "LES", "DE", "DU", "DES", "EN", "ET", "AU", "A", "CARTE", "DEBIT", "CREDIT",
		"BANCONTACT", "MAESTRO", "VISA", "MASTERCARD", "DOMICILIATION", "VIREMENT", "FAVEUR", "EUROPEEN", "INSTANTANE",
		"NUMERO", "REFERENCE", "REF", "DATE", "VALEUR", "COMMUNICATION", "BE", "EUR", "SEPA", "ORDRE", "PERMANENT",
		"RETRAIT", "ACHAT", "TERMINAL", "VIA", "MOBILE", "APP", "PAYCONIQ", "PAR", "POUR", "SUR", "CHEZ", "BELGIUM", "BELGIQUE",
		"SA", "NV", "SRL", "SPRL", "BV", "BVBA", "SC", "SCRL", "CV", "ASBL", "VZW", "SAS", "SARL", "GMBH", "LTD", "INC", "EU" };
	for (auto n : N) if (w == n) return true;
	return false;
}

// le mot-clé proposé pour retenir une opération : le nom du commerçant, sans les mots de paiement
static std::string suggest_keyword (const Tx &t)
{
	for (const std::string *src : { &t.counterparty, &t.remittance })
	{
		std::vector<std::string> ws;
		for (auto &w : words (normalize (*src))) if (w.size () > 1 && !noise (w)) { ws.push_back (w); if (ws.size () == 2) break; }
		// un nom d'enseigne assez long suffit seul (COLRUYT vaut pour tous les magasins Colruyt)
		if (!ws.empty ()) return ws[0].size () >= 5 || ws.size () == 1 ? ws[0] : ws[0] + " " + ws[1];
	}
	return "";
}

static bool contains_kw (const std::string &normText, const std::string &keyword)
{
	std::string k = normalize (keyword);
	return !k.empty () && (" " + normText + " ").find (" " + k + " ") != std::string::npos;
}

static std::string trim (const std::string &s) { size_t a = s.find_first_not_of (" \t\r\n"), b = s.find_last_not_of (" \t\r\n"); return a == std::string::npos ? "" : s.substr (a, b - a + 1); }

// les destinations d'un mois : ses postes fixes, ses colonnes de dépenses, Ignorer, à classer plus tard
static std::vector<std::string> targets (const Month &m)
{
	std::vector<std::string> r;
	for (auto &l : m.lines)
	{
		std::string lb = trim (l.label);
		if (lb.empty ()) continue;
		bool dup = false;
		for (auto &x : r) if (!strcasecmp (x.c_str () + strlen (LINE_PREFIX), lb.c_str ())) dup = true;
		if (!dup) r.push_back (LINE_PREFIX + lb);
	}
	for (auto &c : m.categories) r.push_back (CAT_PREFIX + c);
	r.push_back (IGNORE);
	r.push_back (LATER);
	return r;
}
static bool in_list (const std::vector<std::string> &v, const std::string &s) { return std::find (v.begin (), v.end (), s) != v.end (); }

// la destination proposée pour une opération, et si elle vient d'une règle retenue
static std::string suggest (const Tx &t, const std::vector<std::string> &tg, const std::vector<Rule> &rules, const Month &m, bool *fromRule)
{
	std::string text = match_text (t);
	std::vector<const Rule *> rs;
	for (auto &r : rules) rs.push_back (&r);
	std::stable_sort (rs.begin (), rs.end (), [] (const Rule *a, const Rule *b) { return normalize (a->keyword).size () > normalize (b->keyword).size (); });
	for (auto r : rs) if (contains_kw (text, r->keyword) && in_list (tg, r->target)) { *fromRule = true; return r->target; }
	*fromRule = false;
	// pas de règle : un poste fixe dont le libellé apparaît dans l'opération (ex. « Orange », « Luminus »)
	std::vector<const Line *> ls;
	for (auto &l : m.lines) if (normalize (l.label).size () >= 4) ls.push_back (&l);
	std::stable_sort (ls.begin (), ls.end (), [] (const Line *a, const Line *b) { return a->label.size () > b->label.size (); });
	for (auto l : ls)
	{
		bool sameSide = t.amount < 0 ? nz (l->expense) > 0 : nz (l->income) > 0;
		std::string target = LINE_PREFIX + l->label;
		if (sameSide && contains_kw (text, l->label) && in_list (tg, target)) return target;
	}
	return LATER;
}

// un choix de la fenêtre de classement
struct Choice { Tx tx; std::string target, keyword; bool remember = false; };

static std::string shorten (const std::string &s)		// 60 caractères au plus
{
	size_t i = 0; int n = 0;
	while (i < s.size () && n < 60) { next_cp (s, i); n++; }
	return s.substr (0, i);
}

// range les choix dans le mois et retient les règles -> le résumé
static std::string apply (Month &m, const std::vector<Choice> &choices, Settings &s)
{
	int toLines = 0, toSpend = 0, ignored = 0, later = 0, rules = 0;
	size_t lp = strlen (LINE_PREFIX), cp = strlen (CAT_PREFIX);
	for (auto &c : choices)
	{
		if (c.target == LATER) { later++; continue; }
		if (c.target.compare (0, lp, LINE_PREFIX) == 0)
		{
			std::string label = c.target.substr (lp);
			std::vector<Line *> same;
			for (auto &l : m.lines) if (!strcasecmp (trim (l.label).c_str (), label.c_str ())) same.push_back (&l);
			// le signe selon le poste : une dépense est payée par une sortie (+) et remboursée par une entrée (−) ;
			// un revenu est reçu par une entrée (+), une sortie vient en déduction (−)
			auto isIncome = [] (const Line *l) { return nz (l->income) > 0 && nz (l->expense) == 0; };
			auto contribution = [&] (const Line *l, Money a) { return isIncome (l) ? a : -a; };
			// plusieurs lignes de même nom : un paiement va sur la première pas encore réglée, une déduction sur la
			// première déjà entamée
			bool adds = !same.empty () && contribution (same[0], c.tx.amount) > 0;
			Line *line = 0;
			if (adds) { for (auto l : same) if (l->remaining () > 0) { line = l; break; } }
			else { for (auto l : same) if (nz (l->paid) > 0) { line = l; break; } }
			if (!line && !same.empty ()) line = same[0];
			if (!line) { later++; continue; }
			bool wasFull = line->planned () > 0 && nz (line->paid) >= line->planned ();
			line->paid = nz (line->paid) + contribution (line, c.tx.amount);
			if (line->paid == 0) line->paid = NUL;
			if (line->planned () > 0 && nz (line->paid) >= line->planned ()) line->done = true;
			else if (wasFull && line->done) line->done = false;		// un remboursement rouvre la ligne
			toLines++;
		}
		else if (c.target.compare (0, cp, CAT_PREFIX) == 0)
		{
			// une sortie d'argent est une dépense ; une entrée (remboursement) vient en déduction
			Spending sp;
			sp.date = c.tx.day;
			sp.label = shorten (!c.tx.counterparty.empty () ? c.tx.counterparty : c.tx.remittance);
			sp.set (c.target.substr (cp), -c.tx.amount);
			m.spendings.push_back (sp);
			toSpend++;
		}
		else ignored++;

		s.see (c.tx.key, c.tx.day);
		std::string kw = trim (c.keyword);
		if (c.remember && !kw.empty ())
		{
			std::string nk = normalize (kw);
			s.rules.erase (std::remove_if (s.rules.begin (), s.rules.end (), [&] (const Rule &r) { return normalize (r.keyword) == nk; }), s.rules.end ());
			s.rules.push_back (Rule { kw, c.target });
			rules++;
		}
	}
	std::string r;
	auto part = [&] (int n, const char *what) { if (n <= 0) return; char b[120]; snprintf (b, sizeof b, what, n); if (!r.empty ()) r += ", "; r += b; };
	part (toLines, "%d paiement(s) de postes fixes");
	part (toSpend, "%d d\xC3\xA9pense(s) courante(s)");
	part (ignored, "%d ignor\xC3\xA9" "e(s)");
	part (later, "%d \xC3\xA0 classer plus tard");
	part (rules, "%d r\xC3\xA8gle(s) retenue(s)");
	return r.empty () ? "rien de nouveau" : r;
}

// ---- le client d'Enable Banking (EnableBankingClient.cs) -----------------------------------------------------------
static std::string b64url (const unsigned char *p, size_t n)
{
	std::string o((n + 2) / 3 * 4 + 1, '\0');
	int k = EVP_EncodeBlock ((unsigned char *) &o[0], p, (int) n);
	o.resize ((size_t) k);
	while (!o.empty () && o.back () == '=') o.pop_back ();
	for (auto &ch : o) { if (ch == '+') ch = '-'; else if (ch == '/') ch = '_'; }
	return o;
}
static std::string url_escape (const std::string &s)
{
	static const char hex[] = "0123456789ABCDEF";
	std::string o;
	for (unsigned char c : s)
	{
		if (isalnum (c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char) c;
		else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
	}
	return o;
}
static std::string url_unescape (const std::string &s)
{
	std::string o;
	for (size_t i = 0; i < s.size (); i++)
	{
		if (s[i] == '+') o += ' ';
		else if (s[i] == '%' && i + 2 < s.size () && isxdigit ((unsigned char) s[i + 1]) && isxdigit ((unsigned char) s[i + 2]))
		{ o += (char) strtol (s.substr (i + 1, 2).c_str (), 0, 16); i += 2; }
		else o += s[i];
	}
	return o;
}
// un paramètre d'une adresse (ou de sa seule partie après « ? »)
static bool query_param (const std::string &url, const char *name, std::string &out)
{
	size_t q = url.find ('?');
	std::string qs = q == std::string::npos ? url : url.substr (q + 1);
	size_t h = qs.find ('#'); if (h != std::string::npos) qs.resize (h);
	size_t i = 0;
	while (i <= qs.size ())
	{
		size_t a = qs.find ('&', i); if (a == std::string::npos) a = qs.size ();
		std::string kv = qs.substr (i, a - i);
		size_t e = kv.find ('=');
		if (url_unescape (kv.substr (0, e)) == name) { out = e == std::string::npos ? "" : url_unescape (kv.substr (e + 1)); return true; }
		i = a + 1;
	}
	return false;
}

static std::string q (const std::string &s) { JWriter w; w.str (s); return w.o; }

struct Account { std::string uid, label; };
struct Session { std::string id; long long validUntil = 0; std::vector<Account> accounts; };

struct Client
{
	std::string appId, base, err;
	EVP_PKEY *key = 0;
	long tz = 0;
	~Client () { if (key) EVP_PKEY_free (key); }

	// api : l'adresse de l'API (vide : https://api.enablebanking.com ; un autre : le faux serveur des essais)
	bool open (const Settings &s, const std::string &dir, const std::string &api)
	{
		if (!configured (s, dir)) { err = "La liaison bancaire n'est pas configur\xC3\xA9" "e (identifiant d'application et cl\xC3\xA9 priv\xC3\xA9" "e)."; return false; }
		appId = trim (s.appId);
		base = api.empty () ? "https://api.enablebanking.com" : api;
		while (!base.empty () && base.back () == '/') base.pop_back ();
		// le .pem s'il est là (celui que les réglages nomment, peut-être nouveau), sinon la copie du coffre
		std::string kf = key_file (s, dir);
		FILE *f = kf.empty () ? 0 : fopen (kf.c_str (), "rb");
		if (f) { key = PEM_read_PrivateKey (f, 0, 0, 0); fclose (f); }
		else if (!s.keyPem.empty ())
		{
			BIO *bio = BIO_new_mem_buf (s.keyPem.data (), (int) s.keyPem.size ());
			if (bio) { key = PEM_read_bio_PrivateKey (bio, 0, 0, 0); BIO_free (bio); }
			if (!kf.size ()) kf = "coffre \xC3\xA0 secrets";
		}
		if (!key) { err = "Cl\xC3\xA9 priv\xC3\xA9" "e illisible (" + kf + ")."; return false; }
		return true;
	}

	std::string jwt ()
	{
		long long now = utc_now ();
		std::string h = "{\"typ\":\"JWT\",\"alg\":\"RS256\",\"kid\":" + q (appId) + "}";
		char p[160]; snprintf (p, sizeof p, "{\"iss\":\"enablebanking.com\",\"aud\":\"api.enablebanking.com\",\"iat\":%lld,\"exp\":%lld}", now, now + 3600);
		std::string signedPart = b64url ((const unsigned char *) h.data (), h.size ()) + "." + b64url ((const unsigned char *) p, strlen (p));
		std::string out;
		EVP_MD_CTX *md = EVP_MD_CTX_new ();
		size_t n = 0;
		if (md && EVP_DigestSignInit (md, 0, EVP_sha256 (), 0, key) == 1 &&
		    EVP_DigestSign (md, 0, &n, (const unsigned char *) signedPart.data (), signedPart.size ()) == 1)
		{
			std::vector<unsigned char> sig (n);
			if (EVP_DigestSign (md, sig.data (), &n, (const unsigned char *) signedPart.data (), signedPart.size ()) == 1)
				out = signedPart + "." + b64url (sig.data (), n);
		}
		EVP_MD_CTX_free (md);
		return out;
	}

	// GET (body vide) ou POST -> la réponse lue, sinon false et err
	bool send (bool get, const std::string &path, const std::string &body, JVal &out)
	{
		std::string tok = jwt ();
		if (tok.empty ()) { err = "signature du jeton impossible (cl\xC3\xA9 priv\xC3\xA9" "e RSA attendue)"; return false; }
		std::string resp, e;
		int code = pq::http_request (get, base + path, "Authorization: Bearer " + tok + "\r\n", body, resp, e, 0, 60000);
		std::fill (tok.begin (), tok.end (), 0);
		if (code == 0) { err = "Enable Banking ne r\xC3\xA9pond pas : " + e; return false; }
		JParser jp; jp.p = resp.data (); jp.end = resp.data () + resp.size ();
		JVal v;
		bool parsed = resp.empty () || jp.val (v);
		if (code < 200 || code > 299)
		{
			std::string detail = resp;
			if (parsed && v.type == JVal::OBJ)
				for (const char *k : { "message", "detail", "error" }) if (const JVal *d = v.get (k)) if (d->type == JVal::STR) { detail = d->s; break; }
			char b[48]; snprintf (b, sizeof b, " (%d) : ", code);
			err = std::string ("Enable Banking a refus\xC3\xA9 la demande") + b + detail;
			return false;
		}
		if (!parsed) { err = "r\xC3\xA9ponse illisible d'Enable Banking"; return false; }
		out = v;
		return true;
	}

	static std::string text (const JVal *v) { return !v ? "" : v->type == JVal::STR || v->type == JVal::NUM ? v->s : ""; }

	// la banque dans la liste d'Enable Banking : son nom exact et la durée d'accès permise (secondes)
	bool find_bank (const std::string &name, const std::string &country, std::string &exact, long long &maxAccess)
	{
		JVal j;
		if (!send (true, "/aspsps?country=" + url_escape (country), "", j)) return false;
		const JVal *list = j.get ("aspsps");
		std::string wanted = normalize (name);
		const JVal *hit = 0;
		if (list && list->type == JVal::ARR)
		{
			for (auto &a : list->a) if (normalize (text (a.get ("name"))) == wanted) { hit = &a; break; }
			if (!hit) for (auto &a : list->a) if (normalize (text (a.get ("name"))).find (wanted) != std::string::npos) { hit = &a; break; }
		}
		if (!hit)
		{
			std::string names;
			int n = 0;
			if (list && list->type == JVal::ARR) for (auto &a : list->a) { std::string t = text (a.get ("name")); if (t.empty () || n++ >= 40) continue; if (!names.empty ()) names += ", "; names += t; }
			err = "Banque \xC2\xAB " + name + " \xC2\xBB introuvable pour le pays " + country + ". Banques disponibles : " + names;
			return false;
		}
		exact = text (hit->get ("name"));
		// la durée annoncée par la banque (180 jours au plus), moins une heure de marge (horloges)
		long long max = 90LL * 86400;
		if (const JVal *mv = hit->get ("maximum_consent_validity")) if (mv->type == JVal::NUM) { long long sec = atoll (mv->s.c_str ()); if (sec > 0) max = std::min (sec, 180LL * 86400); }
		max -= 3600;
		if (max < 3600) max = 3600;
		maxAccess = max;
		return true;
	}

	// démarre l'autorisation -> l'adresse de la page de la banque
	bool start_auth (const std::string &bankName, const std::string &country, long long validUntil, const std::string &redirect, const std::string &state, std::string &url)
	{
		int y, mo, d; ymd_of ((int) (validUntil / 86400), &y, &mo, &d);
		long sec = (long) (validUntil % 86400);
		char vu[40]; snprintf (vu, sizeof vu, "%04d-%02d-%02dT%02ld:%02ld:%02ld.000Z", y, mo, d, sec / 3600, sec / 60 % 60, sec % 60);
		std::string body = "{\"access\":{\"valid_until\":" + q (vu) + "},\"aspsp\":{\"name\":" + q (bankName) + ",\"country\":" + q (country) +
			"},\"state\":" + q (state) + ",\"redirect_url\":" + q (redirect) + ",\"psu_type\":\"personal\"}";
		JVal j;
		if (!send (false, "/auth", body, j)) return false;
		url = text (j.get ("url"));
		if (url.empty ()) { err = "R\xC3\xA9ponse inattendue : pas d'adresse d'autorisation."; return false; }
		return true;
	}

	// le code du retour de la banque contre une session et ses comptes
	bool create_session (const std::string &code, Session &s)
	{
		JVal j;
		if (!send (false, "/sessions", "{\"code\":" + q (code) + "}", j)) return false;
		s = Session ();
		s.id = text (j.get ("session_id"));
		if (const JVal *a = j.get ("access")) if (const JVal *v = a->get ("valid_until")) if (v->type == JVal::STR) s.validUntil = parse_time (v->s, tz);
		if (const JVal *accs = j.get ("accounts")) if (accs->type == JVal::ARR)
			for (auto &a : accs->a)
			{
				std::string uid = text (a.get ("uid"));
				if (uid.empty ()) continue;
				std::string iban;
				if (const JVal *ai = a.get ("account_id")) iban = text (ai->get ("iban"));
				std::string name = text (a.get ("name"));
				if (name.empty ()) name = text (a.get ("product"));
				std::string label = trim (iban);
				if (!trim (name).empty ()) label += (label.empty () ? "" : " \xE2\x80\x94 ") + name;
				s.accounts.push_back (Account { uid, label.empty () ? uid : label });
			}
		return true;
	}

	// le solde : disponible ou attendu si la banque le donne, sinon comptable
	bool balance (const std::string &uid, Money &amount)
	{
		JVal j;
		if (!send (true, "/accounts/" + url_escape (uid) + "/balances", "", j)) return false;
		static const char *const order[] = { "ITAV", "CLAV", "XPCD", "ITBD", "CLBD", "OPAV", "OPBD" };
		const JVal *best = 0; int rank = 1000;
		if (const JVal *list = j.get ("balances")) if (list->type == JVal::ARR)
			for (auto &b : list->a)
			{
				std::string t = text (b.get ("balance_type"));
				int r = 99;
				for (int i = 0; i < 7; i++) if (t == order[i]) r = i;
				if (r < rank) { rank = r; best = &b; }
			}
		if (!best) { err = "La banque n'a renvoy\xC3\xA9 aucun solde."; return false; }
		std::string a; if (const JVal *ba = best->get ("balance_amount")) a = text (ba->get ("amount"));
		if (!exact_decimal (a.c_str (), &amount)) amount = mof (strtod (a.c_str (), 0));
		return true;
	}

	static int day_of (const JVal *v)
	{
		if (!v || v->type != JVal::STR) return NODATE;
		int y, m, d;
		return sscanf (v->s.c_str (), "%d-%d-%d", &y, &m, &d) == 3 ? day_number (y, m, d) : NODATE;
	}
	static Tx parse_tx (const JVal &t, int today)
	{
		Tx x;
		std::string raw; if (const JVal *ta = t.get ("transaction_amount")) raw = text (ta->get ("amount"));
		std::string absText = raw;
		while (!absText.empty () && (absText[0] == '-' || absText[0] == '+')) absText.erase (0, 1);
		Money a = 0; if (!exact_decimal (absText.c_str (), &a)) a = mof (strtod (absText.c_str (), 0));
		std::string ind = text (t.get ("credit_debit_indicator"));
		bool debit = ind.empty () || ind == "DBIT";
		const JVal *party = t.get (debit ? "creditor" : "debtor");
		x.counterparty = trim (party ? text (party->get ("name")) : "");
		std::string rem;
		if (const JVal *ri = t.get ("remittance_information")) if (ri->type == JVal::ARR)
			for (auto &r : ri->a) if (r.type == JVal::STR && !trim (r.s).empty ()) { if (!rem.empty ()) rem += ' '; rem += r.s; }
		if (rem.empty ()) rem = text (t.get ("note"));
		x.remittance = trim (rem);
		x.day = day_of (t.get ("booking_date"));
		if (x.day == NODATE) x.day = day_of (t.get ("value_date"));
		if (x.day == NODATE) x.day = day_of (t.get ("transaction_date"));
		if (x.day == NODATE) x.day = today;
		x.amount = debit ? -a : a;
		std::string st = text (t.get ("status"));
		x.booked = st.empty () || st == "BOOK";
		// la clé : l'identifiant de la banque, sinon une empreinte (comme l'app Windows : même texte, mêmes octets)
		std::string id = text (t.get ("transaction_id"));
		if (id.empty ()) id = text (t.get ("entry_reference"));
		if (!id.empty ()) x.key = "id:" + id;
		else
		{
			int y, m, d; ymd_of (x.day, &y, &m, &d);
			char ds[16]; snprintf (ds, sizeof ds, "%04d-%02d-%02d", y, m, d);
			std::string amt = (debit && !absText.empty () ? "-" : "") + absText;
			std::string h = std::string (ds) + "|" + amt + "|" + x.counterparty + "|" + x.remittance;
			unsigned char dg[SHA256_DIGEST_LENGTH];
			SHA256 ((const unsigned char *) h.data (), h.size (), dg);
			static const char hex[] = "0123456789ABCDEF";
			std::string hx;
			for (int i = 0; i < 12; i++) { hx += hex[dg[i] >> 4]; hx += hex[dg[i] & 15]; }
			x.key = "h:" + hx;
		}
		return x;
	}

	// les opérations depuis un jour (toutes les pages)
	bool transactions (const std::string &uid, int fromDay, int today, std::vector<Tx> &out)
	{
		out.clear ();
		std::string cont;
		int y, m, d; ymd_of (fromDay, &y, &m, &d);
		char from[16]; snprintf (from, sizeof from, "%04d-%02d-%02d", y, m, d);
		for (int page = 0; page < 50; page++)
		{
			std::string path = "/accounts/" + url_escape (uid) + "/transactions?date_from=" + from;
			if (!cont.empty ()) path += "&continuation_key=" + url_escape (cont);
			JVal j;
			if (!send (true, path, "", j)) return false;
			if (const JVal *ts = j.get ("transactions")) if (ts->type == JVal::ARR) for (auto &t : ts->a) if (t.type == JVal::OBJ) out.push_back (parse_tx (t, today));
			cont = text (j.get ("continuation_key"));
			if (cont.empty ()) break;
		}
		return true;
	}
};

// un « state » neuf pour une autorisation (32 chiffres hexadécimaux, comme un Guid « N »)
static std::string new_state ()
{
	unsigned char r[16];
	if (RAND_bytes (r, sizeof r) != 1) for (auto &c : r) c = (unsigned char) rand ();
	static const char hex[] = "0123456789abcdef";
	std::string s;
	for (unsigned char c : r) { s += hex[c >> 4]; s += hex[c & 15]; }
	return s;
}

// l'adresse du retour de la banque (collée, ou reçue par l'écoute locale) -> le code, sinon le message
static bool code_from (const std::string &pasted, const std::string &state, std::string &code, std::string &msg)
{
	std::string t = trim (pasted), e, st;
	if (query_param (t, "error", e))
	{
		std::string d; if (!query_param (t, "error_description", d)) d = e;
		msg = "La banque a refus\xC3\xA9 ou l'acc\xC3\xA8s a \xC3\xA9t\xC3\xA9 annul\xC3\xA9 : " + d;
		return false;
	}
	if (!query_param (t, "code", code) || code.empty ()) { msg = "Je ne trouve pas \xC2\xAB code= \xC2\xBB dans cette adresse."; return false; }
	if (query_param (t, "state", st) && st != state) { msg = "Cette adresse ne correspond pas \xC3\xA0 la demande en cours (state diff\xC3\xA9rent). Recommencez."; return false; }
	return true;
}

// l'écoute de l'adresse de retour http://localhost:port/… : le navigateur de cette machine y revient tout seul
struct Listener
{
	int fd = -1;
	~Listener () { stop (); }
	void stop () { if (fd >= 0) close (fd); fd = -1; }
	bool start (const std::string &redirect)
	{
		pq::Url u;
		if (!u.parse (redirect) || u.https || (u.host != "localhost" && u.host != "127.0.0.1")) return false;
		fd = socket (AF_INET, SOCK_STREAM, 0);
		if (fd < 0) return false;
		int one = 1; setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
		struct sockaddr_in a; memset (&a, 0, sizeof a);
		a.sin_family = AF_INET; a.sin_port = htons ((unsigned short) u.port); a.sin_addr.s_addr = htonl (0x7F000001);
		if (bind (fd, (struct sockaddr *) &a, sizeof a) != 0 || listen (fd, 4) != 0) { stop (); return false; }
		fcntl (fd, F_SETFL, fcntl (fd, F_GETFL, 0) | O_NONBLOCK);
		return true;
	}
	// sans attendre : une requête arrivée -> son adresse (chemin et paramètres), sinon vide
	std::string poll_once ()
	{
		if (fd < 0) return "";
		int c = accept (fd, 0, 0);
		if (c < 0) return "";
		std::string req; char b[2048];
		unsigned t0 = pq::now_ms ();
		while (req.find ("\r\n") == std::string::npos && pq::now_ms () - t0 < 2000 && req.size () < 16384)
		{
			struct pollfd p; p.fd = c; p.events = POLLIN; p.revents = 0;
			if (poll (&p, 1, 100) <= 0) continue;
			ssize_t n = recv (c, b, sizeof b, 0);
			if (n <= 0) break;
			req.append (b, (size_t) n);
		}
		std::string path;
		size_t sp = req.find (' ');
		if (sp != std::string::npos) { size_t e = req.find (' ', sp + 1); path = req.substr (sp + 1, e == std::string::npos ? std::string::npos : e - sp - 1); }
		std::string dummy;
		bool hasCode = query_param (path, "code", dummy) || query_param (path, "error", dummy);
		std::string html = hasCode
			? "<html><meta charset='utf-8'><body style='font-family:sans-serif;padding:40px'><h2>C'est fait.</h2><p>Vous pouvez fermer cette page et revenir \xC3\xA0 Budget.</p></body></html>"
			: "<html><body></body></html>";
		std::string resp = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: " + std::to_string (html.size ()) + "\r\nConnection: close\r\n\r\n" + html;
		::send (c, resp.data (), resp.size (), 0);
		close (c);
		return hasCode ? path : "";
	}
};

// essais : suit l'adresse d'autorisation du faux serveur sans navigateur -> l'en-tête Location (l'adresse de retour)
static std::string follow_once (const std::string &url)
{
	pq::Url u;
	if (!u.parse (url)) return "";
	pq::Conn c;
	std::string err;
	if (!pq::conn_open (c, u, 10000, err)) return "";
	std::string req = "GET " + u.path + " HTTP/1.1\r\nHost: " + u.host + "\r\nConnection: close\r\n\r\n";
	if (!pq::conn_send (c, req.data (), req.size (), 10000)) return "";
	std::string all; char b[4096];
	unsigned t0 = pq::now_ms ();
	for (;;)
	{
		int r = pq::conn_recv (c, b, sizeof b);
		if (r < 0) break;
		if (r > 0) { all.append (b, (size_t) r); if (all.find ("\r\n\r\n") != std::string::npos) break; }
		else if (pq::now_ms () - t0 > 10000) break;
	}
	std::string low = all;
	for (auto &ch : low) ch = (char) tolower ((unsigned char) ch);
	size_t l = low.find ("\r\nlocation:");
	if (l == std::string::npos) return "";
	size_t s = l + 11, e = all.find ("\r\n", s);
	return trim (all.substr (s, e == std::string::npos ? std::string::npos : e - s));
}

} // namespace bank

#endif
