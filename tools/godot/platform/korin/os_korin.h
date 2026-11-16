/**************************************************************************/
/*  os_korin.h                                                            */
/**************************************************************************/
/* Korin OS : la classe OS de Godot, sur OS_Unix (libposix de Korin) ; le son
 * par AudioKit, l'affichage par DisplayServerKorin (UIKit, armgl). */

#pragma once

#include "audio_driver_korin.h"

#include "drivers/unix/os_unix.h"
#include "servers/audio/audio_server.h"

class MainLoop;

class OS_Korin : public OS_Unix {
	MainLoop *main_loop = nullptr;
	AudioDriverKorin driver_korin;

	String home() const;

protected:
	virtual void initialize() override;
	virtual void finalize() override;
	virtual void initialize_joypads() override {}
	virtual void set_main_loop(MainLoop *p_main_loop) override;
	virtual void delete_main_loop() override;

public:
	virtual String get_identifier() const override;
	virtual String get_name() const override;
	virtual String get_distribution_name() const override;
	virtual String get_version() const override;
	virtual MainLoop *get_main_loop() const override;

	virtual String get_config_path() const override;
	virtual String get_data_path() const override;
	virtual String get_cache_path() const override;
	virtual String get_temp_path() const override;
	virtual String get_system_dir(SystemDir p_dir, bool p_shared_storage = true) const override;
	virtual String get_executable_path() const override;
	virtual Error shell_open(const String &p_uri) override;
	virtual String get_processor_name() const override;

	virtual bool _check_internal_feature_support(const String &p_feature) override;

	void run();

	OS_Korin();
};
