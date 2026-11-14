// banktest.cpp -- essai de la liaison bancaire de Budget mensuel (src/apps/budget/bank.h) sur le PC, contre le faux
// serveur d'Enable Banking des essais de l'app Windows (aucune donnée réelle) :
//   g++ -std=c++17 -I src/apps/budget tools/budget/banktest.cpp -lssl -lcrypto -o /tmp/banktest
//   /tmp/banktest http://127.0.0.1:8899 DOSSIER 2026-11-10
// DOSSIER contient budget.json (une copie) et banque.json ; tout le parcours de l'app : la banque, l'autorisation
// (suivie sans navigateur), la session, le solde, les opérations du mois, leur classement (comme le scénario
// « banque » de l'app Windows : Hello Fresh vers son poste, Colruyt en Courses retenu), puis l'écriture des deux
// fichiers. Code de sortie 0 si tout s'est passé comme prévu.
#include "bank.h"

static int fails;
static void check (bool ok, const char *what) { printf ("%s %s\n", ok ? "ok  " : "ECHEC", what); if (!ok) fails++; }

int main (int argc, char **argv)
{
	if (argc < 4) { fprintf (stderr, "banktest API DOSSIER AAAA-MM-JJ\n"); return 2; }
	std::string api = argv[1], dir = argv[2];
	int y, m, d; sscanf (argv[3], "%d-%d-%d", &y, &m, &d);
	int today = day_number (y, m, d);
	long tz = 3600;

	// la reconnaissance, sur des textes de banque
	check (bank::normalize ("Paiement \xC3\xA0 COLRUYT Fl\xC3\xA9ron 03/11 4871") == "PAIEMENT A COLRUYT FLERON", "normalize : accents, nombres");
	bank::Tx t1; t1.counterparty = "COLRUYT FLERON"; t1.remittance = "PAIEMENT AVEC LA CARTE DE DEBIT 4871";
	check (bank::suggest_keyword (t1) == "COLRUYT", "mot-cl\xC3\xA9 : COLRUYT");
	bank::Tx t2; t2.counterparty = "ORANGE BELGIUM SA";
	check (bank::suggest_keyword (t2) == "ORANGE", "mot-cl\xC3\xA9 : ORANGE (BELGIUM et SA \xC3\xA9" "cart\xC3\xA9s)");
	bank::Tx t3; t3.counterparty = "Q8 FLERON";
	check (bank::suggest_keyword (t3) == "Q8 FLERON", "mot-cl\xC3\xA9 : Q8 FLERON (nom court : deux mots)");

	std::string bt, err;
	bank::Settings s;
	check (bank::load (dir + "/banque.json", s, tz, &err), "banque.json lu");
	if (!bank::read_file (dir + "/budget.json", bt)) { printf ("budget.json absent\n"); return 1; }
	BudgetData data;
	check (budget_parse (bt.data (), bt.size (), data, &err), "budget.json lu");
	Month *mo = 0;
	for (auto &x : data.months) if (today >= x.start && today < x.end) mo = &x;
	if (!mo) { printf ("pas de mois pour ce jour\n"); return 1; }

	bank::Client c; c.tz = tz;
	if (!c.open (s, dir, api)) { printf ("%s\n", c.err.c_str ()); return 1; }
	std::string exact; long long maxAccess = 0;
	check (c.find_bank (s.bankName, s.country, exact, maxAccess), ("banque trouv\xC3\xA9" "e : " + exact).c_str ());
	if (!c.err.empty ()) printf ("  %s\n", c.err.c_str ());
	printf ("  acc\xC3\xA8s permis : %lld s\n", maxAccess);
	std::string state = bank::new_state (), url;
	check (c.start_auth (exact, s.country, bank::utc_now () + maxAccess, s.redirect, state, url), "autorisation d\xC3\xA9marr\xC3\xA9" "e");
	if (!c.err.empty ()) printf ("  %s\n", c.err.c_str ());
	std::string back = bank::follow_once (url), code, msg;
	printf ("  retour : %s\n", back.c_str ());
	check (bank::code_from (back, state, code, msg), "code lu dans le retour");
	std::string wrong;
	check (!bank::code_from (back, "autre", wrong, msg), ("state diff\xC3\xA9rent refus\xC3\xA9 : " + msg).c_str ());
	bank::Session ses;
	check (c.create_session (code, ses) && ses.accounts.size () == 1, "session et compte");
	if (ses.accounts.empty ()) { printf ("  %s\n", c.err.c_str ()); return 1; }
	s.sessionId = ses.id; s.validUntil = ses.validUntil; s.accountUid = ses.accounts[0].uid; s.accountLabel = ses.accounts[0].label;
	printf ("  compte : %s, jusqu'au %s\n", s.accountLabel.c_str (), bank::format_time (s.validUntil, tz).c_str ());
	check (bank::linked (s), "reli\xC3\xA9");

	Money bal = 0;
	check (c.balance (s.accountUid, bal) && bal == mof (1210.10), ("solde ITAV : " + num2 (bal)).c_str ());
	std::vector<bank::Tx> all;
	check (c.transactions (s.accountUid, mo->start, today, all), "op\xC3\xA9rations (deux pages)");
	std::vector<bank::Tx> fresh; int pending = 0;
	for (auto &t : all)
	{
		if (t.day < mo->start || t.day >= mo->end) continue;
		if (!t.booked) { pending++; continue; }
		if (!s.has_seen (t.key)) fresh.push_back (t);
	}
	printf ("  %zu lues, %zu nouvelles du mois, %d en attente\n", all.size (), fresh.size (), pending);
	std::vector<std::string> tg = bank::targets (*mo);
	std::vector<bank::Choice> ch;
	for (auto &t : fresh)
	{
		bool fromRule = false;
		std::string target = bank::suggest (t, tg, s.rules, *mo, &fromRule);
		bank::Choice c1; c1.tx = t; c1.target = target; c1.keyword = bank::suggest_keyword (t); c1.remember = !fromRule && target != bank::LATER;
		std::string mt = bank::match_text (t);
		if (mt.find ("HELLO") != std::string::npos && bank::in_list (tg, std::string (bank::LINE_PREFIX) + "Hello Fresh")) c1.target = std::string (bank::LINE_PREFIX) + "Hello Fresh";
		else if (c1.target == bank::LATER && mt.find ("COLRUYT") != std::string::npos) { c1.target = bank::CAT_PREFIX + mo->categories[0]; c1.keyword = "COLRUYT"; c1.remember = true; }
		printf ("  %s  %10s  %-60.60s -> %s%s [%s] %s\n", fmt_ddmm (t.day).c_str (), num2 (t.amount).c_str (), t.display ().c_str (), c1.target.c_str (),
			fromRule ? " (r\xC3\xA8gle)" : "", c1.keyword.c_str (), t.key.c_str ());
		ch.push_back (c1);
	}
	std::string sum = bank::apply (*mo, ch, s);
	printf ("  r\xC3\xA9sum\xC3\xA9 : %s\n", sum.c_str ());
	s.lastSync = bank::utc_now ();
	mo->bank = bal;
	for (auto &l : mo->lines) if (has (l.paid)) printf ("  poste %-24s pay\xC3\xA9 %10s %s\n", l.label.c_str (), num2 (l.paid).c_str (), l.done ? "(r\xC3\xA9gl\xC3\xA9)" : "");
	check (bank::save (dir + "/banque.json", s, tz, today), "banque.json \xC3\xA9" "crit");
	std::string j = budget_json (data);
	FILE *f = fopen ((dir + "/budget.json").c_str (), "wb"); fwrite (j.data (), 1, j.size (), f); fclose (f);
	// relu : mêmes réglages
	bank::Settings s2;
	check (bank::load (dir + "/banque.json", s2, tz, &err) && bank::settings_json (s2, tz) == bank::settings_json (s, tz), "banque.json relu \xC3\xA0 l'identique");
	printf ("%s\n", fails ? "DES ECHECS" : "tout est bon");
	return fails ? 1 : 0;
}
