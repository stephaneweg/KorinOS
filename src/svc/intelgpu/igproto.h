/* igproto.h : le protocole du service « intelgpu » (GPU Intel du PC), partagé
 * par le service (src/svc/intelgpu) et son client, le faux libdrm d'iris
 * (tools/mesa/libdrm-pc/korin_i915.c). docs/etude/GPU-INTEL.md § 3, et
 * PC-PILOTES.md « GPU Intel, étape 2 ».
 *
 * Message de 512 o (SYS_SVC_CALL) : +0 op, +8 a, +16 b, +24 c, +32 d,
 * +40 longueur des données, +44 pid de l'appelant (noyau), +48 données (464 o).
 * Le résultat de SVC_CALL : 0 ou une erreur E_* (négative) ; les valeurs
 * rendues sont dans a, b, c, d du message.
 *
 * Les BO du client sont des tranches de ses « morceaux » : des tampons
 * partagés (BUF_CREATE, contigus) qu'il donne au service (BUF_GRANT au pid
 * rendu par IG_OP_INFO, puis IG_OP_CHUNK). Le service ne voit jamais d'autre
 * mémoire du client. Les adresses GPU sont choisies par iris (softpin) :
 * IG_OP_BIND écrit les PTE de la PPGTT du client.
 *
 * L'avancement : une page d'état partagée (tampon du service, ouvert à tous,
 * BUF_MAP), écrite par le GPU : IGS_DONE = dernier numéro de lot fini. Un
 * client y lit sans appel si un lot est fini ; IG_OP_WAIT pour attendre. */
#ifndef IGPROTO_H
#define IGPROTO_H

#define IG_SVC_NAME "intelgpu"
#define IG_VERSION 1

#define IG_OP_INFO 1        /* -> données : struct ig_info */
#define IG_OP_CHUNK 2       /* a = numéro du tampon (BUF_GRANT au service fait) -> 0 */
#define IG_OP_CHUNK_FREE 3  /* a = numéro : le service le démappe (aucun lot ne doit s'en servir) */
#define IG_OP_VM_CREATE 4   /* -> a = espace d'adresses (PPGTT à 4 niveaux) */
#define IG_OP_VM_DESTROY 5  /* a = espace */
#define IG_OP_CTX_CREATE 6  /* a = espace -> a = contexte (image LRC complète, copie du contexte doré) */
#define IG_OP_CTX_DESTROY 7 /* a = contexte (attend la fin de ses lots) */
#define IG_OP_BIND 8        /* a = espace, b = n (IG_BIND_MAX au plus) ; données : n x struct ig_bind */
#define IG_OP_UNBIND 9      /* a = espace, b = n ; données : n x struct ig_bind (gpu, bytes) */
#define IG_OP_EXEC 10       /* a = contexte, b = adresse GPU du lot, c = octets, d = IG_EXEC_* -> a = numéro */
#define IG_OP_WAIT 11       /* a = numéro, b = délai ms (20000 au plus) -> 0, E_TIMEDOUT ; c rendu : 1 si le
			     * lot a été perdu par une remise à zéro */
#define IG_OP_TIMESTAMP 12  /* -> a = horodatage du rendu (64 bits, RCS 0x2358) */
#define IG_OP_RESET_STATS 13 /* a = contexte -> a = remises à zéro en tout, b = lots de ce contexte perdus */
#define IG_OP_STOP 14       /* (intelgpu stop) : arrêt propre, tout remis comme au départ */
#define IG_OP_STATUS 15     /* -> données : texte (intelgpu status) */

#define IG_EXEC_PRIV 1      /* (service seulement) */

#define IG_BIND_MAX 14

/* données de IG_OP_BIND : la tranche [off, off + bytes[ du tampon buf vue à l'adresse gpu */
struct ig_bind {
	unsigned long long gpu;
	unsigned long long bytes;
	unsigned int buf, pad;
	unsigned long long off;
};

/* IG_OP_INFO */
struct ig_info {
	unsigned int version;
	unsigned int state;      /* 1 : prêt ; 2 : simu (faux GPU) ; 0 : GPU perdu */
	unsigned int pid;        /* du service : BUF_GRANT des morceaux */
	unsigned int status_buf; /* numéro du tampon de la page d'état (BUF_MAP) */
	unsigned int device, revision;
	unsigned int eu_total, slice_mask, dss_mask, eu_per_dss;
	unsigned int ts_hz;      /* horodatage du rendu */
	unsigned int resets;
	unsigned long long gtt_size; /* espace d'une PPGTT : 2^48 */
};

/* la page d'état partagée (mots de 32 bits) */
#define IGS_DONE 0      /* dernier lot fini (écrit par le GPU) */
#define IGS_SUBMITTED 1 /* dernier lot donné (écrit par le service) */
#define IGS_LOST 2      /* dernier lot perdu par une remise à zéro (service) */
#define IGS_RESETS 3    /* remises à zéro */
#define IGS_SCRATCH 16  /* écritures sans valeur des PIPE_CONTROL (service) */

#endif
