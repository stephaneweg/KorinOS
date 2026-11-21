/* sh.h : ce que partagent sh.c, sh_word.c, sh_run.c et sh_cmds.c (comme sh.asm et ses .inc :
 * sh_word.inc, sh_run.inc, sh_cmds.inc). Noms des fonctions en anglais, ceux de l'asm en
 * commentaire. */
#ifndef SH_H
#define SH_H

#include "korinrt.h"

#define IN_MAX 1023
#define OBUF 1024
#define PEND_MAX 512
#define TXT_MAX (1L << 20)     /* texte d'un script */
#define AR_MAX (16L << 20)     /* arène des mots */
#define AT_MAX (1L << 20)      /* pointeurs (argv) */
#define VAR_MAX (4L << 20)     /* textes des variables */
#define CAP_MAX (4L << 20)     /* sortie d'un $(...) */
#define SLICE_MAX (1L << 20)   /* texte d'un sh -c */
#define JOBS_MAX 8             /* tâches arrêtées ou en arrière-plan (jobs) */
#define JOB_STOPPED 1
#define JOB_RUNNING 2
#define JOB_DONE 3
#define SIGCONT 19

#define ESC 1                  /* devant un caractère spécial venu d'un texte cité */
#define VARS_MAX 1024
#define RED_MAX 8
#define R_IN 0
#define R_OUT 1
#define R_APP 2
#define R_DUP 3
#define AS_MAX 32              /* affectations devant une commande */
#define TUBE_MAX 32            /* commandes d'un tube */

#define V_PIPE_READ 38         /* (vfs) a poignée, b octets (<= 464), c bit 0 : sans attente */
#define VT_READ 2
#define VT_SPGRP 10
#define E_INTR -4
#define WAIT_EX_TAG (0x57414954L << 32)
#define WEX_NOHANG 1
#define WEX_UNTRACED 2
#define WEX_CONTINUED 8
#define WR_NONE 0
#define WR_EXITED 1
#define WR_STOPPED 2
#define WR_CONTINUED 3

struct job {             /* JOB_* : 56 o */
	long pg;         /* groupe (0 : place libre) */
	long pid;        /* pid attendu (le dernier du tube) */
	long state;      /* JOB_STOPPED, JOB_RUNNING, JOB_DONE */
	char name[32];
};
struct var {             /* VE_* */
	char *text;      /* « NOM=VALEUR » (dans varm) ; 0 : retirée */
	long flag;       /* 1 : exportée */
};
struct redir {           /* RE_* */
	long fd, type;
	char *target;
};
struct assign {          /* AS_* */
	char *name;
	long len;
	char *value;
};

/* ---- état (sh.c) */
extern char *P; /* le curseur dans le texte (x28 de l'asm) */
extern long noexec, cond, jump, syntax, incomplete, chain_ok, subst_code, opt_e, opt_x;
extern long in_pipe, pipe_in, pipe_out, pipe_pid, reds_n, red_dn, last_code, out_fd, err_fd;
extern long interactive, tty, vfs, vtc, my_pid, job_pg, in_h, pend_n, pend_i, obuf_n, last_c;
extern u64 wst;
extern char **arg_v, **pos_v;
extern long arg_n, pos_n, vars_n;
extern char *txt, *ar_p, *varm_p, *cap_p, *slice_p;
extern char **at_p;
extern struct redir reds[RED_MAX];
extern long red_d[RED_MAX];
extern struct var vars[VARS_MAX];
extern char cwd[256], path_tmp[256], tmp_num[32], inbuf[IN_MAX + 1], line_buf[4096];
extern struct dirent dirent_buf;
extern u8 iobuf[520], pend[PEND_MAX], obuf[OBUF], msg[512], iobuf2[64 * 64];
extern struct job jobs[JOBS_MAX];

/* mot_etendre : x19 mode, x20 écriture, x21 début du champ, x22 le champ existe */
extern long we_mode, we_have;
extern char *we_w, *we_fs;

/* ---- sh.c */
NORETURN void sh_exit(void);                 /* sh_sortie */
long run_text(char *t);                      /* run_texte -> 1 si incomplet */
long in_read(long n, long nowait);
void tty_spgrp(long g);
void tty_foreground(void);                   /* tty_premier_plan */
void job_fg(long pid);
void job_end(void);                          /* job_fin */
void job_stop(long pid, long sig);           /* job_arrete */
long cmd_jobs(char **argv);
long cmd_fg(char **argv);
long cmd_bg(char **argv);
void flush(void);
extern long out_werr;
void term_putb(int c);
void term_puts(const char *s);
void term_write(const void *b, u64 n);
void term_dec(long v);
void term_ensure_nl(void);
long count_cols(const char *s);
void update_cwd(void);

/* ---- sh_word.c */
struct var *var_find(const char *name, long len);        /* var_cherche */
char *var_value(const char *name, long len);             /* var_valeur */
void var_set(const char *name, long len, const char *val, long exp); /* var_pose */
void var_set_kv(const char *kv, long exp);               /* var_pose_kv */
void var_unset(const char *name);                        /* var_retire */
char **env_array(long all);                              /* env_tableau */
int word_end(int c);                                     /* fin_mot */
void skip_parens(void);                                  /* sauter_parens */
void word_skip(void);                                    /* mot_sauter */
void text_cut(void);                                     /* texte_coupe */
char *word_expand(long mode);                            /* mot_etendre */
void at_push(char *p);                                   /* at_pousse */
void put_value(const char *v, long quoted);              /* pose_valeur */
void name_skip(void);                                    /* nom_passer */
long shell_code(long c);                                 /* code_shell */
int pattern_match(const char *pat, const char *s);       /* motif_ok */

/* ---- sh_run.c */
void skip_blank(void);
void syntax_error(void);                                 /* erreur_syntaxe */
void do_list(void);
void noexec_inc(void);
void noexec_dec(void);
void launch_external(char **argv, long nas, struct assign *as); /* lance_externe */
long wait_pid(long pid);                                 /* attendre */
void capture(const char *a, const char *b, long quoted); /* capturer */

/* ---- sh_cmds.c */
struct builtin {
	const char *name;
	long (*fn)(char **argv);
};
extern const struct builtin builtins[], legacy[];
const struct builtin *builtin_find(const char *name);    /* integree_cherche */
const struct builtin *legacy_find(const char *name);     /* ancienne_cherche */
void builtin_run(const struct builtin *b, char **argv);  /* integree_lance */
void sh_err_puts(const char *s);                         /* err_puts */
void sh_err_say(const char *s);                          /* err_dire */
void err_nofile(const char *name);                       /* err_fichier */
void err_notfound(const char *name);                     /* err_introuvable */
void error_code(long e);                                 /* erreur_code */
void trace(char **argv);
long signed_dec(const char *s);                          /* dec_signe */

#define S_SYNTAX "sh: erreur de syntaxe\n"

#endif
