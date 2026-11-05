CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2
INCLUDES := -Iinclude -I../alu/include

SRCS := main.cpp $(wildcard src/components/*.cpp src/stages/*.cpp src/core/*.cpp)
OBJS := $(SRCS:.cpp=.o)
ALU_OBJS := build/alu.o build/alu_units.o
TARGET := processor_sim

.PHONY: all clean roms alu-test

all: $(TARGET)

$(TARGET): $(OBJS) $(ALU_OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS) $(ALU_OBJS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# The ALU is the team's project under ../alu; compile its objects into build/.
build/alu.o: ../alu/src/components/alu.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

build/alu_units.o: ../alu/src/components/alu_units.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c $< -o $@

# Build the microassembler and generate the ROM images from its microcode.
build/microasm: ../microassembler/microassembler.cpp ../microassembler/main.cpp
	@mkdir -p build
	$(CXX) $(CXXFLAGS) -o $@ ../microassembler/microassembler.cpp ../microassembler/main.cpp

build/microprograms.rom build/rom_address_table.rom &: build/microasm ../microassembler/microcode.masm
	@mkdir -p build
	build/microasm ../microassembler/microcode.masm -o build

roms: build/microprograms.rom

# Run the ALU's own self-test.
alu-test: 
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(INCLUDES) ../alu/tests/alu_test.cpp \
		../alu/src/components/alu.cpp ../alu/src/components/alu_units.cpp -o build/alu_test
	./build/alu_test

clean:
	rm -f $(OBJS) $(TARGET)
	rm -rf build
