#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>

#include "disassembler.hpp"

int main(int argc, char** argv) {
    if (argc == 1) {
        std::cerr << "Error: No binary file provided.\n";
        return EXIT_FAILURE;
    }

    std::string inputFilePath;
    std::string outputFilePath = "a.asm";

    if (argc == 2) {
        inputFilePath = argv[1];
    } else if (argc == 4 && std::string(argv[2]) == "-o") {
        inputFilePath = argv[1];
        outputFilePath = argv[3];
    } else if (argc > 4) {
        std::cerr << "Error: Too many command line arguments provided.\n";
        return EXIT_FAILURE;
    } else if (argc == 3) {
        if (std::string(argv[2]) == "-o") {
            std::cerr << "Error: Option '-o' requires an output file argument.\n";
        } else {
            std::cerr << "Error: '" << argv[2] << "' is not a valid command line option.\n";
        }
        return EXIT_FAILURE;
    } else {
        std::cerr << "Error: '" << argv[2] << "' is not a valid command line option.\n";
        return EXIT_FAILURE;
    }

    try {
        Disassembler disassembler(inputFilePath, outputFilePath);
        disassembler.Disassemble();
    } catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
