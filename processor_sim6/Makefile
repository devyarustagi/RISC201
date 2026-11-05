# RISC201 6-stage processor simulator + debugger (6StagePipeline.svg).
#
#   make                      build the simulator, the microcode ROMs and the test programs
#   make test                 run every test program in batch and debugger mode
#   make debug PROG=<name>    open the debugger on tests/programs/<name>.asm
#   make tui PROG=<name>      the same, full screen (btop-style)
#   make clean
#
# Tools built from the sibling folders: ../microassembler (microcode -> ROM)
# and ../assembler (RISC201 assembly -> program binary).

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2
INCLUDES := -Iinclude
LDLIBS   ?= -lncurses   # full-screen debugger (--tui); ships with macOS and Linux
BUILD    := build

SIM_SRCS := main.cpp $(wildcard src/components/*.cpp src/stages/*.cpp src/core/*.cpp src/debugger/*.cpp)
SIM_OBJS := $(patsubst %.cpp,$(BUILD)/obj/%.o,$(SIM_SRCS))
SIM      := $(BUILD)/processor_sim6

MASM     := $(BUILD)/microassembler
ROM      := $(BUILD)/microprograms.rom
TABLE    := $(BUILD)/rom_address_table.rom

ASM      := $(BUILD)/assembler
ASM_SRCS := $(wildcard ../assembler/*.cpp)

PROGRAMS := $(patsubst tests/programs/%.asm,$(BUILD)/programs/%.bin,$(wildcard tests/programs/*.asm))

PROG     ?= breakpoints

.PHONY: all test debug tui clean

all: $(SIM) $(ROM) $(PROGRAMS)

$(BUILD)/obj/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

$(SIM): $(SIM_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDLIBS)

$(MASM): ../microassembler/microassembler.cpp
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ $<

$(ROM) $(TABLE): samples/microcode.masm $(MASM)
	$(MASM) samples/microcode.masm -o $(BUILD)

$(ASM): $(ASM_SRCS)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -I../assembler -o $@ $^

$(BUILD)/programs/%.bin: tests/programs/%.asm $(ASM)
	@mkdir -p $(dir $@)
	$(ASM) $< -o $@

test: all
	python3 tests/run_tests.py

debug: all
	$(SIM) $(BUILD)/programs/$(PROG).bin --debug

tui: all
	$(SIM) $(BUILD)/programs/$(PROG).bin --tui

clean:
	rm -rf $(BUILD)
