/*
 * posixtest-dl.cpp -- la part C++ de posixtest (groupe dl) : rattraper une exception levee dans
 * un module charge par dlopen (dl/dltest-cxx.cpp). Le derouleur traverse le module (ses tables
 * inscrites par dlopen.c) puis l'hote.
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
#include <cstdio>
#include <exception>

extern "C" int dlt_catch (void (*fn) (int), int v, char *msg, unsigned n)
{
	try
	{
		fn (v);
	}
	catch (const std::exception &e)
	{
		std::snprintf (msg, n, "%s", e.what ());
		return 1;
	}
	catch (...)
	{
		return 2;
	}
	return 0;
}
