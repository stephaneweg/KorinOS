//
// plug/kptest.cpp -- l'essai des greffons de Koton sur ARMPI, sans moteur ni fenetre : le vrai PlugHost
// lance les vrais process de SD0:/apps/koton.app/plugins, et l'essai tient lui-meme le role du moteur avec
// les memes ShmSource et ShmEffect (plugshm.h). Pour chaque greffon : process lance, parametres vus,
// etat lu et rendu, rendu sans depassement. Instrument : du son apres une note, le silence avant.
// Effet : deux instances du meme greffon, reglages differents, sorties differentes et chacune egale a
// ce qu'elle rend seule (leurs etats ne se melangent pas). Generateur : des notes. Puis un process
// tue (vu par l'hote, relance) et un hote mort (son greffon finit seul).
//
//   SD0:/bin/kptest		-> des lignes PASS / FAIL, « kptest : n echecs » ; code 0 si tout passe
//
#include "plughost.h"
#include <math.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>

extern char **environ;

using namespace kt;

static int s_fail, s_pass;

static void check (bool ok, const char *what, const char *id)
{
	printf ("%s %s %s\n", ok ? "PASS" : "FAIL", id, what);
	if (ok) s_pass++; else s_fail++;
}

// le role du moteur, bloc par bloc, sans jamais attendre le greffon plus que `lead` trames
enum { BLOCK = 256, LEAD = 4096 };

static float peak (const float *a, int n) { float m = 0; for (int i = 0; i < n; i++) { float v = fabsf (a[i]); if (v > m) m = v; } return m; }

static void waitDone (KpShm *s, unsigned long long upTo)
{
	for (int k = 0; k < 2000 && kp_ld64 (&s->done) < upTo; k++) usleep (1000);
}

// un instrument : -> la crete avant la note, *after celle de la note
static float playInstrument (PlugHost &h, PlugInstance *p, float *after, unsigned *under)
{
	KpShm *s = p->shm ();
	ShmSource src (s, LEAD);
	s->active = 1;
	float l[BLOCK], r[BLOCK], before = 0, a = 0;
	long long at = 0;
	src.reset (0);
	for (int b = 0; b < 400; b++)
	{
		if (b == 40) src.noteOn (at + LEAD, 60, 110);
		if (b == 300) src.noteOff (at + LEAD, 60);
		src.flushTo (at + BLOCK + LEAD);
		waitDone (s, (unsigned long long) (at + BLOCK));
		src.render (l, r, BLOCK, at);
		float pk = peak (l, BLOCK), pr = peak (r, BLOCK); if (pr > pk) pk = pr;
		if (b < 40) { if (pk > before) before = pk; }
		else if (pk > a) a = pk;
		at += BLOCK;
		if (!(b & 63)) h.poll ();
	}
	s->active = 0;
	*after = a; *under = src.underruns ();
	return before;
}

// un effet : un signal fixe (les deux canaux differents), n blocs -> sa sortie
enum { BLOCKS = 120, N = BLOCKS * BLOCK, SKIP = 3 * LEAD };
static float *s_inL, *s_inR;

static void makeInput ()
{
	s_inL = new float[N]; s_inR = new float[N];
	for (int k = 0; k < N; k++)
	{
		double t = (double) k / 44100.0;
		s_inL[k] = (float) (0.3 * sin (2 * M_PI * 220 * t) + 0.2 * sin (2 * M_PI * 1760 * t));
		s_inR[k] = (float) (0.3 * sin (2 * M_PI * 330 * t) + 0.2 * sin (2 * M_PI * 90 * t));
	}
}

static void runEffect (PlugHost &h, ShmEffect &fx, float *out, int blocks, long long *clock)
{
	KpShm *s = fx.shm ();
	s->active = 1;
	float l[BLOCK], r[BLOCK];
	long long at = *clock;
	for (int b = 0; b < blocks; b++)
	{
		memcpy (l, s_inL + at, sizeof l);
		memcpy (r, s_inR + at, sizeof r);
		// (le moteur n'attend pas ; ici, laisser le greffon suivre : les sorties se comparent trame a trame)
		fx.processAt (l, r, BLOCK, at);
		memcpy (out + at, l, sizeof l);
		waitDone (s, (unsigned long long) (at + BLOCK - LEAD / 2 > 0 ? at + BLOCK - LEAD / 2 : 0));
		at += BLOCK;
		if (!(b & 63)) h.poll ();
	}
	s->active = 0;
	*clock = at;
}

