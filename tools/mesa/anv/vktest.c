/*
 * vktest -- premier essai de Vulkan sur Korin PC (anv, le pilote Vulkan d'Intel de Mesa ; docs/etude/VULKAN.md) :
 * instance, le GPU et ses capacites, un peripherique, puis un lot : remplir un tampon (vkCmdFillBuffer, que anv fait
 * par un shader interne : le compilateur tourne), en copier une partie (vkCmdCopyBuffer), relire et verifier ; une
 * barriere chronologique attendue par le processeur. Deux constructions : liee a VkKit (/bin/vktest,
 * build-vkkit-pc.sh, VKTEST_KIT : avec --win), ou statique avec anv (build-anv.sh, VKTEST_STATIC : le diagnostic
 * de -d en plus). vk_icdGetInstanceProcAddr directement (pas de couches).
 *
 *     vktest            sur le GPU (le service intelgpu doit tourner)
 *     vktest --dry      a blanc (KORIN_VK_DRY : lots comptes, pas lances ; pas de verification)
 *     vktest -v         plus de capacites (toutes les extensions)
 *     vktest -d         les messages de Mesa et du faux libdrm a l'ecran (KORIN_DRM_DEBUG)
 *     vktest --win      (VkKit) de plus, une fenetre : une surface de Korin (vulkan_korin.h), une chaine d'echange,
 *                       un fond qui change de teinte et un carre qui se deplace (sans shader : effacement et copie),
 *                       jusqu'a la fermeture ; les images par seconde au Terminal
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <vulkan/vulkan.h>

extern PFN_vkVoidFunction vk_icdGetInstanceProcAddr (VkInstance instance, const char *name);

static PFN_vkGetInstanceProcAddr gipa;
static VkInstance inst;
static int fails;

#define IFN(n) PFN_##n n = (PFN_##n) gipa (inst, #n)

static double now_ms (void)
{
	struct timespec t;
	clock_gettime (CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static void check (const char *what, VkResult r)
{
	if (r != VK_SUCCESS) {
		printf ("[ÉCHEC] %s : VkResult %d\n", what, (int) r);
		fails++;
	}
}

static VKAPI_ATTR VkBool32 VKAPI_CALL debug_message (VkDebugUtilsMessageSeverityFlagBitsEXT sev,
						      VkDebugUtilsMessageTypeFlagsEXT type,
						      const VkDebugUtilsMessengerCallbackDataEXT *d, void *user)
{
	(void) type; (void) user;
	printf ("  pilote (%s) : %s\n",
		sev & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT ? "erreur"
		: sev & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT ? "attention" : "info",
		d && d->pMessage ? d->pMessage : "?");
	return VK_FALSE;
}

/* -d : une faute dit ou elle a eu lieu (rip, et le haut de la pile) ; a traduire par addr2line sur
 * ~/armpi-mesa/out-x86_64/anv/vktest (le binaire avec symboles) */
#include <signal.h>
#include <sys/ucontext.h>
int main (int argc, char **argv);
static void on_fault (int sig, siginfo_t *si, void *ctx)
{
	ucontext_t *uc = ctx;
	uint64_t rip = (uint64_t) uc->uc_mcontext.gregs[REG_RIP], rsp = (uint64_t) uc->uc_mcontext.gregs[REG_RSP];
	printf ("FAUTE : signal %d, adresse %p, rip %llx, rsp %llx\n", sig, si ? si->si_addr : 0,
		(unsigned long long) rip, (unsigned long long) rsp);
	const uint64_t *sp = (const uint64_t *) rsp;
	printf ("  pile (le premier mot : le retour d'un appel a un pointeur nul) :");
	for (int i = 0; i < 16; i++)
		printf (" %llx", (unsigned long long) sp[i]);
	printf ("\n  (main %p)\n", (void *) main);
	_exit (2);
}

static const char *type_name (VkPhysicalDeviceType t)
{
	switch (t) {
	case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "GPU intégré";
	case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "GPU séparé";
	case VK_PHYSICAL_DEVICE_TYPE_CPU: return "processeur";
	default: return "autre";
	}
}

