//
// linux_main.cpp -- TerraCube sous Linux : l'entree du programme. Le main.cpp de Korin (src/apps/terracube) est compile
// tel quel, renomme kport_app_main (-Dmain=kport_app_main) ; ici, avant lui, les chemins de Korin (korin_paths.cpp :
// HOME:/terracube -> ~/Korin/terracube, /apps/terracube.app -> src/apps/terracube/data), le coffre a secrets (le jeton
// du MCP : korin_vault.cpp, libsecret) et les mots qui changent sous Linux.
//
//   terracube [--world nom] [--seed n] [--distance n] [--no-api] [--api-any] [--mcp [port]] [--mcp-token t|-]
//             [--selftest [s]]                                   comme sous Korin (docs/TERRACUBE.md)
//
// La 3D passe par OpenGL ES 3 (EGL : common/glkit_egl.cpp) ; LIBGL_ALWAYS_SOFTWARE=1 la fait rendre par le processeur
// (llvmpipe), comme softpipe sous Korin sans GPU. Le son : AudioKit (common/audiokit_fluid.cpp, sortie FluidSynth).
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include "korin_port.h"

int kport_app_main (int argc, char **argv);

static void linux_words ()
{
	static std::string py = std::string (kport_rootdir ()) + "/src/apps/terracube/data/python";
	static std::string bas = std::string (kport_rootdir ()) + "/src/apps/terracube/data/basic";
	static std::string ex = "Exemples Python : " + py + " (terracube.py et examples/).\n"
		"  python3 " + py + "/examples/house.py\n\n"
		"Exemples BASIC (pour le BASIC de Korin, a recopier dans Korin) : " + bas + ".\n\n"
		"Le jeu doit tourner, avec Pilotage > Scripts coch\xC3\xA9 (port 4730).";
	kport_tr_add ("Exemples Python : /apps/terracube.app/python (terracube.py et examples/).\n"
		"  python3 /apps/terracube.app/python/examples/house.py\n\n"
		"Exemples BASIC : /apps/terracube.app/basic.\n  basic /apps/terracube.app/basic/pyramid.bas\n\n"
		"Le jeu doit tourner, avec Pilotage > Scripts coch\xC3\xA9 (port 4730).", ex.c_str ());
	kport_tr_add ("le coffre \xC3\xA0 secrets de Korin", "le trousseau du bureau (Secret Service)");
	kport_tr_add ("TerraCube demande OpenGL ES 3 (GlKit) : pas de GPU ni de rendu par le processeur ici.",
		"TerraCube demande OpenGL ES 3.0 (EGL) : ni le pilote du GPU ni Mesa (llvmpipe) ne le donnent ici.");
}

int main (int argc, char **argv)
{
	kport_setup ("terracube", argv[0]);
	kport_vault_install ();
	linux_words ();
	return kport_app_main (argc, argv);
}
