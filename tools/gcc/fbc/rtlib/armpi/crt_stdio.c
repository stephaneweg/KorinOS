/* stdin, stdout, stderr pour les .bi : chez newlib ce sont des macros (inc/crt/armpi/stdio.bi) */

#include <stdio.h>

FILE *__armpi_stdin( void ) { return stdin; }
FILE *__armpi_stdout( void ) { return stdout; }
FILE *__armpi_stderr( void ) { return stderr; }
