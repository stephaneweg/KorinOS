/*
 * fnmatch.c -- fnmatch (libposix, docs/POSIX.md) : *, ?, [a-z], [!x], \, FNM_NOESCAPE, FNM_PATHNAME,
 * FNM_PERIOD, FNM_CASEFOLD. newlib a l'en-tete, pas la fonction.
 *
 * Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence: Permission is
 * hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the Software is furnished to
 * do so, subject to the following conditions: The above copyright notice and this permission
 * notice shall be included in all copies or substantial portions of the Software. THE SOFTWARE
 * IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
 */
#include <ctype.h>
#include <fnmatch.h>

static int fold (int c, int flags) { return flags & FNM_CASEFOLD ? tolower (c) : c; }

/* p sur le caractere apres « [ » -> apres « ] », 0 : pas d'ensemble ; *hit = 1 si c en est */
static const char *bracket (const char *p, int c, int flags, int *hit)
{
	int neg = *p == '!' || *p == '^';
	if (neg)
		p++;
	int found = 0, first = 1;
	while (first || *p != ']')
	{
		first = 0;
		int lo = (unsigned char) *p++;
		if (lo == 0)
			return 0;
		if (lo == 92 && !(flags & FNM_NOESCAPE) && *p != 0)
			lo = (unsigned char) *p++;
		int hi = lo;
		if (p[0] == '-' && p[1] != ']' && p[1] != 0)
		{
			hi = (unsigned char) p[1];
			p += 2;
		}
		if (fold (c, flags) >= fold (lo, flags) && fold (c, flags) <= fold (hi, flags))
			found = 1;
	}
	*hit = found != neg;
	return p + 1;
}

static int match (const char *p, const char *s, const char *start, int flags)
{
	for (;; p++, s++)
	{
		int c = (unsigned char) *s;
		if (c == '.' && flags & FNM_PERIOD && *p != '.'
		    && (s == start || (flags & FNM_PATHNAME && s[-1] == '/')))
			return FNM_NOMATCH;
		switch (*p)
		{
		case 0:
			return c == 0 ? 0 : FNM_NOMATCH;
		case '?':
			if (c == 0 || (c == '/' && flags & FNM_PATHNAME))
				return FNM_NOMATCH;
			break;
		case '*':
			while (*p == '*')
				p++;
			for (;; s++)
			{
				if (match (p, s, start, flags & ~FNM_PERIOD) == 0)
					return 0;
				if (*s == 0 || (*s == '/' && flags & FNM_PATHNAME))
					return FNM_NOMATCH;
			}
		case '[':
		{
			int hit = 0;
			const char *e = bracket (p + 1, c, flags, &hit);
			if (e == 0)
			{
				if (c != '[')
					return FNM_NOMATCH;
				break;
			}
			if (c == 0 || !hit || (c == '/' && flags & FNM_PATHNAME))
				return FNM_NOMATCH;
			p = e - 1;
			break;
		}
		case 92:		/* la barre inverse */
			if (!(flags & FNM_NOESCAPE) && p[1] != 0)
				p++;
			/* fallthrough */
		default:
			if (c == 0 || fold (c, flags) != fold ((unsigned char) *p, flags))
				return FNM_NOMATCH;
		}
	}
}

int fnmatch (const char *pattern, const char *string, int flags)
{
	return match (pattern, string, string, flags);
}
