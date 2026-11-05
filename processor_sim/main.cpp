// RISC201 processor simulator - command-line entry point.
//
//   processor_sim <program.bin> [options]
//
//   --rom <file>          microprogram ROM      (default build/microprograms.rom)
//   --table <file>        ROM address table     (default build/rom_address_table.rom)
//   --max-cycles <n>      cycle limit           (default 100000)
//   --trace               batch mode: print the pipeline every cycle
//   --debug               start the interactive debugger (rdb)
//   --script <file>       run debugger commands from a file (implies --debug)
//
// Batch mode runs the program to halt (branch to self) or the cycle limit and
// prints the final state; debugger mode reads commands from stdin or a script.

#include <cstdint>
#include <cstdlib>
#include <exception>
#include <fstream>
#include <iostream>
#include <string>

#include "core/cpu.hpp"
#include "debugger/debugger.hpp"

namespace {

void usage() {
    std::cerr << "usage: processor_sim <program.bin> [--rom <file>] [--table <file>]\n"
                 "                     [--max-cycles <n>] [--trace] [--debug] [--script <file>]\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::string programPath;
    std::string romPath = "build/microprograms.rom";
    std::string tablePath = "build/rom_address_table.rom";
    std::string scriptPath;
    uint64_t maxCycles = 100000;
    bool trace = false;
    bool debug = false;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--rom" && hasValue) {
            romPath = argv[++i];
        } else if (arg == "--table" && hasValue) {
            tablePath = argv[++i];
        } else if (arg == "--max-cycles" && hasValue) {
            maxCycles = std::stoull(argv[++i]);
        } else if (arg == "--script" && hasValue) {
            scriptPath = argv[++i];
            debug = true;
        } else if (arg == "--trace") {
            trace = true;
        } else if (arg == "--debug") {
            debug = true;
        } else if (arg == "--help" || arg == "-h") {
            usage();
            return EXIT_SUCCESS;
        } else if (programPath.empty() && arg[0] != '-') {
            programPath = arg;
        } else {
            usage();
            return EXIT_FAILURE;
        }
    }
    if (programPath.empty()) {
        usage();
        return EXIT_FAILURE;
    }

    try {
        Cpu cpu;
        cpu.LoadMicrocode(romPath, tablePath);
        cpu.LoadProgram(programPath);
        cpu.Reset();

        if (debug) {
            Debugger debugger(cpu, programPath, maxCycles);
            if (scriptPath.empty()) {
                debugger.Repl(std::cin, std::cout, false);
            } else {
                std::ifstream script(scriptPath);
                if (!script.is_open()) {
                    throw std::runtime_error("cannot open script '" + scriptPath + "'");
                }
                debugger.Repl(script, std::cout, true);
            }
            return EXIT_SUCCESS;
        }

        cpu.Run(maxCycles, trace);
        cpu.DumpState(std::cout);
        return cpu.Halted() ? EXIT_SUCCESS : 2;   // 2: cycle limit reached
    } catch (const std::exception& e) {
        std::cerr << "processor_sim: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
}
