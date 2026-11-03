# Essai de make sous ARMPI (docs/GCC.md) : un petit projet C et C++, plusieurs fichiers
CC = /gcc/bin/gcc
CXX = /gcc/bin/g++
CFLAGS = -O1
OBJDIR = obj
SRC = $(wildcard *.c) $(wildcard *.cpp)
OBJ = $(patsubst %.c,$(OBJDIR)/%.o,$(patsubst %.cpp,$(OBJDIR)/%.o,$(SRC)))

.PHONY: all clean essai

all: $(OBJDIR)/projet

$(OBJDIR)/projet: $(OBJ)
	$(CXX) $(OBJ) -o $@
	@echo lien fait

$(OBJDIR)/%.o: %.c commun.h
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR)/%.o: %.cpp commun.h
	@mkdir -p $(OBJDIR)
	$(CXX) $(CFLAGS) -std=gnu++20 -c $< -o $@

essai: all
	@./$(OBJDIR)/projet > $(OBJDIR)/sortie.txt
	@if grep -q "somme 42" $(OBJDIR)/sortie.txt; then echo PROJET OK; else echo PROJET ECHEC; fi

clean:
	rm -rf $(OBJDIR)