static double differ (const float *a, const float *b, int n)
{
	double e = 0;
	for (int i = 0; i < n; i++) { double d = a[i] - b[i]; e += d * d; }
	return sqrt (e / n);
}

static void testEffect (PlugHost &h, const PlugInfo &info, const char *id)
{
	float *a = new float[N], *b = new float[N], *c = new float[N];
	memset (a, 0, N * sizeof (float)); memset (b, 0, N * sizeof (float)); memset (c, 0, N * sizeof (float));
	// deux instances ensemble : le dernier parametre continu reglage a l'autre bout chez la seconde
	PlugInstance *p1 = h.create (id, 0, true), *p2 = h.create (id, 0, true);
	check (p1 && p2 && p1->ready () && p2->ready (), "deux process prets", id);
	if (!p1 || !p2 || !p1->ready () || !p2->ready ()) return;
	check (p1->pid () != p2->pid () && p1->shm () != p2->shm (), "deux process, deux regions", id);
	int k = -1;
	for (int i = 0; i < info.params.size (); i++) if (info.params[i].step == 0) k = i;
	if (k >= 0) h.setParam (p2, k, info.params[k].def == info.params[k].max ? info.params[k].min : info.params[k].max);
	// la premiere au reglage par defaut, en entrelacant : si elles partageaient leurs statiques, la
	// seconde changerait la premiere
	for (int i = 0; i < 20; i++) { h.poll (); usleep (5000); }
	long long t1 = 0, t2 = 0;
	{
		ShmEffect f1 (p1->shm (), LEAD), f2 (p2->shm (), LEAD);
		for (int b0 = 0; b0 < BLOCKS; b0 += 20)
		{
			runEffect (h, f1, a, 20, &t1);
			runEffect (h, f2, b, 20, &t2);
		}
		printf ("     sous-passes %u %u\n", f1.underruns (), f2.underruns ());
	}
	float pk = peak (a + SKIP, N - SKIP);
	double two = differ (a + SKIP, b + SKIP, N - SKIP);
	printf ("     crete %.3f, ecart entre les instances %.5f\n", (double) pk, two);
	check (pk > 0.01f && pk < 64 && pk == pk, "sortie non nulle, bornee", id);
	check (k < 0 || two > 1e-5, "les deux reglages sonnent differemment", id);
	// une troisieme, seule, aux defauts : la meme sortie que la premiere
	PlugInstance *p3 = h.create (id, 0, true);
	long long t3 = 0;
	if (p3)
	{
		ShmEffect f3 (p3->shm (), LEAD);
		runEffect (h, f3, c, BLOCKS, &t3);
	}
	double same = differ (a + SKIP, c + SKIP, N - SKIP);
	printf ("     ecart a une instance seule %.6f\n", same);
	check (p3 && same < 0.02 * pk, "une instance ne depend pas de l'autre", id);
	h.destroy (p1); h.destroy (p2); if (p3) h.destroy (p3);
	delete [] a; delete [] b; delete [] c;
}

static void testGenerator (PlugHost &h, const PlugInfo &info, const char *id)
{
	PlugInstance *g = h.generator (id);
	check (g && g->ready (), "process pret", id);
	if (!g) return;
	Project p;
	GeneratorModule m;
	m.id = "m1"; m.generatorId = id; m.durationBeats = 8;
	Riff r; r.spq = SPQ; r.lengthSlices = 8 * SPQ;
	bool ok = h.generate (m, p, 0, r);
	printf ("     %d notes\n", r.notes.size ());
	check (ok && r.notes.size () > 0, "des notes pour le bloc", id);
	// l'etat du module compte : le meme generateur, un autre parametre
	if (info.params.size ())
	{
		Str st; h.getState (g, st);
		check (st.c ()[0] == '{', "etat lu", id);
		m.id = "m2";
		Riff r2; r2.spq = SPQ; r2.lengthSlices = 8 * SPQ;
		check (h.generate (m, p, 0, r2) && r2.notes.size () > 0, "second module", id);
	}
}

// un hote qui meurt sans dire au revoir : son greffon finit seul
static int orphan (const char *id)
{
	PlugHost h;
	h.init (44100); h.scan ();
	PlugInstance *p = h.create (id, 0, true);
	if (!p || !p->ready ()) return 1;
	printf ("%d\n", p->pid ());
	fflush (stdout);
	_exit (0);						// (sans KP_BYE)
}

