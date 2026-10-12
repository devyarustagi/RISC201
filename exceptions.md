# Implementing Precise Exceptions and Interrupts in a Pipelined Datapath

## 1. Architectural Foundations and Precise State
To support an operating system safely, the processor must be able to halt the current program, execute a privileged handler routine, and resume the original program without corrupting its state. This requires implementing **Precise Exceptions**.

An interrupt or exception is considered precise if it meets three hardware conditions when the pipeline halts at a specific dynamic instruction, $I_k$:
*   **Condition 1:** All instructions preceding $I_K$; $I_j$ ($j < k$) have successfully completed their execution and updated the system state.
*   **Condition 2:** No instruction for which $j >= k$ is allowed to complete execution or alter the register file/memory.
*   **Condition 3:** The hardware perfectly preserves the processor context, allowing seamless resumption at either $I_k$ (for re-execution) or $I_{k+1}$ (to continue).

## 2. Exceptions Supported in the RISC-201 Processor
*   `pbrk`: This exception occurs when the OF stage finds the decoded instruction to be an `ebreak` instruction. Useful for debuggers.
*   `scall`: This exception occurs when the OF stage finds the decoded instruction to be an `ecall` instruction.
This is used to implement the syscall interface. The syscall number must be stored in `r15`.
*   `divByZero`: Raised by the ALU when it detects an attempt to divide by zero.
*   `rasOverflow`: Occurs when the return address stack overflows.
*   `wrongInst`: Occurs when the OF stage finds the decoded opcode to be invalid.
*   `lowPriv`: Raised by the OF stage if it finds that the program tries to execute a privileged instruction whilst the `CPL` being 1.
## 3. Required Hardware Datapath Modifications
To support precise control flow hijacking, the standard data path requires several new components and structural extensions:

*   **The Exception Unit:** A dedicated hardware block connected to all pipeline registers. It receives exception/interrupt codes, tracks marked instructions, and coordinates pipeline flushes.
*   **Instruction Packet Extensions:**
    *   `flags`: Carries the current state of the flags register (e.g., from a `cmp` instruction) down the pipeline.
    *   `cause`: This 4 bit field is populated by the exception unit and contains the code of the exception.
    *   `isException`: A single bit field which denotes whether the instruction caused an exception/is the instruction when an interrupt happened.
*   **The Register Unit & Special Registers:** The standard Register File is expanded into a Register Unit containing new hardware-level registers:  
    *   `eBase`: The operating system stores the base address of the exception handler preamble here at boot time. This preamble code will run before the actual code of any exception handler and is used to save the program's context on the kernel stack.
    *   `eCode`: The exception unit stores the code identifying the exception in this register. This value can be used by the operating system to identify the type of exception and thus execute the correct handler.
    *   `oldPC`: Stores the return address for the interrupted program.
    *   `oldSP`: Preserves the user-mode stack pointer, allowing the handler to safely switch to its own private kernel stack.
    *   `oldFlags`: Captures the exact state of the condition codes at the moment of interruption.
*   **Privilege and Register Windows:**
    *   Addition of a **CPL (Current Privilege Level)** register (1 = User, 0 = OS). The decode unit checks the opcode of the instruction and if it is a privileged instruction then it checks the value of the CPL. If the CPL is 1 whilst the instruction being a privileged one, a `lowPriv` exception is raised.
    *   Implementation of **Privileged Instructions** (`movz`, `retz`) that operate on a restricted register window visible only to the OS (general registers + special registers). See [semantics of privileged instructions](#6-privileged-instructions-movz-and-retz-semantics) for more details.

## 4. The Universal Exception Handling Flow
Regardless of the trigger, the hardware follows a strict, deterministic sequence to ensure pipeline stability.
1.  **Marking the Instruction:** When an error occurs or an external signal arrives, the Exception Unit "marks" an instruction in the pipeline and assigns it an exception code. See [procedure for marking instructions](#5-marking-instructions-in-the-pipeline) for more details. 
2.  **Draining the Pipeline:** The marked instruction is allowed to flow forward toward the MA-RW stage. The exception unit checks the value of the isException flag in the EX/MA-RW pipeline register, and if it is set then it converts the the isWb, isLd and isSt signals of the packet to zero.  
3.  **Flushing the remaining pipeline:** Concurrently, the Exception Unit converts all younger instructions fetched after the marked instruction into bubbles (NOPs), preventing them from writing to memory or registers.
4.  **Capturing Context:** When the marked instruction finally reaches the MA-RW stage, the Exception Unit extracts the `flags` field from the instruction packet and saves it to `oldFlags`. It saves `PC` to `oldPC`. It also saves `cause` in `eCode`.
5.  **Context Switch:** The `PC` is force-loaded with the address stored in `eBase`. The `CPL` is set to 0.
6.  **Handler Execution (Software):** 
    *   The OS executes `movz` to save the user stack pointer to `oldSP`.
    *   It spills all general-purpose registers to the kernel stack.
    *   It processes the event.
    *   It restores the general-purpose registers and `oldSP`.
7.  **Resumption:** The OS executes `retz`, which automatically transfers the contents of `oldPC` back into the main `PC` and restores the privilege level, resuming user execution seamlessly.

## 5. Marking Instructions in the Pipeline:
* The Exception unit is responsible for marking instructions. By marking an instruction, we mean setting the `is_exception` bit in the instruction packet of the defaulting instruction to 1 alongside setting the `cause` field too.
* If an asynchronous exception(like a hardware interrupt occurs), the instruction being fetched by the IF stage gets marked. For synchronous exceptions, the Exception Unit uses a priority encoder which prioritizes exceptions occurring in a later stage of the pipeline and marks the highest priority exception only(we do this because multiple exceptions may be triggered in different stages of the pipeline in the same cycle thus we only need to mark the last of those exceptions as the ones in earlier stages of the pipeline will anyways be flushed out when the last one is handled.) The output of the priority encoder is what gets written into the `cause` field.

## 6. Privileged Instructions: `movz` and `retz` Semantics
To prevent user programs from corrupting system state or introducing viruses, the ISA introduces privileged instructions that can only be executed when `CPL == 0`. These instructions use a restricted **Register Window**, meaning they see a different view of the registers (only `r0`, `oldPC`, `oldSP`, `oldFlags`, and `sp`).

*   **`movz` (Move Special):** Transfers values between regular registers and the special registers.
    *   **Implementation:** It uses the same register-format-based encoding as `mov`. The OF and RW (Register Write) stages use multiplexers to route data to/from the special registers based on the restricted register window encoding.
    *   **Memory Restriction:** `movz` is required because the ISA intentionally does not allow instructions to directly transfer the values of special registers to and from memory. This avoids the need to create redundant privileged versions of load and store instructions.
*   **`retz` (Return from Exception):** Reverses the context switch and returns control to the user program.
    *   **Implementation:** It reads the value of `oldPC` and transfers its contents to the `PC`. 
    *   **Privilege Drop:** Executing `retz` automatically changes the `CPL` register from 0 back to 1, returning the processor to user mode.

