/* simu.c : un faux GPU Gen12 en mémoire (options « simu », « simu-hang »),
 * pour passer par tous les chemins de la sonde et du service sous QEMU, qui
 * n'émule aucun GPU Intel. Ce n'est PAS un modèle du matériel : il ne vérifie
 * que ce que le service écrit et ce que le chemin d'iris exige :
 *
 *   - écritures masquées, accusés du forcewake, remise à zéro ;
 *   - la file d'execlists : l'image du contexte (ses MI_LOAD_REGISTER_IMM),
 *     l'anneau de HEAD à TAIL par les adresses de la GGTT, puis l'image
 *     « sauvée » (HEAD) et deux entrées du CSB (vers le contexte, vers le
 *     repos), comme le ZBook l'a montré à l'étape 1 ;
 *   - les lots en PPGTT : les tables à 4 niveaux du contexte (PDP0) parcourues
 *     pour chaque adresse ; les commandes décodées par leur longueur, lots
 *     chaînés et de second niveau suivis ; MI_STORE_DATA_IMM, PIPE_CONTROL
 *     (écriture après), MI_STORE_REGISTER_MEM, MI_COPY_MEM_MEM exécutés ; les
 *     commandes 3D comptées et sautées (pas de dessin : les pixels restent ce
 *     qu'ils étaient). Une adresse hors des tables : « faute », lot arrêté.
 * Rien de réel n'est touché, sauf la mémoire du service (DMA, tampons). */
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

/* ---- la mémoire que le faux GPU atteint : celle du service */

#define ZONES 256
static struct {
	u64 phys, bytes;
	u8 *va;
} zones[ZONES];

void simu_note(u64 phys, void *va, u64 bytes)
{
	for (int i = 0; i < ZONES; i++)
		if (!zones[i].va) {
			zones[i].phys = phys;
			zones[i].bytes = bytes;
			zones[i].va = va;
			return;
		}
	glog("simu : plus de place pour les zones\n");
}

void simu_forget(void *va)
{
	for (int i = 0; i < ZONES; i++)
		if (zones[i].va == va)
			zones[i].va = 0;
}

void *simu_phys(u64 phys)
{
	void *p = submit_phys_to_va(phys);
	if (p)
		return p;
	for (int i = 0; i < ZONES; i++)
		if (zones[i].va && phys >= zones[i].phys && phys < zones[i].phys + zones[i].bytes)
			return zones[i].va + (phys - zones[i].phys);
	return 0;
}

static void *gva(u32 ga)
{
	u64 pte = gtt[ga >> 12];
	if (!(pte & 1)) {
		glog("simu : faute de la GGTT à %x\n", ga);
		return 0;
	}
	u8 *p = simu_phys(pte & ~0xFFFull);
	if (!p) {
		glog("simu : %x -> %lx, hors de la mémoire du service\n", ga, (unsigned long)(pte & ~0xFFFull));
		return 0;
	}
	return p + (ga & 0xFFF);
}

/* ---- l'exécution : contexte courant */

static u64 cur_pml4;
static u64 n_cmds, n_3d, n_writes, n_faults, n_batches;

static void *ppva(u64 a)
{
	u64 ph = vm_translate(cur_pml4, a);
	if (ph == ~0ull) {
		n_faults++;
		glog("simu : FAUTE de la PPGTT à %lx (pas dans les tables du contexte)\n", (unsigned long)a);
		return 0;
	}
	if (ph >> 12 == scratch_phys() >> 12) {
		n_faults++;
		glog("simu : %lx tombe dans la page de garde (BO pas lié)\n", (unsigned long)a);
		return 0;
	}
	void *p = simu_phys(ph);
	if (!p) {
		n_faults++;
		glog("simu : %lx -> %lx, hors de la mémoire connue\n", (unsigned long)a, (unsigned long)ph);
	}
	return p;
}

static void *addr_of(u64 a, int ggtt)
{
	return ggtt ? gva((u32)a) : ppva(a & GPU_VA_MASK);
}

/* longueur d'une commande en mots */
static u32 cmd_len(u32 dw)
{
	u32 type = dw >> 29;
	if (type == 0) { /* MI */
		u32 op = (dw >> 23) & 0x3F;
		if (op < 0x10)
			return 1;
		return (dw & 0xFF) + 2;
	}
	if (type == 3) {
		u32 sub = (dw >> 27) & 3, op = (dw >> 24) & 7, so = (dw >> 16) & 0xFF;
		if (sub == 1 && op == 1 && so == 4) /* PIPELINE_SELECT */
			return 1;
		if (sub == 1 && op == 0 && so == 0x0B) /* 3DSTATE_VF_STATISTICS */
			return 1;
		return (dw & 0xFF) + 2;
	}
	return (dw & 0xFF) + 2; /* (copieur : 2 ; jamais ici) */
}

