//
// linux_main.cpp -- Forge (la CAO de Korin) sous Linux : l'entree du programme. Le main.cpp de Korin (src/apps/forge) est
// compile tel quel, renomme kport_app_main (-Dmain=kport_app_main) ; ici, avant lui, les chemins de Korin
// (korin_paths.cpp : HOME:/forge -> ~/Korin/forge, /apps/forge.app/lang -> src/apps/forge/lang), le coffre a secrets (le
// jeton du MCP : korin_vault.cpp, libsecret) et les mots qui changent sous Linux.
//
//   forge [document.forge] [--mcp [port]] [--mcp-token t|-]       comme sous Korin
//   forge --selftest [s]                                          les verifications de Korin (code 0 : tout passe)
//
// OpenCASCADE est celui du systeme (opencascade 7.9 sur Fedora 45 ; 8.0.1 sous Korin), PlaneGCS est construit avec le
// port (forge/build-planegcs.sh). La vue est rendue par le processeur, comme sous Korin.
//
#include "korin_port.h"

int kport_app_main (int argc, char **argv);

// (Forge ne joue pas de son)
extern "C" int kport_audio_reload_soundfont (const char *) { return 0; }

int main (int argc, char **argv)
{
	kport_setup ("forge", argv[0]);
	kport_vault_install ();
	kport_tr_add ("le coffre \xC3\xA0 secrets de Korin", "le trousseau du bureau (Secret Service)");
	return kport_app_main (argc, argv);
}
