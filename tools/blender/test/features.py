# features.py : les fonctions du jalon 4 (docs/etude/BLENDER.md), sans fenêtre :
#   blender -b --factory-startup --python-exit-code 1 --python features.py
# subdivision (OpenSubdiv), booléens Exact (GMP) et Manifold, océan (FFTW3), rendu Cycles (Embree), Freestyle ;
# écrit « FEATURES OK » si tout passe, sinon lève une exception (code 1).
import time
import bpy

t0 = time.time()
res = {}


def fresh():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete()


def evaluated_verts(ob):
    dg = bpy.context.evaluated_depsgraph_get()
    return len(ob.evaluated_get(dg).data.vertices)


# 1. subdivision : un cube (8 sommets), niveau 2 -> 98 sommets ; sans OpenSubdiv le modificateur ne fait rien
fresh()
bpy.ops.mesh.primitive_cube_add()
cube = bpy.context.object
m = cube.modifiers.new("Subsurf", "SUBSURF")
m.levels = 2
res["subsurf"] = evaluated_verts(cube)
assert res["subsurf"] == 98, "subdivision : %d sommets au lieu de 98" % res["subsurf"]

# 2. booléens : une sphère ôtée d'un cube, solveurs Exact (GMP) et Manifold
for solver in ("EXACT", "MANIFOLD"):
    fresh()
    bpy.ops.mesh.primitive_cube_add()
    a = bpy.context.object
    bpy.ops.mesh.primitive_uv_sphere_add(radius=1.3)
    b = bpy.context.object
    b.hide_set(True)
    mod = a.modifiers.new("Bool", "BOOLEAN")
    mod.operation = "DIFFERENCE"
    mod.solver = solver
    mod.object = b
    n = evaluated_verts(a)
    res["bool_" + solver.lower()] = n
    assert n > 8, "booléen %s sans effet (%d sommets)" % (solver, n)

# 3. océan (FFTW3) : un maillage de résolution 7
fresh()
bpy.ops.mesh.primitive_plane_add()
p = bpy.context.object
oc = p.modifiers.new("Ocean", "OCEAN")
oc.resolution = 7
res["ocean"] = evaluated_verts(p)
assert res["ocean"] > 4, "océan sans effet"

# 4. rendu Cycles (Embree pour le lancer de rayons sur le processeur) avec Freestyle
fresh()
bpy.ops.mesh.primitive_monkey_add()
bpy.context.object.modifiers.new("Subsurf", "SUBSURF").levels = 1
bpy.ops.object.camera_add(location=(0, -4, 0.5), rotation=(1.5, 0, 0))
sc = bpy.context.scene
sc.camera = bpy.context.object
bpy.ops.object.light_add(type="SUN", location=(0, 0, 5))
sc.render.engine = "CYCLES"
sc.cycles.device = "CPU"
sc.cycles.samples = 8
sc.render.resolution_x, sc.render.resolution_y = 160, 120
sc.render.use_freestyle = True
sc.render.filepath = "/tmp/features.png"
t = time.time()
bpy.ops.render.render(write_still=True)
res["render_s"] = round(time.time() - t, 1)
bo = bpy.app.build_options
res["build"] = {k: getattr(bo, k) for k in ("opensubdiv", "gmp", "manifold", "fftw3", "freestyle", "fluid",
                                            "quadriflow", "remesh") if hasattr(bo, k)}

print("RESULTATS", res, "en %.1f s" % (time.time() - t0))
print("FEATURES OK")
