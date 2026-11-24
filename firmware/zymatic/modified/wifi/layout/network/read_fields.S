.section .text
.global ethernet, wifi, fetch, success, cleanup
ethernet:
 ldi r27,1
 rjmp enter
wifi:
 ldi r27,0
enter:
 push r27
 push r2
 push r3
 jmp 0x33a4
/* The caller's frame uses 548 bytes and saves eighteen registers.
 * The extra byte identifies which frontend entered this invocation. */
.macro get_mode
 movw r30,r28
 subi r30,lo8(-567)
 sbci r31,hi8(-567)
 ld r0,Z
 tst r0
.endm
fetch:
 get_mode
 breq fetch_wifi
 jmp 0xcbf2
fetch_wifi:
 jmp 0x3280
success:
 get_mode
 movw r30,r10
 breq success_wifi
 st Z,r1
 ldi r24,1
 jmp 0x35ee
success_wifi:
 ldi r24,1
 st Z,r24
 sts 0x247b,r24
 jmp 0x35ee
cleanup:
 pop r2
 pop r0
 ret
