# Après quelques images (le GL du parent a servi), lance la démo dans un nouveau process, comme le gestionnaire de
# projets lance l'éditeur, puis quitte. Le fils doit dessiner avec son propre contexte GL (docs/GODOT.md).
extends Node2D

var frames := 0

func _process(_delta: float) -> void:
	frames += 1
	if frames == 30:
		var pid := OS.create_instance(["--path", "/sd0/home/godot/demo", "--", "--quit-after-demo"])
		print("SPAWN : fils ", pid, " lancé par ", OS.get_executable_path())
	if frames == 40:
		get_tree().quit()
