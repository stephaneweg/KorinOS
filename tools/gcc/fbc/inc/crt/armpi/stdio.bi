''
'' stdio de newlib : stdin, stdout et stderr y sont des macros (rtlib/armpi/crt_stdio.c)
''
#ifndef __crt_armpi_stdio_bi__
#define __crt_armpi_stdio_bi__

#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2
#define BUFSIZ 1024
#define FILENAME_MAX 1024
#define FOPEN_MAX 20
#define P_tmpdir "/tmp"
#define L_tmpnam 1024
#define TMP_MAX 26

type FILE as __sFILE

extern "c"
declare function __armpi_stdin () as FILE ptr
declare function __armpi_stdout () as FILE ptr
declare function __armpi_stderr () as FILE ptr
#define stdin __armpi_stdin()
#define stdout __armpi_stdout()
#define stderr __armpi_stderr()

type fpos_t as longint

declare function snprintf (byval s as zstring ptr, byval n as size_t, byval format as zstring ptr, ...) as long
declare function vsnprintf (byval s as zstring ptr, byval n as size_t, byval format as zstring ptr, byval arg as va_list) as long
declare function popen (byval as zstring ptr, byval as zstring ptr) as FILE ptr
declare function pclose (byval as FILE ptr) as long
end extern

#endif
