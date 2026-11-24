/*
 * vkkit_loader.c -- le chargeur Vulkan minimal de Korin (libvkkit.a, docs/etude/VULKAN.md) : un seul pilote (anv,
 * dans /lib/vkkit.so), pas de couches. Donne ce qu'un programme attend du chargeur pour commencer :
 * vkGetInstanceProcAddr (tout le reste en vient : volk, Godot, ou le programme lui-meme), et les commandes globales
 * (vkCreateInstance, vkEnumerateInstance*). Les objets d'anv portent deja la marque du chargeur (ICD_LOADER_MAGIC) :
 * ses fonctions s'appellent directement, sans trampoline. Pour les programmes qui appellent toutes les fonctions en
 * direct (Blender) : vkkit_protos.c, produit par gen_protos.py, et vkkit_proc ci-dessous.
 */
#include <stdlib.h>
#include <unistd.h>
#include <vulkan/vulkan.h>

/* (le relais de libvkkit.a : la vraie fonction est dans le kit, liee au premier appel) */
extern PFN_vkVoidFunction vk_icdGetInstanceProcAddr (VkInstance instance, const char *name);

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vkGetInstanceProcAddr (VkInstance instance, const char *name)
{
	if (name && __builtin_strcmp (name, "vkGetInstanceProcAddr") == 0)
		return (PFN_vkVoidFunction) vkGetInstanceProcAddr;
	return vk_icdGetInstanceProcAddr (instance, name);
}

/* la dernière instance créée : les trampolines de vkkit_protos.c (gen_protos.py) y cherchent les fonctions */
static VkInstance s_instance;

VKAPI_ATTR VkResult VKAPI_CALL vkCreateInstance (const VkInstanceCreateInfo *info, const VkAllocationCallbacks *alloc,
						 VkInstance *instance)
{
	PFN_vkCreateInstance f = (PFN_vkCreateInstance) vk_icdGetInstanceProcAddr (NULL, "vkCreateInstance");
	VkResult r = f ? f (info, alloc, instance) : VK_ERROR_INITIALIZATION_FAILED;
	if (r == VK_SUCCESS)
		s_instance = *instance;
	return r;
}

/* pour vkkit_protos.c : la fonction d'anv, ou abort() avec son nom (un appel à une fonction absente) */
static void vkkit_missing (void)
{
	abort ();
}
PFN_vkVoidFunction vkkit_proc (const char *name)
{
	PFN_vkVoidFunction p = vk_icdGetInstanceProcAddr (s_instance, name);
	if (!p) {
		static const char m[] = "vkkit : fonction Vulkan absente : ";
		write (2, m, sizeof m - 1);
		write (2, name, __builtin_strlen (name));
		write (2, "\n", 1);
		p = (PFN_vkVoidFunction) vkkit_missing;
	}
	return p;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceExtensionProperties (const char *layer, uint32_t *count,
								       VkExtensionProperties *props)
{
	PFN_vkEnumerateInstanceExtensionProperties f = (PFN_vkEnumerateInstanceExtensionProperties)
		vk_icdGetInstanceProcAddr (NULL, "vkEnumerateInstanceExtensionProperties");
	return f ? f (layer, count, props) : VK_ERROR_INITIALIZATION_FAILED;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceLayerProperties (uint32_t *count, VkLayerProperties *props)
{
	(void) props;
	*count = 0;		/* (pas de couches) */
	return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL vkEnumerateInstanceVersion (uint32_t *version)
{
	PFN_vkEnumerateInstanceVersion f = (PFN_vkEnumerateInstanceVersion)
		vk_icdGetInstanceProcAddr (NULL, "vkEnumerateInstanceVersion");
	if (!f) {
		*version = VK_API_VERSION_1_0;
		return VK_SUCCESS;
	}
	return f (version);
}
