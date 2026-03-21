CXX       := g++
CXXFLAGS  := -std=gnu++17 -O2 -DNDEBUG -Wall -Wextra

# Library source directories (relative to this Makefile)
CORE_DIR   := ../../lib/core/src
ENGINE_DIR := ../../lib/engine/src
INCLUDES   := -I$(CORE_DIR) -I$(ENGINE_DIR)

# Source files
CORE_SRCS  := $(wildcard $(CORE_DIR)/*.cpp)
ENGINE_SRCS := $(wildcard $(ENGINE_DIR)/*.cpp)

# Object files — tune build (compiled with -DTUNING)
TUNE_LIB_OBJS := $(patsubst $(CORE_DIR)/%.cpp, obj/tune_core_%.o, $(CORE_SRCS)) \
                  $(patsubst $(ENGINE_DIR)/%.cpp, obj/tune_engine_%.o, $(ENGINE_SRCS))

# Targets
all: tune

tune: obj/tune.o $(TUNE_LIB_OBJS)
	$(CXX) $(CXXFLAGS) -pthread -o $@ $^

# Full pipeline: build tuner, then run on quiet-labeled corpus
pipeline: tune
	@echo "=== Running tuner ==="
	./tune quiet-labeled.epd > tune.txt

# --- Tune object rules (compiled with -DTUNING) ---

obj/tune.o: tune.cpp | obj
	$(CXX) $(CXXFLAGS) $(INCLUDES) -DTUNING -c $< -o $@

obj/tune_core_%.o: $(CORE_DIR)/%.cpp | obj
	$(CXX) $(CXXFLAGS) $(INCLUDES) -DTUNING -c $< -o $@

obj/tune_engine_%.o: $(ENGINE_DIR)/%.cpp | obj
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

.PHONY: all clean pipeline
