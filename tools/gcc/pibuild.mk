# pibuild.mk -- la construction d'un programme sous ARMPI (docs/GCC.md), lancee par la commande pibuild dans le dossier
# des sources. Installee en /gcc/share/pibuild.mk.
#   <dossier>.asm                  : assemble par fasmarm
#   main.cpp, main.c, <dossier>.cpp ou <dossier>.c : tous les .c et .cpp du dossier et de ses sous-dossiers, compiles et lies
#   main.bas ou <dossier>.bas      : tous les .bas du dossier et de ses sous-dossiers, compiles et lies par fbc
#   des .cs : compiles par Roslyn (/dotnet) en une DLL, installee comme un programme (AppKit la fait lancer par
#             /dotnet/dotnet) ; ses .ui sont compiles avec elle (uigen). Pour une app, chaque lib/<nom> donne <nom>.dll, a cote de
#             lui aussi (lib/<nom>/refs.txt : les bibliotheques de lib/ dont elle depend) ; sinon, lib/ est fondu dedans
#   un .csproj : c'est lui qui decide (csbuild, docs/DOTNET.md) ; Exe s'installe comme un programme, Library dans /dotnet
# TYPE dit ou il s'installe :
#   app (defaut)  /apps/<nom>.app/main, avec app.txt et les fichiers de data/ s'il y en a
#   bin           /bin/<nom>
#   sys           SD0:/sys/<nom>
#   svc           SD0:/sys/svc/<nom>
#   lib           /dotnet/<nom>.dll : une bibliotheque C# pour tous les programmes (lib/ fondu dedans), sa reference dans
#                 /dotnet/ref/user, que les compilations suivantes voient
NAME ?= $(notdir $(CURDIR))
TYPE ?= app
O := /tmp/pibuild/$(NAME)
SRC := $(notdir $(CURDIR))
MAKEFLAGS += --no-print-directory
ifeq ($(TYPE),app)
DEST := /apps/$(NAME).app
INSTALLED := $(DEST)/main
EXTRA := $(wildcard app.txt data/*)
else ifeq ($(TYPE),bin)
DEST := /bin
else ifeq ($(TYPE),sys)
DEST := SD0:/sys
else ifeq ($(TYPE),svc)
DEST := SD0:/sys/svc
else ifeq ($(TYPE),lib)
DEST := /dotnet
INSTALLED := $(DEST)/$(NAME).dll
else
$(error pibuild : type $(TYPE) inconnu : app, bin, sys, svc ou lib)
endif
INSTALLED ?= $(DEST)/$(NAME)
define INSTALL
@mkdir -p $(DEST)
cp $(O)/main $(INSTALLED)
@$(foreach f,$(EXTRA),cp -r $(f) $(DEST)/$(notdir $(f));) true
@echo Installe : $(INSTALLED)
endef

ifneq ($(wildcard *.csproj),)
all:
	@/dotnet/dotnet /dotnet/csbuild.dll $(if $(filter-out app,$(TYPE)),--type $(TYPE)) . $(NAME)
.PHONY: all
else ifneq ($(shell /bin/find . -name "*.cs"),)
ALLCS := $(patsubst ./%,%,$(shell /bin/find . -name "*.cs"))
UI := $(patsubst ./%,%,$(shell /bin/find . -name "*.ui"))
ifeq ($(TYPE),app)
CS := $(filter-out lib/%,$(ALLCS))
LIBS := $(sort $(foreach f,$(filter lib/%,$(ALLCS)),$(word 2,$(subst /, ,$(f)))))
else
CS := $(ALLCS)
LIBS :=
endif
LIBDLL := $(LIBS:%=$(O)/lib/%.dll)
USERREF := $(filter-out $(foreach l,$(NAME) $(LIBS),/dotnet/ref/user/$(l).dll),$(wildcard /dotnet/ref/user/*.dll))
CSC := /dotnet/dotnet /dotnet/roslyn/csc.dll @/dotnet/csc.rsp -analyzer:/dotnet/roslyn/uigen.dll $(USERREF:%=-r:%)

all: $(O)/main $(LIBDLL)
	$(INSTALL)
	@$(foreach d,$(LIBDLL),cp $(d) $(DEST)/$(notdir $(d));) true
	@$(if $(filter lib,$(TYPE)),mkdir -p /dotnet/ref/user; cp $(O)/main /dotnet/ref/user/$(NAME).dll,true)
.PHONY: all

# (refs.txt lu par findstring : il peut finir ses lignes en CRLF)
define CSLIB
$(1)_TXT := $$(if $$(wildcard lib/$(1)/refs.txt),$$(shell /bin/cat lib/$(1)/refs.txt))
$(1)_REFS := $$(foreach r,$$(filter-out $(1),$$(LIBS)),$$(if $$(findstring $$(r),$$($(1)_TXT)),$(O)/lib/$$(r).dll))
$(1)_UI := $$(filter lib/$(1)/%,$$(UI))
$(O)/lib/$(1).dll: $$(filter lib/$(1)/%,$$(ALLCS)) $$($(1)_UI) $$($(1)_REFS)
	@mkdir -p $(O)/lib
	$$(CSC) -target:library -out:$$@ $$(filter lib/$(1)/%.cs,$$^) $$(patsubst %,-additionalfile:%,$$($(1)_UI)) $$($(1)_REFS:%=-r:%)
endef
$(foreach l,$(LIBS),$(eval $(call CSLIB,$(l))))

ifeq ($(TYPE),app)
MAINUI := $(filter-out lib/%,$(UI))
else
MAINUI := $(UI)
endif

# (une bibliotheque porte son nom, celui que cherchent les programmes qui l'utilisent)
ifeq ($(TYPE),lib)
CSTARGET := library
CSOUT := $(O)/$(NAME).dll
else
CSTARGET := exe
CSOUT := $(O)/main.dll
endif

$(O)/main: $(CS) $(MAINUI) $(LIBDLL)
	@mkdir -p $(O)
	$(CSC) -target:$(CSTARGET) -out:$(CSOUT) $(CS) $(MAINUI:%=-additionalfile:%) $(LIBDLL:%=-r:%)
	cp $(CSOUT) $@
else ifeq ($(TYPE),lib)
$(error pibuild : le type lib demande des .cs)
else ifneq ($(wildcard $(SRC).asm),)
all: $(O)/asm
	$(INSTALL)
.PHONY: all $(O)/asm
$(O)/asm:
	@mkdir -p $(O)
	/bin/fasmarm $(SRC).asm $(O)/main
else ifneq ($(wildcard main.bas $(SRC).bas),)
MAIN := $(firstword $(wildcard main.bas $(SRC).bas))
BAS := $(MAIN) $(filter-out $(MAIN),$(patsubst ./%,%,$(shell /bin/find . -name "*.bas")))
BI := $(patsubst ./%,%,$(shell /bin/find . -name "*.bi"))

all: $(O)/main
	$(INSTALL)
.PHONY: all

# (une seule commande : fbc ne garde les #inclib, donc les kits et fbgfx, que d'une passe a l'autre ; chaque -o met
# l'objet et le C intermediaire dans $(O), pas a cote du source, ou ils ecraseraient un .c du meme nom)
$(O)/main: $(BAS) $(BI)
	@mkdir -p $(sort $(dir $(addprefix $(O)/,$(BAS))))
	/gcc/bin/fbc -O 2 $(foreach f,$(BAS),$(f) -o $(O)/$(f:.bas=.o)) -x $@
	/gcc/bin/strip $@
else
MAIN := $(firstword $(wildcard main.cpp main.c $(SRC).cpp $(SRC).c))
ifeq ($(MAIN),)
$(error pibuild : ni $(SRC).asm, ni main.bas, $(SRC).bas, main.cpp, main.c, $(SRC).cpp, $(SRC).c dans ce dossier)
endif
C_SRC := $(patsubst ./%,%,$(shell /bin/find . -name "*.c"))
CXX_SRC := $(patsubst ./%,%,$(shell /bin/find . -name "*.cpp"))
OBJ := $(addprefix $(O)/,$(C_SRC:.c=.o) $(CXX_SRC:.cpp=.o))
INC := -I. -I/gcc/include -I/gcc/include/kits
FLAGS := -O2 -g0 -ffunction-sections -fdata-sections -MMD -MP $(INC)
LIBS := -L/gcc/lib $(patsubst /gcc/lib/lib%.a,-l%,$(wildcard /gcc/lib/lib*.a))
LD := $(if $(CXX_SRC),/gcc/bin/g++,/gcc/bin/gcc)

all: $(O)/main
	$(INSTALL)
.PHONY: all

$(O)/main: $(OBJ)
	$(LD) -Wl,--gc-sections $(OBJ) $(LIBS) -lm -o $@
	/gcc/bin/strip $@

$(O)/%.o: %.c
	@mkdir -p $(dir $@)
	/gcc/bin/gcc $(FLAGS) -c $< -o $@
$(O)/%.o: %.cpp
	@mkdir -p $(dir $@)
	/gcc/bin/g++ -std=gnu++20 -Wno-write-strings $(FLAGS) -c $< -o $@

-include $(OBJ:.o=.d)
endif
