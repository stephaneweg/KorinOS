/*
 * vulkan_korin.h -- Vulkan sur Korin PC (VkKit, docs/etude/VULKAN.md) : montrer des images dans une fenetre
 * d'Elegant. Pas d'extension a part : une surface de VK_EXT_headless_surface, avec cette structure dans le pNext de
 * VkHeadlessSurfaceCreateInfoEXT. La chaine d'echange de cette surface montre ses images dans la fenetre (la
 * plateforme Korin de la WSI de Mesa, tools/mesa/wsi/wsi_common_korin.c) ; sans elle, une surface headless
 * ordinaire (rien n'est montre).
 *
 *     VkKorinSurfaceInfo k = { VK_STRUCTURE_TYPE_KORIN_SURFACE_INFO, NULL, window, 0, 0 };
 *     VkHeadlessSurfaceCreateInfoEXT h = { VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT, &k, 0 };
 *     vkCreateHeadlessSurfaceEXT (instance, &h, NULL, &surface);
 *
 * window : le numero de la fenetre chez Elegant (celui que GlKit recoit, ui_window_id d'UIKit) ; x, y : le coin de
 * la zone dans le contenu de la fenetre. La taille de la zone est celle de la chaine d'echange (imageExtent) :
 * une fenetre redimensionnee refait sa chaine. Formats : B8G8R8A8 (UNORM, SRGB) d'abord, l'ordre d'Elegant.
 */
#ifndef VULKAN_KORIN_H
#define VULKAN_KORIN_H 1

#include <vulkan/vulkan.h>

#define VK_STRUCTURE_TYPE_KORIN_SURFACE_INFO ((VkStructureType) 0x4B4F5201)

typedef struct VkKorinSurfaceInfo {
	VkStructureType sType;
	const void *pNext;
	long window;
	int x, y;
} VkKorinSurfaceInfo;

#endif
