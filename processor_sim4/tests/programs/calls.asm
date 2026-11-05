# call / ret through the return-address stack, plus mul / mod / xor / not.
# expect: r1=6 r2=7 r3=42 r4=0 r5=1 r6=-7 r7=43

        movi r1, 6          # 0x00
        movi r2, 7          # 0x04
        call MULT           # 0x08  r3 = r1 * r2
        mod r4, r3, r2      # 0x0c  42 mod 7 = 0
        xor r5, r1, r2      # 0x10  6 ^ 7 = 1
        not r6, r1          # 0x14  ~6 = -7
        call INC            # 0x18  r7 = r3 + 1
HALT:
        b HALT              # 0x1c
MULT:
        mul r3, r1, r2      # 0x20
        ret                 # 0x24
INC:
        addi r7, r3, 1      # 0x28
        ret                 # 0x2c
