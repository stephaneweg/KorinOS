// calctest.cpp -- essai des calculs de Budget mensuel sur le PC (src/apps/budget/model.h) :
//   g++ -std=c++17 -I src/apps/budget tools/budget/calctest.cpp -o /tmp/calctest && /tmp/calctest budget.json 2026-11-07
// Montre les chiffres du mois qui contient le jour donne, relit ce que l'ecriture produit et verifie qu'il est identique.
#include <strings.h>
#include "model.h"

int main (int argc, char **argv)
{
	if (argc < 3) { fprintf (stderr, "calctest budget.json AAAA-MM-JJ\n"); return 2; }
	FILE *f = fopen (argv[1], "rb");
	if (!f) { perror (argv[1]); return 1; }
	std::string t; char b[4096]; size_t n;
	while ((n = fread (b, 1, sizeof b, f)) > 0) t.append (b, n);
	fclose (f);
	BudgetData d; std::string err;
	if (!budget_parse (t.data (), t.size (), d, &err)) { fprintf (stderr, "lecture : %s\n", err.c_str ()); return 1; }
	int y, m, dd; sscanf (argv[2], "%d-%d-%d", &y, &m, &dd);
	int today = day_number (y, m, dd);
	int rc = 0;
	for (const Month &mo : d.months)
	{
		if (today < mo.start || today >= mo.end) continue;
		Summary s = compute (mo, today);
		printf ("%s (%s -> %s), jour %d sur %d, %d restants\n", mo.name.c_str (), fmt_ddmm (mo.start).c_str (), fmt_ddmmyyyy (mo.end).c_str (), s.elapsed, s.totalDays, s.remaining);
		printf ("Reste a payer      %s (%d postes)\n", eur (s.unpaid).c_str (), s.unpaidCount);
		const SummaryRow *rows[3] = { &s.initial, &s.estimated, &s.real };
		for (auto r : rows)
			printf ("%-24s banque %s  repas %s  a recevoir %s  factures %s  reste %s  par jour %s\n", r->name,
				r->hBank ? eurd (r->bank).c_str () : "-", r->hMeal ? eurd (r->meal).c_str () : "-", r->hPending ? eurd (r->pending).c_str () : "",
				eurd (r->bills).c_str (), r->hRest ? eurd (r->rest).c_str () : "-", r->hPerDay ? eurd (r->perDay).c_str () : "-");
		if (s.hasGap) printf ("Ecart              %s\n", eurd (s.gap).c_str ());
		for (size_t i = 0; i < mo.categories.size (); i++) printf ("  %s : %s\n", mo.categories[i].c_str (), eur (s.categoryTotals[i]).c_str ());
		printf ("Depenses courantes %s, %s / jour sur %d jours (ch. repas utilises %s)\n", eur (s.spendingTotal).c_str (), eurd (s.spendingPerDay).c_str (), s.spendingDays, eur (s.mealUsed).c_str ());
	}
	// aller-retour : ecrire, relire, reecrire -> le meme texte
	std::string j1 = budget_json (d);
	BudgetData d2;
	if (!budget_parse (j1.data (), j1.size (), d2, &err)) { printf ("relecture : %s\n", err.c_str ()); return 1; }
	std::string j2 = budget_json (d2);
	printf ("aller-retour : %s (%zu octets, original %zu)\n", j1 == j2 ? "identique" : "DIFFERENT", j1.size (), t.size ());
	if (j1 != j2) rc = 1;
	// la saisie
	struct { const char *in; } tests[] = { { "12,50" }, { "1 234,56 \xE2\x82\xAC" }, { "22*8" }, { "=50+30" }, { "10/3" }, { "abc" }, { "" } };
	for (auto &tt : tests) { Money v; bool ok = try_amount (tt.in, &v); printf ("montant \"%s\" -> %s %s\n", tt.in, ok ? "ok" : "refuse", ok ? eur (v).c_str () : ""); }
	const Month *nov = 0; for (auto &mo : d.months) if (today >= mo.start && today < mo.end) nov = &mo;
	const char *dates[] = { "7", "25", "7/11", "07/11/2026", "31/02", "x" };
	for (auto ds : dates) { int z; bool ok = try_date (ds, nov, today, &z); printf ("date \"%s\" -> %s %s\n", ds, ok ? "ok" : "refusee", ok && z != NODATE ? fmt_ddmmyyyy (z).c_str () : ""); }
	if (argc > 3) { FILE *o = fopen (argv[3], "wb"); fwrite (j1.data (), 1, j1.size (), o); fclose (o); }
	return rc;
}
