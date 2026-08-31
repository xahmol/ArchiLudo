
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
# := (immediate expansion), not = -- with plain =, $(shell date)
# re-runs on every reference to VERSION_TIMESTAMP, so two recipe lines (or
# two targets, like zip and disk below, where disk's recipe re-derives
# $(ZIPFILE) rather than being handed it) straddling a minute boundary
# would silently compute two DIFFERENT version strings/filenames in the
# same build. := evaluates the shell command exactly once, at the point
# Make first reads this line, so every reference for the rest of this run
# sees the same value.
VERSION_TIMESTAMP := $(shell date "+%Y%m%d-%H%M")
VERSION           = v$(VERSION_MAJOR).$(VERSION_MINOR).$(VERSION_PATCH)-$(VERSION_TIMESTAMP)

# Compile flags. config.mk sets CFLAGS -- always append with += (see its own comment).
#   -Iinclude       : project headers
#   -MMD -MP        : automatic per-object dependency generation (build/*.d)
#   -DVERSION       : pass version string to source
CFLAGS += -Iinclude \
          -MMD -MP \
          -DVERSION="\"$(VERSION)\""

# debug_log() (src/game_view.c, lib/qtm.c) writes a plain-text "Log" file
# in the app directory on every call -- invaluable for non-interactive
# tracing (Arculator has no other option, see CLAUDE.md's Testing
# section) but not something a release build should ship doing by
# default (needless disk I/O on every redraw/click/animation tick, and
# an ever-growing Log file on the user's own machine). Off by default;
# build with `make DEBUG_LOG=1` to enable it for a debugging session --
# compiles debug_log() out to a no-op entirely (not just a runtime
# check) when unset, per explicit user request that release builds not
# ship with logging enabled.
ifdef DEBUG_LOG
CFLAGS += -DARCHILUDO_DEBUG_LOG
endif

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
# QTMModule (,ffa -- Module filetype) plus the bundled music
# (Music1/Music2/Music3, ProTracker .mod data, ,ffd Data like this
# project's own save files) -- see docs/QTM.md. The 6 one-shot SFX
# (assets/audio/Sfx*) are NOT shipped as separate app-directory files --
# they're embedded directly into Music1/2/3's own MOD sample tables at
# build time (tools/mod_embed_sfx.py) and played from there via
# QTM_PlaySample, so they're build-time inputs only, not runtime assets.
APPFILES  = $(RUNIMAGE) $(APPDIR)/!Run,feb $(APPDIR)/!Sprites,ff9 \
            $(APPDIR)/!Sprites22,ff9 $(APPDIR)/PawnSprite,ff9 \
            $(APPDIR)/QTMModule,ffa $(APPDIR)/Music1,ffd $(APPDIR)/Music2,ffd \
            $(APPDIR)/Music3,ffd

TEST_BINS = build/test_game_logic build/test_board_layout build/test_ai

.SUFFIXES:
.PHONY: all clean asm zip disk docs check-hostfs deploy check-pibridge deploy-pibridge test assets export-sprites import-sprites

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

build:
	@$(MKDIR) build 2>$(NULLDEV) ; true

$(APPDIR): | build
	@$(MKDIR) "$(APPDIR)" 2>$(NULLDEV) ; true

-include $(DEPFILES)

# One .s per source, mirroring the build/%.o pattern rules above --
# NOT a single `$(ARCHIECC) -S $(SRCFILES)` invocation with no -o: GCC's
# compile-only modes (-c/-S) drop the source's own directory and write
# output (plus the -MMD/-MP .d file) into the CURRENT WORKING DIRECTORY
# when no -o is given, not next to the source and not into build/ --
# litters the repo root with untracked (though .d is gitignored, .s
# is not) files instead. Found live: `make asm` from the repo root
# left 11 stray .s/.d files sitting there.
ASMFILES = $(patsubst src/%.c,build/%.s,$(patsubst lib/%.c,build/%.s,$(SRCFILES)))

asm: $(ASMFILES)

build/%.s: src/%.c | build
	$(ARCHIECC) $(CFLAGS) -S $< -o $@

build/%.s: lib/%.c | build
	$(ARCHIECC) $(CFLAGS) -S $< -o $@

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
	# An earlier pre-app-directory layout left a flat
	# ArchiLudo,ff8/PawnSprites,ff9 directly in hostfs -- remove any
	# stale copies so they can't be mistaken for what the Filer/iconbar
	# actually runs now (the app directory above).
	rm -f "$(ARCULATOR_HOSTFS)/$(APPNAME),ff8" "$(ARCULATOR_HOSTFS)/PawnSprites,ff9" \
	      "$(ARCULATOR_HOSTFS)/Sprites,ff9"
	# PawnSprites,ff9 was renamed to PawnSprite,ff9 (see APPFILES comment
	# above) -- remove the old 11-character name from inside the app
	# directory itself so a stale copy doesn't linger alongside the new one.
	rm -f "$(ARCULATOR_HOSTFS)/!$(APPNAME)/PawnSprites,ff9"
	# The 6 one-shot SFX used to be shipped as separate app-directory
	# files -- now embedded into Music1/2/3's own MOD sample tables
	# instead (see APPFILES comment above), so remove any stale copies
	# an earlier deploy left behind.
	rm -f "$(ARCULATOR_HOSTFS)/!$(APPNAME)/SfxDice,ffd" \
	      "$(ARCULATOR_HOSTFS)/!$(APPNAME)/SfxRelease,ffd" \
	      "$(ARCULATOR_HOSTFS)/!$(APPNAME)/SfxMove,ffd" \
	      "$(ARCULATOR_HOSTFS)/!$(APPNAME)/SfxCapture,ffd" \
	      "$(ARCULATOR_HOSTFS)/!$(APPNAME)/SfxHome,ffd" \
	      "$(ARCULATOR_HOSTFS)/!$(APPNAME)/SfxWin,ffd"

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

