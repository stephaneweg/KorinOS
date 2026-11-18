/* acpi.c - ACPICA dans acpid : chargement des tables, contrôleur embarqué (EC), batteries et secteur.
 *
 * Démarrage (acpi_start) : tables, espace de noms, objets ; sans changer de mode (ni ACPI_ENABLE, ni SCI,
 * ni GPE : rien n'est servi par interruption, la batterie se lit en sondant). Puis les méthodes
 * d'initialisation : par défaut seulement \_SB._INI et les _INI de l'EC, des batteries et des adaptateurs
 * (les autres périphériques ont leurs pilotes Korin, qui ne s'attendent pas à un _INI tardif) ; « init =
 * all » dans /etc/acpid.ini : tous les _STA/_INI et _REG, comme Linux. Enfin le gestionnaire de l'EC,
 * installé sur le périphérique PNP0C09 : son installation lance le _REG de l'EC (sans lui, les méthodes de
 * la batterie de bien des PC, dont le ZBook, ne lisent pas l'EC).
 *
 * EC (ACPI 12.2) : ports de données et de commande du _CRS de l'EC (ou de l'ECDT), 0x62 / 0x66 sinon ;
 * lecture 0x80, écriture 0x81 ; IBF (bit 1) attendu à 0 avant chaque octet écrit, OBF (bit 0) à 1 avant
 * de lire. Pas de mode rafale, pas de requêtes (_Qxx : il faudrait le SCI).
 *
 * Batteries (ACPI 10.2) : PNP0C0A, _STA bit 4 (présente), _BIX (sinon _BIF) relu à chaque changement de
 * présence et toutes les minutes, _BST à chaque lecture ; mA et mAh convertis en mW et mWh par la tension.
 * Secteur : ACPI0003, _PSR. */
#include "korin_kit.h"

#include "acpi.h"
#include "accommon.h"

#include "acpid.h"

/* (utprint.c d'ACPICA, renommés par ackorin.h) */
int AcpiKvsnprintf(char *s, ACPI_SIZE n, const char *fmt, va_list ap);
int AcpiKsnprintf(char *s, ACPI_SIZE n, const char *fmt, ...);

static void elog(const char *fmt, ...)
{
	char b[200];
	va_list ap;
	va_start(ap, fmt);
	AcpiKvsnprintf(b, sizeof b, fmt, ap);
	va_end(ap);
	acpid_log(b);
}

/* ---- l'EC */
static UINT16 ec_data = 0x62, ec_cmd = 0x66;
static ACPI_HANDLE ec_dev;
static int ec_errors;

#define EC_OBF 0x01
#define EC_IBF 0x02

static UINT8 inb(UINT16 p)
{
	long r = korin_sys3(SYS_IO_PORT, p, 0, 0);
	return r < 0 ? 0xFF : (UINT8)r;
}

static void outb(UINT16 p, UINT8 v)
{
	korin_sys3(SYS_IO_PORT, p, 1, v);
}

/* attendre que (état & mask) == want -> 0, -1 après 100 ms */
static int ec_wait(UINT8 mask, UINT8 want)
{
	UINT64 t0 = AcpiOsGetTimer();
	for (unsigned i = 0;; i++) {
		if ((inb(ec_cmd) & mask) == want)
			return 0;
		if (i > 64) {
			if (AcpiOsGetTimer() - t0 > 100 * 10000ULL)
				return -1;
			if (i > 1000)
				AcpiOsSleep(1);
		}
	}
}

static int ec_read(UINT8 addr, UINT8 *v)
{
	if (inb(ec_cmd) & EC_OBF)
		(void)inb(ec_data); /* un octet resté là (un événement) */
	if (ec_wait(EC_IBF, 0))
		return -1;
	outb(ec_cmd, 0x80);
	if (ec_wait(EC_IBF, 0))
		return -1;
	outb(ec_data, addr);
	if (ec_wait(EC_OBF, EC_OBF))
		return -1;
	*v = inb(ec_data);
	return 0;
}

static int ec_write(UINT8 addr, UINT8 v)
{
	if (ec_wait(EC_IBF, 0))
		return -1;
	outb(ec_cmd, 0x81);
	if (ec_wait(EC_IBF, 0))
		return -1;
	outb(ec_data, addr);
	if (ec_wait(EC_IBF, 0))
		return -1;
	outb(ec_data, v);
	return ec_wait(EC_IBF, 0);
}