#ifdef VKTEST_KIT
/* --win : une fenetre UIKit, une surface de Korin, une chaine d'echange ; un fond qui change de teinte et un carre
 * blanc qui rebondit, jusqu'a la fermeture (aucun shader : vkCmdClearColorImage, vkCmdCopyBufferToImage) */
#include <math.h>
#include <uikit.h>
#include <vulkan/vulkan_korin.h>
static struct ui_vars ui;

static uint32_t host_type (const VkPhysicalDeviceMemoryProperties *mp, uint32_t bits)
{
	const VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	for (uint32_t k = 0; k < mp->memoryTypeCount; k++)
		if ((bits & (1u << k)) && (mp->memoryTypes[k].propertyFlags & want) == want)
			return k;
	return 0;
}

static int run_window (VkPhysicalDevice gpu, VkDevice dev, VkQueue q, uint32_t qfi,
		       const VkPhysicalDeviceMemoryProperties *mp, PFN_vkGetDeviceProcAddr gdpa)
{
	enum { W = 640, H = 480, SQ = 64, NF = 2 };
	if (!uikit_open (&ui) || ui_window (W, H, (long) "vktest : Vulkan (anv)") <= 0) {
		printf ("[ÉCHEC] pas de fenêtre (UIKit)\n");
		return 1;
	}
#define DF(n) PFN_##n n = (PFN_##n) gdpa (dev, #n)
	IFN (vkCreateHeadlessSurfaceEXT);
	IFN (vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
	IFN (vkGetPhysicalDeviceSurfaceFormatsKHR);
	IFN (vkDestroySurfaceKHR);
	DF (vkCreateSwapchainKHR);
	DF (vkGetSwapchainImagesKHR);
	DF (vkAcquireNextImageKHR);
	DF (vkQueuePresentKHR);
	DF (vkDestroySwapchainKHR);
	DF (vkCreateSemaphore);
	DF (vkCreateFence);
	DF (vkWaitForFences);
	DF (vkResetFences);
	DF (vkCreateCommandPool);
	DF (vkAllocateCommandBuffers);
	DF (vkBeginCommandBuffer);
	DF (vkEndCommandBuffer);
	DF (vkResetCommandBuffer);
	DF (vkCmdPipelineBarrier);
	DF (vkCmdClearColorImage);
	DF (vkCmdCopyBufferToImage);
	DF (vkQueueSubmit);
	DF (vkDeviceWaitIdle);
	DF (vkCreateBuffer);
	DF (vkGetBufferMemoryRequirements);
	DF (vkAllocateMemory);
	DF (vkBindBufferMemory);
	DF (vkMapMemory);
	if (!vkCreateHeadlessSurfaceEXT || !vkCreateSwapchainKHR) {
		printf ("[ÉCHEC] VK_EXT_headless_surface ou VK_KHR_swapchain absents\n");
		return 1;
	}
	VkKorinSurfaceInfo k = { VK_STRUCTURE_TYPE_KORIN_SURFACE_INFO, NULL, ui.win, 0, 0 };
	VkHeadlessSurfaceCreateInfoEXT hci = { VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT, &k, 0 };
	VkSurfaceKHR surf;
	VkResult r = vkCreateHeadlessSurfaceEXT (inst, &hci, NULL, &surf);
	if (r != VK_SUCCESS) {
		printf ("[ÉCHEC] surface : VkResult %d\n", (int) r);
		return 1;
	}
	VkSurfaceCapabilitiesKHR caps;
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR (gpu, surf, &caps);
	uint32_t nf = 1;
	VkSurfaceFormatKHR fmt = { VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
	vkGetPhysicalDeviceSurfaceFormatsKHR (gpu, surf, &nf, &fmt);
	uint32_t want = caps.minImageCount < 3 ? 3 : caps.minImageCount;
	VkSwapchainCreateInfoKHR sci = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, NULL, 0, surf, want, fmt.format,
					 fmt.colorSpace, { W, H }, 1,
					 VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
					 VK_SHARING_MODE_EXCLUSIVE, 0, NULL, VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
					 VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_PRESENT_MODE_FIFO_KHR, VK_TRUE, VK_NULL_HANDLE };
	VkSwapchainKHR sc;
	r = vkCreateSwapchainKHR (dev, &sci, NULL, &sc);
	if (r != VK_SUCCESS) {
		printf ("[ÉCHEC] chaîne d'échange : VkResult %d\n", (int) r);
		return 1;
	}
	uint32_t n = 0;
	vkGetSwapchainImagesKHR (dev, sc, &n, NULL);
	VkImage img[16];
	if (n > 16)
		n = 16;
	vkGetSwapchainImagesKHR (dev, sc, &n, img);
	printf ("[ok] chaîne d'échange : %u images de %dx%d, format %d (%s)\n", n, W, H, (int) fmt.format,
		fmt.format == VK_FORMAT_B8G8R8A8_UNORM || fmt.format == VK_FORMAT_B8G8R8A8_SRGB ? "BGRA" : "autre");

	/* le carre : un tampon blanc, copie dans l'image a une place qui change */
	VkBufferCreateInfo bci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0, SQ * SQ * 4, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
				   VK_SHARING_MODE_EXCLUSIVE, 0, NULL };
	VkBuffer sqb;
	vkCreateBuffer (dev, &bci, NULL, &sqb);
	VkMemoryRequirements mr;
	vkGetBufferMemoryRequirements (dev, sqb, &mr);
	VkMemoryAllocateInfo mai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, mr.size, host_type (mp, mr.memoryTypeBits) };
	VkDeviceMemory sqm;
	vkAllocateMemory (dev, &mai, NULL, &sqm);
	vkBindBufferMemory (dev, sqb, sqm, 0);
	uint32_t *sqp;
	vkMapMemory (dev, sqm, 0, SQ * SQ * 4, 0, (void **) &sqp);
	for (int i = 0; i < SQ * SQ; i++)
		sqp[i] = 0xFFFFFFFFu;

	VkCommandPoolCreateInfo pci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, NULL,
					VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, qfi };
	VkCommandPool pool;
	vkCreateCommandPool (dev, &pci, NULL, &pool);
	VkCommandBufferAllocateInfo cai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL, pool,
					    VK_COMMAND_BUFFER_LEVEL_PRIMARY, NF };
	VkCommandBuffer cb[NF];
	vkAllocateCommandBuffers (dev, &cai, cb);
	VkSemaphoreCreateInfo semi = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, NULL, 0 };
	VkFenceCreateInfo fci = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, NULL, VK_FENCE_CREATE_SIGNALED_BIT };
	VkSemaphore acq[NF], done[16];
	VkFence fence[NF];
	for (int i = 0; i < NF; i++) {
		vkCreateSemaphore (dev, &semi, NULL, &acq[i]);
		vkCreateFence (dev, &fci, NULL, &fence[i]);
	}
	for (uint32_t i = 0; i < n; i++)
		vkCreateSemaphore (dev, &semi, NULL, &done[i]);

	printf ("vktest : Vulkan dessine dans la fenêtre (la fermer pour finir)\n");
	double t0 = now_ms (), tl = t0;
	long frames = 0, shown = 0;
	int rc = 0;
	for (;; frames++) {
		if (ui_event (0) == WEV_CLOSE)
			break;
		int f = (int) (frames % NF);
		vkWaitForFences (dev, 1, &fence[f], VK_TRUE, UINT64_MAX);
		vkResetFences (dev, 1, &fence[f]);
		uint32_t ii;
		r = vkAcquireNextImageKHR (dev, sc, UINT64_MAX, acq[f], VK_NULL_HANDLE, &ii);
		if (r < 0) {
			printf ("[ÉCHEC] vkAcquireNextImageKHR : VkResult %d\n", (int) r);
			rc = 1;
			break;
		}
		double t = (now_ms () - t0) / 1000.0;
		VkCommandBufferBeginInfo cbi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL,
						 VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, NULL };
		vkResetCommandBuffer (cb[f], 0);
		vkBeginCommandBuffer (cb[f], &cbi);
		VkImageSubresourceRange all = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
		VkImageMemoryBarrier b1 = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER, NULL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
					    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
					    VK_QUEUE_FAMILY_IGNORED, VK_QUEUE_FAMILY_IGNORED, img[ii], all };
		vkCmdPipelineBarrier (cb[f], VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL,
				      0, NULL, 1, &b1);
		VkClearColorValue cc = { { 0.5f + 0.4f * (float) sin (t), 0.5f + 0.4f * (float) sin (t * 1.3 + 2.0),
					   0.5f + 0.4f * (float) sin (t * 0.7 + 4.0), 1.0f } };
		vkCmdClearColorImage (cb[f], img[ii], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &cc, 1, &all);
		VkMemoryBarrier mb = { VK_STRUCTURE_TYPE_MEMORY_BARRIER, NULL, VK_ACCESS_TRANSFER_WRITE_BIT,
				       VK_ACCESS_TRANSFER_WRITE_BIT };
		vkCmdPipelineBarrier (cb[f], VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mb, 0, NULL,
				      0, NULL);
		/* le carre rebondit : une onde en triangle sur chaque axe */
		int sx = (int) (frames * 5) % (2 * (W - SQ)), sy = (int) (frames * 3) % (2 * (H - SQ));
		if (sx > W - SQ)
			sx = 2 * (W - SQ) - sx;
		if (sy > H - SQ)
			sy = 2 * (H - SQ) - sy;
		VkBufferImageCopy reg = { 0, 0, 0, { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 }, { sx, sy, 0 }, { SQ, SQ, 1 } };
		vkCmdCopyBufferToImage (cb[f], sqb, img[ii], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &reg);
		VkImageMemoryBarrier b2 = b1;
		b2.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
		b2.dstAccessMask = 0;
		b2.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
		b2.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
		vkCmdPipelineBarrier (cb[f], VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL,
				      0, NULL, 1, &b2);
		vkEndCommandBuffer (cb[f]);
		VkPipelineStageFlags ws = VK_PIPELINE_STAGE_TRANSFER_BIT;
		VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO, NULL, 1, &acq[f], &ws, 1, &cb[f], 1, &done[ii] };
		r = vkQueueSubmit (q, 1, &si, fence[f]);
		if (r != VK_SUCCESS) {
			printf ("[ÉCHEC] vkQueueSubmit : VkResult %d\n", (int) r);
			rc = 1;
			break;
		}
		VkPresentInfoKHR pi = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, NULL, 1, &done[ii], 1, &sc, &ii, NULL };
		r = vkQueuePresentKHR (q, &pi);
		if (r < 0) {
			printf ("[ÉCHEC] vkQueuePresentKHR : VkResult %d\n", (int) r);
			rc = 1;
			break;
		}
		shown++;
		double now = now_ms ();
		if (now - tl >= 2000.0) {
			printf ("vktest : %.1f images/s\n", shown * 1000.0 / (now - tl));
			tl = now;
			shown = 0;
		}
	}
	vkDeviceWaitIdle (dev);
	vkDestroySwapchainKHR (dev, sc, NULL);
	vkDestroySurfaceKHR (inst, surf, NULL);
	printf ("%s fenêtre : %ld images\n", rc ? "[ÉCHEC]" : "[ok]", frames);
	return rc;
}
#endif

