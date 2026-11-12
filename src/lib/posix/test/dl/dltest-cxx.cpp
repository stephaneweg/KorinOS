/*
 * dltest-cxx.cpp -- module C++ d'essai du groupe dl de posixtest : une exception qui sort du
 * module et que l'hote rattrape (posixtest-dl.cpp), une autre levee et rattrapee dans le module,
 * un objet statique (constructeur au chargement, destructeur par __cxa_atexit a la fin).
 *     g++ -specs=posix.specs -fPIC -shared -nostdlib++ dltest-cxx.cpp -o dltest-cxx.so
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
#include <stdexcept>
#include <string>

static std::string s_static ("statique du module");

extern "C" void dlt_cxx_throw (int v)
{
	if (v)
		throw std::runtime_error ("du module " + std::to_string (v));
}

extern "C" const char *dlt_cxx_static (void) { return s_static.c_str (); }

extern "C" int dlt_cxx_inside (void)
{
	try
	{
		throw 5;
	}
	catch (int x)
	{
		return x;
	}
	return 0;
}