# PiEconetBridge's fileserver (PiFS) does NOT use the ",xxx"
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

ZIPFILE_ABS = $(abspath $(ZIPFILE))

# A real Spark-tested bug -- live-tested on real hardware with
# SparkFS, the zip opened fine but NO filetype survived extraction. Root
# cause, found by reading ArchieSDK's own bundled Info-Zip source
# (build-infozip.sh builds zip30 with -DFORRISCOS, which compiles in
# set_extra_field_forriscos() in zipup.c -- Info-Zip's own code literally
# names its constants EB_SPARK_LEN/EB_SPARK_SIZE, confirming this IS the
# extra field Spark-aware tools read): that function, which turns a
# ",xxx"-suffixed filename into the real Acorn "AC"/"ARC0" extra field
# (filetype + stamped load/exec, same scheme this project's own
# tools/prepare_pibridge_deploy.py implements), is gated behind a global
# `decomma` flag that is only set by passing the literal `-,` option
# (zip.c) -- never on by default. Without it, the ",xxx" suffix was just
# being stored as literal text in the zip entry's filename, with no
# filetype metadata at all -- a plain unzip and SparkFS both "worked" in
# the sense of extracting successfully, but neither had anything to
# restore a filetype FROM. `-,` is required on both zip invocations below.
#
# cd into build/ first so archive entries are "!ArchiLudo/..." at the zip's
# own top level, not "build/!ArchiLudo/..." -- zip stores paths exactly as
# given on the command line, and $(APPDIR) is "build/!ArchiLudo" relative
# to the repo root where `make` normally runs. The docs are added with -j
# (junk path) in a second pass since they live outside build/, and -r
# would otherwise need a shared parent directory for both.
#
# Two docs are bundled instead of README.md directly: README.pdf (see the
# `docs` target below) for reading on whatever machine the zip was
# downloaded to, and a plain-text conversion typed as RISC OS Text
# (,fff, restored by -, like everything else here) with CR-only line
# endings -- RISC OS's own native text-file line-ending convention -- for
# reading directly on the target machine in !Edit, no PDF viewer needed
# (RISC OS 3.10 has none, though a handful of third-party viewers exist,
# e.g. riscos.info's xpdf port). README.pdf is staged as
# "README.pdf,adf" -- dot-extension AND comma-hex-suffix together -- so
# -, strips only the trailing ",adf" on typing, leaving the extracted
# name as plain "README.pdf" everywhere: a Windows/Mac/Linux extraction
# (the PDF's actual primary use, since RISC OS 3.10 has no viewer by
# default) keeps its familiar double-clickable extension, while RISC OS
# still gets the correct &ADF filetype (confirmed against both
# Wikipedia's List of RISC OS filetypes and riscos.info's own PDF-viewer
# page) via the extra field for the minority who do have a viewer.
build/README.pdf,adf: README.pdf | build
	cp "$<" "$@"

zip: $(APPFILES) build/README.pdf,adf build/ReadMe,fff
	rm -f "$(ZIPFILE_ABS)"
	cd build && $(ARCHIEZIP) -r -, "$(ZIPFILE_ABS)" "!$(APPNAME)"
	$(ARCHIEZIP) -j -, "$(ZIPFILE_ABS)" "build/README.pdf,adf" "build/ReadMe,fff"

DISKFILE = build/$(APPNAME)-$(VERSION).adf

# An ADFS "D" format (800KB) disc image containing just the
# release zip -- fits comfortably (the zip is ~654KB; ADFS "L", the
# other common DD floppy format, only has 640KB, too small). Filetype
# &A91 is RISC OS's real Zip filetype (confirmed against RISC OS Open's
# own Zipper module docs) -- SparkFS is commonly registered to open
# &A91 too, alongside its own native archives, so double-clicking the
# file from the Filer still works. No third-party disc-image tool is
# used -- tools/build_adfs_disk.py is a from-scratch writer, ground-
# truthed against DiscImageManager's own source and independently
# verified (structural checksums recomputed by a separately-written
# reader, payload checked byte-for-byte via SHA-256) -- see that
# script's own doc comment.
disk: $(DISKFILE)

$(DISKFILE): zip tools/build_adfs_disk.py
	python3 tools/build_adfs_disk.py "$(ZIPFILE)" "$(DISKFILE)" "$(APPNAME)" "$(APPNAME)" a91

# Plain-text conversion of README.md for reading directly on RISC OS
# (see the zip target's comment above for why this exists alongside
# README.pdf). A straight `pandoc -t plain` renders this project's own
# Markdown tables (see the "Building from source" section) as
# ~170-character-wide grid tables regardless of --columns, and
# separately emits UTF-8 "smart" typography (en/em dashes, curly
# quotes) that RISC OS's single-byte text files can't represent.
# tools/riscos_readme.py flattens tables to stacked "Header: value"
# records, wraps everything to 78 columns, and transliterates to plain
# ASCII (failing the build if it can't) before writing LF line endings
# -- see that script's own doc comment for why LF, not the textbook
# RISC OS CR convention.
build/ReadMe,fff: README.md tools/riscos_readme.py | build
	@which pandoc >$(NULLDEV) 2>&1 || \
		(echo "ERROR: pandoc not found -- install with: sudo apt install pandoc" && false)
	python3 tools/riscos_readme.py README.md "$@"

assets:
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
