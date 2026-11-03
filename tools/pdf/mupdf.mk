# mupdf.mk -- MuPDF (ses parties fitz + pdf) pour le Lecteur PDF, en une bibliotheque statique : $(OUT)/libmupdf.a.
# Lance par tools/pdf/build.sh ; repris de user/Apps/pdf/mupdf.mk d'Onyx.
#
# Laisse de cote (comme Onyx, voir src/apps/pdf/README.md) : les autres formats (XPS, EPUB, HTML, SVG, images...),
# JavaScript, la gestion de couleur ICC (lcms2), les polices Noto et CJK (TOFU : les 14 polices standard restent), les
# codes-barres, l'OCR, les traceurs CMYK et separation. Ses bibliotheques : jbig2dec, openjpeg (de l'archive de MuPDF),
# zlib, libjpeg (jpeg-9f) et FreeType (2.14.3, avec les pilotes CFF, Type 1 et CID des polices PDF).

MU := $(EXTRA)/mupdf-1.28.5
MU_FT := $(EXTRA)/freetype-2.14.3
MU_JPEG := $(EXTRA)/jpeg-9f
MU_ZLIB := $(EXTRA)/zlib-1.3.1

MU_DEFS := -DFZ_ENABLE_XPS=0 -DFZ_ENABLE_SVG=0 -DFZ_ENABLE_CBZ=0 -DFZ_ENABLE_IMG=0 -DFZ_ENABLE_HTML=0 \
	-DFZ_ENABLE_FB2=0 -DFZ_ENABLE_MOBI=0 -DFZ_ENABLE_EPUB=0 -DFZ_ENABLE_OFFICE=0 -DFZ_ENABLE_TXT=0 \
	-DFZ_ENABLE_HTML_ENGINE=0 -DFZ_ENABLE_OCR_OUTPUT=0 -DFZ_ENABLE_DOCX_OUTPUT=0 -DFZ_ENABLE_ODT_OUTPUT=0 \
	-DFZ_ENABLE_ICC=0 -DFZ_ENABLE_BROTLI=0 -DFZ_ENABLE_JS=0 -DFZ_ENABLE_BARCODE=0 -DFZ_ENABLE_HYPHEN=0 -DFZ_ENABLE_MD=0 \
	-DFZ_ENABLE_SPOT_RENDERING=0 -DFZ_PLOTTERS_CMYK=0 -DFZ_PLOTTERS_N=0 \
	-DTOFU -DTOFU_CJK -DTOFU_SIL -DTOFU_EMOJI -DTOFU_HISTORIC -DTOFU_SYMBOL -DFZ_HIDE_INTERNAL_JPEG \
	-DMEMENTO_SQUEEZEBUILD=0 -DHAVE_SIGSETJMP=0 -include $(CONF)/armpi_mucompat.h
MU_INC := -I$(MU)/include -I$(CONF) -I$(MU)/scripts/libjpeg -I$(MU_JPEG) -I$(MU_ZLIB) -I$(MU_FT)/include \
	-I$(MU)/thirdparty/jbig2dec -I$(MU)/thirdparty/openjpeg/src/lib/openjp2 \
	-DOPJ_STATIC -DOPJ_HAVE_INTTYPES_H -DOPJ_HAVE_STDINT_H -DHAVE_STDINT_H
MU_FTDEFS := -DFT2_BUILD_LIBRARY '-DFT_CONFIG_MODULES_H=<armpi_muftmodule.h>' '-DFT_CONFIG_OPTIONS_H=<armpi_ftoption.h>'

