// jit.h -- le JIT de JavaScriptCore, réglable (27/11/2026 ; N5, étape 0 de docs/etude/DURCISSEMENT-N3-N5.md). Jet pour
// le PC est construit avec tous les étages du JIT (DFG, FTL ; Wasm BBQ et OMG : tools/mesa/build-webkit.sh) ; JSC lit
// chaque option dans l'environnement (« JSC_<nom> », Options.cpp), que le lanceur de WebKit passe aux process web.
// Les étages les plus hauts (FTL, OMG) sont les plus gros et ceux des failles récentes de JSC : « réduit » (le défaut)
// les coupe ; « sans JIT » coupe tout (l'interpréteur seul : le plus sûr, le plus lent). Le réglage :
// « javascript=complet|reduit|sans » dans HOME:/jet/settings.ini. Appliqué par l'interface (main, avant WebKit) ; les
// process web en héritent (ils ne lisent plus le dossier du compte : bac à sable) ; le changer vaut pour les onglets
// ouverts ensuite.
#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <string>

enum { JIT_FULL, JIT_REDUCED, JIT_NONE };

static const char *const jit_keys[] = { "complet", "reduit", "sans" };

static inline int jit_read (const char *settings)
{
	int m = JIT_REDUCED;
	if (FILE *f = fopen (settings, "rb"))
	{
		char line[256];
		while (fgets (line, sizeof line, f))
		{
			line[strcspn (line, "\r\n")] = 0;
			if (strncmp (line, "javascript=", 11)) continue;
			for (int i = 0; i < 3; i++)
				if (!strcmp (line + 11, jit_keys[i])) m = i;
		}
		fclose (f);
	}
	return m;
}

static inline void jit_apply (int m)
{
	setenv ("JSC_useJIT", m == JIT_NONE ? "false" : "true", 1);
	setenv ("JSC_useFTLJIT", m == JIT_FULL ? "true" : "false", 1);
	setenv ("JSC_useOMGJIT", m == JIT_FULL ? "true" : "false", 1);
}

// la ligne « javascript= » remplacée, les autres gardées
static inline void jit_save (const char *dir, const char *settings, int m)
{
	std::string keep;
	if (FILE *f = fopen (settings, "rb"))
	{
		char line[256];
		while (fgets (line, sizeof line, f))
			if (strncmp (line, "javascript=", 11)) keep += line;
		fclose (f);
	}
	mkdir ("HOME:", 0755);
	mkdir (dir, 0755);
	if (FILE *f = fopen (settings, "wb"))
	{
		fprintf (f, "%sjavascript=%s\n", keep.c_str (), jit_keys[m]);
		fclose (f);
	}
}
