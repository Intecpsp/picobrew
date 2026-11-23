/* Enter from the reviewed stock CRT after stack initialization. Validate the
 * complete private image before processing its compressed SRAM initializer. */
.section .text.zymatic_crt_data,"ax",@progbits
.global zymatic_crt_data
zymatic_crt_data:
    call zymatic_image_valid
    tst r24
    brne 1f
    jmp zymatic_image_invalid
1:
    ldi r26, 0x00
    ldi r27, 0x01
    ldi r30, 0x4a
    ldi r31, 0xb2
    ldi r16, 1
    out 0x3b, r16
    ldi r17, 0x0a
.Lcommand:
    wdr
    elpm r24, Z+
    sbrc r24, 7
    rjmp .Lcopy
    inc r24
.Lliteral:
    elpm r0, Z+
    st X+, r0
    dec r24
    brne .Lliteral
    rjmp .Ldone
.Lcopy:
    mov r21, r24
    andi r21, 7
    elpm r20, Z+
    movw r28, r26
    sub r28, r20
    sbc r29, r21
    sbiw r28, 1
    andi r24, 0x78
    lsr r24
    lsr r24
    lsr r24
    subi r24, -3
.Lrepeat:
    ld r0, Y+
    st X+, r0
    dec r24
    brne .Lrepeat
.Ldone:
    cpi r26, 0x80
    cpc r27, r17
    brne .Lcommand
    jmp 0x2330
