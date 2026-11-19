// module.cpp -- le module Widevine chargé (module.h).
#include "module.h"

#include <stdio.h>
#include <string.h>

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
	FILE *f = fopen (path, "rb");
	if (!f)
	{
		fprintf (stderr, "cdmhost : %s illisible\n", path);
		return false;
	}
	std::vector<uint8_t> img;
	uint8_t b[65536];
	size_t k;
	while ((k = fread (b, 1, sizeof b, f)) > 0)
		img.insert (img.end (), b, b + k);
	fclose (f);
	char err[200];
	// (le chemin donné est celui que le module voit dans /proc/self/maps)
	s_module = linux_load (img.data (), img.size (), path, 0, err, sizeof err);
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
