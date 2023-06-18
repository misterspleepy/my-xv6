#define SYS_write 0
# write(str)
.globl start
start:
    la a0, str
    li a7, SYS_write
    ecall
    li s1, 10000000000000
while:
    bne zero, s1, while
    jal start

str:
    .string "hello user\n\0"