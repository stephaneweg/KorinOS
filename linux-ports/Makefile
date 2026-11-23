# linux-ports/Makefile -- Koton, Arabesque, Budget mensuel, TerraCube et Forge pour Linux (Fedora) : l'interface sur
# GTK 4, le son sur FluidSynth, le reseau sur OpenSSL, le coffre a secrets sur libsecret, la 3D de TerraCube sur
# OpenGL ES 3 (EGL), la CAO de Forge sur OpenCASCADE et PlaneGCS.
# Les sources des apps restent dans src/apps/<app> (compilees telles quelles) ; ici, les couches qui remplacent les
# kits de Korin (common/) et l'entree de chaque programme (<app>/linux_main.cpp).
#
#   make            -> bin/arabesque, bin/koton, bin/koton-plugins/<id>/, bin/llm, bin/budget, bin/terracube, bin/forge
#   make clean
#
# Dependances (Fedora) : gcc-c++ make pkgconf gtk4-devel fluidsynth-devel openssl-devel libsecret-devel python3
# fluid-soundfont-gm (sans libsecret-devel, Budget se construit sans coffre a secrets : banque.json et le .pem) ;
# TerraCube : libglvnd-devel (EGL, GLES 3), wayland-devel, zlib-devel ; Forge : opencascade-devel eigen3-devel boost-devel

ROOT  := ..
SRC   := $(ROOT)/src
B     := build
GEN   := $(B)/gen
PKGS  := gtk4 fluidsynth gdk-pixbuf-2.0 wayland-client
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

# (la souris prise de TerraCube sous Wayland : deux protocoles, common/wayland/*.xml, compiles par wayland-scanner)
WL_PROTO := relative-pointer-unstable-v1 pointer-constraints-unstable-v1
WL_HDR   := $(patsubst %,$(GEN)/%.h,$(WL_PROTO))
WL_OBJ   := $(patsubst %,$(B)/common/%.o,$(WL_PROTO))
UI_OBJ   := $(B)/common/uikit_gtk.o $(WL_OBJ)
SHIM_OBJ := $(UI_OBJ) $(B)/common/audiokit_fluid.o $(B)/common/korin_paths.o $(B)/common/korin_thread.o $(B)/common/korin_sys.o

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

# ---- Budget mensuel (src/apps/budget) : UIKit sans le son, OpenSSL (la banque), libsecret (le coffre) s'il est la ----
BG      := $(SRC)/apps/budget
SSLCF   := $(shell pkg-config --cflags openssl)
SSLLB   := $(shell pkg-config --libs openssl)
SECRET  := $(shell pkg-config --exists libsecret-1 && echo 1)
ifeq ($(SECRET),1)
SECRETCF := $(shell pkg-config --cflags libsecret-1) -DKPORT_LIBSECRET
SECRETLB := $(shell pkg-config --libs libsecret-1)
endif
BG_F    := $(COMMON) $(APPF) -I$(BG) $(SSLCF)
BG_OBJ  := $(B)/budget/main.o $(B)/budget/linux_main.o $(UI_OBJ) $(B)/common/korin_paths.o \
	$(B)/common/korin_thread.o $(B)/common/korin_sys.o $(B)/common/korin_vault.o
BG_LIBS := $(shell pkg-config --libs gtk4 gdk-pixbuf-2.0 wayland-client) $(SSLLB) $(SECRETLB) -lpthread -ldl -lm

# ---- TerraCube (src/apps/terracube) : OpenGL ES 3 par EGL (common/glkit_egl.cpp), le son par AudioKit (FluidSynth
# pour la sortie), le MCP (json.c de src/lib/acp), le coffre (le jeton du MCP) ; C++ avec exceptions, comme sous Korin
# (tools/pc/apps.sh)
TC      := $(SRC)/apps/terracube
TC_F    := $(OPT) -g0 -pthread -std=gnu++20 $(WARN) -Wno-sign-compare -Wno-unused-result -Wno-class-memaccess -Wno-format-overflow \
	-Icommon -Icommon/include -I$(SRC)/kits/uikit/include -I$(GEN) -I$(ROOT)/tools/mesa/armgl -I$(SRC)/lib/acp -I$(TC)
