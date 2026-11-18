extends Node2D
# Parking Jam : vider le parking. On touche (ou on glisse) une voiture : elle roule dans son axe, du côté touché
# (ou dans le sens du glissé), jusqu'à heurter quelque chose ou sortir sur la route. Le niveau est gagné quand le
# parking est vide.

const LevelGen := preload("res://scripts/level_gen.gd")
const CarScript := preload("res://scripts/car.gd")

const SAVE_PATH := "user://parkingjam.cfg"
const HUD_H := 64.0
const SPEED_CELLS := 14.0          # vitesse des voitures, en cases par seconde
const DRAG_THRESHOLD := 0.30       # glissé minimal, en cases, pour choisir le sens au glissé

const COL_ROAD := Color("3b3f46")
const COL_ROAD_LINE := Color(1, 1, 1, 0.55)
const COL_LOT := Color("575b63")
const COL_CURB := Color("cfd2d6")
const COL_SLOT := Color(1, 1, 1, 0.28)

var level_num := 1
var unlocked := 1
var lvl: Dictionary = {}           # le niveau en cours (les voitures y changent de place)
var start_cars: Array = []         # les voitures au début du niveau, pour recommencer
var occ := PackedInt32Array()      # grille d'occupation (indice de voiture, -2 plot, -1 libre)
var gone: Array[bool] = []         # voitures sorties
var moves := 0
var history: Array = []            # instantanés pour « Annuler »
var won := false
var save_enabled := true           # faux pour les essais automatiques (tests/)

var car_nodes: Array = []          # un nœud par voiture (null quand elle est partie)
var tweens: Dictionary = {}        # tween de déplacement en cours, par nœud
var cell := 64.0
var origin := Vector2.ZERO         # coin haut gauche du parking, en pixels
var road := 64.0                   # largeur de la route
var bushes: Array = []             # [position, rayon] des buissons du décor

var pressed_car := -1
var press_pos := Vector2.ZERO

# Interface
var lbl_level: Label
var lbl_moves: Label
var lbl_msg: Label
var btn_prev: Button
var btn_next: Button
var btn_undo: Button
var win_layer: Control
var win_text: Label
var win_next: Button


func _ready() -> void:
	_load_progress()
	_build_ui()
	get_viewport().size_changed.connect(_relayout)
	start_level(level_num)


# --- Niveaux ---------------------------------------------------------------------------------------------------

func start_level(n: int) -> void:
	level_num = maxi(1, n)
	lvl = LevelGen.generate(level_num)
	start_cars = lvl["cars"].duplicate(true)
	_reset_state()
	_save_progress()


func restart_level() -> void:
	lvl["cars"] = start_cars.duplicate(true)
	_reset_state()


func _reset_state() -> void:
	var n: int = lvl["cars"].size()
	gone.clear()
	gone.resize(n)
	gone.fill(false)
	occ = LevelGen.make_occ(lvl, [])
	moves = 0
	history.clear()
	won = false
	pressed_car = -1
	win_layer.visible = false
	lbl_msg.text = ""
	_rebuild_cars()
	_relayout()
	_update_hud()


func _snapshot() -> Dictionary:
	var pos := []
	for c in lvl["cars"]:
		pos.append(Vector2i(int(c["x"]), int(c["y"])))
	return {"pos": pos, "gone": gone.duplicate(), "moves": moves}


func undo() -> void:
	if history.is_empty() or won:
		return
	var s: Dictionary = history.pop_back()
	var cars: Array = lvl["cars"]
	for i in cars.size():
		var p: Vector2i = s["pos"][i]
		cars[i]["x"] = p.x
		cars[i]["y"] = p.y
	gone.assign(s["gone"])
	moves = int(s["moves"])
	var g := []
	for i in gone.size():
		if gone[i]:
			g.append(i)
	occ = LevelGen.make_occ(lvl, g)
	lbl_msg.text = ""
	_rebuild_cars()
	_relayout()
	_update_hud()


# --- Les voitures ----------------------------------------------------------------------------------------------

