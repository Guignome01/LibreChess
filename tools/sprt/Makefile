# ---------------------------------------------------------------------------
# SPRT Testing — validate engine changes via Sequential Probability Ratio Test
#
# Uses fastchess to run engine-vs-engine matches with SPRT bounds.
# See README.md for full documentation.
#
# Prerequisites:
#   - fastchess in this directory or on PATH
#   - g++ and make (or mingw32-make) on PATH
#   - git (for baseline worktree build)
#
# Usage:
#   make                              # Defaults: BASELINE=HEAD, TC=8+0.08
#   make BASELINE=HEAD~1              # Compare against previous commit
#   make TC=10+0.1 ELO1=5            # Custom TC and bounds
#   make BASE_EXE=./old.exe          # Pre-built baseline (skip worktree)
# ---------------------------------------------------------------------------

# --- Configuration (override on command line) ---
BASELINE    ?= HEAD
TC          ?= 8+0.08
ELO0        ?= 0
ELO1        ?= 10
ALPHA       ?= 0.05
BETA        ?= 0.05
ROUNDS      ?= 5000
HASH        ?= 64
BOOK        ?= 8moves_v3.pgn
BASE_EXE    ?=

# --- Platform detection ---
ifeq ($(OS),Windows_NT)
  EXE     := .exe
  CP       = copy /y $(subst /,\,$1) $(subst /,\,$2) >NUL
  RMRF     = if exist $(subst /,\,$1) rmdir /s /q $(subst /,\,$1)
  RMFILE   = if exist $(subst /,\,$1) del /q $(subst /,\,$1)
  CONCURRENCY ?= $(NUMBER_OF_PROCESSORS)
else
  EXE     :=
  CP       = cp $1 $2
  RMRF     = rm -rf $1
  RMFILE   = rm -f $1
  CONCURRENCY ?= $(shell nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
endif

# --- Paths ---
ENGINE_DIR  := ../engine
ROOT_DIR    := ../..
WORKTREE    := $(CURDIR)/worktree
FASTCHESS   := ./fastchess$(EXE)
BOOK_FMT    := $(if $(filter %.pgn,$(BOOK)),pgn,epd)

# ---------------------------------------------------------------------------
.PHONY: sprt dev base clean

sprt: dev base
	@echo === Running SPRT ===
	@echo   Baseline    : $(BASELINE)
	@echo   Dev         : ./dev$(EXE)
	@echo   TC          : $(TC)
	@echo   SPRT        : elo0=$(ELO0) elo1=$(ELO1) alpha=$(ALPHA) beta=$(BETA)
	@echo   Rounds      : $(ROUNDS)
	@echo   Concurrency : $(CONCURRENCY)
	@echo   Hash        : $(HASH) MB
	@echo   Book        : $(BOOK) [$(BOOK_FMT)]
	$(FASTCHESS) \
	  -engine cmd=./base$(EXE) name=baseline \
	  -engine cmd=./dev$(EXE) name=dev \
	  -each tc=$(TC) option.Hash=$(HASH) \
	  -openings file=$(BOOK) format=$(BOOK_FMT) order=random \
	  -draw movenumber=34 movecount=8 score=20 \
	  -resign movecount=3 score=600 \
	  -sprt elo0=$(ELO0) elo1=$(ELO1) alpha=$(ALPHA) beta=$(BETA) \
	  -rounds $(ROUNDS) -repeat -concurrency $(CONCURRENCY) \
	  -recover \
	  -pgnout file=games.pgn notation=san

# --- Build dev engine from current working tree ---
dev:
	@echo === Building dev engine [working tree] ===
	$(MAKE) -C $(ENGINE_DIR) clean
	$(MAKE) -C $(ENGINE_DIR)
	$(call CP,$(ENGINE_DIR)/librechess$(EXE),./dev$(EXE))

# --- Build baseline engine from git ref, or copy pre-built binary ---
base:
ifneq ($(BASE_EXE),)
	@echo === Using pre-built baseline: $(BASE_EXE) ===
	$(call CP,$(BASE_EXE),./base$(EXE))
else
	@echo === Building baseline engine [$(BASELINE)] ===
	-$(call RMRF,$(WORKTREE))
	cd $(ROOT_DIR) && git worktree add --detach $(WORKTREE) $(BASELINE)
	$(MAKE) -C $(WORKTREE)/tools/engine
	$(call CP,$(WORKTREE)/tools/engine/librechess$(EXE),./base$(EXE))
	-cd $(ROOT_DIR) && git worktree remove --force $(WORKTREE)
	-$(call RMRF,$(WORKTREE))
endif

clean:
	-$(call RMFILE,dev$(EXE))
	-$(call RMFILE,base$(EXE))
	-$(call RMFILE,games.pgn)
	-$(call RMFILE,config.json)
	-$(call RMRF,$(WORKTREE))

