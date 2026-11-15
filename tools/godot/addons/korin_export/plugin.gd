@tool
extends EditorPlugin
# Greffon d'export de Korin (docs/GODOT.md, « Exporter un jeu pour Korin PC ») : ajoute ses options au préréglage
# d'export Linux et fait l'app Korin après l'export.

var _export: EditorExportPlugin


func _enter_tree() -> void:
	_export = preload("res://addons/korin_export/korin_export.gd").new()
	add_export_plugin(_export)


func _exit_tree() -> void:
	remove_export_plugin(_export)
	_export = null
