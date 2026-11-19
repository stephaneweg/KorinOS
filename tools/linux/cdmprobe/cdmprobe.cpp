// Sonde native (Linux, WSL) : un hôte Host_10 minimal pour libwidevinecdm.so ; un filtre seccomp
// piège les syscall dont l'instruction est DANS le module (prototype de la personnalité Linux de
// Korin) ; parcours : Initialize, CreateSessionAndGenerateRequest (PSSH de Widevine), minuteries,
// CloseSession, Destroy.
#include <dlfcn.h>
#include <link.h>
#include <linux/filter.h>
#include <linux/seccomp.h>
#include <signal.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/time.h>
#include <ucontext.h>
#include <unistd.h>

#include <vector>

#include "content_decryption_module.h"

static unsigned long s_lo, s_hi;
static unsigned long s_count[512];
static const char *s_phase = "chargement";

static long raw6 (long n, long a, long b, long c, long d, long e, long f)
{
	register long r10 __asm__ ("r10") = d, r8 __asm__ ("r8") = e, r9 __asm__ ("r9") = f;
	long ret;
	__asm__ volatile ("syscall" : "=a" (ret) : "a" (n), "D" (a), "S" (b), "d" (c), "r" (r10), "r" (r8), "r" (r9)
			  : "rcx", "r11", "memory");
	return ret;
}

static void say (const char *fmt, ...) __attribute__ ((format (printf, 1, 2)));
static void say (const char *fmt, ...)
{
	char b[400];
	va_list ap;
	va_start (ap, fmt);
	int k = vsnprintf (b, sizeof b, fmt, ap);
	va_end (ap);
	if (k > (int) sizeof b - 1)
		k = sizeof b - 1;
	raw6 (1, 2, (long) b, k, 0, 0, 0);
}

static void on_sys (int, siginfo_t *si, void *ctx)
{
	greg_t *g = ((ucontext_t *) ctx)->uc_mcontext.gregs;
	long n = si->si_syscall;
	long r = raw6 (n, g[REG_RDI], g[REG_RSI], g[REG_RDX], g[REG_R10], g[REG_R8], g[REG_R9]);
	say ("  [%s] syscall %ld (%#lx, %#lx, %#lx, %#lx) depuis +%#lx = %ld\n", s_phase, n,
	     (unsigned long) g[REG_RDI], (unsigned long) g[REG_RSI], (unsigned long) g[REG_RDX],
	     (unsigned long) g[REG_R10], (unsigned long) si->si_call_addr - s_lo, r);
	if (n == 257 || n == 2)
		say ("    fichier : %s\n", (const char *) (n == 257 ? g[REG_RSI] : g[REG_RDI]));
	if (n >= 0 && n < 512)
		s_count[n]++;
	g[REG_RAX] = r;
}

static int find_end (struct dl_phdr_info *in, size_t, void *)
{
	if (in->dlpi_addr != s_lo)
		return 0;
	for (int i = 0; i < in->dlpi_phnum; i++)
		if (in->dlpi_phdr[i].p_type == PT_LOAD)
		{
			unsigned long e = s_lo + in->dlpi_phdr[i].p_vaddr + in->dlpi_phdr[i].p_memsz;
			if (e > s_hi)
				s_hi = e;
		}
	return 1;
}

class Buf : public cdm::Buffer
{
public:
	explicit Buf (uint32_t cap) : d_ (cap), size_ (0) {}
	void Destroy () override { delete this; }
	uint32_t Capacity () const override { return (uint32_t) d_.size (); }
	uint8_t *Data () override { return d_.data (); }
	void SetSize (uint32_t s) override { size_ = s; }
	uint32_t Size () const override { return size_; }
private:
	std::vector<uint8_t> d_;
	uint32_t size_;
};

struct Timer
{
	double due;
	void *ctx;
};

static double now ()
{
	struct timeval tv;
	gettimeofday (&tv, 0);
	return tv.tv_sec + tv.tv_usec / 1e6;
}

class Host : public cdm::Host_10
{
public:
	cdm::ContentDecryptionModule_10 *cdm = 0;
	std::vector<Timer> timers;
	int storage_req = -1, protection_req = 0;
	int messages = 0;

