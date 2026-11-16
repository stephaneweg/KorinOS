/* rendering_context_driver_vulkan_korin.h -- Vulkan sur Korin PC (VkKit, docs/etude/VULKAN.md) : la surface d'une
 * fenetre d'Elegant est une surface de VK_EXT_headless_surface avec VkKorinSurfaceInfo dans son pNext (la WSI de
 * Korin de Mesa, tools/mesa/wsi/wsi_common_korin.c). Comme rendering_context_driver_vulkan_x11.h. */
#pragma once

#ifdef VULKAN_ENABLED

#include "drivers/vulkan/rendering_context_driver_vulkan.h"

class RenderingContextDriverVulkanKorin : public RenderingContextDriverVulkan {
private:
	virtual const char *_get_platform_surface_extension() const override final;

protected:
	SurfaceID surface_create(const void *p_platform_data) override final;

public:
	struct WindowPlatformData {
		long window; // la fenetre chez Elegant (kui_window)
	};

	RenderingContextDriverVulkanKorin();
	~RenderingContextDriverVulkanKorin();
};

#endif // VULKAN_ENABLED
