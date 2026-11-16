/*
 * vktest -- premier essai de Vulkan sur Korin PC (anv, le pilote Vulkan d'Intel de Mesa ; docs/etude/VULKAN.md) :
 * instance, le GPU et ses capacites, un peripherique, puis un lot : remplir un tampon (vkCmdFillBuffer, que anv fait
 * par un shader interne : le compilateur tourne), en copier une partie (vkCmdCopyBuffer), relire et verifier ; une
 * barriere chronologique attendue par le processeur. Sans affichage. Relie en statique a anv (pas de chargeur) :
 * vk_icdGetInstanceProcAddr directement. tools/mesa/build-anv.sh.
 *
 *     vktest            sur le GPU (le service intelgpu doit tourner)
 *     vktest --dry      a blanc (KORIN_VK_DRY : lots comptes, pas lances ; pas de verification)
 *     vktest -v         plus de capacites (toutes les extensions)
 *     vktest -d         les messages de Mesa et du faux libdrm a l'ecran (KORIN_DRM_DEBUG)
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

int main (int argc, char **argv)
{
	int dry = 0, verbose = 0;
	for (int i = 1; i < argc; i++) {
		if (!strcmp (argv[i], "--dry"))
			dry = 1;
		else if (!strcmp (argv[i], "-v"))
			verbose = 1;
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
	if (getenv ("KORIN_DRM_DEBUG")) {
		/* (diagnostic : ce qu'anv fait avant de garder le GPU, a la main) */
		extern int korin_anv_open (void);
		extern _Bool intel_get_device_info_from_fd (int fd, void *devinfo, int min_ver, int max_ver);
		static char devinfo[1 << 16];
		int fd = korin_anv_open ();
		int ok = fd >= 0 && intel_get_device_info_from_fd (fd, devinfo, 9, -1);
		printf ("diagnostic : descripteur %d, identification de la puce %s\n", fd, ok ? "bonne" : "RATÉE");
	}
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
	if (getenv ("KORIN_DRM_DEBUG")) {
		ici.pNext = &dmi;
		ici.enabledExtensionCount = 1;
		ici.ppEnabledExtensionNames = dbg_ext;
	}
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
	vkDeviceWaitIdle (dev);
	vkDestroyDevice (dev, NULL);
	vkDestroyInstance (inst, NULL);
	printf ("vktest : %s\n", fails ? "ÉCHEC (voir plus haut ; journal : lignes korin_i915, intelgpu)" : "RÉUSSI");
	return fails ? 1 : 0;
}