int main (int argc, char **argv)
{
	if (argc == 3 && !strcmp (argv[1], "--orphan")) return orphan (argv[2]);
	makeInput ();
	PlugHost h;
	check (h.init (44100), "hote pret", "-");
	int n = h.scan ();
	printf ("%d greffons dans %s\n", n, KP_DIR);
	check (n == 11, "11 greffons au catalogue", "-");
	h.installGeneratorHook ();
	for (int i = 0; i < h.count (); i++)
	{
		const PlugInfo &info = h.info (i);
		const char *id = info.id.c ();
		printf ("== %s (%s, %d parametres)\n", info.name.c (), info.kind == KP_INSTRUMENT ? "instrument" : info.kind == KP_EFFECT ? "effet" : "generateur", info.params.size ());
		check (info.params.size () > 0, "parametres vus", id);
		if (info.kind == KP_GENERATOR) { testGenerator (h, info, id); continue; }
		PlugInstance *p = h.create (id, 0, true);
		check (p && p->ready (), "process pret", id);
		if (!p || !p->ready ()) continue;
		check ((int) p->shm ()->nparams == info.params.size (), "le process dit autant de parametres que plugin.json", id);
		Str st;
		check (h.getState (p, st) && st.c ()[0] == '{', "etat lu", id);
		// un etat rendu puis relu : la valeur reste
		h.setParam (p, 0, info.params[0].max);
		Str st2; h.getState (p, st2);
		check (fabsf (p->param (0) - info.params[0].max) < 1e-3f, "parametre recu", id);
		check (h.setState (p, st.c ()), "etat rendu", id);
		for (int k = 0; k < 10 && fabsf (p->param (0) - info.params[0].def) > 1e-3f; k++) { usleep (2000); h.poll (); }
		check (fabsf (p->param (0) - info.params[0].def) < 1e-3f, "etat relu", id);
		if (info.kind == KP_INSTRUMENT)
		{
			float after = 0; unsigned under = 0;
			float before = playInstrument (h, p, &after, &under);
			printf ("     crete %.5f avant la note, %.3f apres, sous-passes %u\n", (double) before, (double) after, under);
			check (before < 1e-3f && after > 0.01f && after < 64, "le silence, puis la note", id);
			check (under < 8, "le rendu suit", id);
			h.destroy (p);
		}
		else { h.destroy (p); testEffect (h, info, id); }
		for (int k = 0; k < 30; k++) { h.poll (); usleep (10000); }
		printf ("     %d process encore la\n", h.instances ());
	}
	// un greffon tue : l'hote le voit, le relance
	const char *crash = h.count () ? h.find ("fm2") ? "fm2" : h.info (0).id.c () : 0;
	PlugInstance *c = crash ? h.create (crash, 0, true) : 0;
	if (c)
	{
		int pid = c->pid ();
		kill (pid, SIGKILL);
		for (int k = 0; k < 150 && c->state () == PLUG_READY; k++) { h.poll (); usleep (10000); }
		check (c->state () == PLUG_CRASHED, "un process tue est vu", crash);
		printf ("     %s\n", c->error ());
		check (h.restart (c) && h.getState (c, *new Str) && c->pid () != pid, "relance", crash);
		h.destroy (c);
	}
	// un hote tue : son greffon finit seul
	char *const av[] = { argv[0], (char *) "--orphan", (char *) "delay", 0 };
	int fd[2];
	posix_spawn_file_actions_t fa;
	pid_t kid = 0;
	if (pipe (fd) == 0 && posix_spawn_file_actions_init (&fa) == 0)
	{
		posix_spawn_file_actions_adddup2 (&fa, fd[1], 1);
		if (posix_spawn (&kid, argv[0], &fa, 0, av, environ) == 0)
		{
			close (fd[1]);
			char t[16] = { 0 };
			read (fd[0], t, sizeof t - 1);
			waitpid (kid, 0, 0);
			int pid = atoi (t);
			printf ("     greffon orphelin : process %d\n", pid);
			bool alive = true;
			for (int k = 0; k < 200 && alive; k++) { alive = kill (pid, 0) == 0; usleep (10000); }
			check (pid > 0 && !alive, "un hote mort : le greffon finit seul", "delay");
		}
		posix_spawn_file_actions_destroy (&fa);
	}
	h.shutdown ();
	printf ("kptest : %d reussis, %d echecs\n", s_pass, s_fail);
	return s_fail != 0;
}
