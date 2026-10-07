.text
.align 2
.arm

.global NNSi_SndArcLoadBank_hook
NNSi_SndArcLoadBank_hook:
ldr r5, =NNSi_SndArcLoadBank_return_address
mov r6, lr
str r6, [r5]
pop {r5-r6}
blx NNSi_SndArcLoadBank
ldr r1, =NNSi_SndArcLoadBank_return_address
ldr r1, [r1]
mov pc, r1

.pool


.global NNS_SndInit_ASM
NNS_SndInit_ASM:
    push {lr}
    blx NNS_SndInit_Hook
    pop {pc}

.global NNS_SndInit_Original
NNS_SndInit_Original:
    push {r3, lr}
    ldr r0, =0x021DD420   
    ldr r1, [r0, #0xc]
    ldr r3, =0x020C78DC
    bx r3
.pool


.global NNS_SndMain_ASM
NNS_SndMain_ASM:
    push {lr}
    blx NNS_SndMain_Hook
    pop {pc}

@ The hook (scripts/make.py HookARM) overwrites 12 bytes: the first THREE instructions of the function, 020C7958, 020C795C and 020C7960.
@ This stub must run all three, then continue at 020C7964. (It used to run two and continue at 020C7960, which now holds the hook's
@ address word, so that word was executed as an instruction and the real third instruction, mov r0, r4, never ran.)
.global NNS_SndMain_Original
NNS_SndMain_Original:
    push {r4, lr}              @ 020C7958 e92d4010
    mov r4, #0                 @ 020C795C e3a04000
    mov r0, r4                 @ 020C7960 e1a00004
    ldr r3, =0x020C7964        @ the original bl 0x20d5604 is at 020C7964 and runs in place
    bx r3
.pool





.global NNS_SndPlayerStopSeqByPlayerNo_ASM
NNS_SndPlayerStopSeqByPlayerNo_ASM:
    push {lr}
    blx NNS_SndPlayerStopSeqByPlayerNo_Hook
    pop {pc}

@ Same story: 020C8068, 020C806C, 020C8070 are overwritten. The lost third instruction, mov r2, #36, is what the next
@ instruction (mla r6, r0, r2, r3) multiplies the player number by. Without it r2 was whatever the caller left there,
@ so the wrong player was stopped (or none).
.global NNS_SndPlayerStopSeqByPlayerNo_Original
NNS_SndPlayerStopSeqByPlayerNo_Original:
    push {r3, r4, r5, r6, r7, lr}  @ 020C8068 e92d40f8
    ldr r3, =0x020C80BC            @ 020C806C was ldr r3, [pc, #72]: that reads the word at 020C80BC.
    ldr r3, [r3]                   @          Read it from the same place, the original literal pool is untouched.
    mov r2, #0x24                  @ 020C8070 e3a02024
    ldr ip, =0x020C8074            @ mla r6, r0, r2, r3 is at 020C8074 and runs in place
    bx ip
.pool


.global NNS_SndPlayerPauseByPlayerNo_ASM
NNS_SndPlayerPauseByPlayerNo_ASM:
    push {lr}
    blx NNS_SndPlayerPauseByPlayerNo_Hook
    pop {pc}

@ 020C816C, 020C8170, 020C8174 are overwritten. The lost third instruction, mul r6, r0, r2, is player number * 36.
@ Without it r6 was garbage, and the code after it used r6 as the pointer to the player to pause.
.global NNS_SndPlayerPauseByPlayerNo_Original
NNS_SndPlayerPauseByPlayerNo_Original:
    push {r4, r5, r6, r7, r8, lr}  @ 020C816C e92d41f0
    mov  r2, #0x24                 @ 020C8170 e3a02024
    mul  r6, r0, r2                @ 020C8174 e0060290
    ldr r3, =0x020C8178            @ ldr r5, [pc, #64] is at 020C8178 and runs in place
    bx r3
.pool

NNSi_SndArcLoadBank_return_address:
.word 0
