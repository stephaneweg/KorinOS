//
// korin_thread.cpp -- les fils de haute priorite de Korin sur Linux (lie a Koton et a ses greffons :
// -Wl,--wrap=pthread_create). Sur Korin, SCHED_FIFO est la haute priorite de libposix, permise a tous ; Linux la refuse
// a un utilisateur sans droit temps reel (EPERM) : le fil part alors avec l'ordonnancement ordinaire.
//
#include <errno.h>
#include <pthread.h>

extern "C" {
int __real_pthread_create (pthread_t *, const pthread_attr_t *, void *(*) (void *), void *);

int __wrap_pthread_create (pthread_t *t, const pthread_attr_t *at, void *(*fn) (void *), void *arg)
{
	int r = __real_pthread_create (t, at, fn, arg);
	if (r != EPERM || !at) return r;
	pthread_attr_t a;
	pthread_attr_init (&a);
	size_t ss = 0;
	if (pthread_attr_getstacksize (at, &ss) == 0 && ss) pthread_attr_setstacksize (&a, ss);
	int det = 0;
	if (pthread_attr_getdetachstate (at, &det) == 0) pthread_attr_setdetachstate (&a, det);
	r = __real_pthread_create (t, &a, fn, arg);
	pthread_attr_destroy (&a);
	return r;
}
}