	cdm::Buffer *Allocate (uint32_t c) override { return new Buf (c); }
	void SetTimer (int64_t ms, void *ctx) override
	{
		say ("hôte : SetTimer %lld ms\n", (long long) ms);
		timers.push_back ({ now () + ms / 1000.0, ctx });
	}
	cdm::Time GetCurrentWallTime () override { return now (); }
	void OnInitialized (bool ok) override { say ("hôte : OnInitialized %d\n", ok); }
	void OnResolveKeyStatusPromise (uint32_t p, cdm::KeyStatus s) override { say ("hôte : key status %u %u\n", p, s); }
	void OnResolveNewSessionPromise (uint32_t p, const char *id, uint32_t n) override
	{
		say ("hôte : nouvelle session (promesse %u) %.*s\n", p, (int) n, id);
	}
	void OnResolvePromise (uint32_t p) override { say ("hôte : promesse %u tenue\n", p); }
	void OnRejectPromise (uint32_t p, cdm::Exception e, uint32_t code, const char *m, uint32_t n) override
	{
		say ("hôte : promesse %u rejetée (%u, %u) %.*s\n", p, e, code, (int) n, m ? m : "");
	}
	void OnSessionMessage (const char *id, uint32_t idn, cdm::MessageType t, const char *m, uint32_t n) override
	{
		messages++;
		say ("hôte : message de la session %.*s, type %u, %u octets (requête de licence) :", (int) idn, id, t, n);
		for (uint32_t i = 0; i < n && i < 24; i++)
			say (" %02x", (unsigned char) m[i]);
		say ("…\n");
	}
	void OnSessionKeysChange (const char *, uint32_t, bool, const cdm::KeyInformation *, uint32_t c) override
	{
		say ("hôte : clés changées (%u)\n", c);
	}
	void OnExpirationChange (const char *, uint32_t, cdm::Time t) override { say ("hôte : expiration %f\n", t); }
	void OnSessionClosed (const char *id, uint32_t n) override { say ("hôte : session fermée %.*s\n", (int) n, id); }
	void SendPlatformChallenge (const char *, uint32_t, const char *, uint32_t) override { say ("hôte : défi de plateforme\n"); }
	void EnableOutputProtection (uint32_t m) override { say ("hôte : EnableOutputProtection %#x\n", m); }
	void QueryOutputProtectionStatus () override
	{
		say ("hôte : QueryOutputProtectionStatus\n");
		protection_req++;
	}
	void OnDeferredInitializationDone (cdm::StreamType, cdm::Status) override {}
	cdm::FileIO *CreateFileIO (cdm::FileIOClient *) override
	{
		say ("hôte : CreateFileIO (refusé)\n");
		return 0;
	}
	void RequestStorageId (uint32_t v) override
	{
		say ("hôte : RequestStorageId %u\n", v);
		storage_req = (int) v;
	}

	// les réponses différées et les minuteries échues
	void pump (double seconds)
	{
		double end = now () + seconds;
		do
		{
			if (storage_req >= 0)
			{
				static const uint8_t id[32] = { 0x4b, 0x6f, 0x72, 0x69, 0x6e, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11,
								12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27 };
				uint32_t v = (uint32_t) storage_req;
				storage_req = -1;
				cdm->OnStorageId (v, id, sizeof id);
			}
			while (protection_req > 0)
			{
				protection_req--;
				cdm->OnQueryOutputProtectionStatus (cdm::kQuerySucceeded, 0, 0);
			}
			for (size_t i = 0; i < timers.size (); i++)
				if (timers[i].due <= now ())
				{
					void *c = timers[i].ctx;
					timers.erase (timers.begin () + i);
					cdm->TimerExpired (c);
					break;
				}
			usleep (5000);
		} while (now () < end);
	}
};

static Host s_host;

static void *get_host (int version, void *)
{
	say ("sonde : le module demande l'hôte, version %d\n", version);
	return version == cdm::Host_10::kVersion ? static_cast<cdm::Host_10 *> (&s_host) : 0;
}

