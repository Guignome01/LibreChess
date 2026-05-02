CXX       := g++
CXXFLAGS  := -std=gnu++17 -O2 -DNDEBUG -Wall -Wextra

# Library source directory (relative to this Makefile)
CORE_DIR   := ../../lib/core/src
INCLUDES   := -I$(CORE_DIR) -I.

# Source files — top-level + eval/ subfolder
CORE_SRCS  := $(wildcard $(CORE_DIR)/*.cpp)
EVAL_SRCS  := $(wildcard $(CORE_DIR)/eval/*.cpp)

# Object files — tune build (compiled with -DTUNING)
TUNE_LIB_OBJS := $(patsubst $(CORE_DIR)/%.cpp, obj/tune_core_%.o, $(CORE_SRCS)) \
                 $(patsubst $(CORE_DIR)/eval/%.cpp, obj/tune_core_eval_%.o, $(EVAL_SRCS))

# Targets
all: tune

tune: obj/tune.o $(TUNE_LIB_OBJS)
	$(CXX) $(CXXFLAGS) -pthread -o $@ $^

# Full pipeline: build tuner, then run on quiet-labeled corpus
pipeline: tune
	@echo "=== Running tuner ==="
	./tune quiet-labeled.epd

# --- Tune object rules (compiled with -DTUNING) ---

obj/tune.o: tune.cpp | obj
	$(CXX) $(CXXFLAGS) $(INCLUDES) -DTUNING -c $< -o $@

obj/tune_core_%.o: $(CORE_DIR)/%.cpp | obj
	$(CXX) $(CXXFLAGS) $(INCLUDES) -DTUNING -c $< -o $@

obj/tune_core_eval_%.o: $(CORE_DIR)/eval/%.cpp | obj
	$(CXX) $(CXXFLAGS) $(INCLUDES) -DTUNING -c $< -o $@

obj:
ifeq ($(OS),Windows_NT)
	if not exist obj mkdir obj
else
	mkdir -p obj
endif

clean:
ifeq ($(OS),Windows_NT)
	if exist obj rmdir /s /q obj
	if exist tune.exe del tune.exe
else
	rm -rf obj tune
endif

# Iterative tuning: build -> tune -> patch eval/params.h -> repeat
iterate: tune
	python autotune.py quiet-labeled.epd --iterations 10 --K 0.989836

.PHONY: all clean pipeline iterate
