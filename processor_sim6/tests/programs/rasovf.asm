# Return-address stack overflow: the RAS holds 16 return addresses. The user
# program calls itself without returning, so the 17th call raises rasovf
# (cause 4). The handler skips that call and the program halts.
# expect: r1=17 r10=1 r11=4 sp=4096

# ---- boot: runs in OS mode (CPL 0) ----
        movi sp, 28         # 0x00  handler address (0x1c)
        movz eBase, sp      # 0x04  eBase = handler
        movi sp, 80         # 0x08  user program address (0x50)
        movz oldPC, sp      # 0x0c
        nop                 # 0x10  let the movz reach MA-RW before retz reads oldPC
        nop                 # 0x14
        retz                # 0x18  CPL = 1, PC = 0x50

# ---- exception handler at 0x1c (OS mode) ----
# r10 counts exceptions, r11 keeps the last eCode.
HANDLER:
        movz oldSP, sp      # 0x1c  save the user sp
        movz sp, eCode      # 0x20
        addi r10, r10, 1    # 0x24
        mov r11, sp         # 0x28
        cmpi sp, 7          # 0x2c  interrupt?
        beq RESUME          # 0x30  yes: the marked instruction never ran, return to it
        movz sp, oldPC      # 0x34  no: skip the faulting instruction
        addi sp, sp, 4      # 0x38
        movz oldPC, sp      # 0x3c
RESUME:
        movz sp, oldSP      # 0x40  restore the user sp
        nop                 # 0x44
        nop                 # 0x48
        retz                # 0x4c  back to user mode at oldPC

# ---- user program at 0x50 (CPL 1) ----
USER:
        movi sp, 4096       # 0x50
        movi r1, 0          # 0x54
AGAIN:
        addi r1, r1, 1      # 0x58  r1 = number of calls made
        call AGAIN          # 0x5c  push 0x60 and jump back
HALT:
        b HALT              # 0x60