MU_SRC := $(sort $(wildcard $(MU)/source/fitz/*.c)) $(sort $(wildcard $(MU)/source/pdf/*.c)) \
	$(wildcard $(MU)/generated/resources/fonts/urw/*.c)
MU_JBIG2 := $(addprefix $(MU)/thirdparty/jbig2dec/,jbig2.c jbig2_arith.c jbig2_arith_iaid.c jbig2_arith_int.c \
	jbig2_generic.c jbig2_halftone.c jbig2_huffman.c jbig2_hufftab.c jbig2_image.c jbig2_mmr.c jbig2_page.c \
	jbig2_refinement.c jbig2_segment.c jbig2_symbol_dict.c jbig2_text.c)
MU_OPJ := $(addprefix $(MU)/thirdparty/openjpeg/src/lib/openjp2/,bio.c cio.c dwt.c event.c function_list.c \
	ht_dec.c image.c invert.c j2k.c jp2.c mct.c mqc.c openjpeg.c pi.c sparse_array.c t1.c t2.c tcd.c tgt.c thread.c)
MU_JPG := $(addprefix $(MU_JPEG)/,jaricom.c jcomapi.c jdapimin.c jdapistd.c jdarith.c jdatasrc.c jdcoefct.c \
	jdcolor.c jddctmgr.c jdhuff.c jdinput.c jdmainct.c jdmarker.c jdmaster.c jdmerge.c jdpostct.c jdsample.c \
	jdtrans.c jerror.c jidctflt.c jidctfst.c jidctint.c jmemmgr.c jquant1.c jquant2.c jutils.c \
	jcapimin.c jcapistd.c jcarith.c jccoefct.c jccolor.c jcdctmgr.c jchuff.c jcinit.c jcmainct.c jcmarker.c \
	jcmaster.c jcparam.c jcprepct.c jcsample.c jdatadst.c jfdctflt.c jfdctfst.c jfdctint.c)
MU_Z := $(addprefix $(MU_ZLIB)/,adler32.c crc32.c deflate.c inflate.c inffast.c inftrees.c trees.c zutil.c \
	compress.c uncompr.c)
MU_FTSRC := $(addprefix $(MU_FT)/src/,base/ftsystem.c base/ftinit.c base/ftdebug.c base/ftbase.c base/ftbitmap.c \
	base/ftsynth.c base/ftbbox.c base/ftglyph.c base/ftstroke.c base/fttype1.c base/ftfstype.c base/ftgasp.c \
	autofit/autofit.c truetype/truetype.c sfnt/sfnt.c smooth/smooth.c raster/raster.c cff/cff.c \
	cid/type1cid.c psaux/psaux.c pshinter/pshinter.c psnames/psnames.c type1/type1.c)

o = $(OUT)/obj/$(1)/$(basename $(notdir $(2))).o
MU_OBJ := $(foreach f,$(MU_SRC),$(call o,mu,$(f))) $(foreach f,$(MU_JBIG2),$(call o,jbig2,$(f))) \
	$(foreach f,$(MU_OPJ),$(call o,opj,$(f))) $(foreach f,$(MU_JPG),$(call o,jpeg,$(f))) \
	$(foreach f,$(MU_Z),$(call o,z,$(f))) $(foreach f,$(MU_FTSRC),$(call o,ft,$(f)))

$(OUT)/libmupdf.a: $(MU_OBJ)
	@echo "  AR      $@"
	@rm -f $@
	@$(AR) rcs $@ $^

define mu_rule
$(call o,$(1),$(2)): $(2)
	@mkdir -p $$(dir $$@)
	@echo "  CC      $$(notdir $$<)"
	@$$(CC) $$(CFLAGS) -w $(3) -c $$< -o $$@
endef
$(foreach f,$(MU_SRC),$(eval $(call mu_rule,mu,$(f),$$(MU_DEFS) $$(MU_INC))))
$(foreach f,$(MU_JBIG2),$(eval $(call mu_rule,jbig2,$(f),-I$$(MU)/include -DHAVE_STDINT_H '-DJBIG_EXTERNAL_MEMENTO_H="mupdf/memento.h"')))
$(foreach f,$(MU_OPJ),$(eval $(call mu_rule,opj,$(f),-DOPJ_STATIC -DOPJ_HAVE_INTTYPES_H -DOPJ_HAVE_STDINT_H -DMUTEX_pthread=0 -I$$(MU)/thirdparty/openjpeg/src/lib/openjp2)))
$(foreach f,$(MU_JPG),$(eval $(call mu_rule,jpeg,$(f),-DFZ_HIDE_INTERNAL_JPEG -I$$(MU)/scripts/libjpeg -I$$(MU_JPEG))))
$(foreach f,$(MU_Z),$(eval $(call mu_rule,z,$(f),-I$$(MU_ZLIB))))
$(foreach f,$(MU_FTSRC),$(eval $(call mu_rule,ft,$(f),$$(MU_FTDEFS) -I$$(CONF) -I$$(MU_FT)/include)))
