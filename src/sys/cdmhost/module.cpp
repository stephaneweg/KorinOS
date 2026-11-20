// module.cpp -- le module Widevine chargé (module.h).
#include "module.h"

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <vector>

#if defined(__linux__)
#include <dlfcn.h>
#else
#include "linux.h"
#endif

static void *s_module;

#if defined(__linux__)

static bool load (const char *path)
{
	s_module = dlopen (path, RTLD_NOW);
	if (!s_module)
		fprintf (stderr, "cdmhost : dlopen : %s\n", dlerror ());
	return s_module != nullptr;
}

void *cdm_module_sym (const char *n) { return s_module ? dlsym (s_module, n) : nullptr; }

#else

static bool load (const char *path)
{
	// (read () en gros morceaux, pas fread : la bibliothèque C lirait les 20 Mo du module par petits blocs, autant
	// d'allers-retours avec vfs ; 11 s sous QEMU le 20/11)
	int fd = open (path, O_RDONLY);
	struct stat st;
	if (fd < 0 || fstat (fd, &st) != 0 || st.st_size <= 0)
	{
		fprintf (stderr, "cdmhost : %s illisible\n", path);
		if (fd >= 0)
			close (fd);
		return false;
	}
	struct timespec t0, t1, t2;
	clock_gettime (CLOCK_MONOTONIC, &t0);
	std::vector<uint8_t> img ((size_t) st.st_size);
	size_t got = 0;
	while (got < img.size ())
	{
		ssize_t k = read (fd, img.data () + got, std::min (img.size () - got, (size_t) (4u << 20)));
		if (k <= 0)
			break;
		got += (size_t) k;
	}
	close (fd);
	img.resize (got);
	clock_gettime (CLOCK_MONOTONIC, &t1);
	char err[200];
	// (le chemin donné est celui que le module voit dans /proc/self/maps)
	s_module = linux_load (img.data (), img.size (), path, 0, err, sizeof err);
	clock_gettime (CLOCK_MONOTONIC, &t2);
	// (le temps de chaque étape : un module qui se charge lentement se voit au journal)
	fprintf (stderr, "cdmhost : module lu en %.2f s (%zu octets), chargé en %.2f s\n",
		 (double) (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9, img.size (),
		 (double) (t2.tv_sec - t1.tv_sec) + (t2.tv_nsec - t1.tv_nsec) / 1e9);
	if (!s_module)
		fprintf (stderr, "cdmhost : linux_load : %s\n", err);
	return s_module != nullptr;
}

void *cdm_module_sym (const char *n) { return s_module ? linux_sym ((struct linux_module *) s_module, n) : nullptr; }

#endif

bool cdm_module_load (const char *path)
{
	if (s_module)
		return true;
	if (!load (path))
		return false;
	void (*init) () = (void (*) ()) cdm_module_sym ("InitializeCdmModule_4");
	if (!init || !cdm_module_sym ("CreateCdmInstance") || !cdm_module_sym ("GetCdmVersion"))
	{
		fprintf (stderr, "cdmhost : %s n'est pas un module CDM (interface 4)\n", path);
		return false;
	}
	init ();
	return true;
}

const char *cdm_module_version ()
{
	const char *(*v) () = (const char *(*) ()) cdm_module_sym ("GetCdmVersion");
	return v ? v () : "?";
}

cdm::ContentDecryptionModule_10 *cdm_module_create (const char *key_system, void *(*get_host) (int, void *), void *user)
{
	void *(*create) (int, const char *, uint32_t, void *(*) (int, void *), void *) =
		(void *(*) (int, const char *, uint32_t, void *(*) (int, void *), void *)) cdm_module_sym ("CreateCdmInstance");
	if (!create)
		return nullptr;
	return static_cast<cdm::ContentDecryptionModule_10 *> (
		create (cdm::ContentDecryptionModule_10::kVersion, key_system, (uint32_t) strlen (key_system), get_host, user));
}

void cdm_module_unload ()
{
	void (*deinit) () = (void (*) ()) cdm_module_sym ("DeinitializeCdmModule");
	if (deinit)
		deinit ();
}
