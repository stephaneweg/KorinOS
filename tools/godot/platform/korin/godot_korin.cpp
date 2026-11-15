/**************************************************************************/
/*  godot_korin.cpp                                                       */
/**************************************************************************/
/* Korin OS : le programme Godot (editeur, ou moteur seul pour un jeu). */

#include "os_korin.h"

#include "core/profiling/profiling.h"
#include "main/main.h"

#include <limits.h>
#include <locale.h>
#include <stdlib.h>
#include <unistd.h>

void korin_set_executable_path(const char *p_argv0);

#if !defined(TOOLS_ENABLED) && defined(__GNUC__)
// Comme sur Linux : la section que l'exportateur remplit avec le PCK du jeu.
static const char dummy[8] __attribute__((section("pck"), used)) = { 0 };
extern "C" const char *pck_section_dummy_call() __attribute__((used));
extern "C" const char *pck_section_dummy_call() {
	return &dummy[0];
}
#endif

int main(int argc, char *argv[]) {
	korin_set_executable_path(argv[0]);
	godot_init_profiler();

	OS_Korin os;

	setlocale(LC_CTYPE, "");

	TEST_MAIN_OVERRIDE

	char *cwd = (char *)malloc(PATH_MAX);
	ERR_FAIL_NULL_V(cwd, ERR_OUT_OF_MEMORY);
	char *ret = getcwd(cwd, PATH_MAX);

	Error err = Main::setup(argv[0], argc - 1, &argv[1]);
	if (err != OK) {
		free(cwd);
		return err == ERR_HELP ? EXIT_SUCCESS : EXIT_FAILURE;
	}

	if (Main::start() == EXIT_SUCCESS) {
		os.run();
	} else {
		os.set_exit_code(EXIT_FAILURE);
	}
	Main::cleanup();

	if (ret && chdir(cwd) != 0) {
		ERR_PRINT("Couldn't return to previous working directory.");
	}
	free(cwd);
	godot_cleanup_profiler();
	return os.get_exit_code();
}
