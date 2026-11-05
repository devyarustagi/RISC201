#ifndef PROCESSOR_SIM_TUI_HPP
#define PROCESSOR_SIM_TUI_HPP

#include <cstdint>
#include <map>
#include <string>
#include <vector>

class Cpu;
class Debugger;

/**
 * @brief Full-screen terminal view of the debugger (btop-style), built on ncurses.
 *
 *   +-- Program -------------------------------------------------+
 *   |  breakpoints, next fetch, pipeline stage of each instruction|
 *   +-- Registers ----------------++-- Stack ---------------------+
 *   |  GPRs, flags, exception     ||  words around sp, call frames |
 *   |  registers, statistics      ||  (or every data word written) |
 *   +-----------------------------++------------------------------+
 *    message line / key hints
 *
 * It drives the same Debugger engine as the command line, so breakpoints,
 * watchpoints, catch and every typed command (after ':') behave identically.
 * Clicking an instruction toggles a breakpoint on it.
 */
class Tui {
public:
    Tui(Debugger& debugger, Cpu& cpu);

    /** @brief Runs the full-screen session until the user quits. */
    void Run();

private:
    enum class Focus { Program, Stack };

    Debugger& dbg_;
    Cpu& cpu_;

    // View state.
    Focus focus_ = Focus::Program;
    int programTop_ = 0;          // first instruction index shown
    int programSel_ = 0;          // selected instruction index
    bool follow_ = true;          // keep the next-fetch PC in view
    bool memoryView_ = false;     // stack panel shows all data words instead
    int stackScroll_ = 0;         // words scrolled from the sp-centred view
    int memoryScroll_ = 0;
    bool playing_ = false;        // auto-step one cycle per tick
    int playDelayMs_ = 120;
    std::string message_;
    bool messageIsStop_ = false;

    // Values before the last action, to highlight what changed.
    uint32_t prevGpr_[16] = {};
    std::map<uint32_t, uint32_t> prevMemory_;

    // Layout (recomputed every frame).
    int rows_ = 0, cols_ = 0;
    int progY_ = 1, progH_ = 0;
    int bottomY_ = 0, bottomH_ = 0, leftW_ = 0;

    // Actions.
    void snapshot();
    void step(uint64_t cycles);
    void next(uint64_t instructions);
    void runToStop(bool restart);
    void toggleBreakpoint(uint32_t address);
    void runCommand(const std::string& line);
    std::string readCommandLine();
    void showOverlay(const std::string& title, const std::vector<std::string>& lines);
    void showHelp();
    void handleMouse();
    void moveSelection(int delta);

    // Drawing.
    void layout();
    void draw();
    void drawBox(int y, int x, int h, int w, const std::string& title, bool focused);
    void drawHeader();
    void drawProgram();
    void drawRegisters();
    void drawStack();
    void drawStatus();
    int programRows() const;
    int programCount() const;
};

#endif
