
# ArchiLudo
# Ludo game for the Acorn Archimedes (RISC OS 3.10, WIMP)
# Written in 2026 by Xander Mol

# Cross-platform shell detection
ifneq ($(shell echo),)
  CMD_EXE = 1
endif

ifdef CMD_EXE
  NULLDEV = nul:
  DEL     = -del /f
  RMDIR   = rmdir /s /q
  MKDIR   = mkdir
  CPDIR   = xcopy /E /I /Y
else
  NULLDEV = /dev/null
  DEL     = $(RM)
  RMDIR   = $(RM) -r
  MKDIR   = mkdir -p
  CPDIR   = cp -r
endif

# Toolchain + deployment paths (see .env.example)
-include .env
ARCULATOR_HOSTFS ?= <set_ARCULATOR_HOSTFS_in_.env>

# `test` builds and runs the game logic unit tests with the HOST compiler --
# it needs no ArchieSDK, no Arculator, no RISC OS at all (that's the whole
# point of keeping src/game_logic.c free of OSLib/WIMP dependencies). Only
# require ArchieSDK for goals that actually cross-compile for RISC OS.
GOALS := $(or $(MAKECMDGOALS),all)
NEEDS_ARCHIESDK := $(filter-out test clean,$(GOALS))

ifneq ($(NEEDS_ARCHIESDK),)
ARCHIESDK ?= <set_ARCHIESDK_in_.env>
ifeq ($(strip $(ARCHIESDK)),<set_ARCHIESDK_in_.env>)
$(error ARCHIESDK not set -- copy .env.example to .env and set ARCHIESDK)
endif
include $(ARCHIESDK)/config.mk
endif

HOSTCC ?= cc

# Application name
APPNAME = ArchiLudo

# Build versioning
VERSION_MAJOR     = 0
VERSION_MINOR     = 1
VERSION_PATCH     = 0
VERSION_TIMESTAMP = $(shell date "+%Y%m%d-%H%M")
VERSION           = v$(VERSION_MAJOR).$(VERSION_MINOR).$(VERSION_PATCH)-$(VERSION_TIMESTAMP)

# Compile flags. config.mk sets CFLAGS -- always append with += (see its own comment).
#   -Iinclude       : project headers
#   -MMD -MP        : automatic per-object dependency generation (build/*.d)
#   -DVERSION       : pass version string to source
CFLAGS += -Iinclude \
          -MMD -MP \
          -DVERSION="\"$(VERSION)\""

# Sources: every .c under src/, one .o per source under build/
LIBS = -lOSLib32

