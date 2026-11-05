#include "debugger/tui.hpp"

#include <ncurses.h>

#include <algorithm>
#include <clocale>
#include <cstdio>
#include <sstream>

#include "core/cpu.hpp"
#include "debugger/debugger.hpp"
#include "debugger/disasm.hpp"

namespace {

// Colour pairs.
enum Pair : short {
    kBorder = 1,   // panel frames
    kTitle,        // panel titles
    kPc,           // next-fetch row
    kBreak,        // breakpoint marker
    kChanged,      // a value that changed in the last action
    kStage,        // pipeline-stage tags
    kHeader,       // top bar
    kAlert,        // exception marks, stop messages
    kDim,          // secondary text
    kKey           // key hints
};

std::string hex4(uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof(b), "0x%04x", v);
    return b;
}

std::string hex8(uint32_t v) {
    char b[16];
    std::snprintf(b, sizeof(b), "0x%08x", v);
    return b;
}

std::string regName(int i) {
    return i == GPR_SP ? "sp" : "r" + std::to_string(i);
}

// Writes at most `width` characters of `text` at (y, x).
void put(int y, int x, const std::string& text, int width) {
    if (width <= 0) {
        return;
    }
    mvaddnstr(y, x, text.c_str(), width);
}

void putAttr(int y, int x, const std::string& text, int width, attr_t attr) {
    attron(attr);
    put(y, x, text, width);
    attroff(attr);
}

std::string pad(const std::string& s, size_t width) {
    return s.size() >= width ? s.substr(0, width) : s + std::string(width - s.size(), ' ');
}

}  // namespace

Tui::Tui(Debugger& debugger, Cpu& cpu) : dbg_(debugger), cpu_(cpu) {
    snapshot();
}

// ------------------------------------------------------------------- actions --

void Tui::snapshot() {
    for (int i = 0; i < GPR_COUNT; ++i) {
        prevGpr_[i] = cpu_.Gpr().Read(static_cast<uint8_t>(i));
    }
    prevMemory_ = cpu_.Mem().DataWords();
}

void Tui::step(uint64_t cycles) {
    snapshot();
    std::ostringstream sink;
    const auto reason = dbg_.advance(cycles, 0, sink);
    messageIsStop_ = reason != Debugger::StopReason::CycleCount;
    message_ = messageIsStop_ ? dbg_.stopMessage_
                              : "Stepped " + std::to_string(cycles) + (cycles == 1 ? " cycle." : " cycles.");
    if (messageIsStop_) {
        playing_ = false;
    }
}

void Tui::next(uint64_t instructions) {
    snapshot();
    std::ostringstream sink;
    const auto reason = dbg_.advance(0, instructions, sink);
    messageIsStop_ = reason != Debugger::StopReason::InstructionCount;
    message_ = messageIsStop_ ? dbg_.stopMessage_ : "Next: one instruction left the pipeline.";
}

void Tui::runToStop(bool restart) {
    snapshot();
    if (restart) {
        dbg_.resetMachine();
    }
    std::ostringstream sink;
    dbg_.advance(0, 0, sink);
    message_ = dbg_.stopMessage_;
    messageIsStop_ = true;
    follow_ = true;
}

void Tui::toggleBreakpoint(uint32_t address) {
    std::ostringstream out;
    for (const auto& bp : dbg_.breakpoints_) {
        if (!bp.hardware && bp.address == address) {
            dbg_.Execute("delete " + std::to_string(bp.id), out);
            message_ = "Breakpoint removed at " + hex4(address) + ".";
            messageIsStop_ = false;
            return;
        }
    }
    dbg_.Execute("break " + hex4(address), out);
    message_ = out.str();
    if (!message_.empty() && message_.back() == '\n') {
        message_.pop_back();
    }
    messageIsStop_ = false;
}

