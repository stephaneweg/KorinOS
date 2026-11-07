//
// budget/model.h -- le modele de Budget mensuel, port de l'app Windows (C:\Projects\gestion\BudgetApp : Models.cs,
// Calc.cs, Storage.cs, la saisie de GridKit.cs) : memes donnees, memes formules, meme fichier JSON.
//
// Les montants sont des entiers en dix-milliemes (Money) : les sommes sont exactes comme le decimal de C# ; les
// calculs au prorata (prevision, par jour) passent par des doubles et sont arrondis a l'affichage.
// Les dates sont des numeros de jour (depuis le 01/01/1970).
// Sans UIKit : se compile aussi sur le PC pour les essais (tools/budget/calctest.cpp).
//
#ifndef BUDGET_MODEL_H
#define BUDGET_MODEL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <string>
#include <vector>

typedef long long Money;
static const Money NUL = (Money) 0x8000000000000000ULL;		// pas de valeur (null de C#)
static const Money ONE = 10000;
static inline bool has (Money m) { return m != NUL; }
static inline Money nz (Money m) { return has (m) ? m : 0; }
static inline Money null_if_zero (Money m) { return m == 0 ? NUL : m; }
static inline double mdbl (Money m) { return (double) nz (m) / ONE; }
static inline Money mof (double d) { return (Money) llround (d * ONE); }

// ---- les dates : numeros de jour ------------------------------------------------------------------------------
static const int NODATE = -0x7FFFFFFF;
static inline int day_number (int y, int m, int d)
{
	y -= m <= 2;
	long era = (y >= 0 ? y : y - 399) / 400, yoe = y - era * 400;
	long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
	return (int) (era * 146097 + yoe * 365 + yoe / 4 - yoe / 100 + doy - 719468);
}
static inline void ymd_of (int z, int *y, int *m, int *d)
{
	long zz = (long) z + 719468;
	long era = (zz >= 0 ? zz : zz - 146096) / 146097;
	long doe = zz - era * 146097;
	long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
	long yy = yoe + era * 400;
	long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
	long mp = (5 * doy + 2) / 153;
	*d = (int) (doy - (153 * mp + 2) / 5 + 1);
	*m = (int) (mp < 10 ? mp + 3 : mp - 9);
	*y = (int) (yy + (*m <= 2));
}
static inline bool leap (int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }
static inline int days_in_month (int y, int m) { static const int n[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 }; return m == 2 && leap (y) ? 29 : n[m - 1]; }
// AddMonths de C# : meme jour, ramene a la fin d'un mois plus court
static inline int add_months (int z, int k)
{
	int y, m, d; ymd_of (z, &y, &m, &d);
	int t = y * 12 + (m - 1) + k;
	y = t / 12; m = t % 12 + 1;
	if (d > days_in_month (y, m)) d = days_in_month (y, m);
	return day_number (y, m, d);
}
static inline int weekday (int z) { return (int) (((long) z % 7 + 7 + 4) % 7); }	// 0 dimanche (le 01/01/1970 : jeudi)

static const char *const MONTH_NAMES[12] = { "Janvier", "F\xC3\xA9vrier", "Mars", "Avril", "Mai", "Juin", "Juillet", "Ao\xC3\xBBt", "Septembre", "Octobre", "Novembre", "D\xC3\xA9" "cembre" };
static const char *const DAY_SHORT[7] = { "dim.", "lun.", "mar.", "mer.", "jeu.", "ven.", "sam." };
static const char *const DAY_LONG[7] = { "dimanche", "lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi" };

static inline std::string fmt_ddmm (int z) { int y, m, d; ymd_of (z, &y, &m, &d); char b[16]; snprintf (b, sizeof b, "%02d/%02d", d, m); return b; }
static inline std::string fmt_ddmmyyyy (int z) { int y, m, d; ymd_of (z, &y, &m, &d); char b[24]; snprintf (b, sizeof b, "%02d/%02d/%04d", d, m, y); return b; }
static inline std::string fmt_ddd_ddmm (int z) { return std::string (DAY_SHORT[weekday (z)]) + " " + fmt_ddmm (z); }

// ---- le modele (Models.cs) -----------------------------------------------------------------------------------
struct Line
{
	bool done = false;			// paye (charge) ou recu (revenu)
	int day = 0;				// 0 : aucun
	std::string label;
	Money income = NUL, expense = NUL;
	Money paid = NUL;			// deja paye quand le poste se regle en plusieurs fois
	bool meal = false;			// revenu verse en cheques repas
	Money planned () const { return nz (income) + nz (expense); }
	Money remaining () const { if (done) return 0; Money r = planned () - nz (paid); return r > 0 ? r : 0; }
};

