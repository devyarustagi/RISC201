#ifndef ASSEMBLER
#define ASSEMBLER

#include <fstream>
#include <string>
#include <vector>

class Assembler {
private:
    int lc{0};
    std::vector<std::string> tokenizer(std::string&);
public :
    void parser(std::ifstream&);
    
};

#endif