int main (int argc, char **argv)
{
	int dry = 0, verbose = 0, win = 0;
	for (int i = 1; i < argc; i++) {
		if (!strcmp (argv[i], "--dry"))
			dry = 1;
		else if (!strcmp (argv[i], "-v"))
			verbose = 1;
		else if (!strcmp (argv[i], "--win"))
			win = 1;
		else if (!strcmp (argv[i], "-d")) {
			setenv ("KORIN_DRM_DEBUG", "1", 1);
			setenv ("MESA_LOG_LEVEL", "debug", 1);
			dup2 (1, 2);		/* (les messages de Mesa vont sur la sortie d'erreur) */
			struct sigaction sa;
			memset (&sa, 0, sizeof sa);
			sa.sa_sigaction = on_fault;
			sa.sa_flags = SA_SIGINFO;
			sigaction (SIGSEGV, &sa, NULL);
			sigaction (SIGILL, &sa, NULL);
		}
	}
	if (dry)
		setenv ("KORIN_VK_DRY", "1", 1);
	setvbuf (stdout, NULL, _IONBF, 0);
#ifdef VKTEST_STATIC
	if (getenv ("KORIN_DRM_DEBUG")) {
		/* (diagnostic : ce qu'anv fait avant de garder le GPU, a la main) */
		extern int korin_anv_open (void);
		extern _Bool intel_get_device_info_from_fd (int fd, void *devinfo, int min_ver, int max_ver);
		static char devinfo[1 << 16];
		int fd = korin_anv_open ();
		int ok = fd >= 0 && intel_get_device_info_from_fd (fd, devinfo, 9, -1);
		printf ("diagnostic : descripteur %d, identification de la puce %s\n", fd, ok ? "bonne" : "RATÉE");
	}
#endif
	gipa = (PFN_vkGetInstanceProcAddr) vk_icdGetInstanceProcAddr;
	printf ("vktest : Vulkan sur Korin PC (anv)%s\n", dry ? ", à blanc" : "");

	/* ---- instance */
	double t0 = now_ms ();
	IFN (vkCreateInstance);
	IFN (vkEnumerateInstanceVersion);
	uint32_t iv = VK_API_VERSION_1_0;
	if (vkEnumerateInstanceVersion)
		vkEnumerateInstanceVersion (&iv);
	VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO, NULL, "vktest", 1, "korin", 1, VK_API_VERSION_1_3 };
	VkInstanceCreateInfo ici = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, NULL, 0, &app, 0, NULL, 0, NULL };
	/* -d : les messages du pilote (vk_errorf d'anv) par VK_EXT_debug_utils, a l'ecran */
	static const char *dbg_ext[] = { VK_EXT_DEBUG_UTILS_EXTENSION_NAME };
	VkDebugUtilsMessengerCreateInfoEXT dmi = { VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT, NULL, 0,
						   VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT |
							   VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
							   VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
							   VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
						   VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
							   VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
							   VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
						   debug_message, NULL };
	static const char *ext[4];
	uint32_t next = 0;
	if (getenv ("KORIN_DRM_DEBUG")) {
		ici.pNext = &dmi;
		ext[next++] = dbg_ext[0];
	}
	if (win) {
		ext[next++] = VK_KHR_SURFACE_EXTENSION_NAME;
		ext[next++] = VK_EXT_HEADLESS_SURFACE_EXTENSION_NAME;
	}
	ici.enabledExtensionCount = next;
	ici.ppEnabledExtensionNames = ext;
	VkResult r = vkCreateInstance (&ici, NULL, &inst);
	if (r != VK_SUCCESS) {
		printf ("[ÉCHEC] vkCreateInstance : VkResult %d\n", (int) r);
		return 1;
	}
	printf ("[ok] instance Vulkan %u.%u.%u en %.1f ms\n", VK_API_VERSION_MAJOR (iv), VK_API_VERSION_MINOR (iv),
		VK_API_VERSION_PATCH (iv), now_ms () - t0);
	if (getenv ("KORIN_DRM_DEBUG")) {	/* (le messager de pNext ne vaut que pendant vkCreateInstance) */
		IFN (vkCreateDebugUtilsMessengerEXT);
		VkDebugUtilsMessengerEXT m;
		if (vkCreateDebugUtilsMessengerEXT)
			vkCreateDebugUtilsMessengerEXT (inst, &dmi, NULL, &m);
	}

	/* ---- le GPU */
	IFN (vkEnumeratePhysicalDevices);
	IFN (vkGetPhysicalDeviceProperties);
	IFN (vkGetPhysicalDeviceMemoryProperties);
	IFN (vkGetPhysicalDeviceQueueFamilyProperties);
	IFN (vkGetPhysicalDeviceFeatures2);
	IFN (vkEnumerateDeviceExtensionProperties);
	IFN (vkCreateDevice);
	IFN (vkDestroyInstance);
	t0 = now_ms ();
	uint32_t n = 0;
	r = vkEnumeratePhysicalDevices (inst, &n, NULL);
	if (r != VK_SUCCESS || n == 0) {
		printf ("[ÉCHEC] aucun GPU Vulkan (VkResult %d, %u GPU) : le service intelgpu tourne-t-il ? "
			"(/bin/intelgpu status ; journal : lignes korin_i915, anv)\n", (int) r, n);
		vkDestroyInstance (inst, NULL);
		return 1;
	}
	VkPhysicalDevice gpu;
	n = 1;
	vkEnumeratePhysicalDevices (inst, &n, &gpu);
	VkPhysicalDeviceProperties pp;
	vkGetPhysicalDeviceProperties (gpu, &pp);
	printf ("[ok] GPU : %s (%s, %04x:%04x), Vulkan %u.%u.%u, pilote %u.%u.%u, trouvé en %.1f ms\n", pp.deviceName,
		type_name (pp.deviceType), pp.vendorID, pp.deviceID, VK_API_VERSION_MAJOR (pp.apiVersion),
		VK_API_VERSION_MINOR (pp.apiVersion), VK_API_VERSION_PATCH (pp.apiVersion),
		VK_API_VERSION_MAJOR (pp.driverVersion), VK_API_VERSION_MINOR (pp.driverVersion),
		VK_API_VERSION_PATCH (pp.driverVersion), now_ms () - t0);
	printf ("     limites : image 2D %u, groupe de calcul %u x %u x %u (%u fils), push %u o, ancrage UBO %llu o\n",
		pp.limits.maxImageDimension2D, pp.limits.maxComputeWorkGroupSize[0], pp.limits.maxComputeWorkGroupSize[1],
		pp.limits.maxComputeWorkGroupSize[2], pp.limits.maxComputeWorkGroupInvocations,
		pp.limits.maxPushConstantsSize, (unsigned long long) pp.limits.minUniformBufferOffsetAlignment);
	VkPhysicalDeviceMemoryProperties mp;
	vkGetPhysicalDeviceMemoryProperties (gpu, &mp);
	for (uint32_t i = 0; i < mp.memoryHeapCount; i++)
		printf ("     mémoire %u : %llu Mo%s\n", i, (unsigned long long) (mp.memoryHeaps[i].size >> 20),
			mp.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT ? " (du GPU)" : "");
	uint32_t nq = 0;
	vkGetPhysicalDeviceQueueFamilyProperties (gpu, &nq, NULL);
	VkQueueFamilyProperties qf[8];
	if (nq > 8)
		nq = 8;
	vkGetPhysicalDeviceQueueFamilyProperties (gpu, &nq, qf);
	uint32_t qfi = UINT32_MAX;
	for (uint32_t i = 0; i < nq; i++) {
		printf ("     file %u : %u x %s%s%s%s\n", i, qf[i].queueCount,
			qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT ? "dessin " : "",
			qf[i].queueFlags & VK_QUEUE_COMPUTE_BIT ? "calcul " : "",
			qf[i].queueFlags & VK_QUEUE_TRANSFER_BIT ? "copie " : "",
			qf[i].queueFlags & VK_QUEUE_SPARSE_BINDING_BIT ? "épars" : "");
		if (qfi == UINT32_MAX && (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT))
			qfi = i;
	}
	VkPhysicalDeviceVulkan13Features f13 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES };
	VkPhysicalDeviceVulkan12Features f12 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, &f13 };
	VkPhysicalDeviceFeatures2 f2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, &f12 };
	if (vkGetPhysicalDeviceFeatures2) {
		vkGetPhysicalDeviceFeatures2 (gpu, &f2);
		printf ("     fonctions : géométrie %d, pavage %d, multiDraw indirect %d, float64 %d, sémaphores "
			"chronologiques %d, adresses de tampons %d, indexation des descripteurs %d, rendu dynamique %d, "
			"synchronization2 %d\n", f2.features.geometryShader, f2.features.tessellationShader,
			f2.features.multiDrawIndirect, f2.features.shaderFloat64, f12.timelineSemaphore,
			f12.bufferDeviceAddress, f12.descriptorIndexing, f13.dynamicRendering, f13.synchronization2);
	}
	uint32_t ne = 0;
	vkEnumerateDeviceExtensionProperties (gpu, NULL, &ne, NULL);
	printf ("     %u extensions du périphérique\n", ne);
	if (verbose && ne) {
		VkExtensionProperties *ep = calloc (ne, sizeof *ep);
		vkEnumerateDeviceExtensionProperties (gpu, NULL, &ne, ep);
		for (uint32_t i = 0; i < ne; i++)
			printf ("       %s\n", ep[i].extensionName);
		free (ep);
	}
	if (qfi == UINT32_MAX) {
		printf ("[ÉCHEC] pas de file de dessin\n");
		return 1;
	}

	/* ---- un peripherique, une file */
	t0 = now_ms ();
	float prio = 1.0f;
	VkDeviceQueueCreateInfo qci = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO, NULL, 0, qfi, 1, &prio };
	VkPhysicalDeviceTimelineSemaphoreFeatures tsf = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES,
							  NULL, VK_TRUE };
	VkDeviceCreateInfo dci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, &tsf, 0, 1, &qci, 0, NULL, 0, NULL, NULL };
	static const char *dev_ext[] = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
	if (win) {
		dci.enabledExtensionCount = 1;
		dci.ppEnabledExtensionNames = dev_ext;
	}
	VkDevice dev;
	r = vkCreateDevice (gpu, &dci, NULL, &dev);
	if (r != VK_SUCCESS) {
		printf ("[ÉCHEC] vkCreateDevice : VkResult %d\n", (int) r);
		return 1;
	}
	printf ("[ok] périphérique créé en %.1f ms\n", now_ms () - t0);
	PFN_vkGetDeviceProcAddr gdpa = (PFN_vkGetDeviceProcAddr) gipa (inst, "vkGetDeviceProcAddr");
