@ summon.s

.global summon

summon:
    cmp r1, #900
    blt .not_long_enough

    cmp r0, #2
    bge .return_four

.not_long_enough:
    mov r0, #3
    mov pc, lr

.return_four:
    mov r0, #4
    mov pc, lr

