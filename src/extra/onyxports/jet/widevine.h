// widevine.h -- le module Widevine de Google pour Jet (docs/etude/WIDEVINE.md ; E60 du plan de sécurité) : commun au
// réglage de Jet (main.cpp) et aux apps web (webapp.cpp).
//
// Décision du 26/11/2026 : le module est un fichier du système, dans /res/widevine (écrit par l'uid 0 seulement, lu
// par tous), posé par cdmupdate lancé élevé (la carte d'authd) ; cdmhost le lit sous le compte. Un programme du compte
// ne peut donc plus le remplacer (avant : HOME/.jet/widevine). Restent au compte : les données du module (licences,
// HOME/.jet/widevine/storage) et la date de la dernière vérification (checked.txt), que cdmupdate --check fait sous
// le compte.
#pragma once

#include <uikit.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string>

#define WV_UPDATER	"/apps/jet.app/libexec/cdmupdate"
#define WV_MODULE	"/res/widevine/libwidevinecdm.so"

// le dossier du compte : storage/ (les licences), checked.txt
static inline std::string wv_user_dir ()
{
	const char *h = getenv ("HOME");
	return std::string (h && *h ? h : "/home") + "/.jet/widevine";
}

// le module installé, et au système (uid 0) : celui que CDMWidevineOnyx prend
static inline bool wv_installed ()
{
	struct stat st;
	return stat (WV_MODULE, &st) == 0 && st.st_uid == 0 && S_ISREG (st.st_mode);
}

// (un module d'avant, dans le dossier du compte : plus jamais chargé ; retiré, ses licences gardées) -> true s'il y
// en avait un
static inline bool wv_forget_old ()
{
	std::string d = wv_user_dir ();
	bool had = access ((d + "/libwidevinecdm.so").c_str (), F_OK) == 0;
	if (!had)
		return false;
	unlink ((d + "/libwidevinecdm.so").c_str ());
	unlink ((d + "/manifest.json").c_str ());
	unlink ((d + "/version.txt").c_str ());
	return true;
}

// cdmupdate (arg : 0, « --check » ou « --remove »), sa sortie (stdout et stderr) dans *out, non bloquante. elevate :
// par la carte d'authd (reason : ce qu'elle dit) ; Jet reprend l'uid du compte aussitôt cdmupdate lancé, seul
// cdmupdate reste en uid 0. -> son pid ; -1 : échec ; -2 : élévation refusée
static inline pid_t wv_spawn (const char *arg, bool elevate, const char *reason, int *out)
{
	uid_t was = getuid ();
	if (elevate && was != 0 && ui_elevate ((long) reason) != 0)
		return -2;
	int p[2];
	pid_t pid = -1;
	if (pipe (p) == 0)
	{
		pid = fork ();
		if (pid == 0)
		{
			dup2 (p[1], 1);
			dup2 (p[1], 2);
			close (p[0]);
			close (p[1]);
			if (arg)
				execl (WV_UPDATER, WV_UPDATER, arg, (char *) nullptr);
			else
				execl (WV_UPDATER, WV_UPDATER, (char *) nullptr);
			_exit (127);
		}
		close (p[1]);
		if (pid < 0)
			close (p[0]);
		else
		{
			fcntl (p[0], F_SETFL, fcntl (p[0], F_GETFL) | O_NONBLOCK);
			*out = p[0];
		}
	}
	if (elevate && was != 0)
		setuid (was);	// (Jet : l'uid du compte de nouveau, cdmupdate lancé ou non)
	return pid;
}
