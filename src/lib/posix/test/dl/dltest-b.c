/*
 * dltest-b.c -- module d'essai du groupe dl de posixtest : il depend de dltest-a.so (DT_NEEDED,
 * trouve par son DT_RUNPATH $ORIGIN) et lit sa globale et sa variable __thread.
 *     gcc -specs=posix.specs -fPIC -shared dltest-b.c dltest-a.so -Wl,-rpath,'$ORIGIN' -o dltest-b.so
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
extern int dlt_a_counter;
extern __thread int dlt_a_tls;
int dlt_a_host (int);

int dlt_b_get (void) { return dlt_a_counter * 1000 + dlt_a_tls; }
int dlt_b_host (int x) { return dlt_a_host (x) * 10; }
