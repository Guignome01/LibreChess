CXX       := g++
CXXFLAGS  := -std=gnu++17 -O2 -DNDEBUG -Wall -Wextra -pthread

# Library source directory (relative to this Makefile)
CORE_DIR   := ../../lib/core/src
INCLUDES   := -I$(CORE_DIR)

# Source files — all core library .cpp files (top-level + eval/ subfolder)
CORE_SRCS  := $(wildcard $(CORE_DIR)/*.cpp)
EVAL_SRCS  := $(wildcard $(CORE_DIR)/eval/*.cpp)

# Object files
CORE_OBJS  := $(patsubst $(CORE_DIR)/%.cpp, obj/core_%.o, $(CORE_SRCS)) \
              $(patsubst $(CORE_DIR)/eval/%.cpp, obj/core_eval_%.o, $(EVAL_SRCS))

# Output executable name
ifeq ($(OS),Windows_NT)
	TARGET := librechess.exe
else
	TARGET := librechess
endif

# Targets
all: $(TARGET)

$(TARGET): obj/main.o $(CORE_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

obj/main.o: main.cpp | obj
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

obj/core_%.o: $(CORE_DIR)/%.cpp | obj
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

obj/core_eval_%.o: $(CORE_DIR)/eval/%.cpp | obj
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

obj:
ifeq ($(OS),Windows_NT)
	if not exist obj mkdir obj
else
	mkdir -p obj
endif

clean:
ifeq ($(OS),Windows_NT)
	if exist obj rmdir /s /q obj
	if exist $(TARGET) del $(TARGET)
else
	rm -rf obj $(TARGET)
endif

.PHONY: all clean
