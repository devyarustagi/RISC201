#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

#include "core/cpu.hpp"

int main(int argc, char** argv) {
    if (argc < 4) {
        std::cerr << "usage: " << argv[0]
                  << " <program.bin> <microprograms.rom> <rom_address_table.rom>"
                     " [--max-cycles N] [--trace]\n";
        return EXIT_FAILURE;
    }

    const std::string programPath = argv[1];
    const std::string microprogramsPath = argv[2];
    const std::string addressTablePath = argv[3];

    uint64_t maxCycles = 200;
    bool trace = false;
    for (int i = 4; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--trace") {
            trace = true;
        } else if (arg == "--max-cycles" && i + 1 < argc) {
            try {
                maxCycles = std::stoull(argv[++i]);
            } catch (const std::exception&) {
                std::cerr << "Error: '--max-cycles' expects a number.\n";
                return EXIT_FAILURE;
            }
        } else {
            std::cerr << "Error: unknown command line option '" << arg << "'.\n";
            return EXIT_FAILURE;
        }
    }

    try {
        Cpu cpu;
        cpu.LoadProgram(programPath);
        cpu.LoadMicrocode(microprogramsPath, addressTablePath);
        cpu.Run(maxCycles, trace);
        cpu.DumpState(std::cout);
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