static ACPI_STATUS ec_handler(UINT32 fn, ACPI_PHYSICAL_ADDRESS addr, UINT32 width, UINT64 *value, void *hctx,
			      void *rctx)
{
	(void)hctx;
	(void)rctx;
	if (width == 0 || width > 64 || (width & 7) || addr > 0xFF)
		return AE_BAD_PARAMETER;
	unsigned n = width / 8;
	if ((fn & ACPI_IO_MASK) == ACPI_READ)
		*value = 0;
	for (unsigned i = 0; i < n; i++) {
		UINT8 b;
		int r;
		if ((fn & ACPI_IO_MASK) == ACPI_READ) {
			r = ec_read((UINT8)(addr + i), &b);
			if (!r)
				*value |= (UINT64)b << (8 * i);
		} else
			r = ec_write((UINT8)(addr + i), (UINT8)(*value >> (8 * i)));
		if (r) {
			if (ec_errors++ < 8)
				elog("EC : pas de réponse (adresse %x, état %x)", (unsigned)(addr + i), inb(ec_cmd));
			return AE_TIME;
		}
	}
	return AE_OK;
}

static ACPI_STATUS ec_setup(ACPI_HANDLE obj, UINT32 depth, void *ctx, void **ret)
{
	(void)depth;
	(void)ctx;
	(void)ret;
	if (!ec_dev)
		ec_dev = obj;
	return AE_OK;
}

static int crs_ports[2], crs_n;

static ACPI_STATUS crs_port(ACPI_RESOURCE *r, void *ctx)
{
	(void)ctx;
	if (crs_n < 2 && r->Type == ACPI_RESOURCE_TYPE_IO)
		crs_ports[crs_n++] = r->Data.Io.Minimum;
	else if (crs_n < 2 && r->Type == ACPI_RESOURCE_TYPE_FIXED_IO)
		crs_ports[crs_n++] = r->Data.FixedIo.Address;
	return AE_OK;
}

static void ec_find(void)
{
	ACPI_TABLE_HEADER *t;
	if (ACPI_SUCCESS(AcpiGetTable(ACPI_SIG_ECDT, 1, &t))) {
		ACPI_TABLE_ECDT *e = (ACPI_TABLE_ECDT *)t;
		if (e->Control.Address && e->Data.Address) {
			ec_cmd = (UINT16)e->Control.Address;
			ec_data = (UINT16)e->Data.Address;
		}
	}
	AcpiGetDevices("PNP0C09", ec_setup, NULL, NULL);
	if (ec_dev && ACPI_SUCCESS(AcpiWalkResources(ec_dev, METHOD_NAME__CRS, crs_port, NULL)) && crs_n == 2) {
		ec_data = (UINT16)crs_ports[0];
		ec_cmd = (UINT16)crs_ports[1];
	}
}

/* ---- périphériques */
#define MAXDEV 4
static ACPI_HANDLE bats[MAXDEV], acs[MAXDEV];
static int nbat, nac;

static ACPI_STATUS add_bat(ACPI_HANDLE obj, UINT32 d, void *c, void **r)
{
	(void)d, (void)c, (void)r;
	if (nbat < MAXDEV)
		bats[nbat++] = obj;
	return AE_OK;
}

static ACPI_STATUS add_ac(ACPI_HANDLE obj, UINT32 d, void *c, void **r)
{
	(void)d, (void)c, (void)r;
	if (nac < MAXDEV)
		acs[nac++] = obj;
	return AE_OK;
}

void acpi_devices(int *batteries, int *adapters)
{
	*batteries = nbat;
	*adapters = nac;
}

/* ---- événements (SCI) : Notify, boutons fixes, GPE de l'EC */
static ACPI_HANDLE lids[MAXDEV], pbtns[MAXDEV], sbtns[MAXDEV];
static int nlid, npbtn, nsbtn, fixed_pb, fixed_sb, ec_gpe_on;
static volatile unsigned pending; /* ACPI_EV_* */
static int events_on;

