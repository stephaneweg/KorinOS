//
// main.cpp -- Vector, the vector drawing editor of ARMPI (README.md, docs/PLAN-VECTOR-NATIVE.md).
//
//   vector [file.svg]
//   vector --render a.svg b.png [zoom]     the page as a PNG, no window
//   vector --bench N [out.txt]             N random objects: how fast the engine goes
//   vector --roundtrip a.svg b.svg         read, write back
//
#include "app.h"
#include <cstdio>
#include <cstring>

int main (int argc, char **argv)
{
	if (argc > 1 && !strncmp (argv[1], "--", 2)) return vapp::run_tool (argc, argv);
	return vapp::run_window (argc, argv);
}
