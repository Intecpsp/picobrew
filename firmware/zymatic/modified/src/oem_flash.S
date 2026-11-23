/* The vendor code addresses the original text and bitmap table by byte.
 * For a 128-byte block, the on-flash index points at a variable-length token
 * stream. Each dictionary entry has left, right and expanded length. Skip
 * complete tokens by length and descend only to the requested byte. No
 * decoder buffer, heap, recursion or cached mutable state is required. */
.section .crc_hole,"ax",@progbits
.global zymatic_original_byte
zymatic_original_byte:              /* Z = original address, r0 = result */
    push r18
    push r19
    push r20
    push r21
    push r22
    push r23
    push r24
    push r25
    push r26
    push r27
    push r30
    push r31
    in r18, 0x3f
    push r18
    movw r24, r30
    subi r24, lo8(0x254)
    sbci r25, hi8(0x254)
    cpi r24, lo8(0x1c77-0x254)
    ldi r18, hi8(0x1c77-0x254)
    cpc r25, r18
    brlo .Lpacked
    lpm r0, Z
    rjmp .Lret
.Lpacked:
    clr r22
    mov r23, r24
    andi r23, 127                   /* position in decoded block */
    lsl r24
    rol r25                         /* block = virtual offset >> 7 */
    mov r30, r25
    clr r31
    lsl r30
    rol r31
    subi r30, lo8(-0x3d4)           /* after 128 three-byte entries */
    sbci r31, hi8(-0x3d4)
    lpm r24, Z+
    lpm r25, Z
    movw r30, r24
    subi r30, lo8(-0x43e)           /* after 53 two-byte block indices */
    sbci r31, hi8(-0x43e)
.Lnext:
    lpm r20, Z+
    cpi r20, 127
    brlo .Lliteral
    brne .Ltoken
    lpm r20, Z+                    /* escaped literal 127..255 */
.Lliteral:
    tst r23
    breq .Lvalue
    dec r23
    rjmp .Lnext
.Ltoken:
    movw r26, r30
    rcall .Lentry
    cp r23, r21
    brlo .Ldescend
    sub r23, r21
    movw r30, r26
    rjmp .Lnext
.Ldescend:
    mov r20, r18
    mov r24, r19
    sbrc r20, 7
    rjmp .Lleft_pair
    tst r23
    breq .Lvalue
    dec r23
    rjmp .Lright
.Lleft_pair:
    rcall .Lentry
    cp r23, r21
    brlo .Ldescend
    sub r23, r21
.Lright:
    mov r20, r24
    sbrs r20, 7
    rjmp .Lvalue
    rcall .Lentry
    rjmp .Ldescend
.Lentry:
    subi r20, 128
    mov r30, r20
    clr r31
    lsl r30
    rol r31
    add r30, r20
    adc r31, r22
    subi r30, lo8(-0x254)
    sbci r31, hi8(-0x254)
    lpm r18, Z+
    lpm r19, Z+
    lpm r21, Z
    ret
.Lvalue:
    mov r0, r20
.Lret:
    pop r18
    out 0x3f, r18
    pop r31
    pop r30
    pop r27
    pop r26
    pop r25
    pop r24
    pop r23
    pop r22
    pop r21
    pop r20
    pop r19
    pop r18
    ret

.section .low_helpers,"ax",@progbits
/* Reproduce exactly the displaced instructions, including status flags. */
.global zymatic_pgm_copy
zymatic_pgm_copy:                   /* lpm r0,Z+; st X+,r0 */
    push r18
    in r18, 0x3f
    rcall zymatic_original_byte
    adiw r30, 1
    out 0x3f, r18
    pop r18
    st X+, r0
    ret

.global zymatic_pgm_wifi_print
zymatic_pgm_wifi_print:             /* lpm r24,Z; tst r24 */
    push r0
    rcall zymatic_original_byte
    mov r24, r0
    pop r0
    tst r24
    ret

.global zymatic_pgm_serial_print
zymatic_pgm_serial_print:           /* lpm r22,Z; tst r22 */
    push r0
    rcall zymatic_original_byte
    mov r22, r0
    pop r0
    tst r22
    ret

.global zymatic_pgm_bitmap
zymatic_pgm_bitmap:                 /* lpm r30,Z; mov r18,r30 */
    push r0
    rcall zymatic_original_byte
    mov r30, r0
    mov r18, r0
    pop r0
    ret

/* 1-Wire CRC-8/MAXIM. The original input index is Z-0x2177. The
 * displaced SUBI restores the same arithmetic flags before the continuation. */
.section .crc_hole,"ax",@progbits
.global zymatic_sensor_crc
zymatic_sensor_crc:
    subi r30, 0x77
    push r24
    push r25
    ldi r24, 8
    ldi r25, 0x8c
.Lcrc_bit:
    lsr r30
    brcc .Lcrc_next
    eor r30, r25
.Lcrc_next:
    dec r24
    brne .Lcrc_bit
    pop r25
    pop r24
    subi r22, 1
    ret
