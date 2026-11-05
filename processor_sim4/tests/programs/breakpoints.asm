# Sum 1..10 into r3, store it at mem[256], load it back.
# Used to demo breakpoints, watchpoints, stepping and the load-use stall.
# expect: r1=11 r2=10 r3=55 r4=55 r5=56 mem[256]=55

        movi r1, 1          # 0x00  i = 1
        movi r2, 10         # 0x04  n = 10
        movi r3, 0          # 0x08  sum = 0
LOOP:
        add r3, r3, r1      # 0x0c  sum += i
        addi r1, r1, 1      # 0x10  i++
        cmp r1, r2          # 0x14
        bgt DONE            # 0x18  i > n -> done
        b LOOP              # 0x1c
DONE:
        st r3, r0, 256      # 0x20  mem[256] = sum
        ld r4, r0, 256      # 0x24
        addi r5, r4, 1      # 0x28  uses r4 right after the load (load-use stall)
HALT:
        b HALT              # 0x2c  branch to self = halt
