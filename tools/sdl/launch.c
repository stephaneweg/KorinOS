/*
 * launch.c -- le main d'une app SDL portee (docs/SDL.md) : le jeu ne sait pas ou sont ses donnees, le bureau lance
 * main sans arguments. Pose l'environnement, va a la racine (SuperTuxKart coupe un chemin a son deux-points, il
 * prend donc /apps/...), puis lance le vrai programme et attend sa fin.
 *     gcc -DSUPERTUX launch.c    ou    -DSUPERTUXKART
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int main (int argc, char **argv)
{
	setenv ("LANG", "fr_FR.UTF-8", 1);
	setenv ("LANGUAGE", "fr", 1);
	chdir ("SD0:/");
#ifdef SUPERTUX
	static char *args[] = { "SD0:/apps/supertux.app/supertux2", "--renderer", "sdl", "--fullscreen", "--geometry", "0x0", NULL };	/* 0x0 : la taille de l'ecran, quelle qu'elle soit */
	setenv ("SUPERTUX2_DATA_DIR", "SD0:/apps/supertux.app/data", 1);
	setenv ("SUPERTUX2_USER_DIR", "SD0:/home/supertux", 1);
#else
	static char *args[] = { "SD0:/apps/supertuxkart.app/supertuxkart", "--fullscreen",	/* la taille de l'ecran : le pilote SDL la donne a la fenetre */
		"--disable-dynamic-lights", "--disable-hd-textures", NULL };
	setenv ("SUPERTUXKART_DATADIR", "/apps/supertuxkart.app", 1);
	setenv ("SUPERTUXKART_SAVEDIR", "SD0:/home/supertuxkart", 1);
	setenv ("HOME", "SD0:/home/supertuxkart", 1);
#endif
	execv (args[0], args);
	perror (args[0]);
	return 1;
}
