/* simu.c : un faux GPU Gen12 en mémoire (options « simu », « simu-hang »),
 * pour passer par tous les chemins de la sonde sous QEMU, qui n'émule aucun
 * GPU Intel. Ce n'est PAS un modèle du matériel : il ne vérifie que ce que la
 * sonde écrit (écritures masquées, accusés du forcewake, image du contexte,
 * commandes de l'anneau et du lot, adresses de la GGTT). Rien de réel n'est
 * touché, sauf la zone DMA de submit.c (la nôtre). */
#include "igpu.h"

int simu_hang;
static volatile u8 *bar;
static volatile u64 *gtt;


static u32 *reg(u32 off)
{
	return (u32 *)(bar + off);
}

volatile u8 *simu_bar0(void)
{
	bar = sys_vm_map(BAR0_SIZE);
	if (!bar)
		return 0;
	gtt = (volatile u64 *)(bar + GGTT_OFF);
	*reg(SLICE_ENABLE) = 1;
	*reg(DSS_ENABLE) = 3;
	*reg(EU_DISABLE) = 0;
	*reg(RPM_CONFIG0) = (1u << 3) | (3u << 1); /* 19,2 MHz, décalage 3 */
	*reg(PAT_INDEX(0)) = 0;
	*reg(PAT_INDEX(1)) = 1;
	*reg(PAT_INDEX(2)) = 2;
	*reg(PAT_INDEX(3)) = 3;
	*reg(PLANE_CTL_1_A) = 0x84000000;
	*reg(PLANE_SURF_1_A) = 0;
	*reg(RCS + R_MI_MODE) = MI_MODE_STOP | MI_MODE_IDLE;
	/* le framebuffer du GOP (1920 x 1080 x 4) au début, une page de garde partout ailleurs */
	u64 n = (8ull << 20) / 8, fb = (1920 * 1080 * 4 + 4095) / 4096;
	for (u64 i = 0; i < n; i++)
		gtt[i] = i < fb ? (0x7C000000ull + i * 4096) | 1 : 0x7FFFF000ull | 1;
	return bar;
}

static void *gva(u32 ga)
{
	u64 pte = gtt[ga >> 12];
	if (!(pte & 1)) {
		glog("simu : faute de la GGTT à %x\n", ga);
		return 0;
	}
	u8 *p = submit_phys_to_va(pte & ~0xFFFull);
	if (!p) {
		glog("simu : %x -> %lx, hors de la zone de la sonde\n", ga, (unsigned long)(pte & ~0xFFFull));
		return 0;
	}
	return p + (ga & 0xFFF);
}

/* des commandes : 0 fini (fin du lot ou de l'anneau), < 0 erreur */
static int exec_cmds(u32 *c, u32 ndw, int batch)
{
	for (u32 i = 0; i < ndw;) {
		u32 dw = c[i], op = dw >> 23;
		if (dw == MI_NOOP) {
			i++;
		} else if ((dw & ~1u) == MI_BB_END) {
			return batch ? 0 : -1;
		} else if (op == 0x31) {
			if (dw & (1u << 8)) {
				glog("simu : lot en PPGTT, non suivi\n");
				return -1;
			}
			u32 *b = gva(c[i + 1]);
			if (!b || exec_cmds(b, 1024, 1))
				return -1;
			i += (dw & 0xFF) + 2;
		} else if (op == 0x20) {
			if (!(dw & MI_USE_GGTT)) {
				glog("simu : MI_STORE_DATA_IMM sans GGTT\n");
				return -1;
			}
			u32 *d = gva(c[i + 1]);
			if (!d)
				return -1;
			*d = c[i + 3];
			i += (dw & 0x3FF) + 2;
		} else {
			glog("simu : commande inconnue %08x\n", dw);
			return -1;
		}
	}
	return batch ? -1 : 0;
}

static void run(void)
{
	u32 lo = *reg(RCS + R_EL_SQ);
	if (!(lo & CTX_VALID) || !(lo & CTX_PRIVILEGE)) {
		glog("simu : descripteur %08x refusé\n", lo);
		return;
	}
	u32 *s = gva((lo & ~0xFFFu) + 4096);
	if (!s)
		return;
	/* l'image : des MI_LOAD_REGISTER_IMM jusqu'à MI_BATCH_BUFFER_END */
	for (u32 i = 0; i < 1024;) {
		u32 dw = s[i];
		if (dw == MI_NOOP) {
			i++;
			continue;
		}
		if ((dw & ~1u) == MI_BB_END)
			break;
		if ((dw >> 23) != 0x22) {
			glog("simu : image du contexte : %08x au mot %u\n", dw, i);
			return;
		}
		u32 n = ((dw & 0xFF) + 1) / 2;
		for (u32 k = 0; k < n; k++)
			*reg(s[i + 1 + 2 * k]) = s[i + 2 + 2 * k];
		i += 1 + 2 * n;
	}
	u32 head = *reg(RCS + R_HEAD), tail = *reg(RCS + R_TAIL);
	u32 *ring = gva(*reg(RCS + R_START));
	if (!ring || !(*reg(RCS + R_CTL) & 1) || tail <= head) {
		glog("simu : anneau illisible (START %x, CTL %x, HEAD %x, TAIL %x)\n", *reg(RCS + R_START),
		     *reg(RCS + R_CTL), head, tail);
		return;
	}
	if (exec_cmds(ring + head / 4, (tail - head) / 4, 0))
		return;
	*reg(RCS + R_HEAD) = tail;
	u32 *hw = gva(*reg(RCS + R_HWS_PGA));
	if (hw) {
		hw[0x10] = 1; /* une entrée de CSB, pour le journal */
		hw[0x2F] = 0;
	}
}

void simu_write(u32 off, u32 v)
{
	switch (off) {
	case FW_GT:
	case FW_RENDER:
	case RCS + R_MI_MODE:
	case RCS + R_MODE:
	case RCS + R_RESET_CTL:
	case RCS + R_CTX_CTL: {
		u32 m = v >> 16, old = *reg(off);
		v = (old & ~m) | (v & m & 0xFFFF);
		break;
	}
	}
	*reg(off) = v;
	switch (off) {
	case FW_GT:
		*reg(FW_GT_ACK) = v & 1;
		break;
	case FW_RENDER:
		*reg(FW_RENDER_ACK) = v & 1;
		break;
	case RCS + R_MI_MODE:
		if ((v & MI_MODE_STOP) && !simu_hang)
			*reg(off) = v | MI_MODE_IDLE;
		else
			*reg(off) = v & ~MI_MODE_IDLE;
		break;
	case RCS + R_RESET_CTL:
		*reg(off) = (v & 1) ? v | 2 : v & ~2u;
		break;
	case GDRST:
		*reg(off) = 0;
		simu_hang = 0; /* la remise à zéro débloque le faux moteur */
		break;
	case GFX_FLSH_CNTL:
		*reg(off) = 0;
		break;
	case RCS + R_EL_CTRL:
		if ((v & 1) && !simu_hang)
			run();
		break;
	}
}
