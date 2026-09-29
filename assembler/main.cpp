#include <iostream>
#include <fstream>
#include <string>
#include <cstdlib>
#include "assembler.hpp"

int main(int argc, char** argv) {
    // handle command line parsing
    if (argc == 1) {
        std::cerr << "Error: No assembly file provided.\n";
        return EXIT_FAILURE;
    }
    std::string inputFilePath;
    std::string outputFilePath = "a.bin";
    if (argc == 2) {
        inputFilePath = argv[1];
    }
    else if (argc == 4 && std::string(argv[2]) == "-o") {
        inputFilePath = argv[1];
        outputFilePath = argv[3];
    }
    else if (argc > 4) {
        std::cerr << "Error: Too many command line arguments provided.\n";
        return EXIT_FAILURE;
    }
    else if (argc == 3) {
        if (std::string(argv[2]) == "-o") {
            std::cerr << "Error: Option '-o' requires an output file argument.\n";
        } else {
            std::cerr << "Error: '" << argv[2] << "' is not a valid command line option.\n";
        }
        return EXIT_FAILURE;
    }
    else {
        std::cerr << "Error: '" << argv[2] << "' is not a valid command line option.\n";
        return EXIT_FAILURE;
    }
    
    //read the assembly file
    std::ofstream outputFile(outputFilePath);
    std::ifstream inputFile(inputFilePath);

    if (!inputFile.is_open()) {
        std::cerr << "Error: Could not open file '" << inputFilePath << "' (it may not exist or lack permissions).\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS; 
}