static int exec_batch(u64 a, int ggtt, int depth);

/* des commandes ; ring : l'anneau (fin à n mots), sinon un lot (fin à MI_BATCH_BUFFER_END) -> 0, < 0 erreur */
static int exec_cmds(u32 *c, u32 n, int ring, int ggtt_batch, int depth)
{
	for (u32 i = 0; i < n;) {
		u32 dw = c[i], op = dw >> 23, len = cmd_len(dw);
		n_cmds++;
		if (dw == MI_NOOP) {
			i++;
			continue;
		}
		if ((dw & ~1u) == MI_BB_END)
			return ring ? -1 : 1;
		if (dw >> 29 == 0) {
			switch (op) {
			case 0x31: { /* MI_BATCH_BUFFER_START */
				u64 t = c[i + 1] | (u64)c[i + 2] << 32;
				int g = !(dw & MI_BB_PPGTT);
				if (!ring && g && !ggtt_batch) {
					glog("simu : saut en GGTT depuis un lot non privilégié : refusé\n");
					return -1;
				}
				if (ring || (dw & (1u << 22))) { /* depuis l'anneau, ou second niveau : revient */
					if (exec_batch(t, g, depth + 1) < 0)
						return -1;
					i += len;
					continue;
				}
				return exec_batch(t, g, depth + 1); /* chaîné : ne revient pas */
			}
			case 0x20: { /* MI_STORE_DATA_IMM */
				u64 t = c[i + 1] | (u64)c[i + 2] << 32;
				u32 *d = addr_of(t, (dw & MI_USE_GGTT) != 0);
				if (!d)
					return -1;
				d[0] = c[i + 3];
				if (len >= 5 && (dw & (1u << 21)))
					d[1] = c[i + 4];
				n_writes++;
				break;
			}
			case 0x24: { /* MI_STORE_REGISTER_MEM : 0 (pas de registres simulés) */
				u64 t = c[i + 2] | (u64)c[i + 3] << 32;
				u32 *d = addr_of(t, (dw & MI_USE_GGTT) != 0);
				if (!d)
					return -1;
				d[0] = c[i + 1] == RCS + R_TIMESTAMP ? (u32)(now_ms() * 19200) : 0;
				n_writes++;
				break;
			}
			case 0x2E: { /* MI_COPY_MEM_MEM */
				u64 dst = c[i + 1] | (u64)c[i + 2] << 32, src = c[i + 3] | (u64)c[i + 4] << 32;
				int g = (dw & MI_USE_GGTT) != 0;
				u32 *d = addr_of(dst, g), *s = addr_of(src, g);
				if (!d || !s)
					return -1;
				d[0] = s[0];
				n_writes++;
				break;
			}
			}
		} else if (dw >> 29 == 3) {
			n_3d++;
			if ((dw & 0xFFFF0000u) == (PIPE_CONTROL & 0xFFFF0000u)) {
				u32 f = c[i + 1];
				if (((f >> 14) & 3) == 1) {
					u64 t = (c[i + 2] | (u64)c[i + 3] << 32) & ~3ull;
					u32 *d = addr_of(t, (f & PC_GGTT) != 0);
					if (!d)
						return -1;
					d[0] = c[i + 4];
					d[1] = c[i + 5];
					n_writes++;
				}
			}
		}
		i += len;
	}
	return ring ? 0 : -1;
}

static int exec_batch(u64 a, int ggtt, int depth)
{
	if (depth > 16) {
		glog("simu : trop de lots imbriqués\n");
		return -1;
	}
	n_batches++;
	/* page par page : un lot peut s'étendre sur plusieurs pages (non contiguës en mémoire) */
	static u32 buf[64 * 1024];
	u32 got = 0;
	for (u64 p = a; got < sizeof buf / 4; p = (p | 0xFFF) + 1) {
		u32 *s = addr_of(p, ggtt);
		if (!s)
			return got ? (int)exec_cmds(buf, got, 0, ggtt, depth) : -1;
		u32 k = (u32)((((p | 0xFFF) + 1) - p) / 4);
		if (got + k > sizeof buf / 4)
			k = sizeof buf / 4 - got;
		memcpy(buf + got, s, k * 4);
		got += k;
		/* assez lu si un MI_BATCH_BUFFER_END est déjà atteint en suivant les longueurs */
		u32 i = 0;
		int end = 0;
		while (i < got) {
			u32 dw = buf[i];
			if ((dw & ~1u) == MI_BB_END || ((dw >> 23) == 0x31 && dw >> 29 == 0 && !(dw & (1u << 22)))) {
				end = i + 3 <= got;
				break;
			}
			i += cmd_len(dw);
		}
		if (end)
			break;
	}
	int r = exec_cmds(buf, got, 0, ggtt, depth);
	return r < 0 ? -1 : 0;
}

