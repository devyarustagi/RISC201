#include <iostream>
#include <string>
#include <cstdlib>
#include <exception>

#include "microassembler.hpp"

int main(int argc, char** argv) {
    if (argc == 1) {
        std::cerr << "Error: No microassembly file provided.\n";
        return EXIT_FAILURE;
    }

    std::string inputFilePath;
    std::string outputDirectory = ".";
    if (argc == 2) {
        inputFilePath = argv[1];
    } else if (argc == 3) {
        if (std::string(argv[2]) == "-o") {
            std::cerr << "Error: Option '-o' requires an output directory argument.\n";
        } else {
            std::cerr << "Error: '" << argv[2] << "' is not a valid command line option.\n";
        }
        return EXIT_FAILURE;
    } else if (argc == 4 && std::string(argv[2]) == "-o") {
        inputFilePath = argv[1];
        outputDirectory = argv[3];
    } else if (argc > 4) {
        std::cerr << "Error: Too many command line arguments provided.\n";
        return EXIT_FAILURE;
    } else {
        std::cerr << "Error: '" << argv[2] << "' is not a valid command line option.\n";
        return EXIT_FAILURE;
    }

    // Normalise the directory separator so the two ROM paths join cleanly.
    if (!outputDirectory.empty() && outputDirectory.back() != '/') {
        outputDirectory.push_back('/');
    }

    try {
        Microassembler microassembler(inputFilePath);
        microassembler.Assemble(outputDirectory + "microprograms.rom",
                                outputDirectory + "rom_address_table.rom");
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