struct Spending
{
	int date = NODATE;
	std::string label;
	std::vector<std::pair<std::string, Money> > amounts;	// dans l'ordre du fichier
	Money get (const std::string &c) const { for (auto &a : amounts) if (a.first == c) return a.second; return NUL; }
	void set (const std::string &c, Money v)
	{
		for (size_t i = 0; i < amounts.size (); i++) if (amounts[i].first == c)
		{
			if (has (v) && v != 0) amounts[i].second = v; else amounts.erase (amounts.begin () + i);
			return;
		}
		if (has (v) && v != 0) amounts.push_back (std::make_pair (c, v));
	}
};

struct Month
{
	std::string name;
	int start = 0, end = 0;
	std::vector<std::string> categories;
	std::vector<Line> lines;
	Money bank = NUL, mealBalance = NUL;	// soldes reels saisis
	int coveredDays = 0;			// jours deja couverts par des courses faites a l'avance
	std::vector<Spending> spendings;
};

struct Template
{
	int startDay = 23;
	std::vector<std::string> categories { "Courses", "Pods", "Divers", "M\xC3\xA9" "dical" };
	std::vector<Line> lines;
};

struct BudgetData
{
	Template tpl;
	std::vector<Month> months;
};

// ---- les calculs (Calc.cs) ---------------------------------------------------------------------------------
struct SummaryRow { const char *name; double bank, meal, pending, bills, rest, perDay; bool hBank, hMeal, hPending, hRest, hPerDay; };

struct Summary
{
	int totalDays = 0, elapsed = 0, remaining = 0, coveredDays = 0;
	Money totalIncome = 0, mealIncome = 0, totalExpense = 0, unpaid = 0, pendingBank = 0, pendingMeal = 0, net = 0;
	int unpaidCount = 0;
	SummaryRow initial, estimated, real;
	bool hasReal = false;
	bool hasGap = false; double gap = 0;
	std::vector<Money> categoryTotals;	// dans l'ordre des categories du mois
	Money mealUsed = 0, spendingTotal = 0;
	double spendingPerDay = 0;
	int spendingDays = 0;
};

static Summary compute (const Month &m, int today)
{
	Summary s;
	for (const Line &l : m.lines)
	{
		s.totalIncome += nz (l.income);
		s.totalExpense += nz (l.expense);
		if (l.meal) s.mealIncome += nz (l.income);
		if (nz (l.expense) > 0)
		{
			s.unpaid += l.remaining ();
			if (l.remaining () > 0) s.unpaidCount++;
		}
		else if (nz (l.income) > 0)
		{
			if (l.meal) s.pendingMeal += l.remaining ();
			else s.pendingBank += l.remaining ();
		}
	}
	s.net = s.totalIncome - s.totalExpense;
	int td = m.end - m.start;
	s.totalDays = td < 1 ? 1 : td;
	int el = today - m.start;
	s.elapsed = el < 0 ? 0 : el > s.totalDays ? s.totalDays : el;
	s.remaining = s.totalDays - s.elapsed;
	s.coveredDays = m.coveredDays;
	double ratio = 1.0 - (double) s.elapsed / s.totalDays;
	Money pending = s.pendingBank + s.pendingMeal;
	double net = mdbl (s.net), mealInc = mdbl (s.mealIncome);

	// Budget initial : tout le mois devant soi
	s.initial = SummaryRow { "Budget initial", mdbl (s.totalIncome - s.mealIncome), mealInc, 0, mdbl (s.totalExpense), net, net / s.totalDays, true, true, false, true, true };
	// Prevision : ce qu'il devrait rester si on depense regulierement chaque jour
	double estRest = ratio * net;
	s.estimated = SummaryRow { "Pr\xC3\xA9vision \xC3\xA0 ce jour", ratio * (net - mealInc) + mdbl (s.unpaid) - mdbl (s.pendingBank),
		ratio * mealInc - mdbl (s.pendingMeal), mdbl (pending), mdbl (s.unpaid), estRest, estRest / (s.remaining < 1 ? 1 : s.remaining), true, true, true, true, true };
	// Reel : a partir des soldes saisis
	s.hasReal = has (m.bank) || has (m.mealBalance);
	if (s.hasReal)
	{
		double bank = mdbl (m.bank), meal = mdbl (m.mealBalance);
		double rest = bank + meal + mdbl (pending) - mdbl (s.unpaid);
		int days = s.remaining - m.coveredDays; if (days < 1) days = 1;
		s.real = SummaryRow { "R\xC3\xA9" "el (soldes actuels)", bank, meal, mdbl (pending), mdbl (s.unpaid), rest, rest / days, true, true, true, true, true };
		s.hasGap = true; s.gap = rest - estRest;
	}
	else s.real = SummaryRow { "R\xC3\xA9" "el (soldes actuels)", 0, 0, mdbl (pending), mdbl (s.unpaid), 0, 0, false, false, true, false, false };

	// Depenses courantes
	Money sum = 0;
	for (const std::string &c : m.categories)
	{
		Money t = 0;
		for (const Spending &sp : m.spendings) t += nz (sp.get (c));
		s.categoryTotals.push_back (t);
		sum += t;
	}
	if (has (m.mealBalance))
	{
		Money u = s.mealIncome - s.pendingMeal - m.mealBalance;
		s.mealUsed = u > 0 ? u : 0;
	}
	s.spendingTotal = sum + s.mealUsed;
	s.spendingDays = s.elapsed + 1 < s.totalDays ? s.elapsed + 1 : s.totalDays;
	s.spendingPerDay = mdbl (s.spendingTotal) / s.spendingDays;
	return s;
}

