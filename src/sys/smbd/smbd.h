/* smbd.h : le serveur de fichiers SMB de Korin (docs/SMB.md), sur le moteur de serveur de libsmb2
 * (tools/libsmb2/build.sh). Le même source sous Linux (essais dans WSL) et sous Korin (libposix). */
#ifndef KORIN_SMBD_H
#define KORIN_SMBD_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

#include <smb2/smb2.h>
#include <smb2/libsmb2.h>
#include <smb2/libsmb2-raw.h>

#define SMBD_MAX_SHARES 32
#define SMBD_MAX_USERS 16
#define SMBD_MAX_TREES 256
#define SMBD_MAX_FILES 1024
#define SMBD_PATH 1024

struct share {
	char name[80];        /* « Korin » (comparé sans la casse) */
	char path[SMBD_PATH]; /* « /media/sd1/home » sous Korin, un dossier sous Linux */
	int readonly;
};

struct user {
	char name[64];
	char password[128];
};

struct config {
	int port;
	char name[64];      /* nom du serveur (NetBIOS), « KORIN » */
	char workgroup[64]; /* « WORKGROUP » */
	char label[64];     /* étiquette des volumes vue par Windows */
	int verbose;
	int nshares, nusers;
	struct share shares[SMBD_MAX_SHARES];
	struct user users[SMBD_MAX_USERS];
};

/* un arbre connecté (TREE_CONNECT) : un partage, ou IPC$ (share = 0) */
struct tree {
	struct smb2_context *ctx; /* 0 : libre */
	uint32_t tid;
	struct share *share; /* (retrouvé par son nom à chaque requête : la configuration peut être relue) */
	int ipc;
	char name[80];
};

extern struct config conf;

int conf_load(const char *path);
void conf_check(void); /* smbd.ini ou shares.ini changé : relu */
struct share *conf_share(const char *name);
struct user *conf_user(const char *name);

struct tree *tree_of(struct smb2_context *smb2);
struct share *tree_share(struct smb2_context *smb2, uint32_t tid); /* 0 : arbre fermé ou partage retiré */
void log_msg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
void log_dbg(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* fs.c : les commandes de fichiers */
int fs_create(struct smb2_server *s, struct smb2_context *smb2, struct smb2_create_request *req,
	      struct smb2_create_reply *rep);
int fs_close(struct smb2_server *s, struct smb2_context *smb2, struct smb2_close_request *req,
	     struct smb2_close_reply *rep);
int fs_flush(struct smb2_server *s, struct smb2_context *smb2, struct smb2_flush_request *req);
int fs_read(struct smb2_server *s, struct smb2_context *smb2, struct smb2_read_request *req,
	    struct smb2_read_reply *rep);
int fs_write(struct smb2_server *s, struct smb2_context *smb2, struct smb2_write_request *req,
	     struct smb2_write_reply *rep);
int fs_lock(struct smb2_server *s, struct smb2_context *smb2, struct smb2_lock_request *req);
int fs_ioctl(struct smb2_server *s, struct smb2_context *smb2, struct smb2_ioctl_request *req,
	     struct smb2_ioctl_reply *rep);
int fs_query_directory(struct smb2_server *s, struct smb2_context *smb2, struct smb2_query_directory_request *req,
		       struct smb2_query_directory_reply *rep);
int fs_change_notify(struct smb2_server *s, struct smb2_context *smb2, struct smb2_change_notify_request *req,
		     struct smb2_change_notify_reply *rep);
int fs_query_info(struct smb2_server *s, struct smb2_context *smb2, struct smb2_query_info_request *req,
		  struct smb2_query_info_reply *rep);
int fs_set_info(struct smb2_server *s, struct smb2_context *smb2, struct smb2_set_info_request *req);
void fs_close_tree(struct smb2_context *smb2, uint32_t tid);
void fs_close_all(struct smb2_context *smb2);

/* une réponse d'erreur avec ce code NT : « return reply_error(...) » (rend -1) */
int reply_error(struct smb2_context *smb2, int cmd, uint32_t status);

/* codes NT (MS-ERREF) */
#define ST_OK 0x00000000u
#define ST_BUFFER_OVERFLOW 0x80000005u
#define ST_NO_MORE_FILES 0x80000006u
#define ST_INVALID_INFO_CLASS 0xC0000003u
#define ST_INFO_LENGTH_MISMATCH 0xC0000004u
#define ST_INVALID_HANDLE 0xC0000008u
#define ST_INVALID_PARAMETER 0xC000000Du
#define ST_NO_SUCH_FILE 0xC000000Fu
#define ST_INVALID_DEVICE_REQUEST 0xC0000010u
#define ST_END_OF_FILE 0xC0000011u
#define ST_ACCESS_DENIED 0xC0000022u
#define ST_BUFFER_TOO_SMALL 0xC0000023u
#define ST_OBJECT_NAME_INVALID 0xC0000033u
#define ST_OBJECT_NAME_NOT_FOUND 0xC0000034u
#define ST_OBJECT_NAME_COLLISION 0xC0000035u
#define ST_OBJECT_PATH_NOT_FOUND 0xC000003Au
#define ST_SHARING_VIOLATION 0xC0000043u
#define ST_DELETE_PENDING 0xC0000056u
#define ST_DISK_FULL 0xC000007Fu
#define ST_FILE_IS_A_DIRECTORY 0xC00000BAu
#define ST_NOT_SUPPORTED 0xC00000BBu
#define ST_BAD_NETWORK_NAME 0xC00000CCu
#define ST_DIRECTORY_NOT_EMPTY 0xC0000101u
#define ST_NOT_A_DIRECTORY 0xC0000103u
#define ST_CANNOT_DELETE 0xC0000121u
#define ST_NOT_FOUND 0xC0000225u
#define ST_MEDIA_WRITE_PROTECTED 0xC00000A2u

#endif