static ACPI_STATUS add_lid(ACPI_HANDLE obj, UINT32 d, void *c, void **r)
{
	(void)d, (void)c, (void)r;
	if (nlid < MAXDEV)
		lids[nlid++] = obj;
	return AE_OK;
}

static ACPI_STATUS add_pbtn(ACPI_HANDLE obj, UINT32 d, void *c, void **r)
{
	(void)d, (void)c, (void)r;
	if (npbtn < MAXDEV)
		pbtns[npbtn++] = obj;
	return AE_OK;
}

static ACPI_STATUS add_sbtn(ACPI_HANDLE obj, UINT32 d, void *c, void **r)
{
	(void)d, (void)c, (void)r;
	if (nsbtn < MAXDEV)
		sbtns[nsbtn++] = obj;
	return AE_OK;
}

static int in_list(ACPI_HANDLE h, ACPI_HANDLE *l, int n)
{
	for (int i = 0; i < n; i++)
		if (l[i] == h)
			return 1;
	return 0;
}

static void notify_handler(ACPI_HANDLE h, UINT32 value, void *ctx)
{
	(void)ctx;
	static int said;
	if (in_list(h, bats, nbat) || in_list(h, acs, nac))
		pending |= ACPI_EV_BATTERY; /* 0x80 état, 0x81 informations : relire tout de suite */
	else if (in_list(h, lids, nlid))
		pending |= ACPI_EV_LID;
	else if (in_list(h, pbtns, npbtn) && value == 0x80)
		pending |= ACPI_EV_POWER;
	else if (in_list(h, sbtns, nsbtn) && value == 0x80)
		pending |= ACPI_EV_SLEEP;
	else if (said < 24) { /* (les autres : au journal, les premiers seulement) */
		char path[64] = "?";
		ACPI_BUFFER pb = {sizeof path, path};
		AcpiGetName(h, ACPI_FULL_PATHNAME, &pb);
		elog("Notify (%s, 0x%x) : non servi", path, value);
		said++;
	}
}

static UINT32 fixed_power(void *ctx)
{
	(void)ctx;
	pending |= ACPI_EV_POWER;
	return ACPI_INTERRUPT_HANDLED;
}

static UINT32 fixed_sleep(void *ctx)
{
	(void)ctx;
	pending |= ACPI_EV_SLEEP;
	return ACPI_INTERRUPT_HANDLED;
}

/* requêtes de l'EC (ACPI 12.3, QR_EC 0x84) tant que SCI_EVT est levé, chacune par sa méthode _Qxx */
#define EC_SCI_EVT 0x20
static int ec_queries;

static void ec_query(void *ctx)
{
	(void)ctx;
	for (int n = 0; n < 32; n++) {
		if (!(inb(ec_cmd) & EC_SCI_EVT))
			break;
		if (ec_wait(EC_IBF, 0))
			break;
		outb(ec_cmd, 0x84);
		if (ec_wait(EC_OBF, EC_OBF))
			break;
		UINT8 q = inb(ec_data);
		if (!q)
			break;
		char m[8];
		AcpiKsnprintf(m, sizeof m, "_Q%02X", q);
		ACPI_HANDLE h;
		ec_queries++;
		if (ACPI_FAILURE(AcpiGetHandle(ec_dev, m, &h))) {
			static int said;
			if (said++ < 16)
				elog("EC : requête %02x sans méthode %s", q, m);
			continue;
		}
		ACPI_STATUS s = AcpiEvaluateObject(ec_dev, m, NULL, NULL);
		if (ACPI_FAILURE(s))
			elog("EC : %s : %s", m, AcpiFormatException(s));
	}
}

static UINT32 ec_gpe(ACPI_HANDLE dev, UINT32 num, void *ctx)
{
	(void)dev, (void)num, (void)ctx;
	AcpiOsExecute(OSL_GPE_HANDLER, ec_query, NULL);
	return ACPI_INTERRUPT_HANDLED | ACPI_REENABLE_GPE;
}

static int eval_int(ACPI_HANDLE h, const char *name, UINT64 *v);

