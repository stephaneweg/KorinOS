/*
 * wsi_common_korin.c -- la WSI de Vulkan pour Korin PC (docs/etude/VULKAN.md, etape 3) : une surface headless qui
 * porte une fenetre d'Elegant (VkKorinSurfaceInfo, tools/mesa/vkkit/include/vulkan/vulkan_korin.h) montre ses images
 * dans cette fenetre. Copie dans src/vulkan/wsi de Mesa par tools/mesa/configure-anv.sh ; appele par la retouche de
 * wsi_common_headless.c (korin-pc.patch).
 *
 * Les images : des images de la WSI de type « processeur avec copie dans un tampon » (WSI_IMAGE_TYPE_CPU,
 * WSI_SWAPCHAIN_BUFFER_BLIT) : le programme dessine dans une image tuilee (rapide pour le GPU) ; a chaque
 * vkQueuePresentKHR, la WSI fait copier par le GPU cette image dans un tampon lineaire visible du processeur, puis
 * signale le fence de l'image. Ce tampon est un BO du faux libdrm, donc une tranche d'un tampon partage du noyau
 * (BUF_CREATE) : Elegant le projette et compose directement depuis lui (korin_wsi_present : WS_OP_SURFACE_ATTACH
 * sur le tampon, a son decalage), sans recopie par le processeur.
 *
 * Un thread par chaine d'echange attend le fence de l'image donnee a vkQueuePresentKHR, puis l'attache chez
 * Elegant : vkQueuePresentKHR rend la main tout de suite. L'image montree reste intouchable (Elegant la lit quand il
 * compose) jusqu'a ce que la suivante la remplace ; vkAcquireNextImageKHR ne rend qu'une image libre (ni en attente
 * de montre, ni a l'ecran). Les etats : libre, acquise, en attente (au thread), a l'ecran.
 */
#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <string.h>
#include <time.h>

#include "util/macros.h"
#include "vk_util.h"
#include "vk_instance.h"
#include "wsi_common_entrypoints.h"
#include "wsi_common_private.h"

/* (la meme que vulkan_korin.h) */
#define KORIN_SURFACE_INFO ((VkStructureType) 0x4B4F5201)
struct korin_surface_info {
   VkStructureType sType;
   const void *pNext;
   long window;
   int x, y;
};

/* le faux libdrm du PC (tools/mesa/libdrm-pc/korin_i915.c, partie anv) : attache un tampon visible du processeur
 * (un BO a lui) a la fenetre <window>, zone x, y, w, h, lignes de <pitch> octets -> 0, < 0 ; detache */
int korin_wsi_present(long window, int x, int y, int w, int h, const void *va, unsigned pitch);
void korin_wsi_detach(long window);

/* (appeles par wsi_common_headless.c, qui les declare aussi) */
VkResult wsi_korin_create_surface(struct vk_instance *instance, const VkHeadlessSurfaceCreateInfoEXT *pCreateInfo,
                                  const VkAllocationCallbacks *pAllocator, VkSurfaceKHR *pSurface);
long wsi_korin_surface_window(VkIcdSurfaceBase *surface);
VkResult wsi_korin_create_swapchain(VkIcdSurfaceBase *icd_surface, VkDevice device, struct wsi_device *wsi_device,
                                    const VkSwapchainCreateInfoKHR *pCreateInfo,
                                    const VkAllocationCallbacks *pAllocator, struct wsi_swapchain **swapchain_out);

#define KORIN_MAGIC 0x4B4F5253u

/* une surface headless de Korin : toutes les surfaces headless le sont (window = 0 : headless ordinaire) */
struct wsi_korin_surface {
   VkIcdSurfaceHeadless base;
   uint32_t magic;
   long window;
   int x, y;
};

