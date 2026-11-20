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

KORIN_TOOLCHAINS ?= /opt/toolchains
TC ?= $(KORIN_TOOLCHAINS)/aarch64-onyx-elf-14.2/bin/aarch64-onyx-elf-
CC := $(TC)gcc -specs=$(S)/lib/onyx.specs $(CPU) -w -ffunction-sections -fdata-sections
AR := $(TC)ar

AV_SRC := $(addprefix $(AV)/,av_demux.c av_mkv.c av_mp4.c av_riff.c av_flac.c av_mp3.c \
	av_codec.c av_yuv.c av_resample.c av_store.c av_player.c av_opus.c av_vpx.c av_dav1d.c av_crypt.c)
AV_CF := -std=gnu99 -O2 -DAV_POSIX -DAV_KAPI_SOUND $(AV_CODECS_CF) -I$(AV) -I$(GLUE)/include \
	-I$(EXTRA)/onyx-libs/i/a/b
GLUE_SRC := $(GLUE)/av_sound_armpi.c $(GLUE)/av_video_armpi.c
# FFmpeg (LGPL, tools/mediakit/ffmpeg.sh FLAVOR=jet) quand il est construit : H.264, H.265, AAC, Vorbis... par
# av_ffmpeg.c, et les conteneurs AVI, MPEG-TS/PS, FLV, Ogg par av_lavf.c (libavformat sur un fil) ;
# build-media.sh et build-jet.sh lient ses bibliotheques (FF_LIBS) apres libonyxav.a
FF ?=
ifneq ($(and $(FF),$(wildcard $(FF)/libavcodec.a)),)
AV_SRC += $(AV)/av_ffmpeg.c $(AV)/av_lavf.c
AV_CF += -DAV_WITH_FFMPEG -I$(FF)/include
endif
# (av_ffmpeg.c d'Onyx donne _stat et _times a la newlib nue d'Onyx ; la libposix de Korin les a : les siens renommes)
$(O)/av/av_ffmpeg.o: AV_CF += -D_stat=av__onyx_stat_unused -D_times=av__onyx_times_unused
# (Korin PC : les decodeurs video sur plusieurs fils, par -Wl,--wrap a l'edition de liens : glue/av_threads_pc.c)
ifeq ($(AV_ARCH),generic)
GLUE_SRC += $(GLUE)/av_threads_pc.c
endif
# (Korin PC : YUV -> RGB en SSE2, glue/av_yuv_x86.c ; av_yuv.c sous un autre nom pour le reste)
ifeq ($(AV_ARCH),generic)
YUV_X86 := $(GLUE)/av_yuv_x86.c
YUV_CF := -Dav_yuv_to_rgb=av__yuv_to_rgb_portable
endif

AV_OBJ := $(patsubst $(AV)/%.c,$(O)/av/%.o,$(AV_SRC))
GLUE_OBJ := $(patsubst $(GLUE)/%.c,$(O)/glue/%.o,$(GLUE_SRC) $(YUV_X86))
OPUS_OBJ := $(patsubst $(OPUS)/%.c,$(O)/opus/%.o,$(OPUS_SRC))
VPX_OBJ := $(patsubst $(VPX)/%.c,$(O)/vpx/%.o,$(VPX_SRC))
DAV1D_OBJ := $(patsubst $(DAV1D)/%.c,$(O)/dav1d/%.o,$(DAV1D_SRC)) $(patsubst $(DAV1D)/%.S,$(O)/dav1d/%.o,$(DAV1D_ASM))
TMPL_OBJ := $(patsubst $(DAV1D)/%.c,$(O)/dav1d/%.o,$(DAV1D_TMPL_SRC))
CODEC_OBJ := $(OPUS_OBJ) $(VPX_OBJ) $(DAV1D_OBJ) $(TMPL_OBJ)

$(O)/libonyxav.a: $(AV_OBJ) $(GLUE_OBJ) $(CODEC_OBJ)
	rm -f $@
	$(AR) rcs $@ $^

$(O)/av/av_yuv.o: AV_CF += $(YUV_CF)
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