static void events_setup(void)
{
	AcpiGetDevices("PNP0C0D", add_lid, NULL, NULL);
	AcpiGetDevices("PNP0C0C", add_pbtn, NULL, NULL);
	AcpiGetDevices("PNP0C0E", add_sbtn, NULL, NULL);
	ACPI_STATUS s = AcpiInstallNotifyHandler(ACPI_ROOT_OBJECT, ACPI_ALL_NOTIFY, notify_handler, NULL);
	if (ACPI_FAILURE(s))
		elog("Notify : gestionnaire refusé (%s)", AcpiFormatException(s));
	if (!(AcpiGbl_FADT.Flags & ACPI_FADT_POWER_BUTTON) && /* (bit à 0 : bouton fixe) */
	    ACPI_SUCCESS(AcpiInstallFixedEventHandler(ACPI_EVENT_POWER_BUTTON, fixed_power, NULL))) {
		AcpiClearEvent(ACPI_EVENT_POWER_BUTTON);
		fixed_pb = ACPI_SUCCESS(AcpiEnableEvent(ACPI_EVENT_POWER_BUTTON, 0));
	}
	if (!(AcpiGbl_FADT.Flags & ACPI_FADT_SLEEP_BUTTON) &&
	    ACPI_SUCCESS(AcpiInstallFixedEventHandler(ACPI_EVENT_SLEEP_BUTTON, fixed_sleep, NULL))) {
		AcpiClearEvent(ACPI_EVENT_SLEEP_BUTTON);
		fixed_sb = ACPI_SUCCESS(AcpiEnableEvent(ACPI_EVENT_SLEEP_BUTTON, 0));
	}
	UINT64 gpe = ~0ULL;
	if (ec_dev && !eval_int(ec_dev, "_GPE", &gpe) && gpe < 256) {
		s = AcpiInstallGpeHandler(NULL, (UINT32)gpe, ACPI_GPE_EDGE_TRIGGERED, ec_gpe, NULL);
		if (ACPI_SUCCESS(s))
			s = AcpiEnableGpe(NULL, (UINT32)gpe);
		if (ACPI_FAILURE(s)) {
			elog("EC : GPE %x refusée (%s)", (unsigned)gpe, AcpiFormatException(s));
			gpe = ~0ULL;
		} else {
			ec_gpe_on = 1;
			AcpiOsExecute(OSL_GPE_HANDLER, ec_query, NULL); /* (des événements déjà en attente) */
		}
	}
	AcpiUpdateAllGpes(); /* les GPE qui ont une méthode _Lxx / _Exx */
	events_on = 1;
	elog("événements : bouton d'alimentation %s, mise en veille %s, %d couvercle(s), GPE de l'EC %s",
	     fixed_pb ? "fixe" : npbtn ? "PNP0C0C" : "absent", fixed_sb ? "fixe" : nsbtn ? "PNP0C0E" : "absent", nlid,
	     gpe < 256 ? "servie" : "absente");
}

void acpi_features(struct acpi_features *f)
{
	f->events = events_on;
	f->lids = nlid;
	f->power_button = fixed_pb || npbtn;
	f->sleep_button = fixed_sb || nsbtn;
	f->ec = ec_dev != NULL;
	f->ec_gpe = ec_gpe_on;
}

unsigned acpi_take_events(void)
{
	unsigned e = pending;
	pending = 0;
	return e;
}

int acpi_lid(void)
{
	UINT64 v;
	if (!nlid || eval_int(lids[0], "_LID", &v))
		return -1;
	return v != 0;
}

/* trop d'interruptions : les GPE coupées, sauf celle de l'EC (une méthode _Lxx qui ne calme pas sa source) */
void acpi_quiet_gpes(void)
{
	elog("SCI : trop d'interruptions, GPE coupées (sauf l'EC)");
	AcpiDisableAllGpes();
	UINT64 gpe;
	if (ec_dev && !eval_int(ec_dev, "_GPE", &gpe) && gpe < 256)
		AcpiEnableGpe(NULL, (UINT32)gpe);
}

static void run_ini(ACPI_HANDLE dev, const char *what)
{
	ACPI_HANDLE m;
	if (ACPI_FAILURE(AcpiGetHandle(dev, "_INI", &m)))
		return;
	ACPI_STATUS s = AcpiEvaluateObject(dev, "_INI", NULL, NULL);
	if (ACPI_FAILURE(s))
		elog("%s._INI : %s", what, AcpiFormatException(s));
}