TC_SRC  := $(wildcard $(TC)/*.cpp)
TC_OBJ  := $(patsubst $(TC)/%.cpp,$(B)/terracube/%.o,$(TC_SRC)) $(B)/terracube/linux_main.o $(B)/acp/json.o
TC_HDR  := $(wildcard $(TC)/*.h) $(ROOT)/tools/mesa/armgl/armgl.h $(ROOT)/tools/mesa/armgl/glview.h
GLLB    := $(shell pkg-config --libs egl glesv2)
ZLB     := $(shell pkg-config --libs zlib)
KITS_OBJ := $(B)/common/glkit_egl.o $(B)/common/kits_misc.o

all: bin/arabesque bin/koton $(PLUG_BIN) bin/llm bin/budget bin/terracube

# les interfaces C des kits, generees de leurs .abi (comme sur Korin : tools/kits/abi2c.py)
$(GEN)/uikit_abi.h: $(SRC)/kits/uikit/uikit.abi $(ROOT)/tools/kits/abi2c.py
	@mkdir -p $(GEN)
	python3 -I $(ROOT)/tools/kits/abi2c.py $< $(GEN) --arch x86_64 \
		--const $(SRC)/inc/armpi.inc $(SRC)/inc/ws_ext.inc $(SRC)/kits/uikit/uikit.inc
	@# (les drapeaux d'ouverture de Korin portent les noms POSIX avec d'autres valeurs : inclus avant <fcntl.h>, ils
	@#  remplaceraient ceux de Linux -- O_NONBLOCK 32 laisse le tube de llm bloquant, O_TRUNC 8 ne tronque pas)
	sed -i -E '/^#ifndef (O_TRUNC|O_APPEND|O_NONBLOCK)$$/,/^#endif$$/d' $@
$(GEN)/audiokit_abi.h: $(SRC)/kits/audiokit/audiokit.abi $(ROOT)/tools/kits/abi2c.py
	@mkdir -p $(GEN)
	python3 -I $(ROOT)/tools/kits/abi2c.py $< $(GEN) --arch x86_64 --const $(SRC)/kits/audiokit/audiokit.inc
$(GEN)/netkit_abi.h: $(SRC)/kits/netkit/netkit.abi $(ROOT)/tools/kits/abi2c.py
	@mkdir -p $(GEN)
	python3 -I $(ROOT)/tools/kits/abi2c.py $< $(GEN) --arch x86_64 --const $(SRC)/kits/netkit/netkit.inc
$(GEN)/archivekit_abi.h: $(SRC)/kits/archivekit/archivekit.abi $(ROOT)/tools/kits/abi2c.py
	@mkdir -p $(GEN)
	python3 -I $(ROOT)/tools/kits/abi2c.py $< $(GEN) --arch x86_64 --const $(SRC)/kits/archivekit/archivekit.inc
$(GEN)/%.h: common/wayland/%.xml
	@mkdir -p $(GEN)
	wayland-scanner client-header $< $@
$(GEN)/%.c: common/wayland/%.xml
	@mkdir -p $(GEN)
	wayland-scanner private-code $< $@
$(B)/common/%.o: $(GEN)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(OPT) -fPIC $(shell pkg-config --cflags wayland-client) -c $< -o $@
HDRS := $(GEN)/uikit_abi.h $(GEN)/audiokit_abi.h $(GEN)/netkit_abi.h $(GEN)/archivekit_abi.h $(WL_HDR) common/korin_port.h common/include/armpi/syscall.h

$(B)/common/%.o: common/%.cpp $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(COMMON) $(SHIMF) -c $< -o $@
$(B)/common/glkit_egl.o: common/glkit_egl.cpp $(HDRS) $(ROOT)/tools/mesa/armgl/armgl.h
	@mkdir -p $(dir $@)
	$(CXX) $(COMMON) $(SHIMF) -I$(ROOT)/tools/mesa/armgl -c $< -o $@

# (le coffre : avec libsecret si libsecret-devel est la ; le .o se refait quand ce choix change)
$(B)/common/korin_vault.o: common/korin_vault.cpp $(HDRS) $(B)/common/.secret-$(or $(SECRET),0)
	@mkdir -p $(dir $@)
	$(CXX) $(COMMON) $(SHIMF) $(SECRETCF) -c $< -o $@
$(B)/common/.secret-%:
	@mkdir -p $(dir $@)
	@rm -f $(B)/common/.secret-*
	@touch $@

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

$(B)/budget/main.o: $(BG)/main.cpp $(wildcard $(BG)/*.h) $(SRC)/apps/pocketqi/http.h $(SRC)/lib/keyvault/keyvault.h $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(BG_F) -Dmain=kport_app_main -c $< -o $@
$(B)/budget/linux_main.o: budget/linux_main.cpp $(SRC)/lib/keyvault/keyvault.h $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(BG_F) -c $< -o $@
bin/budget: $(BG_OBJ)
	@mkdir -p bin
	$(CXX) $(OPT) -pthread $^ $(WRAP) $(BG_LIBS) -o $@
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

# ---- TerraCube ----
$(B)/terracube/main.o: $(TC)/main.cpp $(TC_HDR) $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(TC_F) -Dmain=kport_app_main -c $< -o $@
$(B)/terracube/linux_main.o: terracube/linux_main.cpp $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(TC_F) -c $< -o $@
$(B)/terracube/%.o: $(TC)/%.cpp $(TC_HDR) $(HDRS)
	@mkdir -p $(dir $@)
	$(CXX) $(TC_F) -c $< -o $@
$(B)/acp/json.o: $(SRC)/lib/acp/json.c $(SRC)/lib/acp/json.h
	@mkdir -p $(dir $@)
	$(CC) $(OPT) -g0 -c $< -o $@
bin/terracube: $(TC_OBJ) $(SHIM_OBJ) $(KITS_OBJ) $(B)/common/korin_vault.o
	@mkdir -p bin
	$(CXX) $(OPT) -pthread $^ $(WRAP) $(LIBS) $(SECRETLB) $(GLLB) $(ZLB) -o $@
	strip $@

clean:
	rm -rf $(B)

.PHONY: all clean
