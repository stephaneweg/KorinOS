# Korin OS : la plateforme « korin » de Godot (docs/GODOT.md). Construction croisee depuis WSL par
# tools/godot/build.sh, qui donne les chemins par l'environnement :
#   KORIN_TC      prefixe de la chaine (…/x86_64-onyx-elf-14.2/bin/x86_64-onyx-elf-)
#   KORIN_POSIX   la libposix de Korin (lib/posix.specs)
#   KORIN_KITS    include/kits et lib des kits (uikit_abi.h, libuikit.a : tools/pc/apps.sh)
#   KORIN_GL      les archives de GL a lier (libglkit.a du PC)
#   KORIN_VK      l'archive de Vulkan (libvkkit.a du PC : VkKit, docs/etude/VULKAN.md) ; sans elle, pas de Vulkan
#   KORIN_ROOT    la racine du depot KorinOS (en-tetes d'UIKit, d'armgl, EGL de Mesa)
import os
import sys
from typing import TYPE_CHECKING

from methods import print_error

if TYPE_CHECKING:
    from SCons.Script.SConscript import SConsEnvironment


def get_name():
    return "Korin"


def can_build():
    return "KORIN_TC" in os.environ


def get_opts():
    return []


def get_doc_classes():
    return []


def get_doc_path():
    return "doc_classes"


def get_flags():
    return {
        "arch": "x86_64",
        "supported": [],
        # ce que Korin n'a pas, ou pas encore
        "vulkan": "KORIN_VK" in os.environ,  # (VkKit : Forward+ et Mobile ; sinon Compatibility seul)
        "d3d12": False,
        "metal": False,
        "opengl3": True,
        "sdl": False,
        "accesskit": False,
        "threads": True,
        "lto": "none",
        "builtin_pcre2_with_jit": False,
        "module_camera_enabled": False,
        "module_mono_enabled": False,
        "module_openxr_enabled": False,
        "module_raycast_enabled": False,
        "module_upnp_enabled": False,
        "module_webrtc_enabled": False,
    }


def configure(env: "SConsEnvironment"):
    if env["arch"] not in ("x86_64", "arm64"):
        print_error("Korin : arch=x86_64 (PC) ou arch=arm64 (Pi).")
        sys.exit(255)

    tc = os.environ["KORIN_TC"]
    posix = os.environ["KORIN_POSIX"]
    kits = os.environ["KORIN_KITS"]
    root = os.environ["KORIN_ROOT"]
    gl = os.environ.get("KORIN_GL", "").split()
    vk = os.environ.get("KORIN_VK", "").split() if env["vulkan"] else []

    env["CC"] = tc + "gcc"
    env["CXX"] = tc + "g++"
    env["LINK"] = tc + "g++"
    env["AR"] = tc + "ar"
    env["RANLIB"] = tc + "ranlib"
    env["AS"] = tc + "as"

    specs = "-specs=" + posix + "/lib/posix.specs"
    env.Append(CCFLAGS=[specs, "-ffunction-sections", "-fdata-sections"])
    if env["arch"] == "x86_64":
        env.Append(CCFLAGS=["-msse4.2", "-mpopcnt"])
    else:
        env.Append(CCFLAGS=["-mcpu=cortex-a72"])
    env.Append(LINKFLAGS=[specs, "-Wl,--gc-sections", "-static-libgcc", "-static-libstdc++"])
    if env["target"] != "editor":
        # (la section « pck » du modèle, où l'export intègre le PCK du jeu : sans référence, --gc-sections la retirait
        # et l'export disait « Executable "pck" section not found »)
        env.Append(LINKFLAGS=["-Wl,-u,pck_section_dummy_call"])

    env.Prepend(CPPPATH=["#platform/korin"])
    env.Append(
        CPPPATH=[
            kits + "/include/kits",
            root + "/src/kits/uikit/include",
            root + "/tools/mesa/armgl",
            root + "/prebuilt/mesa/include",
        ]
    )
    env.Append(
        CPPDEFINES=[
            "UNIX_ENABLED",
            "KORIN_ENABLED",
            "_GNU_SOURCE",
            "GLES3_ENABLED",
            "GLAD_ENABLED",
            "GLAD_GLES2",
            "EGL_ENABLED",
            "EGL_STATIC",
            # Jolt (physique 3D) ne connait que des systemes nommes : celui des BSD (pthread, semaphore.h, pas
            # de prctl) est ce qui convient a libposix
            "JPH_PLATFORM_BSD",
        ]
    )
    env.Prepend(CPPPATH=["#thirdparty/glad"])

    # GL (GlKit) et UIKit ; libposix vient des specs
    # (des fichiers dans LIBS : SCons les met apres les objets, a la fin de la ligne de liaison)
    # Vulkan (VkKit) : volk prend vk_icdGetInstanceProcAddr de libvkkit.a (korin.patch) ; pas de libvulkan
    env.Append(LIBS=[env.File(p) for p in gl + vk] + [env.File(kits + "/lib/libuikit.a"), "pthread", "m"])