#define DFN(n) PFN_##n n = (PFN_##n) gdpa (dev, #n)
	DFN (vkGetDeviceQueue);
	DFN (vkCreateBuffer);
	DFN (vkGetBufferMemoryRequirements);
	DFN (vkAllocateMemory);
	DFN (vkBindBufferMemory);
	DFN (vkMapMemory);
	DFN (vkCreateCommandPool);
	DFN (vkAllocateCommandBuffers);
	DFN (vkBeginCommandBuffer);
	DFN (vkEndCommandBuffer);
	DFN (vkCmdFillBuffer);
	DFN (vkCmdCopyBuffer);
	DFN (vkCmdPipelineBarrier);
	DFN (vkCreateSemaphore);
	DFN (vkQueueSubmit);
	DFN (vkWaitSemaphores);
	DFN (vkDeviceWaitIdle);
	DFN (vkDestroyDevice);
	VkQueue q;
	vkGetDeviceQueue (dev, qfi, 0, &q);

	/* ---- deux tampons visibles du processeur, 1 Mo */
	const VkDeviceSize sz = 1 << 20;
	VkBuffer buf[2];
	VkDeviceMemory mem[2];
	uint32_t *map[2];
	for (int i = 0; i < 2; i++) {
		VkBufferCreateInfo bci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, NULL, 0, sz,
					   VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
					   VK_SHARING_MODE_EXCLUSIVE, 0, NULL };
		check ("vkCreateBuffer", vkCreateBuffer (dev, &bci, NULL, &buf[i]));
		VkMemoryRequirements mr;
		vkGetBufferMemoryRequirements (dev, buf[i], &mr);
		uint32_t ti = UINT32_MAX;
		for (uint32_t k = 0; k < mp.memoryTypeCount; k++)
			if ((mr.memoryTypeBits & (1u << k)) &&
			    (mp.memoryTypes[k].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
								VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) ==
				    (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
				ti = k;
				break;
			}
		VkMemoryAllocateInfo mai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, NULL, mr.size, ti };
		check ("vkAllocateMemory", vkAllocateMemory (dev, &mai, NULL, &mem[i]));
		check ("vkBindBufferMemory", vkBindBufferMemory (dev, buf[i], mem[i], 0));
		check ("vkMapMemory", vkMapMemory (dev, mem[i], 0, sz, 0, (void **) &map[i]));
		if (fails)
			return 1;
		memset (map[i], 0x5A, sz);
	}
	printf ("[ok] deux tampons de 1 Mo, vus du processeur\n");

	/* ---- le lot : remplir A, copier sa seconde moitie dans B, signaler la barriere chronologique a 1 */
	VkCommandPoolCreateInfo pci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, NULL, 0, qfi };
	VkCommandPool pool;
	check ("vkCreateCommandPool", vkCreateCommandPool (dev, &pci, NULL, &pool));
	VkCommandBufferAllocateInfo cai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO, NULL, pool,
					    VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1 };
	VkCommandBuffer cb;
	check ("vkAllocateCommandBuffers", vkAllocateCommandBuffers (dev, &cai, &cb));
	VkCommandBufferBeginInfo cbi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO, NULL,
					 VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, NULL };
	vkBeginCommandBuffer (cb, &cbi);
	vkCmdFillBuffer (cb, buf[0], 0, sz, 0xC0FFEE42u);
	VkMemoryBarrier mb = { VK_STRUCTURE_TYPE_MEMORY_BARRIER, NULL, VK_ACCESS_TRANSFER_WRITE_BIT,
			       VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_HOST_READ_BIT };
	vkCmdPipelineBarrier (cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &mb, 0, NULL, 0,
			      NULL);
	VkBufferCopy cp = { sz / 2, 0, sz / 2 };
	vkCmdCopyBuffer (cb, buf[0], buf[1], 1, &cp);
	vkCmdPipelineBarrier (cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb, 0, NULL, 0,
			      NULL);
	check ("vkEndCommandBuffer", vkEndCommandBuffer (cb));
	VkSemaphoreTypeCreateInfo sti = { VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO, NULL, VK_SEMAPHORE_TYPE_TIMELINE, 0 };
	VkSemaphoreCreateInfo sci = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, &sti, 0 };
	VkSemaphore sem;
	check ("vkCreateSemaphore (chronologique)", vkCreateSemaphore (dev, &sci, NULL, &sem));
	uint64_t one = 1;
	VkTimelineSemaphoreSubmitInfo tsi = { VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO, NULL, 0, NULL, 1, &one };
	VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO, &tsi, 0, NULL, NULL, 1, &cb, 1, &sem };
	if (fails)
		return 1;
	t0 = now_ms ();
	check ("vkQueueSubmit", vkQueueSubmit (q, 1, &si, VK_NULL_HANDLE));
	VkSemaphoreWaitInfo swi = { VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO, NULL, 0, 1, &sem, &one };
	r = vkWaitSemaphores (dev, &swi, 5000000000ull);
	double tl = now_ms () - t0;
	if (r == VK_TIMEOUT)
		printf ("[ÉCHEC] la barrière chronologique n'arrive pas en 5 s (le lot n'a pas fini)\n"), fails++;
	else
		check ("vkWaitSemaphores", r);
	if (!fails)
		printf ("[ok] lot soumis et fini en %.2f ms (barrière chronologique attendue)\n", tl);

	/* ---- verification */
	if (dry)
		printf ("[--] à blanc : rien n'est parti, pas de vérification des tampons\n");
	else if (!fails) {
		long badA = 0, badB = 0;
		for (size_t i = 0; i < sz / 4; i++)
			badA += map[0][i] != 0xC0FFEE42u;
		for (size_t i = 0; i < sz / 8; i++)
			badB += map[1][i] != 0xC0FFEE42u;
		for (size_t i = sz / 8; i < sz / 4; i++)
			badB += map[1][i] != 0x5A5A5A5Au;
		if (badA || badB)
			printf ("[ÉCHEC] tampons : %ld mots faux dans A (rempli), %ld dans B (copie) ; A[0] %08x B[0] %08x\n",
				badA, badB, map[0][0], map[1][0]), fails++;
		else
			printf ("[ok] tampons vérifiés : A rempli par le GPU (1 Mo), sa moitié copiée dans B, le reste de B "
				"intact\n");
	}
#ifdef VKTEST_KIT
	if (win && !fails)
		fails += run_window (gpu, dev, q, qfi, &mp, gdpa);
#endif
	vkDeviceWaitIdle (dev);
	vkDestroyDevice (dev, NULL);
	vkDestroyInstance (inst, NULL);
	printf ("vktest : %s\n", fails ? "ÉCHEC (voir plus haut ; journal : lignes korin_i915, intelgpu)" : "RÉUSSI");
	return fails ? 1 : 0;
}
