/* The 1.1.14 list and choice dialog keep the *drawn* row in different
 * call-saved registers. Snapshot it before entering the C ABI. Both original
 * CALLs already pass the encoder in r25:r24. */
.section .text.zymatic_menu_button,"ax",@progbits
.global zymatic_menu_button
zymatic_menu_button:
    mov r22, r15
    ldi r20, 2 /* ZYMATIC_MENU */
    jmp zymatic_menu_input

.section .text.zymatic_dialog_button,"ax",@progbits
.global zymatic_dialog_button
zymatic_dialog_button:
    mov r22, r17
    ldi r20, 3 /* ZYMATIC_DIALOG */
    jmp zymatic_menu_input
