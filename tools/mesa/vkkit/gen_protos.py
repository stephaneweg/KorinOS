"""gen_protos.py : les trampolines de VkKit (vkkit_protos.c) pour les programmes qui appellent Vulkan en direct
(Blender : vkCmdDraw, vkCreateSwapchainKHR…), sans volk. Chaque fonction de vulkan_core.h (sauf celles de
vkkit_loader.c) cherche au premier appel la fonction d'anv par vk_icdGetInstanceProcAddr sur l'instance créée par
le programme (vkkit_loader.c la retient), puis l'appelle ; anv rend aussi les fonctions de périphérique par cette
entrée (trampolines de Mesa). Une fonction qu'anv n'a pas : abort() au premier appel, avec son nom.

    python3 -I tools/mesa/vkkit/gen_protos.py <include>/vulkan/vulkan_core.h vkkit_protos.c
"""
import re
import sys

SKIP = {"vkGetInstanceProcAddr", "vkCreateInstance", "vkEnumerateInstanceExtensionProperties",
        "vkEnumerateInstanceLayerProperties", "vkEnumerateInstanceVersion"}
PROTO = re.compile(r"VKAPI_ATTR\s+([\w\s\*]+?)\s+VKAPI_CALL\s+(vk\w+)\s*\(([^;]*?)\)\s*;", re.S)


def arg_names(params):
    params = " ".join(params.split())
    if params in ("", "void"):
        return []
    names = []
    for p in params.split(","):
        p = re.sub(r"\[[^\]]*\]", "", p).strip()	# float blendConstants[4]
        names.append(re.findall(r"\w+", p)[-1])
    return names


def main():
    src, out = sys.argv[1], sys.argv[2]
    text = open(src, encoding="utf-8").read()
    seen = set()
    body = []
    for ret, name, params in PROTO.findall(text):
        if name in SKIP or name in seen:
            continue
        seen.add(name)
        ret = " ".join(ret.split())
        params1 = " ".join(params.split())
        args = ", ".join(arg_names(params))
        call = "p (%s)" % args
        body.append(
            "VKAPI_ATTR %s VKAPI_CALL %s (%s)\n{\n\tstatic PFN_%s p;\n\tif (!p)\n\t\tp = (PFN_%s) vkkit_proc (\"%s\");\n"
            "\t%s%s;\n}\n" % (ret, name, params1, name, name, name, "" if ret == "void" else "return ", call))
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write("/* vkkit_protos.c : produit par tools/mesa/vkkit/gen_protos.py depuis vulkan_core.h ; ne pas retoucher. */\n")
        f.write("#include <vulkan/vulkan.h>\n\n")
        f.write("extern PFN_vkVoidFunction vkkit_proc (const char *name);\n\n")
        f.write("\n".join(body))
    print("%s : %d fonctions" % (out, len(seen)))


if __name__ == "__main__":
    main()
