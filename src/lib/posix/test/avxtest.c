/*
 * avxtest.c -- l'AVX sur Korin PC (docs/etude/PC-PILOTES.md, « AVX ») : le noyau sauve-t-il les
 * registres YMM aux changements de thread et dans la trame des signaux ?
 *
 *   avxtest [threads] [tours]          (defaut : 6 threads, 20 tours)
 *
 * Dit d'abord ce que voit un programme : CPUID.1:ECX AVX et OSXSAVE, XCR0 (XGETBV), AVX2 et
 * AVX-512F de CPUID 7. Sans OSXSAVE ou sans les bits 1 et 2 de XCR0 : « pas d'AVX » (code 0 : rien
 * a essayer). Sinon chaque thread charge ses seize registres YMM (des valeurs a lui), tourne un
 * moment sans y toucher pendant que le thread principal lui envoie des signaux (SIGUSR1, dont le
 * gestionnaire ecrase les seize registres et verifie la trame : etat FPU aligne sur 64,
 * FP_XSTATE_MAGIC1), puis relit ses registres. Un registre change : « ECHEC », code 1.
 *
 * Copyright (c) 2026 Stéphane Wegener. MIT licence: Permission is hereby granted, free of
 * charge, to any person obtaining a copy of this software and associated documentation files
 * (the "Software"), to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so, subject to the
 * following conditions: The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software. THE SOFTWARE IS PROVIDED "AS IS",
 * WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED.
 */
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NT_MAX 32

static void cpuid (uint32_t leaf, uint32_t sub, uint32_t r[4])
{
	__asm__ volatile ("cpuid" : "=a" (r[0]), "=b" (r[1]), "=c" (r[2]), "=d" (r[3]) : "a" (leaf), "c" (sub));
}

static volatile int s_bad_frame, s_signals, s_go;
static int s_spin = 20000000;

/* SIGUSR1 : les seize YMM ecrases (le noyau doit les rendre au retour) ; la trame verifiee */
static void on_usr1 (int sig, siginfo_t *si, void *ucv)
{
	(void) sig; (void) si;
	const uint8_t *uc = (const uint8_t *) ucv;
	const uint8_t *fp = *(const uint8_t *const *) (uc + 224);
	if (((uintptr_t) fp & 63) || *(const uint32_t *) (fp + 464) != 0x46505853U)
		s_bad_frame = 1;
	__asm__ volatile (
		"vpcmpeqd %%ymm0, %%ymm0, %%ymm0\n"
		"vmovdqa %%ymm0, %%ymm1\n vmovdqa %%ymm0, %%ymm2\n vmovdqa %%ymm0, %%ymm3\n"
		"vmovdqa %%ymm0, %%ymm4\n vmovdqa %%ymm0, %%ymm5\n vmovdqa %%ymm0, %%ymm6\n"
		"vmovdqa %%ymm0, %%ymm7\n vmovdqa %%ymm0, %%ymm8\n vmovdqa %%ymm0, %%ymm9\n"
		"vmovdqa %%ymm0, %%ymm10\n vmovdqa %%ymm0, %%ymm11\n vmovdqa %%ymm0, %%ymm12\n"
		"vmovdqa %%ymm0, %%ymm13\n vmovdqa %%ymm0, %%ymm14\n vmovdqa %%ymm0, %%ymm15\n"
		::: "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7", "xmm8", "xmm9", "xmm10",
		"xmm11", "xmm12", "xmm13", "xmm14", "xmm15");
	__atomic_add_fetch (&s_signals, 1, __ATOMIC_RELAXED);
}

struct job { int id, rounds, errors; uint32_t in[16][8] __attribute__ ((aligned (32))), out[16][8] __attribute__ ((aligned (32))); };

