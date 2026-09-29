#include "assembler.hpp"
#include <cstdlib>
#include <unordered_map>
#include <stdexcept>

#define MAX_LINE_LEN 256

// Splits a single source line into tokens. Strips the comment (everything from
// the first '#'), separates on whitespace, carriage returns and commas, and
// verifies that every token except the first and last is followed by a comma.
std::vector<std::string> Assembler::tokenizer(std::string& line) {
    // Strip a comment if present (the caller does not reuse the line).
    size_t commentPos = line.find('#');
    if (commentPos != std::string::npos) {
        line.erase(commentPos);
    }
    // Split the remaining text into tokens, remembering for each token whether
    // a comma followed it (needed for the separator check below).
    std::vector<std::string> tokens;
    std::vector<bool> commaAfter;
    std::string current;
    bool inToken = false;

    for (char c : line) {
        if (c == ' ' || c == '\t' || c == '\r') {
            // Whitespace is a plain separator with no comma.
            if (inToken) {
                tokens.push_back(current);
                commaAfter.push_back(false);
                current.clear();
                inToken = false;
            }
        } else if (c == ',') {
            // A comma both separates tokens and flags the previous one.
            if (inToken) {
                tokens.push_back(current);
                commaAfter.push_back(true);
                current.clear();
                inToken = false;
            } else if (!tokens.empty()) {
                // Covers forms like "r1 , r2" where the token was already flushed.
                commaAfter.back() = true;
            }
        } else {
            current.push_back(c);
            inToken = true;
        }
    }
    // Flush the final token; there is never a comma after it.
    if (inToken) {
        tokens.push_back(current);
        commaAfter.push_back(false);
    }
    // Every token except the first (the instruction name) and the last must be
    // followed by at least one comma.
    for (size_t i = 1; i + 1 < tokens.size(); ++i) {
        if (!commaAfter[i]) {
            throw std::runtime_error("Error: line " + std::to_string(lc) +
                                        ": expected ',' after '" + tokens[i] + "'.");
        }
    }

    return tokens;
}

void Assembler::parser(std::ifstream& inputFile) {
    std::string line;
    while (std::getline(inputFile, line)) {
        lc++;
        // Enforce the maximum line length on the raw line; abort if exceeded.
        if (line.size() > MAX_LINE_LEN) {
            throw std::runtime_error("Error: line " + std::to_string(lc) +
                                        " is longer than the maximum allowed " +
                                        std::to_string(MAX_LINE_LEN) + " characters.");
        }

        std::vector<std::string> tokens = tokenizer(line);
    }
}