void Tui::runCommand(const std::string& line) {
    if (line.empty()) {
        return;
    }
    snapshot();
    std::ostringstream out;
    if (!dbg_.Execute(line, out)) {
        message_ = "quit";
        return;
    }
    std::vector<std::string> lines;
    std::istringstream in(out.str());
    for (std::string l; std::getline(in, l);) {
        lines.push_back(l);
    }
    messageIsStop_ = false;
    if (lines.size() > 1) {
        message_ = "':" + line + "' done.";
        showOverlay(":" + line, lines);
    } else {
        message_ = lines.empty() ? "':" + line + "' done." : lines.front();
    }
}

std::string Tui::readCommandLine() {
    std::string text;
    timeout(-1);
    curs_set(1);
    while (true) {
        move(rows_ - 2, 0);
        clrtoeol();
        attron(COLOR_PAIR(kKey) | A_BOLD);
        put(rows_ - 2, 0, ":", 1);
        attroff(COLOR_PAIR(kKey) | A_BOLD);
        put(rows_ - 2, 1, text, cols_ - 2);
        move(rows_ - 2, std::min(cols_ - 1, 1 + static_cast<int>(text.size())));
        refresh();
        const int ch = getch();
        if (ch == '\n' || ch == '\r' || ch == KEY_ENTER) {
            break;
        }
        if (ch == 27) {   // Esc
            text.clear();
            break;
        }
        if ((ch == KEY_BACKSPACE || ch == 127 || ch == 8) && !text.empty()) {
            text.pop_back();
        } else if (ch >= 32 && ch < 127) {
            text.push_back(static_cast<char>(ch));
        }
    }
    curs_set(0);
    return text;
}

void Tui::showOverlay(const std::string& title, const std::vector<std::string>& lines) {
    const int w = std::min(cols_ - 4, 100);
    const int h = std::min(rows_ - 4, static_cast<int>(lines.size()) + 3);
    const int y = (rows_ - h) / 2;
    const int x = (cols_ - w) / 2;
    int scroll = 0;
    const int visible = h - 3;
    timeout(-1);
    while (true) {
        for (int r = 0; r < h; ++r) {
            move(y + r, x);
            for (int c = 0; c < w; ++c) {
                addch(' ');
            }
        }
        drawBox(y, x, h, w, title, true);
        for (int r = 0; r < visible && scroll + r < static_cast<int>(lines.size()); ++r) {
            put(y + 1 + r, x + 2, lines[static_cast<size_t>(scroll + r)], w - 4);
        }
        const bool more = static_cast<int>(lines.size()) > visible;
        putAttr(y + h - 2, x + 2, more ? "up/down scroll - any other key closes" : "any key closes",
                w - 4, COLOR_PAIR(kDim));
        refresh();
        const int ch = getch();
        if (more && ch == KEY_DOWN && scroll + visible < static_cast<int>(lines.size())) {
            ++scroll;
        } else if (more && ch == KEY_UP && scroll > 0) {
            --scroll;
        } else if (more && ch == KEY_NPAGE) {
            scroll = std::min(scroll + visible, std::max(0, static_cast<int>(lines.size()) - visible));
        } else if (more && ch == KEY_PPAGE) {
            scroll = std::max(0, scroll - visible);
        } else if (ch != KEY_MOUSE && ch != KEY_RESIZE) {
            break;
        }
    }
}

void Tui::showHelp() {
    showOverlay("keys", {
        "s / F10      step one clock cycle          S  step 10 cycles",
        "n / F11      next: one instruction leaves the pipeline",
        "c / F5       continue to the next stop      r  restart and run",
        "space / p    play / pause (one cycle per tick)   + / -  faster / slower",
        "b / F9 / Enter   toggle breakpoint on the selected instruction",
        "click        toggle breakpoint on an instruction (mouse)",
        "i            raise a hardware interrupt     R  reset (reload program)",
        "Tab          switch focus: Program <-> Stack",
        "up/down, PgUp/PgDn, Home/End   move in the focused panel (mouse wheel too)",
        "f            follow the PC in the Program panel",
        "m            Stack panel: stack around sp <-> every data word written",
        ":            type any rdb command (watch r2, catch all, x 256 4, ...)",
        "h / ?        this help                      q  quit",
        "",
        "Program panel: * breakpoint, => next fetch, [IBU] [RF] [EX] [MA] [RW] = stage now,",
        "! = instruction marked with an exception. Yellow values changed in the last action.",
    });
}

void Tui::moveSelection(int delta) {
    if (focus_ == Focus::Program) {
        programSel_ = std::clamp(programSel_ + delta, 0, std::max(0, programCount() - 1));
        follow_ = false;
    } else if (memoryView_) {
        memoryScroll_ = std::max(0, memoryScroll_ + delta);
    } else {
        stackScroll_ -= delta;   // down = towards lower addresses
    }
}

void Tui::handleMouse() {
    MEVENT ev;
    if (getmouse(&ev) != OK) {
        return;
    }
    const bool inProgram = ev.y >= progY_ && ev.y < progY_ + progH_;
    const bool inStack = ev.y >= bottomY_ && ev.y < bottomY_ + bottomH_ && ev.x >= leftW_;
    int wheel = 0;
    if (ev.bstate & BUTTON4_PRESSED) {
        wheel = -3;
    }
#ifdef BUTTON5_PRESSED
    if (ev.bstate & BUTTON5_PRESSED) {
        wheel = 3;
    }
#endif
    if (wheel != 0) {
        if (inProgram) {
            focus_ = Focus::Program;
            programTop_ = std::clamp(programTop_ + wheel, 0, std::max(0, programCount() - programRows()));
            follow_ = false;
        } else if (inStack) {
            focus_ = Focus::Stack;
            moveSelection(wheel);
        }
        return;
    }
    if (ev.bstate & (BUTTON1_CLICKED | BUTTON1_PRESSED | BUTTON1_RELEASED)) {
        if (inProgram) {
            focus_ = Focus::Program;
            const int row = ev.y - (progY_ + 1);
            const int index = programTop_ + row;
            if (row >= 0 && row < programRows() && index < programCount()) {
                programSel_ = index;
                follow_ = false;
                if (ev.bstate & (BUTTON1_CLICKED | BUTTON1_RELEASED)) {
                    toggleBreakpoint(static_cast<uint32_t>(index) * 4);
                }
            }
        } else if (inStack) {
            focus_ = Focus::Stack;
        }
    }
}

// ------------------------------------------------------------------- drawing --

int Tui::programCount() const {
    return static_cast<int>(cpu_.Mem().InstructionRam().size());
}

int Tui::programRows() const {
    return std::max(1, progH_ - 3);   // frame top/bottom + pipeline strip
}

void Tui::layout() {
    getmaxyx(stdscr, rows_, cols_);
    const int body = rows_ - 3;                       // header + 2 status lines
    progY_ = 1;
    progH_ = std::max(8, body * 45 / 100);
    bottomY_ = progY_ + progH_;
    bottomH_ = std::max(6, body - progH_);
    leftW_ = std::clamp(cols_ * 45 / 100, 46, 60);
}

void Tui::drawBox(int y, int x, int h, int w, const std::string& title, bool focused) {
    const attr_t frame = COLOR_PAIR(kBorder) | (focused ? A_BOLD : A_NORMAL);
    attron(frame);
    mvaddch(y, x, ACS_ULCORNER);
    mvaddch(y, x + w - 1, ACS_URCORNER);
    mvaddch(y + h - 1, x, ACS_LLCORNER);
    mvaddch(y + h - 1, x + w - 1, ACS_LRCORNER);
    mvhline(y, x + 1, ACS_HLINE, w - 2);
    mvhline(y + h - 1, x + 1, ACS_HLINE, w - 2);
    mvvline(y + 1, x, ACS_VLINE, h - 2);
    mvvline(y + 1, x + w - 1, ACS_VLINE, h - 2);
    mvaddch(y, x + 2, ACS_RTEE);
    attroff(frame);
    putAttr(y, x + 3, " " + title + " ", w - 6, COLOR_PAIR(kTitle) | A_BOLD);
    attron(frame);
    mvaddch(y, x + 3 + static_cast<int>(title.size()) + 2, ACS_LTEE);
    attroff(frame);
}

void Tui::drawHeader() {
    attron(COLOR_PAIR(kHeader) | A_BOLD);
    move(0, 0);
    for (int c = 0; c < cols_; ++c) {
        addch(' ');
    }
    put(0, 1, "rdb  RISC201 6-stage pipeline  " + dbg_.programPath_, cols_ - 2);
    const uint64_t cycles = cpu_.Cycles();
    const uint64_t retired = cpu_.Retired();
    char cpi[32] = "-";
    if (retired != 0) {
        std::snprintf(cpi, sizeof(cpi), "%.2f", static_cast<double>(cycles) / static_cast<double>(retired));
    }
    const std::string state = cpu_.Halted() ? "HALTED" : playing_ ? "PLAYING" : "PAUSED";
    const std::string right = "cycle " + std::to_string(cycles) + "  CPI " + cpi + "  CPL " +
                              std::to_string(cpu_.Special().Cpl()) + "  " + state + " ";
    if (static_cast<int>(right.size()) < cols_ - 30) {
        put(0, cols_ - static_cast<int>(right.size()), right, static_cast<int>(right.size()));
    }
    attroff(COLOR_PAIR(kHeader) | A_BOLD);
}

void Tui::drawProgram() {
    drawBox(progY_, 0, progH_, cols_, "Program", focus_ == Focus::Program);
    const int rowsVisible = programRows();
    const int count = programCount();
    const uint32_t pc = cpu_.Pc();
    const int pcIndex = static_cast<int>(pc / 4);

    if (follow_ && pcIndex < count) {
        programSel_ = pcIndex;
    }
    if (programSel_ < programTop_) {
        programTop_ = programSel_;
    }
    if (programSel_ >= programTop_ + rowsVisible) {
        programTop_ = programSel_ - rowsVisible + 1;
    }
    if (follow_ && pcIndex >= programTop_ && pcIndex < programTop_ + rowsVisible &&
        pcIndex - programTop_ > rowsVisible * 2 / 3) {
        programTop_ = std::min(pcIndex - rowsVisible / 3, std::max(0, count - rowsVisible));
    }

    // Pipeline occupancy, youngest first: IBU, RF, EX, MA, RW.
    const PacketBase* slots[5] = {&cpu_.IfIbu().out, &cpu_.IbuRf().out, &cpu_.RfEx().out,
                                  &cpu_.ExMa().out, &cpu_.MaRw().out};
    static const char* const kSlotTag[5] = {"[IBU]", "[RF]", "[EX]", "[MA]", "[RW]"};
    const RetireInfo& retired = cpu_.LastRetired();

    for (int r = 0; r < rowsVisible; ++r) {
        const int index = programTop_ + r;
        const int y = progY_ + 1 + r;
        if (index >= count) {
            break;
        }
        const uint32_t address = static_cast<uint32_t>(index) * 4;
        const uint32_t word = dbg_.originalInstruction(address);
        bool hasBreak = false;
        bool hardBreak = false;
        for (const auto& bp : dbg_.breakpoints_) {
            if (bp.address == address && bp.enabled) {
                (bp.hardware ? hardBreak : hasBreak) = true;
            }
        }
        std::string stage;
        bool marked = false;
        // Oldest stage wins if the same address is in flight twice (a loop).
        for (int k = 4; k >= 0; --k) {
            if (slots[k]->valid && slots[k]->pc == address) {
                stage = kSlotTag[k];
                marked = slots[k]->isException;
                break;
            }
        }
        if (stage.empty()) {
            if (address == pc) {
                stage = "[IF]";
            } else if (retired.valid && retired.pc == address) {
                stage = retired.exception ? "[raised]" : "[done]";
            }
        }

        const bool selected = index == programSel_ && focus_ == Focus::Program;
        if (selected) {
            attron(A_REVERSE);
            move(y, 1);
            for (int c = 1; c < cols_ - 1; ++c) {
                addch(' ');
            }
        }
        if (hasBreak) {
            putAttr(y, 2, "*", 1, COLOR_PAIR(kBreak) | A_BOLD);
        } else if (hardBreak) {
            putAttr(y, 2, "h", 1, COLOR_PAIR(kBreak) | A_BOLD);
        }
        if (address == pc) {
            putAttr(y, 4, "=>", 2, COLOR_PAIR(kPc) | A_BOLD);
        }
        const attr_t textAttr = address == pc ? (COLOR_PAIR(kPc) | A_BOLD) : A_NORMAL;
        putAttr(y, 7, hex4(address), 6, COLOR_PAIR(kDim));
        putAttr(y, 15, hex8(word), 10, COLOR_PAIR(kDim));
        putAttr(y, 27, Disassemble(word, address), std::max(0, cols_ - 27 - 14), textAttr);
        if (!stage.empty()) {
            const int sx = std::max(27, cols_ - 13);
            putAttr(y, sx, stage, 8, COLOR_PAIR(kStage) | A_BOLD);
            if (marked) {
                putAttr(y, sx + static_cast<int>(stage.size()), "!", 1, COLOR_PAIR(kAlert) | A_BOLD);
            }
        }
        if (selected) {
            attroff(A_REVERSE);
        }
    }

    // Pipeline strip on the last inner line.
    const auto cell = [](bool valid, uint32_t pcv, uint32_t inst, bool mark) {
        if (!valid) {
            return std::string("--");
        }
        return hex4(pcv) + " " + MnemonicOf(static_cast<uint8_t>(inst >> 26)) + (mark ? "!" : "");
    };
    const int y = progY_ + progH_ - 2;
    attron(COLOR_PAIR(kDim));
    mvhline(y - 0, 1, ' ', cols_ - 2);
    attroff(COLOR_PAIR(kDim));
    static const char* const kSlotName[5] = {"IBU ", "RF ", "EX ", "MA ", "RW "};
    const int cellWidth = cols_ >= 120 ? 13 : 10;
    std::string strip = "IF " + hex4(pc) + "  ";
    for (int k = 0; k < 5; ++k) {
        std::string c = cell(slots[k]->valid, slots[k]->pc, slots[k]->instruction,
                             slots[k]->isException);
        if (cols_ < 120 && slots[k]->valid) {
            c = hex4(slots[k]->pc).substr(2) + " " + MnemonicOf(static_cast<uint8_t>(slots[k]->instruction >> 26)) +
                (slots[k]->isException ? "!" : "");
        }
        strip += std::string(kSlotName[k]) + pad(c, cellWidth) + " ";
    }
    strip += "done " + (retired.valid ? cell(true, retired.pc,
                                             retired.trapped ? dbg_.originalInstruction(retired.pc) : retired.instruction,
                                             retired.exception && !retired.trapped)
                                      : std::string("--"));
    putAttr(y, 2, strip, cols_ - 4, COLOR_PAIR(kStage));
}

void Tui::drawRegisters() {
    drawBox(bottomY_, 0, bottomH_, leftW_, "Registers", false);
    const int inner = leftW_ - 4;
    int y = bottomY_ + 1;
    const int last = bottomY_ + bottomH_ - 2;
    const bool twoCols = inner >= 48;
    const int perRow = twoCols ? 2 : 1;
    for (int i = 0; i < GPR_COUNT && y <= last; i += perRow) {
        for (int k = 0; k < perRow && i + k < GPR_COUNT; ++k) {
            const int reg = i + k;
            const uint32_t v = cpu_.Gpr().Read(static_cast<uint8_t>(reg));
            const bool changed = v != prevGpr_[reg];
            const int x = 2 + k * (inner / 2 + 1);
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%-3s %08x %11d", regName(reg).c_str(), v,
                          static_cast<int32_t>(v));
            putAttr(y, x, buf, inner / perRow, changed ? (COLOR_PAIR(kChanged) | A_BOLD) : A_NORMAL);
        }
        ++y;
    }
    const SpecialRegs& sp = cpu_.Special();
    const auto line = [&](const std::string& text, attr_t attr) {
        if (y <= last) {
            putAttr(y++, 2, text, inner, attr);
        }
    };
    line("", A_NORMAL);
    line("flags  E " + std::to_string(cpu_.FlagE()) + "  GT " + std::to_string(cpu_.FlagGt()) + "  LT " +
             std::to_string(cpu_.FlagLt()) + "      RAS depth " + std::to_string(cpu_.RasUnit().Tos()),
         A_NORMAL);
    line("CPL " + std::to_string(sp.Cpl()) + (sp.Cpl() == 0 ? " (OS)  " : " (user)") + "  eBase " +
             hex4(sp.EBase()) + "  eCode " + std::to_string(sp.ECode()) + " " + ExceptionName(sp.ECode()),
         COLOR_PAIR(kDim));
    line("oldPC " + hex4(sp.OldPC()) + "  oldSP " + hex8(sp.OldSP()) + "  oldFlags " +
             std::to_string(sp.OldFlagE()) + std::to_string(sp.OldFlagGt()) + std::to_string(sp.OldFlagLt()),
         COLOR_PAIR(kDim));
    line("instr " + std::to_string(cpu_.Retired()) + "  mispred " + std::to_string(cpu_.Mispredicts()) +
             "  stalls " + std::to_string(cpu_.DataStalls()) + "  exceptions " + std::to_string(cpu_.Exceptions()),
         COLOR_PAIR(kDim));
}

void Tui::drawStack() {
    const int x0 = leftW_;
    const int w = cols_ - leftW_;
    drawBox(bottomY_, x0, bottomH_, w, memoryView_ ? "Memory" : "Stack", focus_ == Focus::Stack);
    const int inner = w - 4;
    const int listY = bottomY_ + 2;
    const int listRows = bottomH_ - 3;
    const auto& words = cpu_.Mem().DataWords();
    const auto& frames = dbg_.Frames();
    const uint32_t spv = cpu_.Gpr().Read(GPR_SP) & ~0x3u;

    const auto valueOf = [&](uint32_t addr) {
        const auto it = words.find(addr);
        return it == words.end() ? 0u : it->second;
    };
    const auto changed = [&](uint32_t addr) {
        const auto now = words.find(addr);
        const auto before = prevMemory_.find(addr);
        const uint32_t a = now == words.end() ? 0u : now->second;
        const uint32_t b = before == prevMemory_.end() ? 0u : before->second;
        return a != b;
    };

    if (memoryView_) {
        putAttr(bottomY_ + 1, x0 + 2,
                std::to_string(words.size()) + " words written   [m] stack view", inner, COLOR_PAIR(kDim));
        memoryScroll_ = std::min(memoryScroll_, std::max(0, static_cast<int>(words.size()) - listRows));
        int skip = memoryScroll_;
        int r = 0;
        for (const auto& [addr, value] : words) {
            if (skip-- > 0) {
                continue;
            }
            if (r >= listRows) {
                break;
            }
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%s  %s %11d", hex4(addr).c_str(), hex8(value).c_str(),
                          static_cast<int32_t>(value));
            putAttr(listY + r++, x0 + 2, buf, inner, changed(addr) ? (COLOR_PAIR(kChanged) | A_BOLD) : A_NORMAL);
        }
        return;
    }

    putAttr(bottomY_ + 1, x0 + 2,
            "sp " + hex8(spv) + "   " + std::to_string(frames.size()) + " call frame" +
                (frames.size() == 1 ? "" : "s") + "   [m] memory view",
            inner, COLOR_PAIR(kDim));
    // High addresses at the top; sp sits a third of the way down.
    const int64_t top = static_cast<int64_t>(spv) + 4LL * (listRows / 3 + stackScroll_);
    for (int r = 0; r < listRows; ++r) {
        const int64_t a = top - 4LL * r;
        if (a < 0) {
            break;
        }
        const uint32_t addr = static_cast<uint32_t>(a);
        const uint32_t value = valueOf(addr);
        // A frame starts at the first word below the sp its call committed with.
        std::string note;
        for (size_t k = 0; k < frames.size(); ++k) {
            if (frames[k].sp - 4 == addr) {
                note = "<- frame " + std::to_string(k + 1) + ", call @" + hex4(frames[k].callPc);
            }
        }
        const bool isSp = addr == spv;
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s %s  %s %11d", isSp ? "sp>" : "   ", hex4(addr).c_str(),
                      hex8(value).c_str(), static_cast<int32_t>(value));
        // Below sp is free space (old values from returned calls): dimmed.
        attr_t attr = changed(addr) ? (COLOR_PAIR(kChanged) | A_BOLD)
                                    : (addr >= spv && words.count(addr) != 0 ? A_NORMAL : COLOR_PAIR(kDim));
        if (isSp) {
            attr = COLOR_PAIR(kPc) | A_BOLD;
        }
        putAttr(listY + r, x0 + 2, buf, inner, attr);
        const int used = static_cast<int>(std::string(buf).size()) + 2;
        if (!note.empty() && used < inner) {
            putAttr(listY + r, x0 + 2 + used, note, inner - used, COLOR_PAIR(kStage));
        }
    }
}

void Tui::drawStatus() {
    move(rows_ - 2, 0);
    clrtoeol();
    std::string msg = message_;
    std::replace(msg.begin(), msg.end(), '\n', ' ');
    putAttr(rows_ - 2, 1, msg, cols_ - 2,
            messageIsStop_ ? (COLOR_PAIR(kAlert) | A_BOLD) : A_NORMAL);
    move(rows_ - 1, 0);
    clrtoeol();
    const std::vector<std::pair<std::string, std::string>> keys = {
        {"s", "step"}, {"n", "next"}, {"c", "continue"}, {"r", "run"}, {"space", "play"},
        {"b/click", "break"}, {"i", "interrupt"}, {"Tab", "focus"}, {"m", "stack/mem"},
        {":", "command"}, {"h", "help"}, {"q", "quit"}};
    int x = 1;
    for (const auto& [key, label] : keys) {
        if (x + static_cast<int>(key.size() + label.size()) + 3 >= cols_) {
            break;
        }
        putAttr(rows_ - 1, x, key, static_cast<int>(key.size()), COLOR_PAIR(kKey) | A_BOLD);
        x += static_cast<int>(key.size()) + 1;
        putAttr(rows_ - 1, x, label, static_cast<int>(label.size()), COLOR_PAIR(kDim));
        x += static_cast<int>(label.size()) + 2;
    }
}

void Tui::draw() {
    layout();
    erase();
    if (rows_ < 20 || cols_ < 80) {
        put(0, 0, "Terminal too small for rdb (need 80x20, have " + std::to_string(cols_) + "x" +
                      std::to_string(rows_) + "). Resize, or press q.", cols_);
        refresh();
        return;
    }
    drawHeader();
    drawProgram();
    drawRegisters();
    drawStack();
    drawStatus();
    refresh();
}

// ---------------------------------------------------------------------- loop --

void Tui::Run() {
    std::setlocale(LC_ALL, "");
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
#if NCURSES_VERSION_MAJOR > 5 || (NCURSES_VERSION_MAJOR == 5 && NCURSES_VERSION_MINOR >= 8)
    set_escdelay(25);   // Esc cancels the command line without a long wait
#endif
    mousemask(ALL_MOUSE_EVENTS, nullptr);
    mouseinterval(0);
    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(kBorder, COLOR_CYAN, -1);
        init_pair(kTitle, COLOR_WHITE, -1);
        init_pair(kPc, COLOR_GREEN, -1);
        init_pair(kBreak, COLOR_RED, -1);
        init_pair(kChanged, COLOR_YELLOW, -1);
        init_pair(kStage, COLOR_MAGENTA, -1);
        init_pair(kHeader, COLOR_BLACK, COLOR_CYAN);
        init_pair(kAlert, COLOR_RED, -1);
        init_pair(kDim, COLOR_BLUE, -1);
        init_pair(kKey, COLOR_CYAN, -1);
    }
    message_ = "Ready. Press h for keys. Click an instruction to set a breakpoint, then c to run.";

    bool running = true;
    while (running) {
        draw();
        timeout(playing_ ? playDelayMs_ : -1);
        const int ch = getch();
        if (ch == ERR) {
            if (playing_) {
                step(1);
            }
            continue;
        }
        switch (ch) {
            case 'q': case 'Q': running = false; break;
            case 's': case KEY_F(10): step(1); break;
            case 'S': step(10); break;
            case 'n': case KEY_F(11): next(1); break;
            case 'c': case KEY_F(5): runToStop(false); break;
            case 'r': runToStop(true); break;
            case 'R':
                dbg_.resetMachine();
                snapshot();
                message_ = "Program reloaded; CPU reset.";
                messageIsStop_ = false;
                follow_ = true;
                break;
            case ' ': case 'p':
                playing_ = !playing_ && !cpu_.Halted();
                message_ = playing_ ? "Playing: one cycle per tick (+/- speed, space to pause)." : "Paused.";
                messageIsStop_ = false;
                break;
            case '+': case '=': playDelayMs_ = std::max(10, playDelayMs_ / 2); break;
            case '-': playDelayMs_ = std::min(2000, playDelayMs_ * 2); break;
            case 'b': case KEY_F(9): case '\n': case KEY_ENTER:
                if (focus_ == Focus::Program && programSel_ < programCount()) {
                    toggleBreakpoint(static_cast<uint32_t>(programSel_) * 4);
                }
                break;
            case 'i':
                cpu_.RequestInterrupt();
                message_ = "Interrupt requested: the next instruction fetched will be marked.";
                messageIsStop_ = false;
                break;
            case 'f': follow_ = true; break;
            case 'm': memoryView_ = !memoryView_; break;
            case '\t': focus_ = focus_ == Focus::Program ? Focus::Stack : Focus::Program; break;
            case KEY_UP: case 'k': moveSelection(-1); break;
            case KEY_DOWN: case 'j': moveSelection(1); break;
            case KEY_PPAGE: moveSelection(-programRows()); break;
            case KEY_NPAGE: moveSelection(programRows()); break;
            case KEY_HOME:
                if (focus_ == Focus::Program) { programSel_ = 0; follow_ = false; } else { stackScroll_ = 0; memoryScroll_ = 0; }
                break;
            case KEY_END:
                if (focus_ == Focus::Program) { programSel_ = std::max(0, programCount() - 1); follow_ = false; }
                break;
            case 'h': case '?': showHelp(); break;
            case ':': {
                const std::string line = readCommandLine();
                runCommand(line);
                if (message_ == "quit") {
                    running = false;
                }
                break;
            }
            case KEY_MOUSE: handleMouse(); break;
            default: break;
        }
    }
    endwin();
}
