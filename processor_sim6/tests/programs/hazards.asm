# Pipeline hazards that only a deeper pipeline exposes.
#  - wrong-path ret and call behind a mispredicted branch: the RAS is updated in
#    IBU, three stages before the branch resolves, so it must be restored
#  - back-to-back dependent ALU ops (forwarding from MA and RW)
#  - load followed by a store of the loaded value (load-use on store data)
# expect: r1=7 r2=5 r4=21 r5=42 r6=84 r7=126 mem[256]=7 mem[260]=7 mem[264]=126 sp=4096

        movi sp, 4096       # 0x00
        movi r1, 0          # 0x04
        call F              # 0x08
        call G              # 0x0c
        st r1, r0, 256      # 0x10
        ld r3, r0, 256      # 0x14  load ...
        st r3, r0, 260      # 0x18  ... and store it straight away
        movi r4, 21         # 0x1c
        add r5, r4, r4      # 0x20  42 (r4 from MA)
        add r6, r5, r5      # 0x24  84 (r5 from MA)
        add r7, r6, r5      # 0x28  126 (r6 from MA, r5 from RW)
        st r7, r0, 264      # 0x2c
HALT:
        b HALT              # 0x30
F:
        movi r2, 5          # 0x34
        cmpi r2, 5          # 0x38
        beq SKIP            # 0x3c  taken, predicted not taken: mispredict
        ret                 # 0x40  wrong path: would pop the RAS
        addi r1, r1, 100    # 0x44  wrong path
SKIP:
        addi r1, r1, 7      # 0x48
        ret                 # 0x4c  must return to 0x0c
G:
        cmpi r2, 5          # 0x50
        beq GDONE           # 0x54  taken, predicted not taken: mispredict
        call F              # 0x58  wrong path: would push 0x5c
        addi r1, r1, 100    # 0x5c  wrong path
GDONE:
        ret                 # 0x60  must return to 0x10
