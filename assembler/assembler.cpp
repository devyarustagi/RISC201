#include <iostream>
#include <string>
#include <cstdlib>

int main(int argc, char** argv) {
    if (argc == 1) {
        std::cerr << "Error: No assembly file provided.\n";
        return EXIT_FAILURE;
    }
    std::string input_file;
    std::string output_file = "a.bin";
    if (argc == 2) {
        input_file = argv[1];
    }
    else if (argc == 4 && std::string(argv[2]) == "-o") {
        input_file = argv[1];
        output_file = argv[3];
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
    
    return EXIT_SUCCESS; 
}
