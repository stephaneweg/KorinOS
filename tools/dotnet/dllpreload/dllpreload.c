/*
 * dllpreload -- garde en memoire les assemblys de SD0:/etc/dllpreload.txt (docs/DOTNET.md).
 *
 * Accorde la memoire au fichier, une ligne par DLL : celles de la liste sont lues une fois, les
 * autres de /dotnet sont rendues. Lance par l'applet de control et au demarrage (startup.txt) :
 * « boot » ne fait rien si la liste est vide.
 *
 * Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence: Permission is
 * hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished to
 * do so, subject to the following conditions: The above copyright notice and this permission
 * notice shall be included in all copies or substantial portions of the Software. THE SOFTWARE IS
 * PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.
 */
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>

#define DIR_DLL "SD0:/dotnet/"
#define LIST "SD0:/etc/dllpreload.txt"
#define NAME_MAX_LEN 96

static char (*s_list)[NAME_MAX_LEN];
static int s_n;

static void list_read (void)
{
	s_list = calloc (512, NAME_MAX_LEN);
	FILE *f = fopen (LIST, "r");
	char line[NAME_MAX_LEN];
	if (f == 0)
		return;
	while (s_n < 512 && fgets (line, sizeof line, f))
	{
		line[strcspn (line, " \t\r\n")] = 0;
		if (line[0] && line[0] != '#')
			strcpy (s_list[s_n++], line);
	}
	fclose (f);
}

static int listed (const char *name)
{
	for (int i = 0; i < s_n; i++)
		if (strcasecmp (s_list[i], name) == 0)
			return 1;
	return 0;
}

int main (int argc, char **argv)
{
	list_read ();
	if (argc > 1 && strcmp (argv[1], "boot") == 0 && s_n == 0)
		return 0;
	DIR *d = opendir (DIR_DLL);
	if (d == 0)
	{
		perror (DIR_DLL);
		return 1;
	}
	long kept = 0;
	int n = 0, bad = 0;
	struct dirent *e;
	while ((e = readdir (d)) != 0)
	{
		char path[sizeof DIR_DLL + NAME_MAX_LEN];
		size_t l = strlen (e->d_name);
		if (l < 5 || l >= NAME_MAX_LEN || strcasecmp (e->d_name + l - 4, ".dll") != 0)
			continue;
		strcpy (path, DIR_DLL);
		strcat (path, e->d_name);
		if (!listed (e->d_name))
		{
			file_unpin (path);
			continue;
		}
		struct stat st;
		if (file_pin (path) != 0 || stat (path, &st) != 0)
		{
			perror (path);
			bad = 1;
			continue;
		}
		kept += st.st_size;
		n++;
	}
	closedir (d);
	printf ("dllpreload : %d assemblys, %ld Ko en memoire\n", n, kept / 1024);
	return bad;
}