/* charge les 16 YMM depuis in, tourne n fois sans y toucher, les range dans out */
static void hold (struct job *j, long n)
{
	__asm__ volatile (
		"vmovdqa 0(%0), %%ymm0\n vmovdqa 32(%0), %%ymm1\n vmovdqa 64(%0), %%ymm2\n vmovdqa 96(%0), %%ymm3\n"
		"vmovdqa 128(%0), %%ymm4\n vmovdqa 160(%0), %%ymm5\n vmovdqa 192(%0), %%ymm6\n vmovdqa 224(%0), %%ymm7\n"
		"vmovdqa 256(%0), %%ymm8\n vmovdqa 288(%0), %%ymm9\n vmovdqa 320(%0), %%ymm10\n vmovdqa 352(%0), %%ymm11\n"
		"vmovdqa 384(%0), %%ymm12\n vmovdqa 416(%0), %%ymm13\n vmovdqa 448(%0), %%ymm14\n vmovdqa 480(%0), %%ymm15\n"
		"1: dec %2\n jnz 1b\n"
		"vmovdqa %%ymm0, 0(%1)\n vmovdqa %%ymm1, 32(%1)\n vmovdqa %%ymm2, 64(%1)\n vmovdqa %%ymm3, 96(%1)\n"
		"vmovdqa %%ymm4, 128(%1)\n vmovdqa %%ymm5, 160(%1)\n vmovdqa %%ymm6, 192(%1)\n vmovdqa %%ymm7, 224(%1)\n"
		"vmovdqa %%ymm8, 256(%1)\n vmovdqa %%ymm9, 288(%1)\n vmovdqa %%ymm10, 320(%1)\n vmovdqa %%ymm11, 352(%1)\n"
		"vmovdqa %%ymm12, 384(%1)\n vmovdqa %%ymm13, 416(%1)\n vmovdqa %%ymm14, 448(%1)\n vmovdqa %%ymm15, 480(%1)\n"
		"vzeroupper\n"
		: : "r" (j->in), "r" (j->out), "r" (n)
		: "memory", "cc", "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7", "xmm8", "xmm9",
		"xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15");
}

static void *worker (void *arg)
{
	struct job *j = (struct job *) arg;
	while (!s_go)
		usleep (1000);
	for (int r = 0; r < j->rounds; r++)
	{
		for (int k = 0; k < 16; k++)
			for (int l = 0; l < 8; l++)
				j->in[k][l] = (uint32_t) (j->id * 0x01000000 + r * 0x10000 + k * 0x100 + l) ^ 0xA5A5A5A5U;
		hold (j, s_spin);
		if (memcmp (j->in, j->out, sizeof j->in))
			j->errors++;
	}
	return 0;
}

int main (int argc, char **argv)
{
	int nt = argc > 1 ? atoi (argv[1]) : 6, rounds = argc > 2 ? atoi (argv[2]) : 20;
	if (nt < 1 || nt > NT_MAX)
		nt = 6;
	if (getenv ("AVXTEST_SPIN"))
		s_spin = atoi (getenv ("AVXTEST_SPIN"));
	uint32_t r[4];
	cpuid (1, 0, r);
	int avx = (r[2] >> 28) & 1, osx = (r[2] >> 27) & 1;
	uint64_t xcr0 = 0;
	if (osx)
	{
		uint32_t lo, hi;
		__asm__ volatile ("xgetbv" : "=a" (lo), "=d" (hi) : "c" (0));
		xcr0 = lo | ((uint64_t) hi << 32);
	}
	cpuid (7, 0, r);
	printf ("CPUID : AVX %d, OSXSAVE %d, AVX2 %d, AVX-512F %d ; XCR0 %llx\n", avx, osx, (r[1] >> 5) & 1,
		(r[1] >> 16) & 1, (unsigned long long) xcr0);
	if (!avx || !osx || (xcr0 & 6) != 6)
	{
		printf ("pas d'AVX pour les programmes (le noyau ne sauve pas les YMM) : rien a essayer\n");
		return 0;
	}
	struct sigaction sa;
	memset (&sa, 0, sizeof sa);
	sa.sa_sigaction = on_usr1;
	sa.sa_flags = SA_SIGINFO | SA_RESTART;
	sigaction (SIGUSR1, &sa, 0);
	struct job *jobs = aligned_alloc (64, sizeof (struct job) * (size_t) nt);
	pthread_t t[NT_MAX];
	for (int i = 0; i < nt; i++)
	{
		memset (&jobs[i], 0, sizeof jobs[i]);
		jobs[i].id = i + 1;
		jobs[i].rounds = rounds;
		pthread_create (&t[i], 0, worker, &jobs[i]);
	}
	s_go = 1;
	/* des signaux tant que les threads tournent */
	for (int k = 0; k < rounds * 20; k++)
	{
		for (int i = 0; i < nt; i++)
			pthread_kill (t[i], SIGUSR1);
		usleep (5000);
	}
	int errors = 0;
	for (int i = 0; i < nt; i++)
	{
		pthread_join (t[i], 0);
		errors += jobs[i].errors;
	}
	printf ("%d thread(s), %d tour(s), %d signal(aux) recu(s), trame %s : %s\n", nt, rounds, s_signals,
		s_bad_frame ? "MAUVAISE" : "juste", errors || s_bad_frame ? "ECHEC" : "OK");
	return errors || s_bad_frame ? 1 : 0;
}
