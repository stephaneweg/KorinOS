# kwargs.py : les arguments nommés des fonctions de l'API RNA de Blender (bpy), appelés dans un autre ordre que le leur.
#   /apps/blender/blender -b --factory-startup --python-exit-code 1 --python /home/blender/kwargs.py
import bpy

me = bpy.data.meshes.new("m")
a = bpy.data.objects.new("positionnel", me)
print("positionnel :", a.name, a.data.name)
b = bpy.data.objects.new(object_data=me, name="nomme")
print("nommé, ordre inverse :", b.name, b.data.name if b.data else None)
assert b.name == "nomme" and b.data == me, (b.name, b.data)
c = bpy.data.objects.new(name="nomme2", object_data=None)
print("nommé, ordre normal :", c.name, c.data)
assert c.name == "nomme2" and c.data is None
print("KWARGS OK")
