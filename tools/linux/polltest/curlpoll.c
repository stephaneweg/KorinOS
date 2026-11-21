/* curlpoll -- la boucle de l'ordonnanceur de curl de WebKit (CurlRequestScheduler::workerThread : curl_multi_poll
 * avec un délai infini, puis curl_multi_perform), avec le libcurl.a de Jet : combien de tours en 3 s ? Le thread
 * « curlThread » du process réseau de Jet tournait à vide (36 000 appels par seconde) sur une page au repos.
 *   curlpoll [URL]   (http://10.0.2.2:18765/dl par défaut) */
#include <curl/curl.h>
#include <limits.h>
#include <stdio.h>
#include <time.h>

static double now (void)
{
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec / 1e9;
}

static size_t sink (char *p, size_t s, size_t n, void *u) { (void) p; (void) u; return s * n; }

static void loop (CURLM *m, const char *what)
{
	int turns = 0, running = 0;
	double t = now ();
	while (now () - t < 3) {
		int nfds = 0;
		CURLMcode c = curl_multi_poll (m, 0, 0, 3000, &nfds);	/* (3 s : un tour par délai, au plus) */
		if (c != CURLM_OK) { printf ("  curl_multi_poll : %s\n", curl_multi_strerror (c)); break; }
		while (curl_multi_perform (m, &running) == CURLM_CALL_MULTI_PERFORM) { }
		long tmo = -2;
		curl_multi_timeout (m, &tmo);
		if (++turns <= 3) printf ("  tour %d : nfds %d, running %d, curl_multi_timeout %ld ms\n", turns, nfds, running, tmo);
	}
	printf ("%-34s -> %d tours en 3 s (attendu : 1 ou 2)\n", what, turns);
}

int main (int argc, char **argv)
{
	setvbuf (stdout, 0, _IONBF, 0);
	const char *url = argc > 1 ? argv[1] : "http://10.0.2.2:18765/dl";
	curl_global_init (CURL_GLOBAL_DEFAULT);
	printf ("%s\n", curl_version ());
	CURLM *m = curl_multi_init ();
	/* (sans requête ni réveil, INT_MAX attendrait toujours : 3 s ici) */
	CURL *e = curl_easy_init ();
	curl_easy_setopt (e, CURLOPT_URL, url);
	curl_easy_setopt (e, CURLOPT_WRITEFUNCTION, sink);
	curl_multi_add_handle (m, e);
	int running = 1;
	double t = now ();
	while (running && now () - t < 10) {
		curl_multi_poll (m, 0, 0, 1000, 0);
		curl_multi_perform (m, &running);
	}
	CURLMsg *msg;
	int left;
	while ((msg = curl_multi_info_read (m, &left)))
		printf ("requête finie : %s\n", curl_easy_strerror (msg->data.result));
	curl_multi_remove_handle (m, e);
	curl_easy_cleanup (e);
	loop (m, "après une requête (connexion en cache)");
	/* comme WebKit : curl_multi_wakeup avant chaque attente (une tâche confiée au thread de curl) */
	int turns = 0;
	double t2 = now ();
	while (now () - t2 < 3) {
		curl_multi_wakeup (m);
		int nfds = 0;
		curl_multi_poll (m, 0, 0, INT_MAX, &nfds);
		turns++;
	}
	printf ("%-34s -> %d tours en 3 s (un réveil par tour : beaucoup, mais chacun doit rendre la main)\n", "avec curl_multi_wakeup", turns);
	/* un seul réveil, puis l'attente : doit rendre la main une fois, puis attendre (3 s ici) */
	curl_multi_wakeup (m);
	double t3 = now ();
	curl_multi_poll (m, 0, 0, 3000, 0);
	printf ("%-34s -> rendu en %.3f s (attendu : tout de suite)\n", "un réveil, 1er poll", now () - t3);
	t3 = now ();
	curl_multi_poll (m, 0, 0, 3000, 0);
	printf ("%-34s -> rendu en %.3f s (attendu : 3 s)\n", "un réveil, 2e poll", now () - t3);
	return 0;
}
