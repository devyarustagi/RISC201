# Recursive factorial with a stack frame per call: shows the Stack panel.
# Each call to FACT pushes n (4 bytes) below sp; the ld after the inner call
# restores it (and causes a load-use stall).
# expect: r2=120 mem[256]=120 sp=4096

        movi sp, 4096       # 0x00  stack top
        movi r1, 5          # 0x04  n = 5
        call FACT           # 0x08  r2 = fact(5)
        st r2, r0, 256      # 0x0c  mem[256] = result
HALT:
        b HALT              # 0x10
FACT:
        subi sp, sp, 4      # 0x14  open a 4-byte frame
        st r1, sp, 0        # 0x18  save n in it
        cmpi r1, 1          # 0x1c
        bgt RECURSE         # 0x20
        movi r2, 1          # 0x24  fact(1) = 1
        b RETURN            # 0x28
RECURSE:
        subi r1, r1, 1      # 0x2c
        call FACT           # 0x30  r2 = fact(n - 1)
        ld r1, sp, 0        # 0x34  restore n from the frame
        mul r2, r2, r1      # 0x38  r2 = n * fact(n - 1)
RETURN:
        addi sp, sp, 4      # 0x3c  close the frame
        ret                 # 0x40
