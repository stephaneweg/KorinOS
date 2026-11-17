/**************************************************************************/
/*  os_korin.cpp                                                          */
/**************************************************************************/
/* Korin OS : OS_Korin (os_korin.h). */

#include "os_korin.h"

#include "display_server_korin.h"

#include "core/os/main_loop.h"
#include "core/profiling/profiling.h"
#include "main/main.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static String s_exe_path;

/* Avant les comptes (16/11/2026), HOME valait /home pour tous : les réglages, données et caches de Godot sont dans
 * /home/.config/godot, /home/.local/share/godot, /home/.cache/godot. Sous un compte (HOME = /home/<nom>, libposix
 * le prend de users.ini), ils vont dans son dossier au premier lancement, comme Jet le fait pour /home/jet. */
static void move_old_home(const String &p_home) {
	if (p_home == "/home" || !p_home.begins_with("/")) {
		return;
	}
	static const char *const dirs[] = { ".config", ".local", ".local/share", ".cache", nullptr };
	static const char *const moved[] = { ".config/godot", ".local/share/godot", ".cache/godot", nullptr };
	for (int i = 0; moved[i]; i++) {
		String from = String("/home/") + moved[i], to = p_home.path_join(moved[i]);
		struct stat st;
		if (stat(to.utf8().get_data(), &st) == 0 || stat(from.utf8().get_data(), &st) != 0) {
			continue;
		}
		mkdir(p_home.utf8().get_data(), 0755);
		for (int j = 0; dirs[j]; j++) {
			mkdir(p_home.path_join(dirs[j]).utf8().get_data(), 0755);
		}
		rename(from.utf8().get_data(), to.utf8().get_data());
	}
}

void OS_Korin::initialize() {
	OS_Unix::initialize_core();
	move_old_home(home());
}

void OS_Korin::finalize() {
	delete_main_loop();
}

void OS_Korin::set_main_loop(MainLoop *p_main_loop) {
	main_loop = p_main_loop;
}

void OS_Korin::delete_main_loop() {
	if (main_loop) {
		memdelete(main_loop);
	}
	main_loop = nullptr;
}

MainLoop *OS_Korin::get_main_loop() const {
	return main_loop;
}

String OS_Korin::get_identifier() const {
	return "korin";
}

String OS_Korin::get_name() const {
	return "Korin";
}

String OS_Korin::get_distribution_name() const {
	return "Korin OS";
}

String OS_Korin::get_version() const {
	return "";
}

/* HOME : le dossier du compte (/home/<nom>, de users.ini par libposix) ; sans lui, /home */
String OS_Korin::home() const {
	if (has_environment("HOME") && get_environment("HOME").is_absolute_path()) {
		return get_environment("HOME");
	}
	return "/home";
}

String OS_Korin::get_config_path() const {
	return home().path_join(".config");
}

String OS_Korin::get_data_path() const {
	return home().path_join(".local/share");
}

String OS_Korin::get_cache_path() const {
	return home().path_join(".cache");
}

String OS_Korin::get_temp_path() const {
	if (has_environment("TMPDIR") && get_environment("TMPDIR").is_absolute_path()) {
		return get_environment("TMPDIR");
	}
	return "/tmp";
}

String OS_Korin::get_system_dir(SystemDir p_dir, bool p_shared_storage) const {
	switch (p_dir) {
		case SYSTEM_DIR_DESKTOP:
			return home();
		case SYSTEM_DIR_DOCUMENTS:
			return home().path_join("documents");
		case SYSTEM_DIR_DOWNLOADS:
			return home().path_join("downloads");
		case SYSTEM_DIR_MUSIC:
			return home().path_join("music");
		case SYSTEM_DIR_PICTURES:
			return home().path_join("photos");
		case SYSTEM_DIR_MOVIES:
			return home().path_join("videos");
		default:
			return home();
	}
}

static bool is_file(const String &p_path) {
	struct stat st;
	return stat(p_path.utf8().get_data(), &st) == 0 && S_ISREG(st.st_mode);
}

/* Le noyau ne garde pas le chemin du programme : il vient d'argv[0] (godot_korin.cpp). Le bureau lance une app avec
 * son nom seul (« godot ») : son programme est alors /apps/<nom>.app/main (l'editeur, un jeu exporte installe
 * en app) ; sinon le PATH. Un argv[0] relatif est lu depuis le dossier courant. L'editeur s'en sert pour se relancer
 * (conversion d'un projet, changement de rendu) : faux, il se fermait (15/11, ZBook : « /godot »). */
void korin_set_executable_path(const char *p_argv0) {
	String p = String::utf8(p_argv0);
	if (!p.contains_char('/') && !p.contains_char(':')) {
		String app = "/apps/" + p + ".app/main";
		if (is_file(app)) {
			s_exe_path = app;
			return;
		}
		const char *path = getenv("PATH");
		Vector<String> dirs = String::utf8(path ? path : "/bin").split(":", false);
		for (const String &d : dirs) {
			if (is_file(d.path_join(p))) {
				s_exe_path = d.path_join(p);
				return;
			}
		}
	}
	if (!p.is_absolute_path()) {
		char cwd[1024];
		if (getcwd(cwd, sizeof cwd)) {
			p = String::utf8(cwd).path_join(p);
		}
	}
	s_exe_path = p.simplify_path();
}

String OS_Korin::get_executable_path() const {
	return s_exe_path;
}

/* un lien s'ouvre dans Jet, un dossier dans Fichiers, comme les autres apps */
Error OS_Korin::shell_open(const String &p_uri) {
	List<String> args;
	args.push_back(p_uri);
	if (p_uri.begins_with("http://") || p_uri.begins_with("https://")) {
		return create_process("/apps/jet.app/main", args);
	}
	return create_process("/apps/files.app/main", args);
}

String OS_Korin::get_processor_name() const {
#if defined(__x86_64__)
	return "x86-64";
#else
	return "AArch64";
#endif
}

bool OS_Korin::_check_internal_feature_support(const String &p_feature) {
	return p_feature == "pc" || p_feature == "korin";
}

void OS_Korin::run() {
	if (!main_loop) {
		return;
	}
	main_loop->initialize();
	while (true) {
		DisplayServer::get_singleton()->process_events();
		if (Main::iteration()) {
			break;
		}
	}
	main_loop->finalize();
}

OS_Korin::OS_Korin() {
	AudioDriverManager::add_driver(&driver_korin);
	DisplayServerKorin::register_korin_driver();
}
