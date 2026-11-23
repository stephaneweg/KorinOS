//
// linux_main.cpp -- Budget mensuel sous Linux : l'entree du programme. Le main.cpp de Korin (src/apps/budget) est
// compile tel quel, renomme kport_app_main (-Dmain=kport_app_main) ; ici, avant lui, les chemins de Korin
// (korin_paths.cpp : HOME:/budget -> ~/Korin/budget), le coffre a secrets (korin_vault.cpp : libsecret) et les mots
// qui changent sous Linux (le navigateur du systeme a la place de Jet).
//
//   budget [--today AAAA-MM-JJ] [--month Novembre] [--bank-api URL] [--bank-follow]   la fenetre (comme sous Korin)
//   budget --vault-status          dit ou vont la cle privee de la banque et la session (Secret Service, ou fichiers)
//   budget --vault-forget          retire du trousseau la cle et la session de Budget (banque.json et le .pem restent)
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include "korin_port.h"
#include "../../src/lib/keyvault/keyvault.h"

int kport_app_main (int argc, char **argv);

// (Budget ne joue pas de son : le menu « Son » de uikit_gtk.cpp n'existe pas pour lui)
extern "C" int kport_audio_reload_soundfont (const char *) { return 0; }

static void linux_words ()
{
	// (les chemins des donnees, dits par l'app avec HOME:/ : ceux de Linux)
	static std::string data;
	for (const char *c = kport_homedir (); *c; c++) { data += *c; if (*c == '%') data += '%'; }	// (un format de printf)
	data += "/budget/budget.json";
	static std::string w1 = "Donn\xC3\xA9" "es : " + data, w2 = "Enregistr\xC3\xA9 \xC3\xA0 %02ld:%02ld:%02ld \xE2\x80\x94 " + data;
	kport_tr_add ("Donn\xC3\xA9" "es : %s", w1.c_str ());
	kport_tr_add ("Enregistr\xC3\xA9 \xC3\xA0 %02ld:%02ld:%02ld \xE2\x80\x94 %s", w2.c_str ());
	kport_tr_add ("Ouvrir dans Jet", "Ouvrir dans le navigateur");
	kport_tr_add ("La page de votre banque s'ouvre dans Jet.", "La page de votre banque s'ouvre dans votre navigateur.");
	kport_tr_add ("Jet ne d\xC3\xA9marre pas : ouvrez l'adresse sur un autre appareil.",
		"Le navigateur ne s'ouvre pas (xdg-open) : copiez l'adresse dans un navigateur, ici ou sur un autre appareil.");
	static std::string hint = std::string (
		"1. Sur enablebanking.com, cr\xC3\xA9" "ez une application (production) et t\xC3\xA9l\xC3\xA9" "chargez sa cl\xC3\xA9 priv\xC3\xA9" "e (.pem).\n"
		"2. D\xC3\xA9" "clarez dans l'application exactement la m\xC3\xAAme \xC2\xAB adresse de retour \xC2\xBB que ci-dessous.\n"
		"3. Indiquez l'identifiant et la cl\xC3\xA9, puis \xC2\xAB Relier mon compte \xC2\xBB. Un acc\xC3\xA8s reli\xC3\xA9 par l'app Windows sert tel quel : "
		"copiez banque.json et la cl\xC3\xA9 dans ") + kport_homedir () + "/budget.";
	kport_tr_add (
		"1. Sur enablebanking.com, cr\xC3\xA9" "ez une application (production) et t\xC3\xA9l\xC3\xA9" "chargez sa cl\xC3\xA9 priv\xC3\xA9" "e (.pem).\n"
		"2. D\xC3\xA9" "clarez dans l'application exactement la m\xC3\xAAme \xC2\xAB adresse de retour \xC2\xBB que ci-dessous.\n"
		"3. Indiquez l'identifiant et la cl\xC3\xA9, puis \xC2\xAB Relier mon compte \xC2\xBB. Un acc\xC3\xA8s reli\xC3\xA9 par l'app Windows sert tel quel : "
		"copiez banque.json et la cl\xC3\xA9 dans HOME:/budget.", hint.c_str ());
}

int main (int argc, char **argv)
{
	kport_setup ("budget", argv[0]);
	bool vault = kport_vault_install ();
	if (argc > 1 && (!strcmp (argv[1], "--vault-status") || !strcmp (argv[1], "--vault-forget")))
	{
		long st = vault ? kv_status () : -38;
		if (!strcmp (argv[1], "--vault-status"))
		{
			if (st >= 0) printf ("Coffre \xC3\xA0 secrets : le Secret Service (libsecret), sch\xC3\xA9ma org.korin.KeyVault, app=budget, home=%s\n", kport_homedir ());
			else printf ("Coffre \xC3\xA0 secrets : aucun (%s) ; la session va dans %s/budget/banque.json et la cl\xC3\xA9 reste dans son .pem\n",
				vault ? "pas de Secret Service" : "construit sans libsecret", kport_homedir ());
			return 0;
		}
		if (st < 0) { fprintf (stderr, "budget: pas de coffre \xC3\xA0 secrets\n"); return 1; }
		long a = kv_delete ("bank-key"), b = kv_delete ("bank-session");
		printf ("bank-key : %s ; bank-session : %s\n", a >= 0 ? "retir\xC3\xA9" "e" : "absente", b >= 0 ? "retir\xC3\xA9" "e" : "absente");
		return 0;
	}
	if (argc > 1 && (!strcmp (argv[1], "--help") || !strcmp (argv[1], "-h")))
	{
		printf ("budget [--today AAAA-MM-JJ] [--month Novembre] [--bank-api URL] [--bank-follow]\n"
			"budget --vault-status | --vault-forget\nHOME:/ = %s (les donn\xC3\xA9" "es : %s/budget/budget.json)\n", kport_homedir (), kport_homedir ());
		return 0;
	}
	linux_words ();
	return kport_app_main (argc, argv);
}
