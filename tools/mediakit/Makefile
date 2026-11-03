#
# tools/mediakit/Makefile -- MediaKit (lance par build.sh, dans WSL) : la bibliotheque av
# d'Onyx (av/, copie changee), libvpx 1.15.2, dav1d 1.5.1 (sources copiees d'Onyx dans
# src/extra : EXTRA) ; le son par AudioKit (av/av_audiokit.c) ; la glue (mediakit.c, mk_libc.c,
# mk_math.c, mediakit.S) ;
# memes drapeaux que codecs.mk d'Onyx (user/Libs/av), plus le code independant de la position.
#
TC    ?= /opt/toolchains/aarch64-onyx-elf-14.2/bin/aarch64-onyx-elf-
CC    := $(TC)gcc
EXTRA ?= $(abspath $(dir $(lastword $(MAKEFILE_LIST)))../../src/extra)
OUT   ?= /tmp/armpi-mediakit
HERE  := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))

VPX   := $(EXTRA)/libvpx-1.15.2
DAV1D := $(EXTRA)/dav1d-1.5.1

BASE := -mcpu=cortex-a72 -fPIE -fvisibility=hidden -fno-stack-protector -ffunction-sections \
	-fdata-sections -fno-asynchronous-unwind-tables -fno-unwind-tables -mno-outline-atomics \
	-fno-math-errno -include $(HERE)/hidden.h -DNDEBUG -w

# ---- libvpx (codecs.mk : AV_ARCH aarch64)
VPX_SRC := $(shell find $(VPX) -name '*.c' -not -path '$(VPX)/onyx/*' | sort) $(VPX)/onyx/arm64/vpx_config.c
VPX_CF  := -O3 -std=gnu99 -I$(VPX)/onyx/arm64 -I$(VPX)/onyx -I$(VPX)
# ---- dav1d
DAV1D_TMPL := $(wildcard $(DAV1D)/src/*_tmpl.c)
DAV1D_SRC  := $(filter-out $(DAV1D_TMPL),$(wildcard $(DAV1D)/src/*.c)) $(DAV1D)/src/arm/cpu.c $(DAV1D)/onyx/onyx_sysconf.c
DAV1D_ASM  := $(addprefix $(DAV1D)/src/arm/64/,itx.S looprestoration_common.S msac.S refmvs.S \
	cdef.S filmgrain.S ipred.S loopfilter.S looprestoration.S mc.S mc_dotprod.S)
DAV1D_CF   := -O2 -std=gnu11 -D__ONYX_DAV1D_PI -I$(DAV1D)/onyx/pthread -I$(DAV1D)/onyx -I$(DAV1D) -I$(DAV1D)/include
# ---- av (copie d'Onyx) et la glue
AV_SRC := $(addprefix $(HERE)/av/,av_demux.c av_mkv.c av_mp4.c av_riff.c av_codec.c av_yuv.c \
	av_resample.c av_vpx.c av_dav1d.c av_audiokit.c)
AV_CF  := -O2 -std=gnu99 -DMK_ARMPI -DAV_WITH_VPX -DAV_WITH_DAV1D -I$(HERE)/av \
	-I$(VPX) -I$(DAV1D)/include
GLUE_CF := -O2 -std=gnu11 -DMK_ARMPI -I$(HERE)

obj = $(patsubst /%,$(OUT)/obj/%.o,$(1))
VPX_OBJ   := $(call obj,$(VPX_SRC))
DAV1D_OBJ := $(call obj,$(DAV1D_SRC) $(DAV1D_ASM))
TMPL_OBJ  := $(call obj,$(DAV1D_TMPL))
AV_OBJ    := $(call obj,$(AV_SRC))
GLUE_OBJ  := $(OUT)/mediakit_table.o $(OUT)/mediakit.o $(OUT)/mk_libc.o $(OUT)/mk_math.o
ALL_OBJ   := $(GLUE_OBJ) $(AV_OBJ) $(VPX_OBJ) $(DAV1D_OBJ) $(TMPL_OBJ)

$(VPX_OBJ): CF := $(VPX_CF)
$(DAV1D_OBJ): CF := $(DAV1D_CF)
$(TMPL_OBJ): CF := -DBITDEPTH=8 $(DAV1D_CF)
$(AV_OBJ): CF := $(AV_CF)

all: $(OUT)/mediakit.elf

$(OUT)/obj/%.c.o: /%.c
	@mkdir -p $(@D)
	@$(CC) $(BASE) $(CF) -MMD -MP -MF $@.d -c $< -o $@
$(OUT)/obj/%.S.o: /%.S
	@mkdir -p $(@D)
	@$(CC) -mcpu=cortex-a72 -fPIE $(CF) -MMD -MP -MF $@.d -c $< -o $@

$(OUT)/mediakit.o: $(HERE)/mediakit.c $(HERE)/mk_sys.h $(HERE)/av/av.h
	$(CC) $(BASE) $(GLUE_CF) -c $< -o $@
$(OUT)/mk_libc.o: $(HERE)/mk_libc.c $(HERE)/mk_sys.h
	$(CC) $(BASE) $(GLUE_CF) -fno-builtin -fno-tree-loop-distribute-patterns -c $< -o $@
$(OUT)/mk_math.o: $(HERE)/mk_math.c
	$(CC) $(BASE) $(GLUE_CF) -fno-builtin -c $< -o $@
$(OUT)/mediakit_table.o: $(HERE)/mediakit.S
	$(CC) -c $< -o $@

$(OUT)/mediakit.elf: $(ALL_OBJ) $(HERE)/mediakit.ld
	$(CC) -nostdlib -pie -Wl,--no-dynamic-linker -Wl,-T,$(HERE)/mediakit.ld \
		-Wl,--gc-sections -Wl,--hash-style=sysv -Wl,--unresolved-symbols=report-all \
		-o $@ $(ALL_OBJ) -lgcc

-include $(wildcard $(patsubst %.o,%.o.d,$(VPX_OBJ) $(DAV1D_OBJ) $(TMPL_OBJ) $(AV_OBJ)))
