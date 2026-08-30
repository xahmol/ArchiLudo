
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

# PiEconetBridge deployment -- real hardware target (Econet-over-IP bridge
# on a Raspberry Pi), separate from the Arculator emulator deploy above.
# SFTP with password auth (matching how the user already connects via
# FileZilla), so all four of these live in .env, not just the usual
# ARCULATOR_HOSTFS-style connection details -- PIBRIDGE_PATH included,
# since per explicit user request it's not hardcoded as a Makefile
# default either.
PIBRIDGE_USER ?= <set_PIBRIDGE_USER_in_.env>
PIBRIDGE_HOST ?= <set_PIBRIDGE_HOST_in_.env>
PIBRIDGE_PASS ?= <set_PIBRIDGE_PASS_in_.env>
PIBRIDGE_PATH ?= <set_PIBRIDGE_PATH_in_.env>

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

# Sources: every .c under src/, plus every .c under lib/ (dedicated SWI-
# wrapper libraries, e.g. lib/qtm.c -- see CLAUDE.md's "lib/<name>.c"
# convention), one .o per source under build/.
LIBS = -lOSLib32

SRCFILES = $(wildcard src/*.c) $(wildcard lib/*.c)
OBJFILES = $(patsubst src/%.c,build/%.o,$(patsubst lib/%.c,build/%.o,$(SRCFILES)))
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
# Round 7.60: QTMModule (,ffa -- Module filetype) plus the bundled music
# (Music1/Music2/Music3 -- round 7.65 added the third track, ProTracker
# .mod data) and sample-effect (Sfx* -- raw 16-bit PCM, converted to
# QTM's own format at runtime, see lib/qtm.c) files, all ,ffd (Data) like
# this project's own save files -- see docs/QTM.md.
APPFILES  = $(RUNIMAGE) $(APPDIR)/!Run,feb $(APPDIR)/!Sprites,ff9 \
            $(APPDIR)/!Sprites22,ff9 $(APPDIR)/PawnSprite,ff9 \
            $(APPDIR)/QTMModule,ffa $(APPDIR)/Music1,ffd $(APPDIR)/Music2,ffd \
            $(APPDIR)/Music3,ffd \
            $(APPDIR)/SfxDice,ffd $(APPDIR)/SfxRelease,ffd $(APPDIR)/SfxMove,ffd \
            $(APPDIR)/SfxCapture,ffd $(APPDIR)/SfxHome,ffd $(APPDIR)/SfxWin,ffd

TEST_BINS = build/test_game_logic build/test_board_layout build/test_ai

.SUFFIXES:
.PHONY: all clean asm zip docs check-hostfs deploy check-pibridge deploy-pibridge test assets export-sprites import-sprites

all: $(APPFILES)

$(RUNIMAGE): $(ELF) | $(APPDIR)
	$(ARCHIEOBJCOPY) -O binary $< $@

$(ELF): $(OBJFILES)
	$(ARCHIECC) $(CFLAGS) -o $@ $(OBJFILES) $(LIBS)

build/%.o: src/%.c | build
	$(ARCHIECC) $(CFLAGS) -c $< -o $@

build/%.o: lib/%.c | build
	$(ARCHIECC) $(CFLAGS) -c $< -o $@

# Static application-directory files -- checked into the repo (app/!Run)
# or pre-built and checked in (assets/!Sprites, assets/!Sprites22,
# assets/PawnSprite -- see the `assets` target to regenerate them),
# just copied into place here with their RISC OS filetype suffix added,
# matching how PawnSprite,ff9 was already handled before this app-
# directory restructuring. PawnSprite is deliberately kept to 10
# characters or fewer, like every other filename in this list -- round
# 7.88 found that PiEconetBridge's PiFS (see `deploy-pibridge` below)
# truncates BOTH the listed name and the name it actually opens on disk
# to 10 characters by default (classic-Econet compatibility,
# `FS_DEFAULT_NAMELEN` in its own `utilities/fs.c`), so the old 11-
# character "PawnSprites" silently became unopenable over that deploy
# path (worked fine locally/in Arculator, since neither enforces this).
$(APPDIR)/!Run,feb: app/!Run | $(APPDIR)
	cp "$<" "$@"

$(APPDIR)/!Sprites,ff9: assets/!Sprites | $(APPDIR)
	cp "$<" "$@"

$(APPDIR)/!Sprites22,ff9: assets/!Sprites22 | $(APPDIR)
	cp "$<" "$@"

$(APPDIR)/PawnSprite,ff9: assets/PawnSprite | $(APPDIR)
	cp "$<" "$@"

$(APPDIR)/QTMModule,ffa: assets/audio/QTMModule | $(APPDIR)
	cp "$<" "$@"

$(APPDIR)/Music1,ffd: assets/audio/Music1 | $(APPDIR)
	cp "$<" "$@"

$(APPDIR)/Music2,ffd: assets/audio/Music2 | $(APPDIR)
	cp "$<" "$@"

$(APPDIR)/Music3,ffd: assets/audio/Music3 | $(APPDIR)
	cp "$<" "$@"

$(APPDIR)/SfxDice,ffd: assets/audio/SfxDice | $(APPDIR)
	cp "$<" "$@"

$(APPDIR)/SfxRelease,ffd: assets/audio/SfxRelease | $(APPDIR)
	cp "$<" "$@"

$(APPDIR)/SfxMove,ffd: assets/audio/SfxMove | $(APPDIR)
	cp "$<" "$@"

$(APPDIR)/SfxCapture,ffd: assets/audio/SfxCapture | $(APPDIR)
	cp "$<" "$@"

$(APPDIR)/SfxHome,ffd: assets/audio/SfxHome | $(APPDIR)
	cp "$<" "$@"

$(APPDIR)/SfxWin,ffd: assets/audio/SfxWin | $(APPDIR)
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
	# Round 7.88: PawnSprites,ff9 -> PawnSprite,ff9 (see APPFILES comment
	# above) -- remove the old 11-character name from inside the app
	# directory itself so a stale copy doesn't linger alongside the new one.
	rm -f "$(ARCULATOR_HOSTFS)/!$(APPNAME)/PawnSprites,ff9"

# Password auth via sshpass (matching how the user already connects with
# FileZilla over SFTP, rather than SSH keys) -- SSHPASS is passed as an
# environment variable (sshpass -e), not -p, so the password doesn't
# appear in `ps` output. StrictHostKeyChecking=accept-new auto-accepts
# the Pi's host key on first connect (and remembers it after) so a
# password-auth deploy needs no other interactive prompt either.
# ConnectTimeout fails fast rather than hanging if the Pi's unreachable.
check-pibridge:
	@which sshpass >/dev/null 2>&1 || \
		(echo "ERROR: sshpass not found -- install with: sudo apt install sshpass" && false)
	@SSHPASS="$(PIBRIDGE_PASS)" sshpass -e ssh -o ConnectTimeout=3 -o StrictHostKeyChecking=accept-new \
		"$(PIBRIDGE_USER)@$(PIBRIDGE_HOST)" "test -d '$(PIBRIDGE_PATH)'" 2>$(NULLDEV) || \
		(echo "ERROR: Cannot reach PiEconetBridge at $(PIBRIDGE_USER)@$(PIBRIDGE_HOST):$(PIBRIDGE_PATH) -- check PIBRIDGE_USER/PIBRIDGE_HOST/PIBRIDGE_PASS/PIBRIDGE_PATH in .env" && false)

PIBRIDGE_STAGE = build/pibridge-stage/!$(APPNAME)

# Round 7.87: PiEconetBridge's fileserver (PiFS) does NOT use the ",xxx"
# hex-suffix convention Arculator's hostfs uses -- a live deploy showed
# filetypes weren't preserved. Confirmed against PiFS's own source
# (cr12925/PiEconetBridge, utilities/fs.c): it expects either Linux
# xattrs or a classic Acorn ".inf" sidecar file per stored file.
# tools/prepare_pibridge_deploy.py converts $(APPDIR)'s ",xxx"-suffixed
# files into plain-named files + .inf sidecars (the .inf route, not
# xattrs -- see that script's own doc comment for why) into
# $(PIBRIDGE_STAGE), which is what actually gets rsynced below.
$(PIBRIDGE_STAGE): $(APPFILES)
	python3 tools/prepare_pibridge_deploy.py "$(APPDIR)" "$(PIBRIDGE_STAGE)"

# rsync over SSH (via sshpass, same password-auth approach as check-pibridge
# above) -- handles the same "merge into an existing directory" job as
# deploy's cp/xcopy above, but over the network and only transferring
# what changed. --delete keeps the remote !ArchiLudo an exact mirror of
# the local build (safe here since this directory is fully owned by this
# deploy, not shared with anything else on the Pi).
deploy-pibridge: check-pibridge $(PIBRIDGE_STAGE)
	@SSHPASS="$(PIBRIDGE_PASS)" sshpass -e rsync -av --delete \
		-e "ssh -o StrictHostKeyChecking=accept-new" \
		"$(PIBRIDGE_STAGE)/" "$(PIBRIDGE_USER)@$(PIBRIDGE_HOST):$(PIBRIDGE_PATH)/!$(APPNAME)/"

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
