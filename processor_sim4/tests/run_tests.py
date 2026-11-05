#!/usr/bin/env python3
"""Test runner for processor_sim4 and the rdb debugger.

Run from processor_sim4/ (make test does this):

  1. Batch tests: every tests/programs/*.asm has an '# expect:' line
     (rN=value, sp=value, mem[addr]=value); the program is run to halt and the
     final registers / memory are checked.
  2. Breakpoint transparency: the same programs are run in the debugger with a
     software breakpoint on EVERY instruction, continuing until halt. The final
     state must be identical: breakpoints must never change program behaviour.
  3. Debugger sessions: every tests/scripts/*.rdb is replayed and its output is
     compared with tests/expected/<name>.out (use --update to regenerate).
"""

import re
import subprocess
import sys
from pathlib import Path

SIM = "build/processor_sim4"
ROOT = Path(__file__).resolve().parent


def run(args, stdin=None):
    result = subprocess.run(args, input=stdin, capture_output=True, text=True)
    return result.stdout + result.stderr


def expectations(asm):
    for line in asm.read_text().splitlines():
        if line.lstrip("# ").startswith("expect:"):
            pairs = line.split("expect:", 1)[1].split()
            return [p.split("=") for p in pairs]
    return []


def location_value(dump, name):
    """Value of rN / sp / mem[addr] in a batch-mode state dump (unsigned)."""
    if name == "sp":
        name = "r14"
    if name.startswith("mem["):
        addr = int(name[4:-1], 0)
        m = re.search(r"^mem\[%d\] = (\d+)$" % addr, dump, re.M)
        return int(m.group(1)) if m else 0
    m = re.search(r"^%s = (\d+)$" % re.escape(name), dump, re.M)
    return int(m.group(1)) if m else None


def batch_tests():
    failures = 0
    for asm in sorted((ROOT / "programs").glob("*.asm")):
        binary = f"build/programs/{asm.stem}.bin"
        dump = run([SIM, binary])
        bad = []
        if "halted          : yes" not in dump:
            bad.append("did not halt")
        for name, value in expectations(asm):
            got = location_value(dump, name)
            want = int(value, 0) & 0xFFFFFFFF
            if got != want:
                bad.append(f"{name}={got} (want {want})")
        status = "ok" if not bad else "FAIL " + ", ".join(bad)
        failures += bool(bad)
        print(f"  batch   {asm.stem:<12} {status}")
    return failures


def transparency_tests():
    failures = 0
    for asm in sorted((ROOT / "programs").glob("*.asm")):
        binary = f"build/programs/{asm.stem}.bin"
        words = Path(binary).stat().st_size // 4
        script = [f"break {4 * i}" for i in range(words)]
        script += ["run"] + ["continue"] * 5000
        names = [n for n, _ in expectations(asm)]
        script += [f"print {n}" if not n.startswith("mem[") else f"print mem {n[4:-1]}" for n in names]
        script += ["stats", "quit"]
        out = run([SIM, binary, "--debug"], "\n".join(script) + "\n")
        bad = []
        if "halted           yes" not in out:
            bad.append("did not halt")
        for name, value in expectations(asm):
            label = "r14" if name == "sp" else name
            if name == "sp":
                pattern = r"sp = 0x[0-9a-f]+ \((-?\d+)\)"
            elif name.startswith("mem["):
                pattern = r"mem\[0x%04x\] = 0x[0-9a-f]+ \((-?\d+)\)" % int(name[4:-1], 0)
            else:
                pattern = r"^\(rdb\) %s = 0x[0-9a-f]+ \((-?\d+)\)" % re.escape(label)
            m = re.search(pattern, out, re.M)
            got = (int(m.group(1)) & 0xFFFFFFFF) if m else None
            want = int(value, 0) & 0xFFFFFFFF
            if got != want:
                bad.append(f"{name}={got} (want {want})")
        hits = out.count("Breakpoint ") - words
        status = f"ok ({hits} breakpoint stops)" if not bad else "FAIL " + ", ".join(bad)
        failures += bool(bad)
        print(f"  bp-all  {asm.stem:<12} {status}")
    return failures


def script_tests(update):
    failures = 0
    expected_dir = ROOT / "expected"
    expected_dir.mkdir(exist_ok=True)
    for script in sorted((ROOT / "scripts").glob("*.rdb")):
        program = re.search(r"^# program: (\S+)", script.read_text(), re.M).group(1)
        out = run([SIM, f"build/programs/{program}.bin", "--script", str(script.relative_to(ROOT.parent))])
        golden = expected_dir / (script.stem + ".out")
        if update or not golden.exists():
            golden.write_text(out)
            print(f"  script  {script.stem:<24} written")
            continue
        ok = golden.read_text() == out
        failures += not ok
        print(f"  script  {script.stem:<24} {'ok' if ok else 'FAIL (output differs from ' + str(golden.name) + ')'}")
    return failures


def main():
    update = "--update" in sys.argv
    print("Batch runs (final state vs '# expect:'):")
    failures = batch_tests()
    print("Breakpoint on every instruction (state must not change):")
    failures += transparency_tests()
    print("Debugger sessions (output vs tests/expected):")
    failures += script_tests(update)
    print("\nPASS: all tests" if failures == 0 else f"\nFAIL: {failures} test(s)")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
