#ifndef ASSEMBLER_H
#define ASSEMBLER_H

#include <fstream>
#include <string>
#include <vector>

class Assembler {
private:
    int lc{0};
    std::ifstream inputFile;
    std::ofstream outputFile;
    std::vector<std::string> tokenizer(std::string&);
    void firstPass();
public :
    Assembler(const std::string& inputFilePath, const std::string& outputFilePath);
    void Assemble();
    
};

#endif