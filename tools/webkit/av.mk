#
# av.mk -- la bibliotheque media d'Onyx (src/extra/onyx-libs/av, copie de user/Libs/av) pour ARMPI :
# libonyxav.a, ce que lie le moteur media de WebKit (MediaPlayerPrivateOnyx). Repris de
# tools/webkit/av.mk d'Onyx (docs/WEBKIT.md, « Son » et « Video ») :
#   - le son : FLAC, MP3 (minimp3), PCM, Opus (src/extra/opus-1.5.2) ; la sortie est AudioKit
#     (glue/av_sound_armpi.c sous les noms kapi_sound_* que prend av_player.c avec -DAV_KAPI_SOUND,
#     glue/include/appkit/appkit.h) ;
#   (av_mp3.c inclut "../../../third_party/minimp3/minimp3.h" : -I onyx-libs/i/a/b le trouve dans
#   onyx-libs/third_party/minimp3)
#   - la video : VP8 et VP9 (libvpx 1.15.2, src/extra/libvpx-1.15.2), AV1 (dav1d 1.5.1,
#     src/extra/dav1d-1.5.1), par av_vpx.c et av_dav1d.c d'Onyx, avec les sources et les drapeaux de
#     codecs.mk (AV_ARCH=aarch64 : NEON de libvpx, assembleur AArch64 de dav1d ; generic, le PC : le C
#     seul) ; glue/av_video_armpi.c (av__stub_of) reste le recours des codecs absents (av_stub.c n'est
#     pas compile).
#
#   make -f tools/webkit/av.mk EXTRA=<src/extra> GLUE=<tools/webkit/glue> S=<sysroot> O=<objets>
#
# Copyright (c) 2026 Stéphane Wegener and the Onyx contributors. MIT licence.
#
TP := $(EXTRA)
AV := $(EXTRA)/onyx-libs/av
AV_ARCH ?= aarch64
CPU ?= -mcpu=cortex-a72
include $(AV)/codecs.mk

TC ?= /opt/toolchains/aarch64-onyx-elf-14.2/bin/aarch64-onyx-elf-
CC := $(TC)gcc -specs=$(S)/lib/onyx.specs $(CPU) -w -ffunction-sections -fdata-sections
AR := $(TC)ar

AV_SRC := $(addprefix $(AV)/,av_demux.c av_mkv.c av_mp4.c av_riff.c av_flac.c av_mp3.c \
	av_codec.c av_yuv.c av_resample.c av_store.c av_player.c av_opus.c av_vpx.c av_dav1d.c)
AV_CF := -std=gnu99 -O2 -DAV_POSIX -DAV_KAPI_SOUND $(AV_CODECS_CF) -I$(AV) -I$(GLUE)/include \
	-I$(EXTRA)/onyx-libs/i/a/b
GLUE_SRC := $(GLUE)/av_sound_armpi.c $(GLUE)/av_video_armpi.c

AV_OBJ := $(patsubst $(AV)/%.c,$(O)/av/%.o,$(AV_SRC))
GLUE_OBJ := $(patsubst $(GLUE)/%.c,$(O)/glue/%.o,$(GLUE_SRC))
OPUS_OBJ := $(patsubst $(OPUS)/%.c,$(O)/opus/%.o,$(OPUS_SRC))
VPX_OBJ := $(patsubst $(VPX)/%.c,$(O)/vpx/%.o,$(VPX_SRC))
DAV1D_OBJ := $(patsubst $(DAV1D)/%.c,$(O)/dav1d/%.o,$(DAV1D_SRC)) $(patsubst $(DAV1D)/%.S,$(O)/dav1d/%.o,$(DAV1D_ASM))
TMPL_OBJ := $(patsubst $(DAV1D)/%.c,$(O)/dav1d/%.o,$(DAV1D_TMPL_SRC))
CODEC_OBJ := $(OPUS_OBJ) $(VPX_OBJ) $(DAV1D_OBJ) $(TMPL_OBJ)

$(O)/libonyxav.a: $(AV_OBJ) $(GLUE_OBJ) $(CODEC_OBJ)
	rm -f $@
	$(AR) rcs $@ $^

$(O)/av/%.o: $(AV)/%.c
	@mkdir -p $(@D)
	$(CC) $(AV_CF) -MMD -MP -c $< -o $@

$(O)/glue/%.o: $(GLUE)/%.c
	@mkdir -p $(@D)
	$(CC) $(AV_CF) -O2 -MMD -MP -c $< -o $@

$(O)/opus/%.o: $(OPUS)/%.c
	@mkdir -p $(@D)
	$(CC) -O2 $(OPUS_CF) -MMD -MP -c $< -o $@

# (les drapeaux AV_WITH_* sont dans ce fichier : un changement recompile la bibliotheque)
$(AV_OBJ) $(GLUE_OBJ): $(firstword $(MAKEFILE_LIST))

# (libvpx : -O3 comme MediaKit ; les modeles de dav1d en 8 bits)
$(O)/vpx/%.o: $(VPX)/%.c
	@mkdir -p $(@D)
	$(CC) -O3 $(VPX_CF) -MMD -MP -c $< -o $@

$(TMPL_OBJ): $(O)/dav1d/%.o: $(DAV1D)/%.c
	@mkdir -p $(@D)
	$(CC) -O2 -DBITDEPTH=8 $(DAV1D_CF) -MMD -MP -c $< -o $@

$(O)/dav1d/%.o: $(DAV1D)/%.c
	@mkdir -p $(@D)
	$(CC) -O2 $(DAV1D_CF) -MMD -MP -c $< -o $@

$(O)/dav1d/%.o: $(DAV1D)/%.S
	@mkdir -p $(@D)
	$(CC) $(DAV1D_CF) -MMD -MP -c $< -o $@

-include $(AV_OBJ:.o=.d) $(GLUE_OBJ:.o=.d) $(CODEC_OBJ:.o=.d)
