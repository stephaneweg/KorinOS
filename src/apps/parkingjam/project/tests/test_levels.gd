extends SceneTree
# Essai du générateur : chaque niveau doit être soluble et non vide.
# godot --headless --path . -s res://tests/test_levels.gd

const LevelGen := preload("res://scripts/level_gen.gd")


func _init() -> void:
	var bad := 0
	var t0 := Time.get_ticks_msec()
	for n in range(1, 31):
		var lvl := LevelGen.generate(n)
		var rounds := LevelGen.solve_rounds(lvl)
		var filled := 0
		for c in lvl["cars"]:
			filled += int(c["len"])
		var ok: bool = rounds > 0 and lvl["cars"].size() >= 3
		if not ok:
			bad += 1
		print("niveau %2d : %dx%d, %2d voitures, %d plots, remplissage %d%%, %d vagues, bloquées au départ %d %s" % [
			n, lvl["w"], lvl["h"], lvl["cars"].size(), lvl["cones"].size(),
			100 * filled / (int(lvl["w"]) * int(lvl["h"])), rounds, LevelGen._blocked_at_start(lvl),
			"" if ok else "  <-- MAUVAIS"])
	# Même niveau, même parking.
	var a := LevelGen.generate(7)
	var b := LevelGen.generate(7)
	if str(a) != str(b):
		print("ERREUR : le niveau 7 change d'un appel à l'autre")
		bad += 1
	print("durée : %d ms ; %s" % [Time.get_ticks_msec() - t0, "TOUT VA BIEN" if bad == 0 else "%d ERREURS" % bad])
	quit(1 if bad else 0)
