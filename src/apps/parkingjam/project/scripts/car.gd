extends Node2D
# Une voiture (ou un camion) dessinée vue de dessus. Le nœud est au centre de la voiture ; l'avant est vers +x
# dans le repère local, la rotation du nœud la tourne dans le bon sens.

const COLORS := [
	Color("e5484d"), Color("3e8ef7"), Color("f5b83d"), Color("30a46c"), Color("8e5bd8"),
	Color("f76b15"), Color("12a5c7"), Color("d6409f"), Color("e9e4d8"), Color("6b7280"),
]

var length_cells := 2
var cell := 64.0
var color_index := 0
var body := StyleBoxFlat.new()
var highlight := false:
	set(v):
		highlight = v
		queue_redraw()
# Petit décalage d'un choc, dans le repère de l'écran (ne touche pas à position, qui sert au déplacement).
var bump := Vector2.ZERO:
	set(v):
		bump = v
		queue_redraw()


func setup(len_cells: int, color_idx: int, cell_size: float) -> void:
	length_cells = len_cells
	color_index = color_idx
	cell = cell_size
	var c: Color = COLORS[color_idx % COLORS.size()]
	body.bg_color = c
	body.border_color = c.darkened(0.35)
	body.set_border_width_all(maxi(1, int(cell * 0.03)))
	body.set_corner_radius_all(int(cell * 0.18))
	body.shadow_color = Color(0, 0, 0, 0.35)
	body.shadow_size = int(cell * 0.06)
	body.shadow_offset = Vector2(cell * 0.04, cell * 0.06)
	body.anti_aliasing = true
	queue_redraw()


func _draw() -> void:
	var L := length_cells * cell - cell * 0.16
	var W := cell * 0.72
	var half := Vector2(L, W) * 0.5
	var c: Color = body.bg_color
	var wheel := Color("1f2228")
	var glass := Color("26303c")
	draw_set_transform(bump.rotated(-rotation))
	# Roues, qui dépassent un peu de la caisse.
	var ww := cell * 0.22
	var wh := cell * 0.08
	for sx in [-1.0, 1.0]:
		for sy in [-1.0, 1.0]:
			var cx: float = sx * (half.x - cell * 0.30)
			if length_cells == 3 and sx < 0:
				cx = -half.x + cell * 0.45
			draw_rect(Rect2(cx - ww * 0.5, sy * (half.y + wh * 0.1) - wh * 0.5, ww, wh), wheel)
	if highlight:
		var glow := StyleBoxFlat.new()
		glow.bg_color = Color(1, 1, 1, 0.55)
		glow.set_corner_radius_all(int(cell * 0.24))
		glow.anti_aliasing = true
		draw_style_box(glow, Rect2(-half - Vector2(5, 5), half * 2 + Vector2(10, 10)))
	draw_style_box(body, Rect2(-half, half * 2))
	if length_cells >= 3:
		_draw_truck(half, c, glass)
	else:
		_draw_car(half, c, glass)
	# Phares à l'avant, feux à l'arrière.
	var r := cell * 0.06
	for sy in [-1.0, 1.0]:
		draw_circle(Vector2(half.x - r * 1.4, sy * (half.y - r * 1.8)), r, Color("fff4b0"))
		draw_rect(Rect2(-half.x + r * 0.4, sy * (half.y - r * 1.8) - r * 0.8, r * 1.2, r * 1.6), Color("b81d24"))


func _draw_car(half: Vector2, c: Color, glass: Color) -> void:
	var W := half.y * 2
	# Pare-brise, toit, lunette arrière.
	var wind := PackedVector2Array([
		Vector2(half.x * 0.42, -half.y * 0.78), Vector2(half.x * 0.62, -half.y * 0.62),
		Vector2(half.x * 0.62, half.y * 0.62), Vector2(half.x * 0.42, half.y * 0.78),
		Vector2(half.x * 0.12, half.y * 0.70), Vector2(half.x * 0.12, -half.y * 0.70)])
	draw_colored_polygon(wind, glass)
	var roof := StyleBoxFlat.new()
	roof.bg_color = c.lightened(0.18)
	roof.set_corner_radius_all(int(W * 0.12))
	roof.anti_aliasing = true
	draw_style_box(roof, Rect2(Vector2(-half.x * 0.42, -half.y * 0.68), Vector2(half.x * 0.52, half.y * 1.36)))
	var rear := PackedVector2Array([
		Vector2(-half.x * 0.44, -half.y * 0.66), Vector2(-half.x * 0.44, half.y * 0.66),
		Vector2(-half.x * 0.62, half.y * 0.56), Vector2(-half.x * 0.62, -half.y * 0.56)])
	draw_colored_polygon(rear, glass)
	# Reflet sur le capot.
	draw_line(Vector2(half.x * 0.70, -half.y * 0.45), Vector2(half.x * 0.88, -half.y * 0.45),
		Color(1, 1, 1, 0.35), maxf(1.0, cell * 0.03))


func _draw_truck(half: Vector2, c: Color, glass: Color) -> void:
	# Cabine à l'avant, caisse à l'arrière.
	var cab_x := half.x - cell * 0.95
	draw_rect(Rect2(Vector2(cab_x - cell * 0.04, -half.y), Vector2(cell * 0.06, half.y * 2)), c.darkened(0.4))
	var box := StyleBoxFlat.new()
	box.bg_color = Color("eef0f2")
	box.border_color = c.darkened(0.25)
	box.set_border_width_all(maxi(1, int(cell * 0.03)))
	box.set_corner_radius_all(int(cell * 0.08))
	box.anti_aliasing = true
	draw_style_box(box, Rect2(Vector2(-half.x + cell * 0.08, -half.y * 0.86),
		Vector2(cab_x - cell * 0.08 - (-half.x + cell * 0.08), half.y * 1.72)))
	var n := 4
	var x0 := -half.x + cell * 0.08
	var x1 := cab_x - cell * 0.08
	for i in range(1, n):
		var x := lerpf(x0, x1, float(i) / n)
		draw_line(Vector2(x, -half.y * 0.8), Vector2(x, half.y * 0.8), Color(0, 0, 0, 0.12), maxf(1.0, cell * 0.02))
	draw_rect(Rect2(Vector2(cab_x + cell * 0.30, -half.y * 0.70), Vector2(cell * 0.16, half.y * 1.40)), glass)