func _rebuild_cars() -> void:
	for nd in car_nodes:
		if nd != null and is_instance_valid(nd):
			nd.queue_free()
	for nd in get_children():
		if nd.has_meta("exiting"):
			nd.queue_free()
	car_nodes.clear()
	tweens.clear()
	var cars: Array = lvl["cars"]
	for i in cars.size():
		if gone[i]:
			car_nodes.append(null)
			continue
		var nd := Node2D.new()
		nd.set_script(CarScript)
		add_child(nd)
		car_nodes.append(nd)


func _car_center(car: Dictionary) -> Vector2:
	var L := float(car["len"])
	var off := Vector2(L * 0.5, 0.5) if car["horiz"] else Vector2(0.5, L * 0.5)
	return origin + (Vector2(float(car["x"]), float(car["y"])) + off) * cell


func _car_rotation(car: Dictionary) -> float:
	var f: int = car["facing"]
	if car["horiz"]:
		return 0.0 if f > 0 else PI
	return PI * 0.5 if f > 0 else -PI * 0.5


func _dir_vector(car: Dictionary, dir: int) -> Vector2:
	return (Vector2(1, 0) if car["horiz"] else Vector2(0, 1)) * float(dir)


# Fait rouler la voiture i dans le sens dir (+1 droite/bas, -1 gauche/haut).
func move_car(i: int, dir: int) -> void:
	var cars: Array = lvl["cars"]
	var car: Dictionary = cars[i]
	var w: int = lvl["w"]
	var h: int = lvl["h"]
	var res := LevelGen.probe(occ, w, h, car, dir)
	var steps: int = res["steps"]
	var nd: Node2D = car_nodes[i]
	var dv := _dir_vector(car, dir)
	if steps == 0 and not res["exit"]:
		_bump(nd, dv, int(res["blocker"]))
		return
	history.append(_snapshot())
	moves += 1
	for c in LevelGen.cells(car):
		occ[c.y * w + c.x] = -1
	if res["exit"]:
		gone[i] = true
		car_nodes[i] = null
		_drive_out(nd, car, dir)
	else:
		var step := LevelGen.axis(car) * dir * steps
		car["x"] = int(car["x"]) + step.x
		car["y"] = int(car["y"]) + step.y
		for c in LevelGen.cells(car):
			occ[c.y * w + c.x] = i
		var tw := _new_tween(nd)
		nd.z_index = 1
		tw.tween_property(nd, "position", _car_center(car), maxf(0.10, steps / SPEED_CELLS)) \
			.set_trans(Tween.TRANS_QUAD).set_ease(Tween.EASE_OUT)
		tw.tween_callback(func(): nd.z_index = 0)
		tw.tween_callback(_bump.bind(nd, dv, int(res["blocker"])))
	_update_hud()
	_check_end()


func _new_tween(nd: Node2D) -> Tween:
	if tweens.has(nd) and tweens[nd].is_valid():
		tweens[nd].kill()
	var tw := nd.create_tween()      # lié à la voiture : s'arrête avec elle si elle disparaît
	tweens[nd] = tw
	return tw


# Le choc contre un obstacle : la voiture tressaute, celle qu'elle heurte aussi.
func _bump(nd: Node2D, dv: Vector2, blocker: int) -> void:
	if nd == null or not is_instance_valid(nd):
		return
	var tw := nd.create_tween()
	tw.tween_property(nd, "bump", dv * cell * 0.10, 0.06).set_trans(Tween.TRANS_QUAD).set_ease(Tween.EASE_OUT)
	tw.tween_property(nd, "bump", Vector2.ZERO, 0.14).set_trans(Tween.TRANS_BACK).set_ease(Tween.EASE_OUT)
	if blocker >= 0 and blocker < car_nodes.size() and car_nodes[blocker] != null:
		var other: Node2D = car_nodes[blocker]
		var t2 := other.create_tween()
		t2.tween_interval(0.05)
		t2.tween_property(other, "bump", dv * cell * 0.06, 0.05)
		t2.tween_property(other, "bump", -dv * cell * 0.03, 0.06)
		t2.tween_property(other, "bump", Vector2.ZERO, 0.08)


