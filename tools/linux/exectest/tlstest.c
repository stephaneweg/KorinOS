/* tlstest -- jalon 1 de l'hôte Linux : les variables de thread d'un exécutable (TLS « local exec » sous %fs,
 * posé par liblinux dans la réserve de l'hôte), dans le fil principal et dans des threads, et errno. */
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static __thread int counter = 42;
static __thread char name[32] = "initial";
static __thread long big[1000];	/* (.tbss) */

static void *worker (void *arg)
{
	long id = (long) arg;
	int ok = counter == 42 && !strcmp (name, "initial") && big[999] == 0;
	counter = (int) id * 100;
	snprintf (name, sizeof name, "thread %ld", id);
	big[999] = id;
	usleep (20000);
	ok = ok && counter == (int) id * 100 && big[999] == id;
	errno = (int) id;
	usleep (10000);
	ok = ok && errno == (int) id;
	printf ("tlstest : %s : %s\n", name, ok ? "bon" : "MAUVAIS");
	return (void *) (long) ok;
}

int main (void)
{
	int ok = counter == 42 && !strcmp (name, "initial");
	printf ("tlstest : fil principal, counter %d, name %s, &counter %p\n", counter, name, (void *) &counter);
	counter = 1;
	pthread_t t[4];
	for (long i = 0; i < 4; i++)
		if (pthread_create (&t[i], 0, worker, (void *) (i + 1)) != 0)
			ok = 0;
	for (int i = 0; i < 4; i++)
	{
		void *r = 0;
		pthread_join (t[i], &r);
		ok = ok && r;
	}
	ok = ok && counter == 1;
	printf ("tlstest : %s\n", ok ? "RÉUSSI" : "ÉCHEC");
	return ok ? 0 : 1;
}
