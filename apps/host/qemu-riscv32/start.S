/* QEMU virt は -bios none だと 0x80000000 から実行する。libgloss の crt0 は sp を設定しないのでここで渡す */
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
