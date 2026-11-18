extends RefCounted
# Générateur de niveaux de Parking Jam.
#
# Un niveau est un parking de w × h cases. Chaque voiture occupe 2 cases (3 pour un camion), couchée
# (horiz = true) ou debout, et ne roule que dans son axe, en avant ou en arrière. Elle sort quand rien ne la gêne
# jusqu'au bord. Des plots (cônes) bloquent certaines cases.
#
# Les niveaux sont toujours solubles : on pose les voitures dans l'ordre inverse de leur sortie. Chaque nouvelle
# voiture doit avoir la voie libre jusqu'au bord (dans un sens au moins) parmi celles déjà posées, qui sortiront
# après elle. Parmi plusieurs essais, on garde le parking le plus difficile (le plus de « vagues » de sorties).
#
# Une voiture : {x, y, len, horiz, facing (+1 ou -1 : le sens de l'avant), color (indice)}.

const COLOR_COUNT := 10


# Les réglages d'un niveau : la taille grandit, puis la densité et les plots.
static func params(level: int) -> Dictionary:
	var w := clampi(4 + (level + 1) / 3, 4, 8)
	var h := clampi(w + 1 + (1 if level >= 6 else 0), 5, 10)
	return {
		"w": w,
		"h": h,
		"cones": clampi((level - 2) / 3, 0, 4),
		"density": clampf(0.60 + level * 0.02, 0.60, 0.86),
		"trucks": clampf(0.05 + level * 0.02, 0.05, 0.30),
		"tries": 40 + mini(level * 10, 160),
	}


# Le niveau n (1, 2, ...) : toujours le même pour un même numéro.
static func generate(level: int) -> Dictionary:
	var p := params(level)
	var rng := RandomNumberGenerator.new()
	rng.seed = hash(level * 7919 + 104729)
	var best := {}
	var best_score := -1
	for attempt in int(p["tries"]):
		var lvl := _build(rng, p)
		var rounds := solve_rounds(lvl)
		if rounds < 0:
			continue
		var filled := 0
		for c in lvl["cars"]:
			filled += int(c["len"])
		var score := rounds * 1000 + filled * 10 + _blocked_at_start(lvl)
		if score > best_score:
			best_score = score
			best = lvl
	best["level"] = level
	return best


# Pose des voitures au hasard ; chacune doit pouvoir sortir parmi celles déjà posées.
static func _build(rng: RandomNumberGenerator, p: Dictionary) -> Dictionary:
	var w: int = p["w"]
	var h: int = p["h"]
	var occ := PackedInt32Array()
	occ.resize(w * h)
	occ.fill(-1)
	var cones: Array[Vector2i] = []
	for k in int(p["cones"]):
		for t in 20:
			var c := Vector2i(rng.randi_range(0, w - 1), rng.randi_range(0, h - 1))
			if occ[c.y * w + c.x] == -1:
				occ[c.y * w + c.x] = -2
				cones.append(c)
				break
	var cars: Array = []
	var target := int(float(w * h - cones.size()) * float(p["density"]))
	var filled := 0
	var last_color := -1
	for t in 600:
		if filled >= target:
			break
		var horiz := rng.randf() < 0.5
		var length := 3 if rng.randf() < float(p["trucks"]) else 2
		var x := rng.randi_range(0, w - (length if horiz else 1))
		var y := rng.randi_range(0, h - (1 if horiz else length))
		var car := {"x": x, "y": y, "len": length, "horiz": horiz, "facing": 1 if rng.randf() < 0.5 else -1}
		if not _fits(occ, w, car):
			continue
		if exit_dir(occ, w, h, car, -99) == 0:
			continue
		var color := rng.randi_range(0, COLOR_COUNT - 1)
		if color == last_color:
			color = (color + 1) % COLOR_COUNT
		last_color = color
		car["color"] = color
		var idx := cars.size()
		for cell in cells(car):
			occ[cell.y * w + cell.x] = idx
		cars.append(car)
		filled += length
	# L'ordre de pose est l'inverse de l'ordre de sortie : on mélange pour ne rien trahir.
	_shuffle(rng, cars)
	return {"w": w, "h": h, "cars": cars, "cones": cones}


static func _shuffle(rng: RandomNumberGenerator, a: Array) -> void:
	for i in range(a.size() - 1, 0, -1):
		var j := rng.randi_range(0, i)
		var tmp = a[i]
		a[i] = a[j]
		a[j] = tmp


