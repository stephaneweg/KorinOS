# config.mak de git pour Korin (tools/git/build.sh, docs/GIT.md). @SPECS@, @DEP@, @SSL@, @TC@ et
# @CPU@ sont remplaces par le script (posix.specs, build/git ou build/pc/git, OpenSSL, prefixe de la
# chaine, options du processeur). Les variables uname_* sont donnees a make : rien ne vient de
# config.mak.uname (l'hote).

CC = @TC@gcc -specs=@SPECS@
AR = @TC@ar
STRIP = @TC@strip
CFLAGS = @CPU@-O2 -g0 -fno-stack-protector -ffunction-sections -fdata-sections -Wno-error
LDFLAGS =
PTHREAD_LIBS =
NO_R_TO_GCC_LINKER = YesPlease

# Emplacements sur la carte : git dans /bin (SD0:/bin), ses aides dans /bin/git-core, la
# configuration du systeme dans /etc/gitconfig, le modele dans /etc/git-core/templates. Chemins
# fixes (pas de RUNTIME_PREFIX) ; /bin et /etc sont des chemins virtuels de SD0:/etc/paths.txt.
prefix = /git
bindir = /bin
gitexecdir = /bin/git-core
sysconfdir = /etc
template_dir = /etc/git-core/templates
sharedir = /git/share
mandir = /git/share/man
infodir = /git/share/info
htmldir = /git/share/doc
SHELL_PATH = /bin/sh
PERL_PATH = /bin/perl
DEFAULT_PAGER = cat

# Ce que le systeme n'a pas
NO_ICONV = YesPlease
NO_GETTEXT = YesPlease
NO_TCLTK = YesPlease
NO_PERL = YesPlease
NO_PYTHON = YesPlease
NO_UNIX_SOCKETS = YesPlease
NO_EXPAT = YesPlease
NO_IPV6 = YesPlease
NO_MMAP = YesPlease
NO_NSEC = YesPlease
NO_OPENSSL = YesPlease
NO_INSTALL_HARDLINKS = YesPlease
SKIP_DASHED_BUILT_INS = YesPlease
NO_REGEX = YesPlease
NO_SYMLINK_HEAD = YesPlease
NO_TRUSTABLE_FILEMODE = YesPlease
FREAD_READS_DIRECTORIES = UnfortunatelyYes
HAVE_PATHS_H = YesPlease
HAVE_CLOCK_GETTIME = YesPlease
HAVE_CLOCK_MONOTONIC = YesPlease
HAVE_DEV_TTY = YesPlease
HAVE_ALLOCA_H = YesPlease

# HTTP, HTTPS : libcurl, OpenSSL, zlib de build/git
CURLDIR = @DEP@/curl
CURL_CFLAGS = -I@DEP@/curl/include
CURL_LDFLAGS = -L@DEP@/curl/lib -L@SSL@/lib -L@DEP@/zlib/lib -lcurl -lssl -lcrypto -lz
ZLIB_PATH = @DEP@/zlib
