CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2
TARGET := microasm
SRCS := microassembler.cpp main.cpp

.PHONY: all test clean

all: $(TARGET)

$(TARGET): $(SRCS) microassembler.hpp
	$(CXX) $(CXXFLAGS) -o $@ $(SRCS)

# Assemble the bundled microcode into build/microprograms.rom + build/rom_address_table.rom.
test: $(TARGET)
	@mkdir -p build
	./$(TARGET) microcode.masm -o build

clean:
	rm -f $(TARGET)
	rm -rf build
