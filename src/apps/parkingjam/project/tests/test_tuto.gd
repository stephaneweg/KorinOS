extends SceneTree
# Essai du tutoriel du niveau 1, sans écran : à chaque étape, la voiture montrée doit sortir quand on touche le
# point où pulse la main ; l'étape 1 doit montrer une marche arrière ; le bandeau disparaît à la victoire et
# n'existe pas au niveau 2.
# godot --headless --path . -s res://tests/test_tuto.gd

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


# Le point que montre la main (comme _draw_tuto).
func _hint_point(game: Node) -> Vector2:
	var car: Dictionary = game.lvl["cars"][game.tuto_car]
	var ax := Vector2(1, 0) if car["horiz"] else Vector2(0, 1)
	return game._car_center(car) + ax * float(game.tuto_dir) * (float(car["len"]) * 0.5 - 0.5) * game.cell


func _run() -> void:
	var game: Node = load("res://scenes/main.tscn").instantiate()
	game.save_enabled = false
	root.add_child(game)
	await process_frame
	game.unlocked = 1
	game.start_level(1)
	_check(game.tuto_step == 0 and game.tuto_panel.visible, "tutoriel absent au niveau 1")
	for step in [0, 1]:
		var i: int = game.tuto_car
		_check(i >= 0, "étape %d : aucune voiture montrée" % step)
		if i < 0:
			break
		var car: Dictionary = game.lvl["cars"][i]
		var back: bool = game.tuto_dir != int(car["facing"])
		print("étape %d : voiture %d, %s" % [step, i, "marche arrière" if back else "marche avant"])
		if step == 1:
			_check(back, "étape 1 : la voiture montrée ne recule pas")
		_click(game, _hint_point(game))
		_check(game.gone[i], "étape %d : la voiture montrée n'est pas sortie" % step)
		_check(game.tuto_step == step + 1, "étape attendue %d, trouvée %d" % [step + 1, game.tuto_step])
	_check(game.tuto_car == -1 and game.tuto_panel.visible, "étape 2 : bandeau sans flèche attendu")
	# Finir le niveau : la première voiture qui sort, tant qu'il en reste.
	for t in 40:
		var moved := false
		for i in game.lvl["cars"].size():
			if game.gone[i]:
				continue
			for d in [-1, 1]:
				if LevelGen.probe(game.occ, game.lvl["w"], game.lvl["h"], game.lvl["cars"][i], d)["exit"]:
					game.move_car(i, d)
					moved = true
					break
			if moved:
				break
		if not moved:
			break
	await create_timer(1.2).timeout
	_check(game.won, "niveau 1 pas gagné")
	_check(not game.tuto_panel.visible, "bandeau encore visible après la victoire")
	game.start_level(2)
	_check(game.tuto_step == -1 and not game.tuto_panel.visible, "tutoriel au niveau 2")
	print("TOUT VA BIEN" if errors == 0 else "%d ERREUR(S)" % errors)
	quit(1 if errors else 0)
