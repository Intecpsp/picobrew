.section .wifi,"ax",@progbits
.global wifi
wifi:
 push r8
 clr r8
 inc r8
 rjmp common
.section .ethernet,"ax",@progbits
.global ethernet
ethernet:
 push r8
 clr r8
 jmp common
.section .wifi,"ax",@progbits
common:
 call parse
 pop r8
 ret
