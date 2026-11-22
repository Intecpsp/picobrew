; In 1.1.14 the integrity routine holds the persistent board stamp in r14:r15
; and the independently accumulated sum in r22:r23. Its existing branch
; displays the OEM corruption screen unless the comparison sets SREG.Z.
.section .text.zymatic_factory_compare,"ax",@progbits
.global zymatic_factory_compare
.type zymatic_factory_compare,@function
zymatic_factory_compare:
    movw r24, r14
    call zymatic_factory_matches
    cpi r24, 1
    ret
.size zymatic_factory_compare, .-zymatic_factory_compare

; The original commissioning writer computes the running checksum in r22:r23.
; Preserve its persistent 1.1.14 record format for a later stock restore.
.section .text.zymatic_factory_stamp,"ax",@progbits
.global zymatic_factory_stamp
.type zymatic_factory_stamp,@function
zymatic_factory_stamp:
    movw r24, r22
    call zymatic_commissioned_sum
    std Y+1, r24
    std Y+2, r25
    ret
.size zymatic_factory_stamp, .-zymatic_factory_stamp