# La sortie : jusqu'au milieu de la route, un quart de tour, puis hors de l'écran par la route.
func _drive_out(nd: Node2D, car: Dictionary, dir: int) -> void:
	nd.set_meta("exiting", true)
	nd.z_index = 2
	var view := get_viewport_rect().size
	var p0 := nd.position
	var lot_size := Vector2(float(lvl["w"]), float(lvl["h"])) * cell
	var p1 := p0
	var travel := Vector2.ZERO        # sens de la route, après le virage
	var p2 := p0
	var margin := float(car["len"]) * cell + 40.0
	if car["horiz"]:
		p1.x = origin.x - road * 0.5 if dir < 0 else origin.x + lot_size.x + road * 0.5
		travel = Vector2(0, -1) if p0.y < origin.y + lot_size.y * 0.5 else Vector2(0, 1)
		p2 = Vector2(p1.x, -margin if travel.y < 0 else view.y + margin)
	else:
		p1.y = origin.y - road * 0.5 if dir < 0 else origin.y + lot_size.y + road * 0.5
		travel = Vector2(-1, 0) if p0.x < origin.x + lot_size.x * 0.5 else Vector2(1, 0)
		p2 = Vector2(-margin if travel.x < 0 else view.x + margin, p1.y)
	var speed := cell * SPEED_CELLS
	var turn := wrapf(travel.angle() - nd.rotation, -PI, PI)
	var tw := _new_tween(nd)
	tw.tween_property(nd, "position", p1, maxf(0.08, p0.distance_to(p1) / speed)) \
		.set_trans(Tween.TRANS_QUAD).set_ease(Tween.EASE_IN)
	tw.tween_property(nd, "rotation", nd.rotation + turn, 0.14).set_trans(Tween.TRANS_SINE)
	tw.tween_property(nd, "position", p2, p1.distance_to(p2) / (speed * 1.3)) \
		.set_trans(Tween.TRANS_QUAD).set_ease(Tween.EASE_IN)
	tw.tween_callback(nd.queue_free)


# Gagné ? Ou plus aucun coup possible ?
func _check_end() -> void:
	var left := 0
	for g in gone:
		if not g:
			left += 1
	if left == 0:
		won = true
		unlocked = maxi(unlocked, level_num + 1)
		_save_progress()
		_update_hud()
		await get_tree().create_timer(0.9).timeout
		if won:
			win_text.text = "Niveau %d terminé en %d coups." % [level_num, moves]
			win_layer.visible = true
		return
	if not _any_move():
		lbl_msg.text = "Plus aucun coup possible : Annuler ou Recommencer."


func _any_move() -> bool:
	var cars: Array = lvl["cars"]
	for i in cars.size():
		if gone[i]:
			continue
		for d in [-1, 1]:
			var r := LevelGen.probe(occ, lvl["w"], lvl["h"], cars[i], d)
			if r["exit"] or int(r["steps"]) > 0:
				return true
	return false


# --- Mise en page et dessin -------------------------------------------------------------------------------------

func _relayout() -> void:
	if lvl.is_empty():
		return
	var view := get_viewport_rect().size
	var w := float(lvl["w"])
	var h := float(lvl["h"])
	var avail := Vector2(view.x, view.y - HUD_H)
	cell = floorf(minf(minf(avail.x / (w + 2.8), avail.y / (h + 2.8)), 110.0))
	cell = maxf(cell, 16.0)
	road = cell
	origin = Vector2(floorf((view.x - w * cell) * 0.5), floorf(HUD_H + (avail.y - h * cell) * 0.5))
	var cars: Array = lvl["cars"]
	for i in cars.size():
		var nd: Node2D = car_nodes[i]
		if nd == null:
			continue
		if tweens.has(nd) and tweens[nd].is_valid():
			tweens[nd].kill()
		nd.setup(int(cars[i]["len"]), int(cars[i]["color"]), cell)
		nd.position = _car_center(cars[i])
		nd.rotation = _car_rotation(cars[i])
		nd.z_index = 0
	for nd in get_children():
		if nd.has_meta("exiting"):
			nd.queue_free()
	_place_bushes(view)
	queue_redraw()


