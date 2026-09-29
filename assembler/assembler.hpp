#ifndef ASSEMBLER
#define ASSEMBLER

class Assembler {
private:
    int lc;
public :
    void parser(std::ifstream&);
    std::vector<std::string> tokenizer(std::string&);
};

#endif