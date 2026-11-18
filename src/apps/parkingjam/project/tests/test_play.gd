extends SceneTree
# Essai du jeu sans écran : charge la scène, joue les premiers niveaux en faisant sortir les voitures une à une
# (par de faux clics de souris), essaie Annuler et Recommencer, et vérifie que le niveau est gagné.
# godot --headless --path . -s res://tests/test_play.gd

const LevelGen := preload("res://scripts/level_gen.gd")

var errors := 0


func _initialize() -> void:
	_run.call_deferred()


func _check(cond: bool, what: String) -> void:
	if not cond:
		errors += 1
		print("ERREUR : ", what)


func _click(game: Node, pos: Vector2) -> void:
	for pressed in [true, false]:
		var ev := InputEventMouseButton.new()
		ev.button_index = MOUSE_BUTTON_LEFT
		ev.pressed = pressed
		ev.position = pos
		game._unhandled_input(ev)


func _run() -> void:
	var game: Node = load("res://scenes/main.tscn").instantiate()
	game.save_enabled = false        # ne pas toucher à la progression du joueur
	root.add_child(game)
	await process_frame
	game.unlocked = 1
	game.start_level(1)
	for n in range(1, 4):
		_check(game.level_num == n, "niveau attendu %d, trouvé %d" % [n, game.level_num])
		var lvl: Dictionary = game.lvl
		var first := true
		var guard := 0
		while not game.won and guard < 200:
			guard += 1
			var done := false
			for i in lvl["cars"].size():
				if game.gone[i]:
					continue
				var car: Dictionary = lvl["cars"][i]
				var d := LevelGen.exit_dir(game.occ, lvl["w"], lvl["h"], car, i)
				if d == 0:
					continue
				# Un clic du côté où elle doit sortir.
				var ax := Vector2(1, 0) if car["horiz"] else Vector2(0, 1)
				var pos: Vector2 = game._car_center(car) + ax * d * game.cell * 0.4
				_click(game, pos)
				_check(game.gone[i], "niveau %d : la voiture %d n'est pas sortie" % [n, i])
				if first:
					# Annuler la remet en place, puis on rejoue.
					game.undo()
					_check(not game.gone[i] and game.moves == 0, "Annuler n'a pas remis la voiture %d" % i)
					_click(game, pos)
					first = false
				done = true
				break
			_check(done, "niveau %d : plus aucune voiture ne peut sortir" % n)
			if not done:
				break
			for f in 3:
				await process_frame
		_check(game.won, "niveau %d non gagné" % n)
		print("niveau %d gagné en %d coups" % [n, game.moves])
		await create_timer(1.2).timeout
		_check(game.win_layer.visible, "niveau %d : pas de panneau de victoire" % n)
		_check(game.unlocked == n + 1, "niveau %d : le suivant n'est pas débloqué" % n)
		game.start_level(n + 1)
		await process_frame
	game.restart_level()
	_check(game.moves == 0 and not game.won, "Recommencer")
	print("TOUT VA BIEN" if errors == 0 else "%d ERREURS" % errors)
	quit(1 if errors else 0)
