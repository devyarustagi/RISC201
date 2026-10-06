CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2
INCLUDES := -Ialu/include

SRCS := main.cpp $(wildcard src/components/*.cpp src/stages/*.cpp src/core/*.cpp)
OBJS := $(SRCS:.cpp=.o)
TARGET := processor_sim

.PHONY: all clean roms run program check alu-test

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Build the microassembler from ../microassembler and generate the ROM images.
build/microasm: ../microassembler/microassembler.cpp ../microassembler/main.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ ../microassembler/microassembler.cpp ../microassembler/main.cpp

build/microprograms.rom build/rom_address_table.rom &: build/microasm samples/microcode.masm
	@mkdir -p build
	build/microasm samples/microcode.masm -o build

roms: build/microprograms.rom

# Regenerate samples/program.bin from samples/program.asm.
program:
	python3 tools/gen_program.py samples/program.asm samples/program.bin

run: $(TARGET) roms
	./$(TARGET) samples/program.bin build/microprograms.rom build/rom_address_table.rom \
		--max-cycles 200 --trace

# Run the sample and diff the final state against the checked-in expectation.
check: $(TARGET) roms
	./$(TARGET) samples/program.bin build/microprograms.rom build/rom_address_table.rom \
		--max-cycles 200 > build/actual.txt
	diff -u samples/expected.txt build/actual.txt && echo "PASS: final state matches samples/expected.txt"

# Unit-test the ALU against native C++ arithmetic.
ALU_TEST := build/alu_test

$(ALU_TEST): alu/tests/alu_test.cpp alu/src/components/alu.cpp alu/src/components/alu_units.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $^

alu-test: $(ALU_TEST)
	./$(ALU_TEST)

clean:
	rm -f $(OBJS) $(TARGET)
	rm -rf build