// ---- l'affichage des montants : « 1 234,56 € » (N2 de fr-BE, arrondi loin de zero comme le decimal) ----------
static std::string num2 (Money v)
{
	bool neg = v < 0;
	unsigned long long a = (unsigned long long) (neg ? -v : v);
	unsigned long long c = (a + 50) / 100;			// centimes, arrondis loin de zero
	char digits[32]; snprintf (digits, sizeof digits, "%llu", c / 100);
	std::string out = neg && c ? "-" : "";
	int n = (int) strlen (digits);
	for (int i = 0; i < n; i++)
	{
		out += digits[i];
		if ((n - 1 - i) % 3 == 0 && i < n - 1) out += ' ';
	}
	char f[8]; snprintf (f, sizeof f, ",%02llu", c % 100);
	return out + f;
}
static std::string eur (Money v) { return has (v) ? num2 (v) + " \xE2\x82\xAC" : "\xE2\x80\x94"; }
static std::string eurd (double v) { return eur (mof (v)); }
static std::string num_edit (Money v) { return has (v) ? num2 (v) : ""; }

// ---- la saisie (GridKit.cs) ----------------------------------------------------------------------------------
// un calcul « 22*8 », « (50+30)/2 » : + - * / et parentheses
struct Expr
{
	const char *p; bool bad = false;
	double prim ()
	{
		if (*p == '(') { p++; double v = sum (); if (*p == ')') p++; else bad = true; return v; }
		if (*p == '-') { p++; return -prim (); }
		if (*p == '+') { p++; return prim (); }
		if ((*p >= '0' && *p <= '9') || *p == '.')
		{
			char *e; double v = strtod (p, &e);
			if (e == p) { bad = true; return 0; }
			p = e; return v;
		}
		bad = true; return 0;
	}
	double prod ()
	{
		double v = prim ();
		while (*p == '*' || *p == '/')
		{
			char o = *p++; double r = prim ();
			if (o == '*') v *= r;
			else { if (r == 0) { bad = true; return 0; } v /= r; }
		}
		return v;
	}
	double sum ()
	{
		double v = prod ();
		while (*p == '+' || *p == '-') { char o = *p++; double r = prod (); v = o == '+' ? v + r : v - r; }
		return v;
	}
};
static std::string trim (const std::string &s)
{
	size_t a = 0, b = s.size ();
	while (a < b && (unsigned char) s[a] <= ' ') a++;
	while (b > a && (unsigned char) s[b - 1] <= ' ') b--;
	return s.substr (a, b - a);
}
// un nombre decimal exact « -12.50 » -> dix-milliemes ; false si ce n'en est pas un
static bool exact_decimal (const char *t, Money *out)
{
	const char *p = t; bool neg = false;
	if (*p == '+' || *p == '-') { neg = *p == '-'; p++; }
	Money ip = 0, fp = 0; int nd = 0, fd = 0; bool dot = false;
	for (; *p; p++)
	{
		if (*p == '.') { if (dot) return false; dot = true; continue; }
		if (*p < '0' || *p > '9') return false;
		if (!dot) { ip = ip * 10 + (*p - '0'); nd++; if (ip > 100000000000LL) return false; }
		else { if (fd < 4) fp = fp * 10 + (*p - '0'); else if (fd == 4 && *p >= '5') fp++; fd++; }
	}
	if (!nd && !fd) return false;
	while (fd < 4) { fp *= 10; fd++; }
	Money v = ip * ONE + fp;
	*out = neg ? -v : v;
	return true;
}
// « 12,50 », « 1 234,56 € », « 22*8 », « =50+30 » ; vide : true et NUL
static bool try_amount (const std::string &text, Money *value)
{
	*value = NUL;
	std::string t;
	for (size_t i = 0; i < text.size (); i++)
	{
		unsigned char c = (unsigned char) text[i];
		if (c == ' ' || c == '\t') continue;
		if (c == 0xC2 && i + 1 < text.size () && (unsigned char) text[i + 1] == 0xA0) { i++; continue; }		// espace insecable
		if (c == 0xE2 && i + 2 < text.size () && (unsigned char) text[i + 1] == 0x80 && (unsigned char) text[i + 2] == 0xAF) { i += 2; continue; }
		if (c == 0xE2 && i + 2 < text.size () && (unsigned char) text[i + 1] == 0x82 && (unsigned char) text[i + 2] == 0xAC) { i += 2; continue; }	// €
		t += c == ',' ? '.' : (char) c;
	}
	size_t k = 0; while (k < t.size () && t[k] == '=') k++;
	t = t.substr (k);
	if (t.empty ()) return true;
	if (exact_decimal (t.c_str (), value)) return true;
	for (char c : t) if (!strchr ("0123456789.+-*/()", c)) return false;
	Expr e; e.p = t.c_str ();
	double v = e.sum ();
	if (e.bad || *e.p || !std::isfinite (v)) return false;
	*value = (Money) llround (v * 100) * 100;		// Math.Round (r, 2)
	return true;
}
static bool try_day (const std::string &text, int *day)
{
	std::string t = trim (text);
	*day = 0;
	if (t.empty ()) return true;
	char *e; long v = strtol (t.c_str (), &e, 10);
	if (*e || v < 1 || v > 31) return false;
	*day = (int) v;
	return true;
}
// « 7 » (jour dans la periode), « 7/11 », « 07/11/2026 » ; m : le mois (0 : aucun), today pour l'annee par defaut
static bool try_date (const std::string &text, const Month *m, int today, int *value)
{
	*value = NODATE;
	std::string t = trim (text);
	if (t.empty ()) return true;
	int part[3], n = 0; bool year4 = false;
	const char *p = t.c_str ();
	while (*p && n < 3)
	{
		if (*p < '0' || *p > '9') return false;
		long v = 0; int digits = 0;
		while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; if (++digits > 4) return false; }
		if (n == 2 && digits == 4) year4 = true;
		part[n++] = (int) v;
		if (!*p) break;
		if (*p != '/' && *p != '.' && *p != '-') return false;
		p++;
		if (!*p) return false;
	}
	if (*p) return false;
	if (n == 1)
	{
		int day = part[0];
		if (day < 1 || day > 31) return false;
		int start = m ? m->start : today;
		int y, mo, d; ymd_of (start, &y, &mo, &d);
		for (int i = 0; i < 3; i++)
		{
			int yy = y + (mo - 1 + i) / 12, mm = (mo - 1 + i) % 12 + 1;
			if (day <= days_in_month (yy, mm))
			{
				int c = day_number (yy, mm, day);
				if (c >= start || !m) { *value = c; return true; }
			}
		}
		return false;
	}
	int ty, tm, td; ymd_of (today, &ty, &tm, &td);
	int d = part[0], mo = part[1], y = n == 3 ? part[2] : ty;
	if (n == 3 && !year4) y += y < 100 ? 2000 : 0;
	if (mo < 1 || mo > 12 || d < 1 || d > days_in_month (y, mo) || y < 1900 || y > 9999) return false;
	int dt = day_number (y, mo, d);
	// « 5/01 » saisi dans une periode decembre -> janvier : l'annee qui tombe dans la periode
	if (m && !year4)
	{
		int up = add_months (dt, 12), down = add_months (dt, -12);
		if (dt < m->start && up <= m->end) dt = up;
		else if (dt > m->end && down >= m->start) dt = down;
	}
	*value = dt;
	return true;
}