# Des buissons dans l'herbe, hors des routes et du parking (toujours les mêmes pour un niveau).
func _place_bushes(view: Vector2) -> void:
	bushes.clear()
	var rng := RandomNumberGenerator.new()
	rng.seed = level_num * 31 + 7
	var lot := Rect2(origin - Vector2(road, road), Vector2(float(lvl["w"]), float(lvl["h"])) * cell + Vector2(road, road) * 2)
	for t in 120:
		var p := Vector2(rng.randf_range(0, view.x), rng.randf_range(HUD_H, view.y))
		var r := rng.randf_range(cell * 0.16, cell * 0.30)
		var grown := lot.grow(r + 4)
		if grown.has_point(p):
			continue
		# Pas sur les routes qui partent vers les bords.
		if absf(p.y - (origin.y - road * 0.5)) < road * 0.5 + r or \
				absf(p.y - (lot.end.y - road * 0.5)) < road * 0.5 + r or \
				absf(p.x - (origin.x - road * 0.5)) < road * 0.5 + r or \
				absf(p.x - (lot.end.x - road * 0.5)) < road * 0.5 + r:
			continue
		bushes.append([p, r])
		if bushes.size() >= 18:
			break


func _draw() -> void:
	if lvl.is_empty():
		return
	var view := get_viewport_rect().size
	var w: int = lvl["w"]
	var h: int = lvl["h"]
	var lot := Rect2(origin, Vector2(w, h) * cell)
	# Les routes : deux horizontales et deux verticales qui encadrent le parking et filent vers les bords.
	var top := Rect2(0, origin.y - road, view.x, road)
	var bottom := Rect2(0, lot.end.y, view.x, road)
	var left := Rect2(origin.x - road, 0, road, view.y)
	var right := Rect2(lot.end.x, 0, road, view.y)
	for r in [top, bottom, left, right]:
		draw_rect(r, COL_ROAD)
	var dash := cell * 0.32
	var lw := maxf(2.0, cell * 0.05)
	for r in [top, bottom]:
		var y: float = r.position.y + road * 0.5
		var x := 0.0
		while x < view.x:
			if x + dash < left.position.x or x > right.end.x or (x > left.end.x and x + dash < right.position.x):
				draw_line(Vector2(x, y), Vector2(x + dash, y), COL_ROAD_LINE, lw)
			x += dash * 2
	for r in [left, right]:
		var x: float = r.position.x + road * 0.5
		var y := 0.0
		while y < view.y:
			if y + dash < top.position.y or y > bottom.end.y or (y > top.end.y and y + dash < bottom.position.y):
				draw_line(Vector2(x, y), Vector2(x, y + dash), COL_ROAD_LINE, lw)
			y += dash * 2
	# Le parking, sa bordure et ses places.
	var curb := maxf(3.0, cell * 0.08)
	draw_rect(lot.grow(curb), COL_CURB)
	draw_rect(lot, COL_LOT)
	var sl := maxf(1.0, cell * 0.035)
	for x in range(1, w):
		draw_line(origin + Vector2(x * cell, cell * 0.08), origin + Vector2(x * cell, h * cell - cell * 0.08), COL_SLOT, sl)
	for y in range(1, h):
		draw_line(origin + Vector2(cell * 0.08, y * cell), origin + Vector2(w * cell - cell * 0.08, y * cell), COL_SLOT, sl)
	# Les plots.
	for c in lvl["cones"]:
		var p := origin + (Vector2(c) + Vector2(0.5, 0.5)) * cell
		draw_circle(p + Vector2(cell * 0.04, cell * 0.06), cell * 0.30, Color(0, 0, 0, 0.3))
		draw_rect(Rect2(p - Vector2(cell * 0.30, cell * 0.30), Vector2(cell * 0.60, cell * 0.60)), Color("d9580f"))
		draw_circle(p, cell * 0.24, Color("ff7a1a"))
		draw_arc(p, cell * 0.15, 0, TAU, 24, Color.WHITE, cell * 0.07)
		draw_circle(p, cell * 0.07, Color("ff9a4a"))
	# Les buissons.
	for b in bushes:
		var p: Vector2 = b[0]
		var r: float = b[1]
		draw_circle(p + Vector2(r * 0.15, r * 0.25), r, Color(0, 0, 0, 0.18))
		draw_circle(p, r, Color("3f7f3a"))
		draw_circle(p - Vector2(r * 0.3, r * 0.3), r * 0.55, Color("579c4f"))


