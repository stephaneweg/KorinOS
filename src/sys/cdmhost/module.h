// module.h -- le module Widevine chargé : par dlopen sous Linux (les essais), par liblinux dans Korin
// (docs/etude/WIDEVINE.md § 3.1). cdmhost et cdmtest s'en servent.
#pragma once

#include "content_decryption_module.h"

// charge le module (une seule fois par process) et appelle InitializeCdmModule_4 ; false : message sur stderr
bool cdm_module_load (const char *path);
void *cdm_module_sym (const char *name);
// la version du module (GetCdmVersion), « ? » sans module
const char *cdm_module_version ();
// une instance de l'interface 10 pour ce système de clés, hôte rendu par get_host (nullptr : refus)
cdm::ContentDecryptionModule_10 *cdm_module_create (const char *key_system, void *(*get_host) (int, void *), void *user);
// DeinitializeCdmModule
void cdm_module_unload ();