static func _fits(occ: PackedInt32Array, w: int, car: Dictionary) -> bool:
	for cell in cells(car):
		if occ[cell.y * w + cell.x] != -1:
			return false
	return true


# Les cases d'une voiture.
static func cells(car: Dictionary) -> Array[Vector2i]:
	var out: Array[Vector2i] = []
	for k in int(car["len"]):
		if car["horiz"]:
			out.append(Vector2i(int(car["x"]) + k, int(car["y"])))
		else:
			out.append(Vector2i(int(car["x"]), int(car["y"]) + k))
	return out


# Le pas d'une voiture dans son axe (dir = +1 vers la droite ou le bas, -1 vers la gauche ou le haut).
static func axis(car: Dictionary) -> Vector2i:
	return Vector2i(1, 0) if car["horiz"] else Vector2i(0, 1)


# La case juste devant la voiture, à `steps` cases, dans le sens dir.
static func ahead(car: Dictionary, dir: int, steps: int) -> Vector2i:
	var base := Vector2i(int(car["x"]), int(car["y"]))
	if dir > 0:
		base += axis(car) * (int(car["len"]) - 1)
	return base + axis(car) * dir * steps


# Jusqu'où la voiture roule dans le sens dir : {"steps": cases parcourues, "exit": sort du parking,
# "blocker": ce qui l'arrête (indice de voiture, -2 pour un plot, -1 pour rien)}.
static func probe(occ: PackedInt32Array, w: int, h: int, car: Dictionary, dir: int) -> Dictionary:
	var steps := 0
	while true:
		var c := ahead(car, dir, steps + 1)
		if c.x < 0 or c.y < 0 or c.x >= w or c.y >= h:
			return {"steps": steps, "exit": true, "blocker": -1}
		var o := occ[c.y * w + c.x]
		if o != -1:
			return {"steps": steps, "exit": false, "blocker": o}
		steps += 1
	return {}


# Un sens dans lequel la voiture sort tout de suite (+1 ou -1, l'avant d'abord), ou 0.
static func exit_dir(occ: PackedInt32Array, w: int, h: int, car: Dictionary, _self_idx: int) -> int:
	var f: int = car["facing"]
	if probe(occ, w, h, car, f)["exit"]:
		return f
	if probe(occ, w, h, car, -f)["exit"]:
		return -f
	return 0


# Résolution gloutonne : à chaque vague, toutes les voitures qui peuvent sortir sortent. Rend le nombre de vagues,
# ou -1 si le parking reste bloqué (ce qui n'arrive pas pour un niveau fait par _build).
static func solve_rounds(lvl: Dictionary) -> int:
	var w: int = lvl["w"]
	var h: int = lvl["h"]
	var cars: Array = lvl["cars"]
	var occ := make_occ(lvl, [])
	var alive := []
	for i in cars.size():
		alive.append(i)
	var rounds := 0
	while not alive.is_empty():
		var out := []
		for i in alive:
			if exit_dir(occ, w, h, cars[i], i) != 0:
				out.append(i)
		if out.is_empty():
			return -1
		for i in out:
			for cell in cells(cars[i]):
				occ[cell.y * w + cell.x] = -1
			alive.erase(i)
		rounds += 1
	return rounds


# Les voitures qui ne peuvent pas sortir au début : plus il y en a, plus il faut réfléchir.
static func _blocked_at_start(lvl: Dictionary) -> int:
	var occ := make_occ(lvl, [])
	var n := 0
	for i in lvl["cars"].size():
		if exit_dir(occ, lvl["w"], lvl["h"], lvl["cars"][i], i) == 0:
			n += 1
	return n


# La grille d'occupation : indice de voiture, -2 pour un plot, -1 pour une case libre. `gone` : voitures sorties.
static func make_occ(lvl: Dictionary, gone: Array) -> PackedInt32Array:
	var w: int = lvl["w"]
	var occ := PackedInt32Array()
	occ.resize(w * int(lvl["h"]))
	occ.fill(-1)
	for c in lvl["cones"]:
		occ[c.y * w + c.x] = -2
	var cars: Array = lvl["cars"]
	for i in cars.size():
		if i in gone:
			continue
		for cell in cells(cars[i]):
			occ[cell.y * w + cell.x] = i
	return occ
