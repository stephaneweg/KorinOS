@tool
extends EditorExportPlugin
# L'export d'un jeu en app de Korin OS pour PC (docs/GODOT.md). Korin PC lit le même PCK que Linux x86_64 : le
# préréglage d'export est un préréglage Linux, avec le modèle d'export de Korin en « Modèle personnalisé ». Ce
# greffon ajoute les options « korin/... » et, l'export fini, fait le dossier de l'app à côté du fichier exporté :
#
#   <id>.app/main        le binaire exporté (modèle de Korin, PCK dedans si « Intégrer le PCK »)
#   <id>.app/main.pck    le PCK s'il n'est pas intégré (le modèle le cherche à côté de lui, de même nom)
#   <id>.app/app.txt     le manifeste (nom, catégorie, description), comme toute app de Korin
#   <id>.app/icon.rgba   l'icône du projet, aux tailles du bureau (format ARIC : tools/icons/build.py de Korin)
#
# Sur Korin, SD0:/apps/<id>.app/main est exécutable d'office (vfs, SD0:/etc/exec.txt) : copier le dossier dans
# SD0:/apps suffit (ou l'archive <id>.app.zip, puis unzip). « korin/install_dir » : un dossier où copier l'app après
# l'export, par exemple le partage SMB de la carte (\\machine\SD0\apps).

const ICON_SIZES := [16, 20, 24, 26, 30, 32, 36, 38, 40, 44, 48, 64]

var _path := ""
var _enabled := false


func _get_name() -> String:
	return "Korin"


func _supports_platform(platform: EditorExportPlatform) -> bool:
	return platform.get_os_name() == "Linux"


func _get_export_options(platform: EditorExportPlatform) -> Array[Dictionary]:
	return [
		{"option": {"name": "korin/make_app", "type": TYPE_BOOL}, "default_value": false},
		{"option": {"name": "korin/app_id", "type": TYPE_STRING, "hint": PROPERTY_HINT_PLACEHOLDER_TEXT,
			"hint_string": "nom du dossier : lettres minuscules et chiffres (vide : d'après le nom du projet)"}, "default_value": ""},
		{"option": {"name": "korin/category", "type": TYPE_STRING}, "default_value": "Jeux"},
		{"option": {"name": "korin/description", "type": TYPE_STRING, "hint": PROPERTY_HINT_MULTILINE_TEXT}, "default_value": ""},
		{"option": {"name": "korin/zip", "type": TYPE_BOOL}, "default_value": true},
		{"option": {"name": "korin/install_dir", "type": TYPE_STRING, "hint": PROPERTY_HINT_GLOBAL_DIR}, "default_value": ""},
	]


func _get_export_option_warning(platform: EditorExportPlatform, option: String) -> String:
	if option == "korin/make_app" and get_option("korin/make_app"):
		if str(get_option("custom_template/release")).is_empty() and str(get_option("custom_template/debug")).is_empty():
			return "Korin : choisir le modèle d'export de Korin (template_release.x86_64, template_debug.x86_64) dans Modèle personnalisé ; le modèle de Linux ne tourne pas sur Korin."
		if str(get_option("binary_format/architecture")) != "x86_64":
			return "Korin : architecture x86_64 (Korin PC)."
	return ""


func _export_begin(features: PackedStringArray, is_debug: bool, path: String, flags: int) -> void:
	_path = path
	_enabled = bool(get_option("korin/make_app"))


func _export_end() -> void:
	if not _enabled or _path.is_empty():
		return
	var err := _make_app()
	if err != OK:
		push_error("Korin : l'app n'a pas été faite (erreur %d), voir les messages plus haut." % err)


func _app_id() -> String:
	var id := str(get_option("korin/app_id")).strip_edges().to_lower()
	if id.is_empty():
		id = str(ProjectSettings.get_setting("application/config/name", "jeu")).to_lower()
	var out := ""
	for c in id:
		if (c >= "a" and c <= "z") or (c >= "0" and c <= "9"):
			out += c
	return out if not out.is_empty() else "jeu"


func _make_app() -> Error:
	var bin := ProjectSettings.globalize_path(_path)
	if not FileAccess.file_exists(bin):
		push_error("Korin : le fichier exporté est introuvable : " + bin)
		return ERR_FILE_NOT_FOUND
	var id := _app_id()
	var dir := bin.get_base_dir().path_join(id + ".app")
	var e := DirAccess.make_dir_recursive_absolute(dir)
	if e != OK:
		return e
	e = DirAccess.copy_absolute(bin, dir.path_join("main"))
	if e != OK:
		push_error("Korin : copie de " + bin + " refusée")
		return e
	var pck := bin.get_basename() + ".pck"
	if FileAccess.file_exists(pck) and not bool(get_option("binary_format/embed_pck")):
		e = DirAccess.copy_absolute(pck, dir.path_join("main.pck"))
		if e != OK:
			return e
	e = _write_manifest(dir.path_join("app.txt"))
	if e != OK:
		return e
	_write_icon(dir.path_join("icon.rgba"))
	print("Korin : app faite : ", dir)
	if bool(get_option("korin/zip")):
		e = _zip(dir, bin.get_base_dir().path_join(id + ".app.zip"), id + ".app")
		if e != OK:
			return e
	var inst := str(get_option("korin/install_dir")).strip_edges()
	if not inst.is_empty():
		e = _copy_dir(dir, inst.path_join(id + ".app"))
		if e != OK:
			push_error("Korin : copie vers " + inst + " refusée (le partage est-il monté ?)")
			return e
		print("Korin : app copiée dans ", inst.path_join(id + ".app"))
	return OK


