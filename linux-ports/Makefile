# linux-ports/Makefile -- Koton et Arabesque pour Linux (Fedora) : l'interface sur GTK 4, le son sur FluidSynth.
# Les sources des apps restent dans src/apps/<app> (compilees telles quelles) ; ici, les couches qui remplacent les
# kits de Korin (common/) et l'entree de chaque programme (<app>/linux_main.cpp).
#
#   make            -> bin/arabesque, bin/koton, bin/koton-plugins/<id>/, bin/llm
#   make clean
#
# Dependances (Fedora) : gcc-c++ make pkgconf gtk4-devel fluidsynth-devel openssl-devel python3 fluid-soundfont-gm

ROOT  := ..
SRC   := $(ROOT)/src
B     := build
GEN   := $(B)/gen
PKGS  := gtk4 fluidsynth gdk-pixbuf-2.0
PKGCF := $(shell pkg-config --cflags $(PKGS))
PKGLB := $(shell pkg-config --libs $(PKGS))

CXX      ?= g++
OPT      ?= -O2
WARN     := -Wall -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable -Wno-write-strings -Wno-comment -Wno-reorder -Wno-format-truncation -Wno-deprecated-declarations
COMMON   := $(OPT) -g0 -pthread -Icommon -Icommon/include -I$(SRC)/kits/uikit/include -I$(GEN)
# (les apps de Korin : sans exceptions ni RTTI, comme tools/webkit/build-koton.sh et tools/office/build.sh)
APPF     := -std=gnu++17 -fno-exceptions -fno-rtti -fno-threadsafe-statics $(WARN) -Wno-sign-compare -Wno-class-memaccess \
	-Wno-format-truncation -Wno-stringop-truncation -Wno-misleading-indentation -Wno-unused-result
SHIMF    := -std=gnu++17 $(WARN) $(PKGCF)
WRAP     := -Wl,--wrap=open,--wrap=fopen,--wrap=opendir,--wrap=readdir,--wrap=closedir,--wrap=mkdir,--wrap=stat,--wrap=access,--wrap=unlink,--wrap=remove,--wrap=rename,--wrap=posix_spawn,--wrap=pthread_create
LIBS     := $(PKGLB) -lpthread -ldl -lm

SHIM_OBJ := $(B)/common/uikit_gtk.o $(B)/common/audiokit_fluid.o $(B)/common/korin_paths.o $(B)/common/korin_thread.o

# ---- Arabesque (src/apps/arabesque) : le moteur se compile sans FMA, memes bits partout (tools/office/build.sh) ----
AR      := $(SRC)/apps/arabesque
AR_F    := $(COMMON) $(APPF) -ffp-contract=off -I$(AR)
AR_OBJ  := $(B)/arabesque/main.o $(B)/arabesque/linux_main.o

# ---- Koton (src/apps/koton) ----
KT      := $(SRC)/apps/koton
KT_F    := $(COMMON) $(APPF) -I$(KT) -I$(KT)/ui
KT_SRC  := $(wildcard $(KT)/engine/*.cpp) $(wildcard $(KT)/ui/*.cpp) $(KT)/plug/plughost.cpp $(KT)/plug/plugctx.cpp
KT_OBJ  := $(patsubst $(KT)/%.cpp,$(B)/koton/%.o,$(KT_SRC)) $(B)/koton/main.o $(B)/koton/linux_main.o
KT_HDR  := $(wildcard $(KT)/*.h $(KT)/ui/*.h $(KT)/ui/*.hpp $(KT)/engine/*.h $(KT)/plug/*.h)
PLUGINS := $(notdir $(wildcard $(KT)/plugins/*))
PLUG_BIN := $(foreach p,$(PLUGINS),bin/koton-plugins/$(p)/main bin/koton-plugins/$(p)/plugin.json)

all: bin/arabesque bin/koton $(PLUG_BIN) bin/llm

# les interfaces C des kits, generees de leurs .abi (comme sur Korin : tools/kits/abi2c.py)
$(GEN)/uikit_abi.h: $(SRC)/kits/uikit/uikit.abi $(ROOT)/tools/kits/abi2c.py
	@mkdir -p $(GEN)
	python3 -I $(ROOT)/tools/kits/abi2c.py $< $(GEN) --arch x86_64 \
		--const $(SRC)/inc/armpi.inc $(SRC)/inc/ws_ext.inc $(SRC)/kits/uikit/uikit.inc
$(GEN)/audiokit_abi.h: $(SRC)/kits/audiokit/audiokit.abi $(ROOT)/tools/kits/abi2c.py
	@mkdir -p $(GEN)
	python3 -I $(ROOT)/tools/kits/abi2c.py $< $(GEN) --arch x86_64 --const $(SRC)/kits/audiokit/audiokit.inc
HDRS := $(GEN)/uikit_abi.h $(GEN)/audiokit_abi.h common/korin_port.h

$(B)/common/%.o: common/%.cpp $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(COMMON) $(SHIMF) -c $< -o $@

$(B)/arabesque/main.o: $(AR)/main.cpp $(wildcard $(AR)/*.h) $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(AR_F) -Dmain=kport_app_main -c $< -o $@
$(B)/arabesque/linux_main.o: arabesque/linux_main.cpp $(wildcard $(AR)/*.h) $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(AR_F) -c $< -o $@
bin/arabesque: $(AR_OBJ) $(SHIM_OBJ)
	@mkdir -p bin
	$(CXX) $(OPT) -pthread $^ $(WRAP) $(LIBS) -o $@
	strip $@

$(B)/koton/main.o: $(KT)/main.cpp $(KT_HDR) $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(KT_F) -Dmain=kport_app_main -c $< -o $@
$(B)/koton/linux_main.o: koton/linux_main.cpp $(KT_HDR) $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(KT_F) -c $< -o $@
$(B)/koton/%.o: $(KT)/%.cpp $(KT_HDR) $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(KT_F) -c $< -o $@
bin/koton: $(KT_OBJ) $(SHIM_OBJ)
	@mkdir -p bin
	$(CXX) $(OPT) -pthread $^ $(WRAP) $(LIBS) -o $@
	strip $@

# les greffons de Koton : un programme chacun, POSIX seul (plug/kplug.h), dans bin/koton-plugins/<id>/ (le dossier
# /apps/koton.app/plugins de Korin : korin_paths.cpp)
bin/koton-plugins/%/main: $(KT)/plugins/%/main.cpp $(KT_HDR) $(B)/common/korin_thread.o
	@mkdir -p $(dir $@)
	$(CXX) $(OPT) -g0 -pthread -std=gnu++17 -fno-exceptions -fno-rtti $(WARN) -Wno-sign-compare -I$(KT) -I$(KT)/ui -I$(KT)/plug $< \
		$(B)/common/korin_thread.o -Wl,--wrap=pthread_create -lm -o $@
	strip $@
bin/koton-plugins/%/plugin.json: $(KT)/plugins/%/plugin.json
	@mkdir -p $(dir $@)
	cp $< $@

# llm (src/bin/llm) : l'aide reseau de « Composer avec l'IA » de Koton, sur OpenSSL (/bin/llm de Korin -> bin/llm)
bin/llm: $(SRC)/bin/llm/main.cpp
	@mkdir -p bin
	$(CXX) $(OPT) -g0 -std=gnu++17 -fno-exceptions -fno-rtti $(WARN) -I$(KT)/ui $< $(shell pkg-config --cflags --libs openssl) -o $@
	strip $@

clean:
	rm -rf $(B)

.PHONY: all clean