int acpi_start(int init_all, int events)
{
	osl_init();
	/* mode souple, comme l'interprète de Windows pour lequel les DSDT sont écrits : une variable locale
	 * lue avant d'être écrite vaut 0 (sur le ZBook, le _REG de l'EC passe par \_SB.PC00.TM03, qui le fait) */
	AcpiGbl_EnableInterpreterSlack = TRUE;
	ACPI_STATUS s = AcpiInitializeSubsystem();
	if (ACPI_SUCCESS(s))
		s = AcpiInitializeTables(NULL, 32, FALSE);
	if (ACPI_SUCCESS(s))
		s = AcpiLoadTables();
	if (ACPI_FAILURE(s)) {
		elog("ACPICA : tables refusées (%s)", AcpiFormatException(s));
		return -1;
	}
	ec_find();
	AcpiGetDevices("PNP0C0A", add_bat, NULL, NULL);
	AcpiGetDevices("ACPI0003", add_ac, NULL, NULL);
	/* events : mode ACPI, SCI, GPE et événements fixes ; sinon (events = 0) le mode n'est pas changé */
	s = AcpiEnableSubsystem(events ? ACPI_FULL_INITIALIZATION
				       : ACPI_NO_ACPI_ENABLE | ACPI_NO_EVENT_INIT | ACPI_NO_HANDLER_INIT);
	if (ACPI_FAILURE(s)) {
		elog("ACPICA : AcpiEnableSubsystem : %s", AcpiFormatException(s));
		return -1;
	}
	if (init_all && ec_dev) /* comme Linux : l'EC servi avant les _REG et _INI */
		AcpiInstallAddressSpaceHandler(ec_dev, ACPI_ADR_SPACE_EC, ec_handler, NULL, NULL);
	s = AcpiInitializeObjects(init_all ? ACPI_FULL_INITIALIZATION : ACPI_NO_DEVICE_INIT);
	if (ACPI_FAILURE(s)) {
		elog("ACPICA : AcpiInitializeObjects : %s", AcpiFormatException(s));
		return -1;
	}
	if (!init_all) {
		ACPI_HANDLE sb;
		if (ACPI_SUCCESS(AcpiGetHandle(NULL, "\\_SB", &sb)))
			run_ini(sb, "\\_SB");
		if (ec_dev) {
			run_ini(ec_dev, "EC");
			s = AcpiInstallAddressSpaceHandler(ec_dev, ACPI_ADR_SPACE_EC, ec_handler, NULL, NULL);
			if (ACPI_FAILURE(s))
				elog("EC : gestionnaire refusé (%s)", AcpiFormatException(s));
		}
		for (int i = 0; i < nbat; i++)
			run_ini(bats[i], "batterie");
		for (int i = 0; i < nac; i++)
			run_ini(acs[i], "secteur");
	}
	if (events)
		events_setup();
	UINT32 sci = 0;
	AcpiReadBitRegister(ACPI_BITREG_SCI_ENABLE, &sci);
	char path[64] = "?";
	ACPI_BUFFER pb = {sizeof path, path};
	if (ec_dev)
		AcpiGetName(ec_dev, ACPI_FULL_PATHNAME, &pb);
	elog("ACPICA %x : espace de noms chargé, mode %s, EC %s (ports %x / %x), %d batterie(s), %d adaptateur(s)",
	     ACPI_CA_VERSION, sci ? "ACPI" : "ancien (SCI_EN à 0)", ec_dev ? path : "absent", ec_data, ec_cmd,
	     nbat, nac);
	return 0;
}

/* ---- lectures */
static int eval_int(ACPI_HANDLE h, const char *name, UINT64 *v)
{
	ACPI_OBJECT o;
	ACPI_BUFFER b = {sizeof o, &o};
	if (ACPI_FAILURE(AcpiEvaluateObjectTyped(h, (char *)name, NULL, &b, ACPI_TYPE_INTEGER)))
		return -1;
	*v = o.Integer.Value;
	return 0;
}

