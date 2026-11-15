# Démo 2D : un carré qui tourne, un cercle qui suit la souris ; dit dans la sortie ce qu'il rend et à quelle vitesse.
# « -- --quit-after-demo » : s'arrête après 120 images (essais automatiques).
extends Node2D

var t := 0.0
var frames := 0
var mouse := Vector2(320, 240)

func _ready() -> void:
	print("DEMO PRET : ", RenderingServer.get_video_adapter_name(), " / ", RenderingServer.get_video_adapter_vendor())

func _input(event: InputEvent) -> void:
	if event is InputEventMouseMotion:
		mouse = event.position
	elif event is InputEventKey and event.pressed:
		print("DEMO TOUCHE : ", event.as_text_keycode(), " caractère ", char(event.unicode) if event.unicode > 0 else "-")
	elif event is InputEventMouseButton and event.pressed:
		print("DEMO CLIC : ", event.button_index, " en ", event.position)

func _process(delta: float) -> void:
	t += delta
	frames += 1
	$Label.text = "Godot sur Korin  -  %d images/s" % Engine.get_frames_per_second()
	queue_redraw()
	if frames == 120:
		print("DEMO 120 IMAGES : ", Engine.get_frames_per_second(), " images/s, fenêtre ", get_viewport().get_visible_rect().size)
		if "--quit-after-demo" in OS.get_cmdline_user_args():
			get_tree().quit()

func _draw() -> void:
	var c := get_viewport_rect().size / 2
	draw_set_transform(c, t, Vector2.ONE)
	draw_rect(Rect2(-80, -80, 160, 160), Color(0.25, 0.55, 0.95))
	draw_set_transform(Vector2.ZERO, 0, Vector2.ONE)
	draw_circle(mouse, 24, Color(0.95, 0.6, 0.2))
