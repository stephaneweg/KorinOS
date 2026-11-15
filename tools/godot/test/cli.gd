# Essai sans affichage : « godot --headless -s cli.gd ». GDScript, fichiers, threads, temps.
extends SceneTree

func _init() -> void:
	print("GDSCRIPT OK : ", 6 * 7)
	var path := "user://essai.txt"
	var f := FileAccess.open(path, FileAccess.WRITE)
	f.store_string("bonjour Korin")
	f.close()
	print("FICHIER OK : ", FileAccess.get_file_as_string(path), " dans ", ProjectSettings.globalize_path("user://"))
	var th := Thread.new()
	th.start(func(): return 21 * 2)
	print("THREAD OK : ", th.wait_to_finish())
	print("OS : ", OS.get_name(), ", ", OS.get_processor_count(), " coeurs, ", OS.get_processor_name())
	print("TEMPS : ", Time.get_datetime_string_from_system())
	quit()
