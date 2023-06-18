        # qemu -kernel loads the kernel at 0x80000000
        # and causes each hart (i.e. CPU) to jump there.
        # kernel.ld causes the following code to
        # be placed at 0x80000000.
.section .text
.global _entry
_entry:
        la sp, cpus
        li a0, 1024*4
        add sp, sp, a0
        call start
spin:
        j spin