static void run(void)
{
	u32 lo = *reg(RCS + R_EL_SQ), hi = *reg(RCS + R_EL_SQ + 4);
	if (!(lo & CTX_VALID) || !(lo & CTX_PRIVILEGE)) {
		glog("simu : descripteur %08x refusé\n", lo);
		return;
	}
	u32 *s = gva((lo & ~0xFFFu) + 4096);
	if (!s)
		return;
	/* l'image : des MI_LOAD_REGISTER_IMM jusqu'à MI_BATCH_BUFFER_END */
	u32 pdp0[2] = {0, 0};
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
		for (u32 k = 0; k < n; k++) {
			u32 r = s[i + 1 + 2 * k], v = s[i + 2 + 2 * k];
			if (r == RCS + 0x270)
				pdp0[0] = v;
			else if (r == RCS + 0x274)
				pdp0[1] = v;
			else if (r == RCS + R_CTX_CTL) {
				u32 m = v >> 16, old = *reg(r);
				*reg(r) = (old & ~m) | (v & m & 0xFFFF);
			} else
				*reg(r) = v;
		}
		i += 1 + 2 * n;
	}
	cur_pml4 = pdp0[0] | (u64)pdp0[1] << 32;
	u32 head = *reg(RCS + R_HEAD), tail = *reg(RCS + R_TAIL);
	u32 size = (*reg(RCS + R_CTL) & 0x1FF000) + 4096;
	u32 *ring = gva(*reg(RCS + R_START));
	if (!ring || !(*reg(RCS + R_CTL) & 1) || tail >= size || head >= size) {
		glog("simu : anneau illisible (START %x, CTL %x, HEAD %x, TAIL %x)\n", *reg(RCS + R_START),
		     *reg(RCS + R_CTL), head, tail);
		return;
	}
	u64 c0 = n_cmds, w0 = n_writes, d0 = n_3d, b0 = n_batches, f0 = n_faults;
	/* l'anneau peut faire le tour : en deux morceaux */
	if (tail < head) {
		if (exec_cmds(ring + head / 4, (size - head) / 4, 1, 1, 0) || exec_cmds(ring, tail / 4, 1, 1, 0))
			return;
	} else if (exec_cmds(ring + head / 4, (tail - head) / 4, 1, 1, 0))
		return;
	gdbg("simu : contexte %u, anneau %x..%x : %lu commandes (%lu 3D), %lu lots, %lu écritures, %lu fautes\n",
	     (hi >> 5) & 0x7FF, head, tail, (unsigned long)(n_cmds - c0), (unsigned long)(n_3d - d0),
	     (unsigned long)(n_batches - b0), (unsigned long)(n_writes - w0), (unsigned long)(n_faults - f0));
	*reg(RCS + R_HEAD) = tail;
	s[5] = tail; /* l'image « sauvée » : HEAD */
	u32 *hw = gva(*reg(RCS + R_HWS_PGA));
	if (hw) {
		/* CSB : vers le contexte, puis vers le repos (format de Gen12 vu sur le ZBook) */
		u32 id = (hi >> 5) & 0x7FF, wp = hw[0x2F] & 0xF;
		for (int k = 0; k < 2; k++) {
			wp = wp >= CSB_ENTRIES - 1 ? 0 : wp + 1;
			hw[0x10 + 2 * wp] = k ? (CSB_IDLE << 15) : (id << 15) | 1;
			hw[0x11 + 2 * wp] = k ? (id << 15) : (CSB_IDLE << 15);
		}
		hw[0x2F] = wp;
	}
}

void simu_write(u32 off, u32 v)
{
	switch (off) {
	case FW_GT:
	case FW_RENDER:
	case RCS + R_MI_MODE:
	case RCS + R_MODE:
	case RCS + R_RESET_CTL: {
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