# --- Entrées -------------------------------------------------------------------------------------------------------

func _car_at(pos: Vector2) -> int:
	if lvl.is_empty():
		return -1
	var g := Vector2i(floori((pos.x - origin.x) / cell), floori((pos.y - origin.y) / cell))
	if g.x < 0 or g.y < 0 or g.x >= int(lvl["w"]) or g.y >= int(lvl["h"]):
		return -1
	var o := occ[g.y * int(lvl["w"]) + g.x]
	return o if o >= 0 else -1


func _unhandled_input(event: InputEvent) -> void:
	if won:
		if win_layer.visible and event is InputEventKey and event.pressed and not event.echo \
				and event.keycode in [KEY_ENTER, KEY_KP_ENTER, KEY_SPACE, KEY_N]:
			start_level(level_num + 1)
		return
	if event is InputEventMouseButton and event.button_index == MOUSE_BUTTON_LEFT:
		if event.pressed:
			pressed_car = _car_at(event.position)
			press_pos = event.position
			if pressed_car >= 0:
				car_nodes[pressed_car].highlight = true
		elif pressed_car >= 0:
			_release(event.position, false)
	elif event is InputEventMouseMotion and pressed_car >= 0:
		_release(event.position, true)
	elif event is InputEventKey and event.pressed and not event.echo:
		if event.keycode == KEY_Z and event.ctrl_pressed or event.keycode == KEY_U or event.keycode == KEY_BACKSPACE:
			undo()
		elif event.keycode == KEY_R:
			restart_level()


# Fin d'un appui (ou glissé assez long) : choisit le sens et fait rouler la voiture.
func _release(pos: Vector2, from_motion: bool) -> void:
	var i := pressed_car
	if i < 0 or gone[i]:
		pressed_car = -1
		return
	var car: Dictionary = lvl["cars"][i]
	var ax := Vector2(1, 0) if car["horiz"] else Vector2(0, 1)
	var drag := (pos - press_pos).dot(ax)
	var dir := 0
	if absf(drag) >= cell * DRAG_THRESHOLD:
		dir = 1 if drag > 0 else -1
	elif from_motion:
		return       # glissé encore trop court : on attend
	else:
		# Simple toucher : la voiture part du côté touché (l'avant ou l'arrière).
		var along := (press_pos - _car_center(car)).dot(ax)
		dir = (1 if along > 0 else -1) if absf(along) > 1.0 else int(car["facing"])
	var nd: Node2D = car_nodes[i]
	if nd != null:
		nd.highlight = false
	pressed_car = -1
	lbl_msg.text = ""
	move_car(i, dir)


# --- Interface --------------------------------------------------------------------------------------------------

