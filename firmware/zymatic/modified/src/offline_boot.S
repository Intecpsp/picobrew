/* Entered via JMP at 0x110c2, with the original stack and interrupts.
 * Rejoin each original startup path with the same r24 and condition codes. */
.section .text.zymatic_wifi_boot_branch,"ax",@progbits
.global zymatic_wifi_boot_branch
.type zymatic_wifi_boot_branch,@function
zymatic_wifi_boot_branch:
    lds r24, 0x230c
    tst r24
    breq 1f
    jmp 0x110ca             /* Connected Wi-Fi banner. */
1:  lds r24, zymatic_offline_boot
    tst r24
    breq 2f
    jmp 0x110e2             /* Common startup and interrupted-brew check. */
2:  jmp 0x11054             /* Existing network failure and retry. */
.size zymatic_wifi_boot_branch, . - zymatic_wifi_boot_branch
