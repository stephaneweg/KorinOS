/* rendering_context_driver_vulkan_korin.cpp -- Vulkan sur Korin PC (VkKit) : la surface d'une fenetre d'Elegant
 * (docs/etude/VULKAN.md, § 3). */
#ifdef VULKAN_ENABLED

#include "rendering_context_driver_vulkan_korin.h"
// (Vulkan, par volk ou non : les en-tetes que rendering_context_driver_vulkan.h prend deja)

/* (la meme que tools/mesa/vkkit/include/vulkan/vulkan_korin.h : Godot garde ses propres en-tetes de Vulkan) */
#define KORIN_STRUCTURE_TYPE_SURFACE_INFO ((VkStructureType)0x4B4F5201)
struct KorinSurfaceInfo {
	VkStructureType sType;
	const void *pNext;
	long window;
	int x, y;
};

const char *RenderingContextDriverVulkanKorin::_get_platform_surface_extension() const {
	return VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME;
}

RenderingContextDriver::SurfaceID RenderingContextDriverVulkanKorin::surface_create(const void *p_platform_data) {
	const WindowPlatformData *wpd = (const WindowPlatformData *)(p_platform_data);

	KorinSurfaceInfo korin = {};
	korin.sType = KORIN_STRUCTURE_TYPE_SURFACE_INFO;
	korin.window = wpd->window;

	VkHeadlessSurfaceCreateInfoEXT create_info = {};
	create_info.sType = VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT;
	create_info.pNext = &korin;

	VkSurfaceKHR vk_surface = VK_NULL_HANDLE;
	VkResult err = vkCreateHeadlessSurfaceEXT(instance_get(), &create_info, get_allocation_callbacks(VK_OBJECT_TYPE_SURFACE_KHR), &vk_surface);
	ERR_FAIL_COND_V(err != VK_SUCCESS, SurfaceID());

	Surface *surface = memnew(Surface);
	surface->vk_surface = vk_surface;
	return SurfaceID(surface);
}

RenderingContextDriverVulkanKorin::RenderingContextDriverVulkanKorin() {
}

RenderingContextDriverVulkanKorin::~RenderingContextDriverVulkanKorin() {
}

#endif // VULKAN_ENABLED