static ACPI_OBJECT *eval_pkg(ACPI_HANDLE h, const char *name, ACPI_BUFFER *b)
{
	b->Length = ACPI_ALLOCATE_BUFFER;
	b->Pointer = NULL;
	ACPI_STATUS s = AcpiEvaluateObjectTyped(h, (char *)name, NULL, b, ACPI_TYPE_PACKAGE);
	if (ACPI_FAILURE(s)) {
		if (s != AE_NOT_FOUND) {
			static int said;
			if (said++ < 8)
				elog("%s : %s", name, AcpiFormatException(s));
		}
		return NULL;
	}
	return b->Pointer;
}

static long pkg_int(ACPI_OBJECT *p, unsigned i)
{
	if (i >= p->Package.Count || p->Package.Elements[i].Type != ACPI_TYPE_INTEGER)
		return -1;
	UINT64 v = p->Package.Elements[i].Integer.Value;
	return v >= 0xFFFFFFFFULL ? -1 : (long)v;
}

static void pkg_str(ACPI_OBJECT *p, unsigned i, char *out, unsigned n)
{
	out[0] = 0;
	if (i >= p->Package.Count)
		return;
	ACPI_OBJECT *e = &p->Package.Elements[i];
	const char *s = 0;
	unsigned len = 0;
	if (e->Type == ACPI_TYPE_STRING) {
		s = e->String.Pointer;
		len = e->String.Length;
	} else if (e->Type == ACPI_TYPE_BUFFER) {
		s = (const char *)e->Buffer.Pointer;
		len = e->Buffer.Length;
	}
	unsigned k = 0;
	for (unsigned j = 0; s && j < len && s[j] && k + 1 < n; j++)
		if (s[j] >= 0x20 && s[j] < 0x7F)
			out[k++] = s[j];
	while (k && out[k - 1] == ' ')
		k--;
	out[k] = 0;
}

/* informations fixes d'une batterie (_BIX, sinon _BIF), en mWh */
static struct binfo {
	int valid;
	int ma;             /* unités : mA / mAh */
	long design, full, voltage, warn, low, cycles;
	char model[24], oem[24], tech[8];
	UINT64 when;        /* AcpiOsGetTimer de la lecture */
	UINT64 sta;
} binfo[MAXDEV];

/* les capacités en mAh se convertissent par la tension de conception (_BIX, _BIF), la même pour la
 * capacité pleine et la restante (le rapport, donc le %, reste juste) ; inconnue : une tension nominale
 * (3 éléments Li-ion), signalée au journal : le % et le temps restent justes, les Wh sont estimés */
#define MV_NOMINAL 11100

static long conv_mv(long design_mv, int ma)
{
	return !ma ? 0 : design_mv > 0 ? design_mv : MV_NOMINAL;
}

static long to_mw(long v, int ma, long mv)
{
	if (v < 0)
		return -1;
	if (!ma)
		return v;
	return mv > 0 ? v * mv / 1000 : -1;
}

static void read_info(int i)
{
	struct binfo *b = &binfo[i];
	ACPI_BUFFER buf;
	ACPI_OBJECT *p = eval_pkg(bats[i], "_BIX", &buf);
	b->valid = 0;
	if (p && p->Package.Count >= 16) {
		b->ma = pkg_int(p, 1) == 1;
		b->voltage = pkg_int(p, 5);
		long mv = conv_mv(b->voltage, b->ma);
		b->design = to_mw(pkg_int(p, 2), b->ma, mv);
		b->full = to_mw(pkg_int(p, 3), b->ma, mv);
		b->warn = to_mw(pkg_int(p, 6), b->ma, mv);
		b->low = to_mw(pkg_int(p, 7), b->ma, mv);
		b->cycles = pkg_int(p, 8);
		pkg_str(p, 16, b->model, sizeof b->model);
		pkg_str(p, 18, b->tech, sizeof b->tech);
		pkg_str(p, 19, b->oem, sizeof b->oem);
		b->valid = 1;
	} else {
		if (p)
			ACPI_FREE(p);
		p = eval_pkg(bats[i], "_BIF", &buf);
		if (p && p->Package.Count >= 13) {
			b->ma = pkg_int(p, 0) == 1;
			b->voltage = pkg_int(p, 4);
			long mv = conv_mv(b->voltage, b->ma);
			b->design = to_mw(pkg_int(p, 1), b->ma, mv);
			b->full = to_mw(pkg_int(p, 2), b->ma, mv);
			b->warn = to_mw(pkg_int(p, 5), b->ma, mv);
			b->low = to_mw(pkg_int(p, 6), b->ma, mv);
			b->cycles = -1;
			pkg_str(p, 9, b->model, sizeof b->model);
			pkg_str(p, 11, b->tech, sizeof b->tech);
			pkg_str(p, 12, b->oem, sizeof b->oem);
			b->valid = 1;
		}
	}
	if (p)
		ACPI_FREE(p);
	b->when = AcpiOsGetTimer();
	static long said_full[MAXDEV], said_cycles[MAXDEV];
	if (b->valid && (b->full != said_full[i] || b->cycles != said_cycles[i])) { /* (au journal quand ça change) */
		said_full[i] = b->full;
		said_cycles[i] = b->cycles;
		elog("batterie %d : %s %s (%s), %ld mWh neuve, %ld à la dernière charge, %ld mV, %ld cycles%s", i,
		     b->oem, b->model, b->tech, b->design, b->full, b->voltage, b->cycles,
		     !b->ma ? "" : b->voltage > 0 ? " (en mA)" : " (en mA, tension inconnue : Wh estimés)");
	}
}

