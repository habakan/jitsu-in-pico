/* With -bios none, QEMU virt starts executing at 0x80000000. libgloss's crt0 does not set sp, so it
 * is handed over here */
    .section .qemu_start, "ax"
    .global qemu_start
qemu_start:
    la sp, qemu_stack_top
    j _start

    .bss
    .balign 16
    .global qemu_stack, qemu_stack_top
qemu_stack:
    .space 65536
qemu_stack_top:
