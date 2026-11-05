# processor_sim — RISC201 pipeline simulator and debugger (rdb)

4-stage pipelined RISC201 CPU (IF → OF → EX → MA-RW) with a microprogrammed
control unit, precise exceptions (see `../exceptions.md`) and an interactive
command-line debugger.

## Build and test

```sh
cd processor_sim
make            # simulator, microcode ROMs, assembler, test programs (into build/)
make test       # batch runs + breakpoint-on-every-instruction + debugger sessions
```

`make` builds three tools from the repo:

| Tool | Source | Output |
| :--- | :--- | :--- |
| microassembler | `../microassembler/microassembler.cpp` | `build/microprograms.rom`, `build/rom_address_table.rom` from `samples/microcode.masm` |
| assembler | `../assembler/` | `build/programs/*.bin` from `tests/programs/*.asm` |
| simulator | `main.cpp`, `src/` | `build/processor_sim` |

## Run

```sh
./build/processor_sim build/programs/breakpoints.bin            # run to halt, print final state
./build/processor_sim build/programs/breakpoints.bin --trace    # one line per cycle
./build/processor_sim build/programs/exceptions.bin --debug     # interactive debugger
./build/processor_sim build/programs/exceptions.bin --script tests/scripts/3_exceptions.rdb
make debug PROG=interrupt                                        # shortcut for --debug
```

Options: `--rom <file>`, `--table <file>` (microcode images), `--max-cycles <n>`
(default 100000; in the debugger this is the limit per `run` / `continue`).

A program halts on a branch to itself (`HALT: b HALT`).

## Full-screen debugger (`--tui`)

```sh
make tui PROG=stack          # or: ./build/processor_sim build/programs/stack.bin --tui
```

A btop-style view built on ncurses (`-lncurses`, preinstalled on macOS and most Linux;
on Debian/Ubuntu `sudo apt install libncurses-dev` if the link fails). Terminal must be at least 80x20.

| Panel | Shows |
|---|---|
| Program (top) | every instruction: `*` breakpoint, `h` hbreak, `=>` next fetch, `[IF]/[OF]/[EX]/[MA]` stage, pipeline strip |
| Registers (bottom left) | r0-r15 (changed values in yellow), flags, RAS depth, CPL/eBase/eCode, oldPC/oldSP/oldFlags, counters |
| Stack (bottom right) | words around `sp` (high addresses on top), call frames from `call`/`ret`; `m` switches to every data word written |

| Key | Action |
|---|---|
| click / `b` / Enter / F9 | toggle breakpoint on the instruction |
| `s` / F10, `S` | step 1 cycle, 10 cycles |
| `n` / F11 | next instruction (retire) |
| `c` / F5 | continue to the next stop |
| `r`, `R` | restart and run, reset |
| space / `p`, `+` / `-` | play (auto-step), faster / slower |
| `i` | raise an external interrupt |
| Tab, arrows, `j`/`k`, PgUp/PgDn, wheel | focus a panel and scroll |
| `f`, `m` | follow PC on/off, stack / memory view |
| `:` | type any debugger command below (e.g. `:watch 256`, `:catch all`) |
| `h` / `?`, `q` | help, quit |

## Debugger commands

| Command | What it does |
| :--- | :--- |
| `run` | Reload the program, reset the CPU and run |
| `continue` / `c` | Run until a breakpoint, watchpoint, caught exception or halt |
| `step [n]` / `s` | Advance `n` clock cycles, then show the pipeline |
| `next [n]` / `n` | Advance until `n` instructions leave the pipeline |
| `break <addr>` / `b` | Software breakpoint: patches `ebreak`, stops precisely at MA-RW |
| `hbreak <addr>` | Hardware breakpoint: stops when the address is fetched |
| `watch r<n>` / `watch mem <addr>` | Stop when the value changes |
| `catch <exc>…` / `catch all` | Stop when that exception is taken: `pbrk scall div0 rasovf wronginst lowpriv interrupt` |
| `uncatch <exc>…` / `uncatch all` | Stop catching |
| `delete [id]`, `enable <id>`, `disable <id>` | Manage breakpoints / watchpoints |
| `info break`, `info watch`, `info catch` | List them |
| `regs` | All GPRs, next-fetch PC, flags |
| `print r<n>` / `print mem <addr>` / `print pc` | One value |
| `set r<n> <v>` / `set mem <addr> <v>` / `set inst <addr> <word>` | Change state (`set inst` patches instruction memory) |
| `x <addr> [n]` | `n` data-memory words |
| `mem` | Every data-memory word written so far |
| `disasm [addr] [n]` | Disassembly (`=>` next fetch, `*` breakpoint) |
| `pipe` | Contents of IF/OF, OF/EX, EX/MA and what retired last cycle |
| `special` | CPL, eBase, eCode, oldPC, oldSP, oldFlags, pending interrupt |
| `scratch` | Microcode scratch registers s0–s15 |
| `stats` | Cycles, instructions, CPI, mispredictions, stall cycles, exceptions |
| `interrupt` | Raise a hardware interrupt (the next fetched instruction is marked) |
| `trace on|off` | One line per cycle while running |
| `reset`, `help`, `quit` | |

Numbers can be decimal, `0x` hex or `0b` binary.

## Test programs (`tests/programs/`)

| Program | Shows |
| :--- | :--- |
| `breakpoints` | Loop + store/load: breakpoints, watchpoints, load-use stall |
| `calls` | `call` / `ret` through the RAS; `mul`, `mod`, `xor`, `not` |
| `exceptions` | Boot code sets `eBase`, drops to user mode with `retz`; user code raises div0, scall, pbrk, lowpriv; the handler counts and skips them |
| `interrupt` | Counting loop for the `interrupt` command |
| `rasovf` | 17 nested calls overflow the 16-entry RAS |

Each program's `# expect:` line lists its final register / memory values;
`tests/scripts/*.rdb` are recorded debugger sessions with golden output in
`tests/expected/` (`python3 tests/run_tests.py --update` regenerates them).
