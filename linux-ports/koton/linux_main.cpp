//
// linux_main.cpp -- Koton (« KotonStudio ») sous Linux : l'entree du programme. Le main.cpp de Korin
// (src/apps/koton) est compile tel quel, renomme kport_app_main (-Dmain=kport_app_main) ; ici, avant lui, les
// chemins de Korin (korin_paths.cpp), et un mode sans fenetre pour exporter une chanson :
//
//   koton [chanson.kson]                              la fenetre (comme sous Korin)
//   koton --export-wav chanson.kson sortie.wav        la chanson rendue hors ligne (le meme chemin que
//                                                     Fichier > Exporter en WAV : AudioHost::exportWav)
//   koton --soundfont fichier.sf2 ...                 une autre SoundFont pour ce lancement
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "korin_port.h"
#include "ui/view.h"
#include "ui/doc.h"
#include "ui/audio.h"
#include "ui/ops.h"

int kport_app_main (int argc, char **argv);

static void progress (int pct) { if (pct % 10 == 0) fprintf (stderr, "\rkoton: export %d %%", pct); }

static int export_wav (const char *song, const char *out)
{
	using namespace kui;
	new (&g_doc) Doc ();
	g_drumCatalog.load ();
	char err[160];
	if (!g_doc.load (song, err, sizeof err)) { fprintf (stderr, "koton: %s : %s\n", song, err); return 1; }
	g_audio.start ("");
	for (int i = 0; i < 3000 && !g_audio.sf; i++) usleep (10000);
	if (!g_audio.sf) { fprintf (stderr, "koton: la SoundFont ne se charge pas\n"); return 1; }
	fprintf (stderr, "koton: %s, %d pistes, SoundFont %s\n", song, g_doc.p.tracks.size (), g_audio.sfName);
	bool ok = g_audio.exportWav (g_doc, out, progress);
	fprintf (stderr, "\n");
	g_audio.shutdown ();
	if (!ok) { fprintf (stderr, "koton: l'export a echoue\n"); return 1; }
	printf ("%s\n", out);
	return 0;
}

int main (int argc, char **argv)
{
	kport_setup ("koton", argv[0]);
	for (int i = 1; i + 1 < argc; i++)
		if (!strcmp (argv[i], "--soundfont"))
		{
			setenv ("KORIN_SOUNDFONT", argv[i + 1], 1);
			kport_setup ("koton", argv[0]);
			for (int k = i; k + 2 <= argc; k++) argv[k] = argv[k + 2];
			argc -= 2;
			break;
		}
	if (argc > 3 && !strcmp (argv[1], "--export-wav")) return export_wav (argv[2], argv[3]);
	if (argc > 1 && (!strcmp (argv[1], "--help") || !strcmp (argv[1], "-h")))
	{
		printf ("koton [chanson.kson]\nkoton --export-wav chanson.kson sortie.wav\nkoton --soundfont fichier.sf2 ...\n"
			"SoundFont : %s\nHOME:/ = %s\n", kport_soundfont (), kport_homedir ());
		return 0;
	}
	return kport_app_main (argc, argv);
}