// ---- JSON --------------------------------------------------------------------------------------------------
struct JVal
{
	enum { NUL_, BOOL, NUM, STR, ARR, OBJ } type = NUL_;
	bool b = false;
	std::string s;				// texte d'un nombre, ou la chaine
	std::vector<JVal> a;
	std::vector<std::pair<std::string, JVal> > o;
	const JVal *get (const char *k) const
	{
		for (auto &kv : o) if (!strcasecmp (kv.first.c_str (), k)) return &kv.second;	// (PropertyNameCaseInsensitive)
		return 0;
	}
};
struct JParser
{
	const char *p, *end; std::string err;
	void ws () { while (p < end && (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')) p++; }
	static void put_u8 (std::string &o, unsigned cp)
	{
		if (cp < 0x80) o += (char) cp;
		else if (cp < 0x800) { o += (char) (0xC0 | cp >> 6); o += (char) (0x80 | (cp & 63)); }
		else if (cp < 0x10000) { o += (char) (0xE0 | cp >> 12); o += (char) (0x80 | (cp >> 6 & 63)); o += (char) (0x80 | (cp & 63)); }
		else { o += (char) (0xF0 | cp >> 18); o += (char) (0x80 | (cp >> 12 & 63)); o += (char) (0x80 | (cp >> 6 & 63)); o += (char) (0x80 | (cp & 63)); }
	}
	unsigned hex4 ()
	{
		unsigned v = 0;
		for (int i = 0; i < 4; i++, p++)
		{
			if (p >= end) { err = "\\u"; return 0; }
			char c = *p; v <<= 4;
			if (c >= '0' && c <= '9') v |= c - '0'; else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10; else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10; else { err = "\\u"; return 0; }
		}
		return v;
	}
	bool str (std::string &o)
	{
		p++;
		while (p < end && *p != '"')
		{
			if (*p == '\\')
			{
				p++;
				if (p >= end) break;
				char c = *p++;
				switch (c)
				{
				case 'n': o += '\n'; break; case 't': o += '\t'; break; case 'r': o += '\r'; break;
				case 'b': o += '\b'; break; case 'f': o += '\f'; break;
				case 'u':
				{
					unsigned cp = hex4 ();
					if (cp >= 0xD800 && cp < 0xDC00 && p + 1 < end && p[0] == '\\' && p[1] == 'u') { p += 2; unsigned lo = hex4 (); cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00); }
					put_u8 (o, cp);
					break;
				}
				default: o += c;
				}
			}
			else o += *p++;
		}
		if (p >= end) { err = "chaine non terminee"; return false; }
		p++;
		return true;
	}
	bool val (JVal &v, int depth = 0)
	{
		if (depth > 64) { err = "trop profond"; return false; }
		ws ();
		if (p >= end) { err = "fin du fichier"; return false; }
		if (*p == '{')
		{
			v.type = JVal::OBJ; p++; ws ();
			if (p < end && *p == '}') { p++; return true; }
			for (;;)
			{
				ws ();
				if (p >= end || *p != '"') { err = "nom attendu"; return false; }
				std::string k; if (!str (k)) return false;
				ws ();
				if (p >= end || *p != ':') { err = "« : » attendu"; return false; }
				p++;
				v.o.push_back (std::make_pair (k, JVal ()));
				if (!val (v.o.back ().second, depth + 1)) return false;
				ws ();
				if (p < end && *p == ',') { p++; continue; }
				if (p < end && *p == '}') { p++; return true; }
				err = "« } » attendu"; return false;
			}
		}
		if (*p == '[')
		{
			v.type = JVal::ARR; p++; ws ();
			if (p < end && *p == ']') { p++; return true; }
			for (;;)
			{
				v.a.push_back (JVal ());
				if (!val (v.a.back (), depth + 1)) return false;
				ws ();
				if (p < end && *p == ',') { p++; continue; }
				if (p < end && *p == ']') { p++; return true; }
				err = "« ] » attendu"; return false;
			}
		}
		if (*p == '"') { v.type = JVal::STR; return str (v.s); }
		if (end - p >= 4 && !strncmp (p, "true", 4)) { v.type = JVal::BOOL; v.b = true; p += 4; return true; }
		if (end - p >= 5 && !strncmp (p, "false", 5)) { v.type = JVal::BOOL; p += 5; return true; }
		if (end - p >= 4 && !strncmp (p, "null", 4)) { v.type = JVal::NUL_; p += 4; return true; }
		const char *q = p;
		while (p < end && strchr ("+-0123456789.eE", *p)) p++;
		if (p == q) { err = "valeur inattendue"; return false; }
		v.type = JVal::NUM; v.s.assign (q, (size_t) (p - q));
		return true;
	}
};

static Money j_money (const JVal *v)
{
	if (!v || v->type != JVal::NUM) return NUL;
	Money m;
	if (exact_decimal (v->s.c_str (), &m)) return m;
	return mof (strtod (v->s.c_str (), 0));
}
static int j_int (const JVal *v, int def) { return v && v->type == JVal::NUM ? (int) strtol (v->s.c_str (), 0, 10) : def; }
static bool j_bool (const JVal *v) { return v && v->type == JVal::BOOL && v->b; }
static std::string j_str (const JVal *v) { return v && v->type == JVal::STR ? v->s : ""; }
static int j_date (const JVal *v)
{
	if (!v || v->type != JVal::STR) return NODATE;
	int y, m, d;
	if (sscanf (v->s.c_str (), "%d-%d-%d", &y, &m, &d) != 3 || m < 1 || m > 12 || d < 1 || d > 31) return NODATE;
	return day_number (y, m, d);
}
static std::vector<std::string> j_strings (const JVal *v)
{
	std::vector<std::string> r;
	if (v && v->type == JVal::ARR) for (auto &e : v->a) if (e.type == JVal::STR) r.push_back (e.s);
	return r;
}
static std::vector<Line> j_lines (const JVal *v)
{
	std::vector<Line> r;
	if (!v || v->type != JVal::ARR) return r;
	for (auto &e : v->a)
	{
		Line l;
		l.done = j_bool (e.get ("Done"));
		l.day = j_int (e.get ("Day"), 0);
		l.label = j_str (e.get ("Label"));
		l.income = j_money (e.get ("Income"));
		l.expense = j_money (e.get ("Expense"));
		l.paid = j_money (e.get ("Paid"));
		l.meal = j_bool (e.get ("MealVoucher"));
		r.push_back (l);
	}
	return r;
}
// -> true ; false : *err dit pourquoi (le fichier n'est pas touche)
static bool budget_parse (const char *text, size_t n, BudgetData &d, std::string *err)
{
	d = BudgetData ();
	JParser jp; jp.p = text; jp.end = text + n;
	if (n >= 3 && (unsigned char) text[0] == 0xEF && (unsigned char) text[1] == 0xBB && (unsigned char) text[2] == 0xBF) jp.p += 3;
	JVal root;
	if (!jp.val (root)) { if (err) { char b[64]; snprintf (b, sizeof b, " (octet %ld)", (long) (jp.p - text)); *err = jp.err + b; } return false; }
	if (root.type == JVal::NUL_) return true;
	if (root.type != JVal::OBJ) { if (err) *err = "objet attendu"; return false; }
	if (const JVal *t = root.get ("Template"))
	{
		if (t->type == JVal::OBJ)
		{
			d.tpl.startDay = j_int (t->get ("StartDay"), 23);
			if (const JVal *c = t->get ("Categories")) if (c->type == JVal::ARR) d.tpl.categories = j_strings (c);
			d.tpl.lines = j_lines (t->get ("Lines"));
		}
	}
	if (const JVal *ms = root.get ("Months"))
		if (ms->type == JVal::ARR) for (auto &e : ms->a)
		{
			Month m;
			m.name = j_str (e.get ("Name"));
			m.start = j_date (e.get ("Start")); if (m.start == NODATE) m.start = day_number (1, 1, 1);
			m.end = j_date (e.get ("End")); if (m.end == NODATE) m.end = day_number (1, 1, 1);
			m.categories = j_strings (e.get ("Categories"));
			m.lines = j_lines (e.get ("Lines"));
			m.bank = j_money (e.get ("Bank"));
			m.mealBalance = j_money (e.get ("MealBalance"));
			m.coveredDays = j_int (e.get ("CoveredDays"), 0);
			if (const JVal *ss = e.get ("Spendings")) if (ss->type == JVal::ARR) for (auto &se : ss->a)
			{
				Spending sp;
				sp.date = j_date (se.get ("Date"));
				sp.label = j_str (se.get ("Label"));
				if (const JVal *am = se.get ("Amounts")) if (am->type == JVal::OBJ)
					for (auto &kv : am->o) { Money v = j_money (&kv.second); if (has (v)) sp.amounts.push_back (std::make_pair (kv.first, v)); }
				m.spendings.push_back (sp);
			}
			d.months.push_back (m);
		}
	return true;
}

// l'ecriture : celle de System.Text.Json (WriteIndented, UnsafeRelaxedJsonEscaping), proprietes calculees comprises
struct JWriter
{
	std::string o; int ind = 0;
	void nl () { o += '\n'; o.append ((size_t) ind * 2, ' '); }
	void str (const std::string &s)
	{
		o += '"';
		for (unsigned char c : s)
		{
			switch (c)
			{
			case '"': o += "\\\""; break; case '\\': o += "\\\\"; break;
			case '\n': o += "\\n"; break; case '\r': o += "\\r"; break; case '\t': o += "\\t"; break;
			case '\b': o += "\\b"; break; case '\f': o += "\\f"; break;
			default:
				if (c < 0x20) { char b[8]; snprintf (b, sizeof b, "\\u%04X", c); o += b; }
				else o += (char) c;
			}
		}
		o += '"';
	}
	void money (Money m)
	{
		if (!has (m)) { o += "null"; return; }
		bool neg = m < 0; unsigned long long a = (unsigned long long) (neg ? -m : m);
		char b[48]; snprintf (b, sizeof b, "%s%llu", neg ? "-" : "", a / ONE);
		o += b;
		unsigned long long f = a % ONE;
		if (f)
		{
			snprintf (b, sizeof b, ".%04llu", f);
			int n = (int) strlen (b); while (b[n - 1] == '0') b[--n] = 0;
			o += b;
		}
	}
	void date (int z)
	{
		if (z == NODATE) { o += "null"; return; }
		int y, m, d; ymd_of (z, &y, &m, &d);
		char b[40]; snprintf (b, sizeof b, "\"%04d-%02d-%02dT00:00:00\"", y, m, d); o += b;
	}
	void key (const char *k, bool first) { if (!first) o += ','; nl (); str (k); o += ": "; }
	void strings (const std::vector<std::string> &v)
	{
		if (v.empty ()) { o += "[]"; return; }
		o += '['; ind++;
		for (size_t i = 0; i < v.size (); i++) { if (i) o += ','; nl (); str (v[i]); }
		ind--; nl (); o += ']';
	}
	void lines (const std::vector<Line> &v)
	{
		if (v.empty ()) { o += "[]"; return; }
		o += '['; ind++;
		for (size_t i = 0; i < v.size (); i++)
		{
			const Line &l = v[i];
			if (i) o += ',';
			nl (); o += '{'; ind++;
			key ("Done", true); o += l.done ? "true" : "false";
			key ("Day", false); if (l.day) { char b[16]; snprintf (b, sizeof b, "%d", l.day); o += b; } else o += "null";
			key ("Label", false); str (l.label);
			key ("Income", false); money (l.income);
			key ("Expense", false); money (l.expense);
			key ("Paid", false); money (l.paid);
			key ("MealVoucher", false); o += l.meal ? "true" : "false";
			key ("Planned", false); money (l.planned ());
			key ("Remaining", false); money (l.remaining ());
			ind--; nl (); o += '}';
		}
		ind--; nl (); o += ']';
	}
};
static std::string budget_json (const BudgetData &d)
{
	JWriter w;
	w.o += '{'; w.ind = 1;
	w.key ("Template", true); w.o += '{'; w.ind++;
	{ char b[16]; w.key ("StartDay", true); snprintf (b, sizeof b, "%d", d.tpl.startDay); w.o += b; }
	w.key ("Categories", false); w.strings (d.tpl.categories);
	w.key ("Lines", false); w.lines (d.tpl.lines);
	w.ind--; w.nl (); w.o += '}';
	w.key ("Months", false);
	if (d.months.empty ()) w.o += "[]";
	else
	{
		w.o += '['; w.ind++;
		for (size_t i = 0; i < d.months.size (); i++)
		{
			const Month &m = d.months[i];
			if (i) w.o += ',';
			w.nl (); w.o += '{'; w.ind++;
			w.key ("Name", true); w.str (m.name);
			w.key ("Start", false); w.date (m.start);
			w.key ("End", false); w.date (m.end);
			w.key ("Categories", false); w.strings (m.categories);
			w.key ("Lines", false); w.lines (m.lines);
			w.key ("Bank", false); w.money (m.bank);
			w.key ("MealBalance", false); w.money (m.mealBalance);
			{ char b[16]; w.key ("CoveredDays", false); snprintf (b, sizeof b, "%d", m.coveredDays); w.o += b; }
			w.key ("Spendings", false);
			if (m.spendings.empty ()) w.o += "[]";
			else
			{
				w.o += '['; w.ind++;
				for (size_t k = 0; k < m.spendings.size (); k++)
				{
					const Spending &s = m.spendings[k];
					if (k) w.o += ',';
					w.nl (); w.o += '{'; w.ind++;
					w.key ("Date", true); w.date (s.date);
					w.key ("Label", false); w.str (s.label);
					w.key ("Amounts", false);
					if (s.amounts.empty ()) w.o += "{}";
					else
					{
						w.o += '{'; w.ind++;
						for (size_t a = 0; a < s.amounts.size (); a++) { w.key (s.amounts[a].first.c_str (), a == 0); w.money (s.amounts[a].second); }
						w.ind--; w.nl (); w.o += '}';
					}
					w.ind--; w.nl (); w.o += '}';
				}
				w.ind--; w.nl (); w.o += ']';
			}
			w.ind--; w.nl (); w.o += '}';
		}
		w.ind--; w.nl (); w.o += ']';
	}
	w.ind = 0; w.nl (); w.o += '}';
	return w.o;
}

// ---- les actions du modele (MainForm.cs) -------------------------------------------------------------------
// Nouveau mois : debut = fin du dernier mois (sinon le jour de paie de ce mois-ci), fin = +1 mois, nom = mois du milieu
static Month new_month (const BudgetData &d, int today)
{
	const Template &t = d.tpl;
	int last = -1;
	for (size_t i = 0; i < d.months.size (); i++) if (last < 0 || d.months[i].start >= d.months[(size_t) last].start) last = (int) i;
	int start;
	if (last >= 0) start = d.months[(size_t) last].end;
	else
	{
		int y, m, dd; ymd_of (today, &y, &m, &dd);
		int day = t.startDay < days_in_month (y, m) ? t.startDay : days_in_month (y, m);
		start = day_number (y, m, day);
		if (start > today) start = add_months (start, -1);
	}
	int end = add_months (start, 1);
	int mid = start + (end - start) / 2;		// (AddDays d'un nombre de jours eventuellement demi : la date seule compte)
	int y, mo, dd; ymd_of (mid, &y, &mo, &dd);
	Month m;
	char b[64]; snprintf (b, sizeof b, "%s %d", MONTH_NAMES[mo - 1], y);
	m.name = b; m.start = start; m.end = end;
	m.categories = t.categories;
	for (const Line &l : t.lines)
	{
		Line c = l;
		c.done = nz (c.income) > 0;		// les revenus sont consideres comme recus (le mois demarre au jour de paie)
		c.paid = NUL;
		m.lines.push_back (c);
	}
	return m;
}
// Categories renommees par position ; les montants des colonnes retirees vont dans la derniere
static void set_categories (Month &m, const std::vector<std::string> &cats)
{
	if (cats.empty ()) return;
	std::vector<std::string> old = m.categories;
	for (Spending &s : m.spendings)
	{
		Spending n;
		for (size_t i = 0; i < cats.size (); i++)
			if (i < old.size () && has (s.get (old[i]))) n.amounts.push_back (std::make_pair (cats[i], s.get (old[i])));
		for (size_t i = cats.size (); i < old.size (); i++)
			if (has (s.get (old[i])))
			{
				Money v = s.get (old[i]), prev = n.get (cats.back ());
				Money sum = nz (prev) + v;
				bool found = false;
				for (auto &a : n.amounts) if (a.first == cats.back ()) { a.second = sum; found = true; }
				if (!found) n.amounts.push_back (std::make_pair (cats.back (), sum));
			}
		s.amounts = n.amounts;
	}
	m.categories = cats;
}
static std::vector<std::string> split_categories (const std::string &text)
{
	std::vector<std::string> r;
	size_t a = 0;
	while (a <= text.size ())
	{
		size_t b = text.find (';', a);
		if (b == std::string::npos) b = text.size ();
		std::string c = trim (text.substr (a, b - a));
		bool dup = false;
		for (auto &x : r) if (x == c) dup = true;
		if (!c.empty () && !dup) r.push_back (c);
		a = b + 1;
	}
	return r;
}
static std::string join_categories (const std::vector<std::string> &c)
{
	std::string r;
	for (size_t i = 0; i < c.size (); i++) { if (i) r += "; "; r += c[i]; }
	return r;
}

#endif
