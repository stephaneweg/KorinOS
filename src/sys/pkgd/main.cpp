//
// pkgd -- le service des mises a jour (docs/PAQUETS.md, port de user/Apps/pkgd d'Onyx) : pas de fenetre, lance par
// SD0:/etc/session. Une fois le reseau et l'heure la, puis une fois par jour : l'index du depot relu (signature
// verifiee), les paquets en mode auto mis a jour (pkglib.h, comme `pkg upgrade` ; une app qui tourne attend le
// tour suivant, le systeme s'etage pour le prochain demarrage), une notification pour le reste.
//
//   pkgd            le service (un seul a la fois)
//   pkgd --once     un tour tout de suite, puis fin (le jour n'est pas compte)
//
// SD0:/etc/pkg/pkg.ini : check = daily (par defaut) ou never (le service s'arrete aussitot). Le jour du dernier
// tour : SD0:/var/pkg/lastcheck (aaaammjj).
//
#include "pkglib.h"

using namespace pkg;

struct Quiet : Report {};			// (pkgd ne dit rien, sauf sa notification)

static int today (void)
{
	time_t t = time (0); struct tm *tm = localtime (&t);
	return (tm->tm_year + 1900) * 10000 + (tm->tm_mon + 1) * 100 + tm->tm_mday;
}
static bool time_set (void) { return today () / 10000 >= 2025; }
static bool net_up (void)
{
	static int kit = -1;
	if (kit < 0) kit = netkit_kit_open () ? 1 : 0;
	if (!kit) return false;
	unsigned char ni[64]; memset (ni, 0, sizeof ni);
	net_info ((long) ni);
	return ni[0] | ni[1] | ni[2] | ni[3];
}

static void add_name (char *list, int cap, const char *s)
{
	if (list[0]) strncat (list, ", ", (size_t) cap - strlen (list) - 1);
	strncat (list, s, (size_t) cap - strlen (list) - 1);
}

static void round (bool count_day)
{
	static Manager *mp; if (!mp) mp = new Manager; else mp->db.load ();
	Manager &m = *mp;
	Quiet q;
	if (m.refresh (q) != OK || !m.verified) return;		// pas d'index verifie : la prochaine fois
	if (count_day) { char t[16]; snprintf (t, sizeof t, "%d\n", today ()); write_file (PKG_VAR "/lastcheck", t, strlen (t)); }
	char done[300] = "", wait[300] = ""; int nd = 0, nw = 0; bool staged = false;
	int abi = abi_level ();
	static char names[256][40]; int nn0 = 0;			// (install recharge la base : ses Inst changent)
	for (int i = 0; i < m.db.n && nn0 < 256; i++) cpy (names[nn0++], m.db.v[i]->name, 40);
	for (int i = 0; i < nn0; i++)
	{
		Inst *ip = m.db.find (names[i]);
		if (!ip) continue;
		Inst &in = *ip;
		const Pkg *u = m.update_for (in);
		if (!u || m.staged (in.name) || eq (in.mode (), "never")) continue;
		char title[64], mode[8]; cpy (title, in.ini.get ("package", "title", in.name), sizeof title); cpy (mode, in.mode (), sizeof mode);
		bool abiOk = true;
		Need nd2[16]; int nn = parse_needs (u->needs, nd2, 16);
		for (int k = 0; k < nn; k++) if (eq (nd2[k].name, "abi") && abi < atoi (nd2[k].ver)) abiOk = false;
		if (eq (mode, "auto") && abiOk)
		{
			const Pkg *list[16]; int err = 0, n = 0;
			for (int k = 0; k < nn; k++)			// ses nouveaux besoins d'abord
			{
				if (eq (nd2[k].name, "abi")) continue;
				Inst *have = m.db.find (nd2[k].name);
				if (have && vcmp (have->version (), nd2[k].ver) >= 0) continue;
				n += m.resolve (nd2[k].name, list + n, 16 - n, &err, q);
			}
			bool ok = true;
			for (int k = 0; k < n && ok; k++) ok = m.install (*list[k], q) == OK;
			int rc = ok ? m.install (*u, q) : E_NEEDS;
			if (rc == OK)
			{
				if (u->restart) staged = true;
				if (nd++ < 4) add_name (done, sizeof done, title);
				continue;
			}
		}
		if (nw++ < 4) add_name (wait, sizeof wait, title);
	}
	m.refresh_desktop ();
	if (!nd && !nw) return;
	char text[480] = "";
	if (nw) snprintf (text, sizeof text, "%s%s", wait, nw > 4 ? "..." : "");
	if (nd)
	{
		char t[360]; snprintf (t, sizeof t, TR ("%sMis à jour : %s%s%s"), nw ? ". " : "", done, nd > 4 ? "..." : "", staged ? TR (" (le système au prochain démarrage)") : "");
		strncat (text, t, sizeof text - strlen (text) - 1);
	}
	char title[64];
	if (nw) snprintf (title, sizeof title, TR (nw > 1 ? "%d mises à jour disponibles" : "%d mise à jour disponible"), nw);
	else snprintf (title, sizeof title, TR (nd > 1 ? "%d paquets mis à jour" : "%d paquet mis à jour"), nd);
	notify (title, text);
}

int main (int argc, char **argv)
{
	bool once = argc > 1 && eq (argv[1], "--once");
	if (!once && proc_running ("pkgd")) return 0;		// un autre pkgd tourne
	Ini c; c.load (PKG_CONF);
	if (!once && eq (c.get ("", "check", "daily"), "never")) return 0;
	for (;;)
	{
		if (net_up () && time_set ())
		{
			char *t = read_file (PKG_VAR "/lastcheck");
			int was = t ? atoi (t) : 0; free (t);
			if (once || was != today ()) round (!once);
			if (once) return 0;
		}
		else if (once) { sleep (2); continue; }
		sleep (15 * 60);				// un regard tous les quarts d'heure (le jour change)
	}
}