func _build_ui() -> void:
	var layer := CanvasLayer.new()
	layer.layer = 5
	add_child(layer)
	var root := Control.new()
	root.set_anchors_preset(Control.PRESET_FULL_RECT)
	root.mouse_filter = Control.MOUSE_FILTER_IGNORE
	layer.add_child(root)

	var bar := PanelContainer.new()
	var sb := StyleBoxFlat.new()
	sb.bg_color = Color(0.10, 0.12, 0.14, 0.78)
	sb.content_margin_left = 12
	sb.content_margin_right = 12
	sb.content_margin_top = 8
	sb.content_margin_bottom = 8
	bar.add_theme_stylebox_override("panel", sb)
	bar.set_anchors_preset(Control.PRESET_TOP_WIDE)
	bar.custom_minimum_size.y = HUD_H
	root.add_child(bar)
	var hb := HBoxContainer.new()
	hb.add_theme_constant_override("separation", 8)
	bar.add_child(hb)

	btn_prev = _button("<", func(): start_level(level_num - 1))
	btn_prev.tooltip_text = "Niveau précédent"
	hb.add_child(btn_prev)
	lbl_level = _label(22)
	lbl_level.custom_minimum_size.x = 120
	lbl_level.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	hb.add_child(lbl_level)
	btn_next = _button(">", func(): start_level(level_num + 1))
	btn_next.tooltip_text = "Niveau suivant (débloqué)"
	hb.add_child(btn_next)
	lbl_moves = _label(18)
	lbl_moves.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	lbl_moves.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	hb.add_child(lbl_moves)
	btn_undo = _button("Annuler", undo)
	btn_undo.tooltip_text = "Annuler le dernier coup (Ctrl+Z)"
	hb.add_child(btn_undo)
	var b := _button("Recommencer", restart_level)
	b.tooltip_text = "Recommencer le niveau (R)"
	hb.add_child(b)

	lbl_msg = _label(18)
	lbl_msg.set_anchors_preset(Control.PRESET_BOTTOM_WIDE)
	lbl_msg.offset_top = -44
	lbl_msg.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	lbl_msg.add_theme_color_override("font_outline_color", Color(0, 0, 0, 0.8))
	lbl_msg.add_theme_constant_override("outline_size", 6)
	root.add_child(lbl_msg)

	# Le panneau de victoire.
	win_layer = ColorRect.new()
	(win_layer as ColorRect).color = Color(0, 0, 0, 0.45)
	win_layer.set_anchors_preset(Control.PRESET_FULL_RECT)
	win_layer.visible = false
	root.add_child(win_layer)
	var center := CenterContainer.new()
	center.set_anchors_preset(Control.PRESET_FULL_RECT)
	win_layer.add_child(center)
	var panel := PanelContainer.new()
	var ps := StyleBoxFlat.new()
	ps.bg_color = Color(0.13, 0.15, 0.18, 0.96)
	ps.set_corner_radius_all(16)
	ps.set_content_margin_all(28)
	panel.add_theme_stylebox_override("panel", ps)
	center.add_child(panel)
	var vb := VBoxContainer.new()
	vb.add_theme_constant_override("separation", 14)
	panel.add_child(vb)
	var title := _label(34)
	title.text = "Parking vidé !"
	title.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	title.add_theme_color_override("font_color", Color("f5b83d"))
	vb.add_child(title)
	win_text = _label(20)
	win_text.horizontal_alignment = HORIZONTAL_ALIGNMENT_CENTER
	vb.add_child(win_text)
	win_next = _button("Niveau suivant", func(): start_level(level_num + 1))
	win_next.add_theme_font_size_override("font_size", 22)
	vb.add_child(win_next)
	var again := _button("Rejouer ce niveau", restart_level)
	vb.add_child(again)


func _label(size: int) -> Label:
	var l := Label.new()
	l.add_theme_font_size_override("font_size", size)
	l.add_theme_color_override("font_color", Color.WHITE)
	l.vertical_alignment = VERTICAL_ALIGNMENT_CENTER
	return l


func _button(text: String, cb: Callable) -> Button:
	var b := Button.new()
	b.text = text
	b.focus_mode = Control.FOCUS_NONE
	b.add_theme_font_size_override("font_size", 18)
	b.custom_minimum_size = Vector2(44, 44)
	b.pressed.connect(cb)
	return b


func _update_hud() -> void:
	lbl_level.text = "Niveau %d" % level_num
	var left := 0
	for g in gone:
		if not g:
			left += 1
	lbl_moves.text = "Coups : %d   ·   Voitures : %d" % [moves, left]
	btn_prev.disabled = level_num <= 1
	btn_next.disabled = level_num >= unlocked
	btn_undo.disabled = history.is_empty() or won


# --- Sauvegarde ---------------------------------------------------------------------------------------------------

func _load_progress() -> void:
	if not save_enabled:
		return
	var cf := ConfigFile.new()
	if cf.load(SAVE_PATH) == OK:
		unlocked = maxi(1, int(cf.get_value("progress", "unlocked", 1)))
		level_num = clampi(int(cf.get_value("progress", "level", 1)), 1, unlocked)


func _save_progress() -> void:
	if not save_enabled:
		return
	var cf := ConfigFile.new()
	cf.set_value("progress", "level", level_num)
	cf.set_value("progress", "unlocked", unlocked)
	cf.save(SAVE_PATH)