int acpi_read(struct power_raw *r)
{
	memset(r, 0, sizeof *r);
	r->ac = -1;
	r->rate_mw = r->remain_mwh = r->full_mwh = r->design_mwh = -1;
	r->voltage_mv = r->cycles = r->warn_mwh = r->low_mwh = -1;
	for (int i = 0; i < nac; i++) {
		UINT64 v;
		if (!eval_int(acs[i], "_PSR", &v))
			r->ac = r->ac == 1 ? 1 : (v != 0);
	}
	long rate = 0, remain = 0, full = 0, design = 0, warn = 0, low = 0;
	int rate_ok = 1, cap_ok = 1;
	for (int i = 0; i < nbat; i++) {
		UINT64 sta = 0x1F;
		ACPI_HANDLE m;
		if (ACPI_SUCCESS(AcpiGetHandle(bats[i], "_STA", &m)) && eval_int(bats[i], "_STA", &sta))
			sta = 0;
		if (!(sta & 0x10)) {
			binfo[i].valid = 0;
			binfo[i].sta = sta;
			continue;
		}
		struct binfo *b = &binfo[i];
		if (!b->valid || b->sta != sta || AcpiOsGetTimer() - b->when > 60 * 10000000ULL)
			read_info(i);
		b->sta = sta;
		ACPI_BUFFER buf;
		ACPI_OBJECT *p = eval_pkg(bats[i], "_BST", &buf);
		if (!p)
			continue;
		if (p->Package.Count >= 4) {
			long st = pkg_int(p, 0), pr = pkg_int(p, 1), rc = pkg_int(p, 2), pv = pkg_int(p, 3);
			long mv = pv > 0 ? pv : b->voltage, cv = conv_mv(b->voltage, b->ma);
			if (pr >= 0x80000000L) /* (des micrologiciels la donnent négative en décharge) */
				pr = 0x100000000L - pr;
			long prw = to_mw(pr, b->ma, cv), rcw = to_mw(rc, b->ma, cv);
			r->batteries++;
			if (st > 0)
				r->state |= (unsigned)st & 7;
			if (prw < 0)
				rate_ok = 0;
			else
				rate += prw;
			if (rcw < 0 || !b->valid || b->full <= 0)
				cap_ok = 0;
			else {
				remain += rcw;
				full += b->full;
				design += b->design > 0 ? b->design : 0;
				warn += b->warn > 0 ? b->warn : 0;
				low += b->low > 0 ? b->low : 0;
			}
			if (r->batteries == 1) {
				r->voltage_mv = mv;
				r->cycles = b->cycles;
				memcpy(r->model, b->model, sizeof r->model);
				memcpy(r->oem, b->oem, sizeof r->oem);
				memcpy(r->tech, b->tech, sizeof r->tech);
			}
		}
		ACPI_FREE(p);
	}
	if (r->batteries) {
		if (rate_ok)
			r->rate_mw = rate;
		if (cap_ok) {
			r->remain_mwh = remain;
			r->full_mwh = full;
			r->design_mwh = design;
			r->warn_mwh = warn;
			r->low_mwh = low;
		}
	}
	return 0;
}
