/**************************************************************************/
/*  os_korin.cpp                                                          */
/**************************************************************************/
/* Korin OS : OS_Korin (os_korin.h). */

#include "os_korin.h"

#include "display_server_korin.h"

#include "core/os/main_loop.h"
#include "core/profiling/profiling.h"
#include "main/main.h"

#include <stdlib.h>
#include <unistd.h>

static String s_exe_path;

void OS_Korin::initialize() {
	OS_Unix::initialize_core();
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

/* HOME vaut /sd0 sur la carte (SD0:) ; sans lui, le dossier des utilisateurs */
String OS_Korin::home() const {
	if (has_environment("HOME") && get_environment("HOME").is_absolute_path()) {
		return get_environment("HOME");
	}
	return "/sd0/home";
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

/* pas de /proc : le chemin vient d'argv[0] (godot_korin.cpp), rendu absolu */
void korin_set_executable_path(const char *p_argv0) {
	String p = String::utf8(p_argv0);
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
		return create_process("/sd0/apps/jet.app/main", args);
	}
	return create_process("/sd0/apps/files.app/main", args);
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