VkResult
wsi_korin_create_surface(struct vk_instance *instance, const VkHeadlessSurfaceCreateInfoEXT *pCreateInfo,
                         const VkAllocationCallbacks *pAllocator, VkSurfaceKHR *pSurface)
{
   struct wsi_korin_surface *s = vk_zalloc2(&instance->alloc, pAllocator, sizeof *s, 8,
                                            VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (s == NULL)
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   s->base.base.platform = VK_ICD_WSI_PLATFORM_HEADLESS;
   s->magic = KORIN_MAGIC;
   vk_foreach_struct_const(ext, pCreateInfo->pNext) {
      if (ext->sType == KORIN_SURFACE_INFO) {
         const struct korin_surface_info *k = (const void *)ext;
         s->window = k->window;
         s->x = k->x;
         s->y = k->y;
      }
   }
   *pSurface = VkIcdSurfaceBase_to_handle(&s->base.base);
   return VK_SUCCESS;
}

/* la fenetre d'Elegant d'une surface headless, 0 : aucune */
long
wsi_korin_surface_window(VkIcdSurfaceBase *surface)
{
   struct wsi_korin_surface *s = (struct wsi_korin_surface *)surface;
   return s->magic == KORIN_MAGIC ? s->window : 0;
}

enum { IMG_FREE, IMG_ACQUIRED, IMG_QUEUED, IMG_SHOWN };

struct korin_image {
   struct wsi_image base;
   int state;
};

struct korin_chain {
   struct wsi_swapchain base;
   long window;
   int x, y, w, h;
   pthread_t thread;
   pthread_mutex_t mu;
   pthread_cond_t cv;
   int queue[32];
   int qhead, qcount;
   int stop, shown, thread_ok;
   VkResult err;
   struct korin_image images[0];
};

static struct wsi_image *
korin_get_wsi_image(struct wsi_swapchain *wsi_chain, uint32_t index)
{
   struct korin_chain *c = (struct korin_chain *)wsi_chain;
   return &c->images[index].base;
}

static void *
korin_present_main(void *arg)
{
   struct korin_chain *c = arg;
   const struct wsi_device *wsi = c->base.wsi;
   pthread_mutex_lock(&c->mu);
   for (;;) {
      while (!c->stop && c->qcount == 0)
         pthread_cond_wait(&c->cv, &c->mu);
      if (c->qcount == 0)
         break; /* (arret, plus rien a montrer) */
      int i = c->queue[c->qhead];
      c->qhead = (c->qhead + 1) % ARRAY_SIZE(c->queue);
      c->qcount--;
      VkFence f = c->base.fences[i];
      pthread_mutex_unlock(&c->mu);
      /* la copie dans le tampon est finie quand le fence de l'image l'est (wsi_common_queue_present) */
      if (f != VK_NULL_HANDLE)
         wsi->WaitForFences(c->base.device, 1, &f, VK_TRUE, UINT64_MAX);
      struct korin_image *img = &c->images[i];
      int r = korin_wsi_present(c->window, c->x, c->y, c->w, c->h, img->base.cpu_map,
                                img->base.row_pitches[0]);
      pthread_mutex_lock(&c->mu);
      if (r < 0)
         c->err = VK_ERROR_SURFACE_LOST_KHR;
      if (c->shown >= 0 && c->shown != i)
         c->images[c->shown].state = IMG_FREE;
      c->shown = i;
      img->state = IMG_SHOWN;
      pthread_cond_broadcast(&c->cv);
   }
   pthread_mutex_unlock(&c->mu);
   return NULL;
}

static VkResult
korin_acquire_next_image(struct wsi_swapchain *wsi_chain, const VkAcquireNextImageInfoKHR *info,
                         uint32_t *image_index)
{
   struct korin_chain *c = (struct korin_chain *)wsi_chain;
   struct timespec end;
   clock_gettime(CLOCK_MONOTONIC, &end);
   uint64_t t = info->timeout;
   bool forever = t == UINT64_MAX;
   if (!forever) {
      end.tv_sec += t / 1000000000ull;
      end.tv_nsec += t % 1000000000ull;
      if (end.tv_nsec >= 1000000000L) {
         end.tv_sec++;
         end.tv_nsec -= 1000000000L;
      }
   }
   pthread_mutex_lock(&c->mu);
   VkResult r = VK_SUCCESS;
   for (;;) {
      if (c->err != VK_SUCCESS) {
         r = c->err;
         break;
      }
      uint32_t i;
      for (i = 0; i < c->base.image_count; i++)
         if (c->images[i].state == IMG_FREE)
            break;
      if (i < c->base.image_count) {
         c->images[i].state = IMG_ACQUIRED;
         *image_index = i;
         break;
      }
      if (t == 0) {
         r = VK_NOT_READY;
         break;
      }
      /* (pas de pthread_cond_timedwait sur l'horloge monotone ici : attente par tranches de 2 ms) */
      if (forever) {
         pthread_cond_wait(&c->cv, &c->mu);
      } else {
         struct timespec now;
         clock_gettime(CLOCK_MONOTONIC, &now);
         if (now.tv_sec > end.tv_sec || (now.tv_sec == end.tv_sec && now.tv_nsec >= end.tv_nsec)) {
            r = VK_TIMEOUT;
            break;
         }
         pthread_mutex_unlock(&c->mu);
         struct timespec d = { 0, 2000000 };
         nanosleep(&d, NULL);
         pthread_mutex_lock(&c->mu);
      }
   }
   pthread_mutex_unlock(&c->mu);
   return r;
}

static VkResult
korin_queue_present(struct wsi_swapchain *wsi_chain, uint32_t image_index, uint64_t present_id,
                    const VkPresentRegionKHR *damage)
{
   struct korin_chain *c = (struct korin_chain *)wsi_chain;
   (void)present_id;
   (void)damage;
   pthread_mutex_lock(&c->mu);
   VkResult r = c->err;
   c->images[image_index].state = IMG_QUEUED;
   c->queue[(c->qhead + c->qcount) % ARRAY_SIZE(c->queue)] = image_index;
   c->qcount++;
   pthread_cond_broadcast(&c->cv);
   pthread_mutex_unlock(&c->mu);
   return r;
}

static VkResult
korin_release_images(struct wsi_swapchain *wsi_chain, uint32_t count, const uint32_t *indices)
{
   struct korin_chain *c = (struct korin_chain *)wsi_chain;
   pthread_mutex_lock(&c->mu);
   for (uint32_t i = 0; i < count; i++)
      if (c->images[indices[i]].state == IMG_ACQUIRED)
         c->images[indices[i]].state = IMG_FREE;
   pthread_cond_broadcast(&c->cv);
   pthread_mutex_unlock(&c->mu);
   return VK_SUCCESS;
}

static VkResult
korin_wait_for_present(struct wsi_swapchain *wsi_chain, uint64_t value, uint64_t timeout)
{
   return wsi_swapchain_wait_for_present_semaphore(wsi_chain, value, timeout);
}

static VkResult
korin_destroy(struct wsi_swapchain *wsi_chain, const VkAllocationCallbacks *pAllocator)
{
   struct korin_chain *c = (struct korin_chain *)wsi_chain;
   if (c->thread_ok) {
      pthread_mutex_lock(&c->mu);
      c->stop = 1; /* (le thread montre d'abord ce qui attend) */
      pthread_cond_broadcast(&c->cv);
      pthread_mutex_unlock(&c->mu);
      pthread_join(c->thread, NULL);
   }
   /* l'image montree appartient a la chaine : Elegant la lache avant qu'elle parte */
   if (c->shown >= 0)
      korin_wsi_detach(c->window);
   for (uint32_t i = 0; i < c->base.image_count; i++)
      if (c->images[i].base.image != VK_NULL_HANDLE)
         wsi_destroy_image(&c->base, &c->images[i].base);
   pthread_cond_destroy(&c->cv);
   pthread_mutex_destroy(&c->mu);
   wsi_swapchain_finish(&c->base);
   vk_free(pAllocator, c);
   return VK_SUCCESS;
}

VkResult
wsi_korin_create_swapchain(VkIcdSurfaceBase *icd_surface, VkDevice device, struct wsi_device *wsi_device,
                           const VkSwapchainCreateInfoKHR *pCreateInfo,
                           const VkAllocationCallbacks *pAllocator, struct wsi_swapchain **swapchain_out)
{
   struct wsi_korin_surface *surface = (struct wsi_korin_surface *)icd_surface;
   uint32_t n = pCreateInfo->minImageCount < 3 ? 3 : pCreateInfo->minImageCount;
   if (n > 16)
      n = 16;
   struct korin_chain *c = vk_zalloc(pAllocator, sizeof(*c) + n * sizeof(c->images[0]), 8,
                                     VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (c == NULL)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   /* une image du processeur, copiee par le GPU dans un tampon lineaire visible du processeur */
   struct wsi_cpu_image_params cpu_params = {
      .base.image_type = WSI_IMAGE_TYPE_CPU,
   };
   VkResult result = wsi_swapchain_init(wsi_device, &c->base, device, pCreateInfo, &cpu_params.base, pAllocator);
   if (result != VK_SUCCESS) {
      vk_free(pAllocator, c);
      return result;
   }
   c->base.destroy = korin_destroy;
   c->base.get_wsi_image = korin_get_wsi_image;
   c->base.acquire_next_image = korin_acquire_next_image;
   c->base.release_images = korin_release_images;
   c->base.queue_present = korin_queue_present;
   c->base.wait_for_present = korin_wait_for_present;
   c->base.present_mode = wsi_swapchain_get_present_mode(wsi_device, pCreateInfo);
   c->base.image_count = n;
   c->window = surface->window;
   c->x = surface->x;
   c->y = surface->y;
   c->w = pCreateInfo->imageExtent.width;
   c->h = pCreateInfo->imageExtent.height;
   c->shown = -1;
   c->err = VK_SUCCESS;
   pthread_mutex_init(&c->mu, NULL);
   pthread_cond_init(&c->cv, NULL);

   uint32_t i = 0;
   for (; i < n; i++) {
      result = wsi_create_image(&c->base, &c->base.image_info, &c->images[i].base);
      if (result != VK_SUCCESS)
         break;
      c->images[i].state = IMG_FREE;
   }
   if (result == VK_SUCCESS && pthread_create(&c->thread, NULL, korin_present_main, c) != 0)
      result = VK_ERROR_INITIALIZATION_FAILED;
   if (result != VK_SUCCESS) {
      for (uint32_t k = 0; k < i; k++)
         wsi_destroy_image(&c->base, &c->images[k].base);
      pthread_cond_destroy(&c->cv);
      pthread_mutex_destroy(&c->mu);
      wsi_swapchain_finish(&c->base);
      vk_free(pAllocator, c);
      return result;
   }
   c->thread_ok = 1;
   *swapchain_out = &c->base;
   return VK_SUCCESS;
}