int main (int, char **argv)
{
	void *h = dlopen (argv[1], RTLD_NOW);
	if (!h)
	{
		printf ("dlopen : %s\n", dlerror ());
		return 1;
	}
	struct link_map *lm;
	dlinfo (h, RTLD_DI_LINKMAP, &lm);
	s_lo = lm->l_addr;
	dl_iterate_phdr (find_end, 0);
	say ("sonde : module à %#lx - %#lx\n", s_lo, s_hi);
	if ((s_lo >> 32) != (s_hi >> 32))
		return 1;
	struct sigaction sa;
	memset (&sa, 0, sizeof sa);
	sa.sa_sigaction = on_sys;
	sa.sa_flags = SA_SIGINFO;
	sigaction (SIGSYS, &sa, 0);
	struct sock_filter f[] = {
		BPF_STMT (BPF_LD | BPF_W | BPF_ABS, offsetof (struct seccomp_data, instruction_pointer) + 4),
		BPF_JUMP (BPF_JMP | BPF_JEQ | BPF_K, (unsigned) (s_lo >> 32), 0, 4),
		BPF_STMT (BPF_LD | BPF_W | BPF_ABS, offsetof (struct seccomp_data, instruction_pointer)),
		BPF_JUMP (BPF_JMP | BPF_JGE | BPF_K, (unsigned) s_lo, 0, 2),
		BPF_JUMP (BPF_JMP | BPF_JGE | BPF_K, (unsigned) s_hi, 1, 0),
		BPF_STMT (BPF_RET | BPF_K, SECCOMP_RET_TRAP),
		BPF_STMT (BPF_RET | BPF_K, SECCOMP_RET_ALLOW),
	};
	struct sock_fprog prog = { (unsigned short) (sizeof f / sizeof f[0]), f };
	prctl (PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
	if (prctl (PR_SET_SECCOMP, SECCOMP_MODE_FILTER, &prog))
	{
		perror ("seccomp");
		return 1;
	}
	s_phase = "InitializeCdmModule_4";
	((void (*) ()) dlsym (h, "InitializeCdmModule_4")) ();
	s_phase = "CreateCdmInstance";
	const char *ks = "com.widevine.alpha";
	void *p = ((void *(*) (int, const char *, uint32_t, void *(*) (int, void *), void *))
		   dlsym (h, "CreateCdmInstance")) (10, ks, (uint32_t) strlen (ks), get_host, 0);
	say ("sonde : CreateCdmInstance -> %p\n", p);
	if (!p)
		return 1;
	s_host.cdm = static_cast<cdm::ContentDecryptionModule_10 *> (p);
	s_phase = "Initialize";
	s_host.cdm->Initialize (false, false, false);
	s_host.pump (0.5);
	// PSSH de Widevine (boîte v0) : system id edef8ba9-79d6-4ace-a3c8-27dcd51d21ed, données = protobuf
	// WidevinePsshData { key_id (2) = 16 octets } ; boîte de 50 octets
	static const uint8_t pssh[] = {
		0, 0, 0, 50, 'p', 's', 's', 'h', 0, 0, 0, 0,
		0xed, 0xef, 0x8b, 0xa9, 0x79, 0xd6, 0x4a, 0xce, 0xa3, 0xc8, 0x27, 0xdc, 0xd5, 0x1d, 0x21, 0xed,
		0, 0, 0, 18,
		0x12, 0x10, 0x9e, 0xb4, 0x05, 0x0d, 0xe4, 0x4b, 0x47, 0x80, 0x2b, 0x1d, 0xa8, 0x5e, 0x5a, 0x0e, 0xa0, 0x4b,
	};
	s_phase = "CreateSessionAndGenerateRequest";
	s_host.cdm->CreateSessionAndGenerateRequest (1, cdm::kTemporary, cdm::kCenc, pssh, sizeof pssh);
	s_phase = "minuteries";
	s_host.pump (2.0);
	s_phase = "Destroy";
	s_host.cdm->Destroy ();
	s_phase = "DeinitializeCdmModule";
	((void (*) ()) dlsym (h, "DeinitializeCdmModule")) ();
	say ("sonde : messages de licence : %d ; appels directs du module par numéro :", s_host.messages);
	for (int i = 0; i < 512; i++)
		if (s_count[i])
			say (" %d×%lu", i, s_count[i]);
	say ("\n");
	return 0;
}
