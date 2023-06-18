
user/initcode.o:     file format elf64-littleriscv


Disassembly of section .text:

0000000000000000 <start>:
#define SYS_write 0
# write(str)
.globl start
start:
    la a0, str
   0:	00000517          	auipc	a0,0x0
   4:	00050513          	mv	a0,a0
    li a7, SYS_write
   8:	00000893          	li	a7,0
    ecall
   c:	00000073          	ecall
    li s1, 10000000000000
  10:	48c274b7          	lui	s1,0x48c27
  14:	3954849b          	addiw	s1,s1,917
  18:	00d49493          	slli	s1,s1,0xd

000000000000001c <while>:
while:
    bne zero, s1, while
  1c:	00901063          	bne	zero,s1,1c <while>
    jal start
  20:	fe1ff0ef          	jal	ra,0 <start>

0000000000000024 <str>:
  24:	6568                	ld	a0,200(a0)
  26:	6c6c                	ld	a1,216(s0)
  28:	7375206f          	j	52f5e <str+0x52f3a>
  2c:	7265                	lui	tp,0xffff9
  2e:	000a                	c.slli	zero,0x2
  30:	0000                	unimp
	...