SRCFILES = $(wildcard src/*.c)
OBJFILES = $(patsubst src/%.c,build/%.o,$(SRCFILES))
DEPFILES = $(OBJFILES:.o=.d)

ELF      = build/$(APPNAME).elf
ZIPFILE  = build/$(APPNAME)-$(VERSION).zip

# Proper application directory (build/!ArchiLudo) rather than a flat
# ArchiLudo,ff8 file directly in hostfs -- see docs/BUILDCHAIN.md's
# "Application directory" section and app/!Run's own doc comment for the
# full writeup (follows Steve Fryatt's wimp-prog tutorial, Chapter 17).
# The leading "!" is an ordinary filename character to Make itself (no
# special meaning in a target/prerequisite name) and to both Linux and
# Windows filesystems -- recipe lines below still quote paths for the
# shell as usual, but target/prerequisite references themselves are
# never quoted, since Make treats quote characters as literal parts of
# the name rather than shell-style delimiters.
APPDIR    = build/!$(APPNAME)
RUNIMAGE  = $(APPDIR)/!RunImage,ff8
APPFILES  = $(RUNIMAGE) $(APPDIR)/!Run,feb $(APPDIR)/!Sprites,ff9 \
            $(APPDIR)/!Sprites22,ff9 $(APPDIR)/PawnSprites,ff9

TEST_BINS = build/test_game_logic build/test_board_layout build/test_ai

.SUFFIXES:
.PHONY: all clean asm zip docs check-hostfs deploy test assets export-sprites import-sprites

all: $(APPFILES)

$(RUNIMAGE): $(ELF) | $(APPDIR)
	$(ARCHIEOBJCOPY) -O binary $< $@

$(ELF): $(OBJFILES)
	$(ARCHIECC) $(CFLAGS) -o $@ $(OBJFILES) $(LIBS)

build/%.o: src/%.c | build
	$(ARCHIECC) $(CFLAGS) -c $< -o $@

# Static application-directory files -- checked into the repo (app/!Run)
# or pre-built and checked in (assets/!Sprites, assets/!Sprites22,
# assets/PawnSprites -- see the `assets` target to regenerate them),
# just copied into place here with their RISC OS filetype suffix added,
# matching how PawnSprites,ff9 was already handled before this app-
# directory restructuring.
$(APPDIR)/!Run,feb: app/!Run | $(APPDIR)
	cp "$<" "$@"

$(APPDIR)/!Sprites,ff9: assets/!Sprites | $(APPDIR)
	cp "$<" "$@"

$(APPDIR)/!Sprites22,ff9: assets/!Sprites22 | $(APPDIR)
	cp "$<" "$@"

$(APPDIR)/PawnSprites,ff9: assets/PawnSprites | $(APPDIR)
	cp "$<" "$@"

build:
	@$(MKDIR) build 2>$(NULLDEV) ; true

$(APPDIR): | build
	@$(MKDIR) "$(APPDIR)" 2>$(NULLDEV) ; true

-include $(DEPFILES)

asm: $(SRCFILES)
	$(ARCHIECC) $(CFLAGS) -S $(SRCFILES)

clean:
	$(RMDIR) build 2>$(NULLDEV) ; true

check-hostfs:
	@test -d "$(ARCULATOR_HOSTFS)" || \
		(echo "ERROR: Arculator hostfs not found at $(ARCULATOR_HOSTFS) -- check ARCULATOR_HOSTFS in .env" && false)

deploy: check-hostfs $(APPFILES)
	@$(MKDIR) "$(ARCULATOR_HOSTFS)/!$(APPNAME)" 2>$(NULLDEV) ; true
	# Trailing "/." on the source copies its CONTENTS into an
	# already-existing destination -- plain `cp -r SRC DEST` would nest
	# a second !ArchiLudo one level too deep on every deploy after the
	# first, since DEST already exists as a directory from the previous
	# run (the classic cp -r gotcha).
	$(CPDIR) "$(APPDIR)/." "$(ARCULATOR_HOSTFS)/!$(APPNAME)/"
	# Pre-app-directory deploys (round 7.17 through 7.34) left a flat
	# ArchiLudo,ff8/PawnSprites,ff9 directly in hostfs -- remove any
	# stale copies so they can't be mistaken for what the Filer/iconbar
	# actually runs now (the app directory above).
	rm -f "$(ARCULATOR_HOSTFS)/$(APPNAME),ff8" "$(ARCULATOR_HOSTFS)/PawnSprites,ff9" \
	      "$(ARCULATOR_HOSTFS)/Sprites,ff9"

zip: $(APPFILES)
	$(ARCHIEZIP) -r $(ZIPFILE) "$(APPDIR)" README.md

assets:
	python3 assets/generate_placeholder_art.py
	python3 assets/generate_icon_sprites.py
	python3 assets/generate_app_icon.py

# Hand pixel-editing round-trip (assets/edit/) -- see
# assets/export_sprites_for_editing.py's own doc comment and
# assets/edit/README.md (written by export-sprites) for the full
# workflow. Not part of `assets`/`all` -- export-sprites is a one-off
# setup step before editing, import-sprites is what actually needs
# re-running (and re-deploying) after each edit.
export-sprites:
	python3 assets/export_sprites_for_editing.py

import-sprites:
	python3 assets/import_edited_sprites.py

test: $(TEST_BINS)
	@for t in $(TEST_BINS); do echo "=== $$t ==="; ./$$t || exit 1; done

build/test_game_logic: tests/test_game_logic.c src/game_logic.c include/game_logic.h | build
	$(HOSTCC) -Wall -Wextra -std=c99 -Iinclude -o $@ tests/test_game_logic.c src/game_logic.c

build/test_board_layout: tests/test_board_layout.c src/board_layout.c src/game_logic.c include/board_layout.h include/game_logic.h | build
	$(HOSTCC) -Wall -Wextra -std=c99 -Iinclude -o $@ tests/test_board_layout.c src/board_layout.c src/game_logic.c

build/test_ai: tests/test_ai.c src/ai.c src/game_logic.c include/ai.h include/game_logic.h | build
	$(HOSTCC) -Wall -Wextra -std=c99 -Iinclude -o $@ tests/test_ai.c src/ai.c src/game_logic.c

docs: README.pdf

README.pdf: README.md
	@if which pandoc >$(NULLDEV) 2>&1; then \
		pandoc README.md -o README.pdf; \
	else \
		echo "WARNING: pandoc not found -- README.pdf not updated"; \
	fi