func _write_manifest(file: String) -> Error:
	var name := str(ProjectSettings.get_setting("application/config/name", "Jeu"))
	var desc := str(get_option("korin/description")).strip_edges().replace("\n", " ")
	if desc.is_empty():
		desc = str(ProjectSettings.get_setting("application/config/description", "")).strip_edges().replace("\n", " ")
	if desc.is_empty():
		desc = name + ", un jeu fait avec Godot."
	var f := FileAccess.open(file, FileAccess.WRITE)
	if f == null:
		return FileAccess.get_open_error()
	f.store_string("# Manifeste de %s, exporté par Godot (greffon Korin Export)\n" % name)
	f.store_string("name     = %s\n" % name)
	f.store_string("category = %s\n" % str(get_option("korin/category")).strip_edges())
	f.store_string("description = %s\n" % desc)
	f.store_string("keywords = jeu godot %s\n" % name.to_lower())
	f.close()
	return OK


# L'icône du projet aux tailles du bureau de Korin : en-tête « ARIC », version 1, n tailles ; par taille : côté (u16),
# 0 (u16), décalage des pixels (u32) ; pixels 0xAARRGGBB petit-boutistes, alpha prémultiplié, ligne du haut d'abord.
func _write_icon(file: String) -> void:
	var path := str(ProjectSettings.get_setting("application/config/icon", ""))
	if path.is_empty():
		return
	var img: Image = null
	var tex = load(path)
	if tex is Texture2D:
		img = tex.get_image()
	if img == null:
		img = Image.load_from_file(ProjectSettings.globalize_path(path))
	if img == null or img.is_empty():
		push_warning("Korin : icône du projet illisible (" + path + ") : l'app aura l'icône par défaut")
		return
	if img.is_compressed():
		img.decompress()
	img.convert(Image.FORMAT_RGBA8)
	var head := PackedByteArray()
	var body := PackedByteArray()
	var n := ICON_SIZES.size()
	head.resize(8 + 8 * n)
	head.encode_u8(0, 0x41)
	head.encode_u8(1, 0x52)
	head.encode_u8(2, 0x49)
	head.encode_u8(3, 0x43)
	head.encode_u16(4, 1)
	head.encode_u16(6, n)
	var off := 8 + 8 * n
	for i in n:
		var z: int = ICON_SIZES[i]
		var s := img.duplicate() as Image
		s.resize(z, z, Image.INTERPOLATE_LANCZOS)
		head.encode_u16(8 + 8 * i, z)
		head.encode_u16(8 + 8 * i + 2, 0)
		head.encode_u32(8 + 8 * i + 4, off)
		var px := PackedByteArray()
		px.resize(z * z * 4)
		var data := s.get_data()
		for k in z * z:
			var r := data[k * 4]
			var g := data[k * 4 + 1]
			var b := data[k * 4 + 2]
			var a := data[k * 4 + 3]
			px[k * 4] = b * a / 255
			px[k * 4 + 1] = g * a / 255
			px[k * 4 + 2] = r * a / 255
			px[k * 4 + 3] = a
		body.append_array(px)
		off += z * z * 4
	var f := FileAccess.open(file, FileAccess.WRITE)
	if f == null:
		return
	f.store_buffer(head)
	f.store_buffer(body)
	f.close()


func _zip(dir: String, zip_path: String, root: String) -> Error:
	var z := ZIPPacker.new()
	var e := z.open(zip_path)
	if e != OK:
		return e
	for name in DirAccess.get_files_at(dir):
		e = z.start_file(root + "/" + name)
		if e != OK:
			z.close()
			return e
		z.write_file(FileAccess.get_file_as_bytes(dir.path_join(name)))
		z.close_file()
	z.close()
	print("Korin : archive faite : ", zip_path)
	return OK


func _copy_dir(src: String, dst: String) -> Error:
	var e := DirAccess.make_dir_recursive_absolute(dst)
	if e != OK:
		return e
	for name in DirAccess.get_files_at(src):
		e = DirAccess.copy_absolute(src.path_join(name), dst.path_join(name))
		if e != OK:
			return e
	return OK
