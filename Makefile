
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
else
  NULLDEV = /dev/null
  DEL     = $(RM)
  RMDIR   = $(RM) -r
  MKDIR   = mkdir -p
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
TARGET   = build/$(APPNAME),ff8
ZIPFILE  = build/$(APPNAME)-$(VERSION).zip

TEST_BINS = build/test_game_logic build/test_board_layout

.SUFFIXES:
.PHONY: all clean asm zip docs check-hostfs deploy test assets

all: $(TARGET)

$(TARGET): $(ELF)
	$(ARCHIEOBJCOPY) -O binary $< $@

$(ELF): $(OBJFILES)
	$(ARCHIECC) $(CFLAGS) -o $@ $(OBJFILES) $(LIBS)

build/%.o: src/%.c | build
	$(ARCHIECC) $(CFLAGS) -c $< -o $@

build:
	@$(MKDIR) build 2>$(NULLDEV) ; true

-include $(DEPFILES)

asm: $(SRCFILES)
	$(ARCHIECC) $(CFLAGS) -S $(SRCFILES)

clean:
	$(RMDIR) build 2>$(NULLDEV) ; true

check-hostfs:
	@test -d "$(ARCULATOR_HOSTFS)" || \
		(echo "ERROR: Arculator hostfs not found at $(ARCULATOR_HOSTFS) -- check ARCULATOR_HOSTFS in .env" && false)

deploy: check-hostfs $(TARGET)
	cp $(TARGET) "$(ARCULATOR_HOSTFS)/"
	# Round 6.3 dropped sprite plotting entirely (see src/game_view.c's
	# plot_pawn() doc comment) -- remove any stale Sprites,ff9 left over
	# from an earlier deploy so it can't be mistaken for something the
	# running game still reads.
	rm -f "$(ARCULATOR_HOSTFS)/Sprites,ff9"

zip: $(TARGET)
	$(ARCHIEZIP) -r $(ZIPFILE) $(TARGET) README.md

assets:
	python3 assets/generate_placeholder_art.py

test: $(TEST_BINS)
	@for t in $(TEST_BINS); do echo "=== $$t ==="; ./$$t || exit 1; done

build/test_game_logic: tests/test_game_logic.c src/game_logic.c include/game_logic.h | build
	$(HOSTCC) -Wall -Wextra -std=c99 -Iinclude -o $@ tests/test_game_logic.c src/game_logic.c

build/test_board_layout: tests/test_board_layout.c src/board_layout.c src/game_logic.c include/board_layout.h include/game_logic.h | build
	$(HOSTCC) -Wall -Wextra -std=c99 -Iinclude -o $@ tests/test_board_layout.c src/board_layout.c src/game_logic.c

docs: README.pdf

README.pdf: README.md
	@if which pandoc >$(NULLDEV) 2>&1; then \
		pandoc README.md -o README.pdf; \
	else \
		echo "WARNING: pandoc not found -- README.pdf not updated"; \
	fi
