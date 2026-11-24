/* Decode a byte from the stock text/bitmap pool by following backward
 * LZ references. Every 128 original bytes has an independent seek pointer.
 * A reference always decreases the source offset, including overlap. */
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
.Lseek:
    movw r26, r24                   /* original offset for backward references */
    mov r23, r24
    andi r23, 127
    lsl r24
    rol r25
    mov r30, r25
    clr r31
    lsl r30
    rol r31
    subi r30, lo8(-0x254)
    sbci r31, hi8(-0x254)
    lpm r24, Z+
    lpm r25, Z
    movw r30, r24
    subi r30, lo8(-0x2be)           /* 53 little-endian stream offsets */
    sbci r31, hi8(-0x2be)
    clr r22
.Lnext:
    lpm r20, Z+
    sbrc r20, 7
    rjmp .Lreference
    inc r20
    cp r23, r20
    brlo .Lliteral
    add r30, r20
    adc r31, r22
.Lskip:
    sub r23, r20
    rjmp .Lnext
.Lliteral:
    add r30, r23
    adc r31, r22
    lpm r0, Z
    rjmp .Lret
.Lreference:
    lpm r18, Z+
    mov r19, r20
    andi r19, 15
    swap r20
    andi r20, 7
    subi r20, -3
    cp r23, r20
    brsh .Lskip
    movw r24, r26
    sub r24, r18
    sbc r25, r19
    sbiw r24, 1
    rjmp .Lseek
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
    call zymatic_original_byte
    adiw r30, 1
    out 0x3f, r18
    pop r18
    st X+, r0
    ret

.global zymatic_pgm_wifi_print
zymatic_pgm_wifi_print:             /* lpm r24,Z; tst r24 */
    push r0
    call zymatic_original_byte
    mov r24, r0
    pop r0
    tst r24
    ret

.global zymatic_pgm_serial_print
zymatic_pgm_serial_print:           /* lpm r22,Z; tst r22 */
    push r0
    call zymatic_original_byte
    mov r22, r0
    pop r0
    tst r22
    ret

.global zymatic_pgm_bitmap
zymatic_pgm_bitmap:                 /* lpm r30,Z; mov r18,r30 */
    push r0
    call zymatic_original_byte
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
