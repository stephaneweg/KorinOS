# scene.py : essai du jalon 2 (docs/etude/BLENDER.md) dans Korin, sans écran :
#   /apps/blender/blender -b --factory-startup --python /home/blender/scene.py
# Crée une scène par l'API Python (objets, modificateurs, matériau), l'enregistre, la relit et vérifie ce qu'elle
# contient. Écrit « SCENE OK » à la fin ; une exception fait sortir Blender avec un code non nul.
import os
import sys
import bpy

out = sys.argv[sys.argv.index("--") + 1] if "--" in sys.argv else "/tmp/korin-scene.blend"
print("Blender", bpy.app.version_string, "Python", sys.version.split()[0], "plateforme", sys.platform)

bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete()
bpy.ops.mesh.primitive_monkey_add(location=(0, 0, 1))
singe = bpy.context.active_object
singe.name = "Singe"
sub = singe.modifiers.new("Sub", "SUBSURF")
sub.levels = 2
singe.modifiers.new("Bevel", "BEVEL")
bpy.ops.mesh.primitive_plane_add(size=10)
mat = bpy.data.materials.new("Rouge")
mat.use_nodes = True
mat.node_tree.nodes["Principled BSDF"].inputs["Base Color"].default_value = (0.8, 0.1, 0.1, 1)
singe.data.materials.append(mat)

dg = bpy.context.evaluated_depsgraph_get()
n = len(singe.evaluated_get(dg).data.polygons)
print("faces du singe après modificateurs :", n)
# 500 faces ; subdivisées deux fois (× 16) si Blender a OpenSubdiv, puis biseautées (plus de faces dans tous les cas)
osd = bpy.app.build_options.opensubdiv
print("OpenSubdiv :", osd)
assert n > (500 * 16 if osd else 500), n

bpy.ops.wm.save_as_mainfile(filepath=out)
print("enregistré :", out, os.path.getsize(out), "octets")
bpy.ops.wm.open_mainfile(filepath=out)
names = sorted(o.name for o in bpy.data.objects)
print("objets relus :", names)
assert names == ["Plane", "Singe"], names
assert [m.type for m in bpy.data.objects["Singe"].modifiers] == ["SUBSURF", "BEVEL"]
assert bpy.data.materials["Rouge"].users == 1
print("SCENE OK")
