/* 
  Bootloader for MIPS32S
  Chen Yuheng
  2012/07/16
 */

#define FLASH_START 0xBE000000
#define FLASH_SIZE  0x01000000
#define ELF_MAGIC 0x464C457F

/* off = offset from s0 */
/* load a 32bit word from Flash, 
 * off is byte-addressed */

/*I类型：具体操作由op指定，指令的低16位是立即数，
运算时要将其扩展至32位，然后作为其中一个源操作数参与运算
但目前尚不清楚该函数的具体功效*/
#define LOAD_WORD_I(dst, off) \
  la $t7, ((off) << 1); \
  addu $t7, $s0, $t7; \
  lw dst, 0($t7); \
  lw $t7, 4($t7); \
  sll $t7, $t7, 16; \
  or dst, dst, $t7

/*R类型：具体操作由op、func结合指定，
rs和rt是源寄存器的编号，rd是目的寄存器的编号
这里也是对该函数具体功效不清楚*/
#define LOAD_WORD_R(dst, off, base) \
  addiu $t7, base, off;\
  sll  $t7, $t7, 1; \ 
  addu $t7, $s0, $t7; \
  lw dst, 0($t7); \
  lw $t7, 4($t7); \
  sll $t7, $t7, 16; \
  or dst, dst, $t7;
   
#assume file base == FLASH_START

.set noreorder
.set noat
.globl __start

__start:
  #空转指令(NOP),使得CPU的PIPELINE不会错误的执行一个预取(PRE_FETCH)得指令,放一个NOP是最简单和安全的
  nop
  #条件分支跳转指令，如果确定跳转目标地址在分支指令前后的128KB范围内，编译器就可以编码只生成一条简单的条件分支指令
  b load_elf
  nop

load_elf:
  #dirty hack 这里为什么要 -8？
  la $t0, (FLASH_START+FLASH_SIZE - 8)
  # 这个0x00FF有啥含义？
  la $t1, 0x00FF
  #将源寄存器中的值存入指定的地址 MEM($t0+0)=$t1
  sw $t1, 0($t0)

  #addr of elfheader, s0
  la $s0, FLASH_START
  #e_magic -->具体是指？
  LOAD_WORD_I($t1, 0)
  la $t0, ELF_MAGIC
  # branch to 1 if  $t0 = $t1
  beq $t0, $t1, 1f
  # 如果上一步没有完成则陷入死循环
  nop
  b bad
  nop
1:
  #e_phoff -->具体是指？
  LOAD_WORD_I($s1, 28)
  #e_phnum -->具体是指？
  LOAD_WORD_I($s2, 44)
  andi $s2, $s2, 0xFFFF

  #e_entry 这里应该是kenerl入口地址
  LOAD_WORD_I($s3, 24)

next_sec:
  #s1, addr proghdr
  #s4, p_va
  LOAD_WORD_R($s4, 8, $s1)
  #s5, p_filesz
  LOAD_WORD_R($s5, 16, $s1)
  #s6, p_offset
  LOAD_WORD_R($s6, 4, $s1)

  beq  $s4, $zero, 3f
  nop
  beq  $s5, $zero, 3f
  nop

#copy from file_base+p_offset to p_va
copy_sec:
  LOAD_WORD_R($t0, 0, $s6)
  sw $t0, 0($s4)
  addiu $s6, $s6, 4
  addiu $s4, $s4, 4
  addiu $s5, $s5, -4
  bgtz  $s5, copy_sec
  nop

3:
  # 这里32和-1的操作？
  addiu $s1, $s1, 32
  addiu $s2, $s2, -1
  # $s2 > 0 则跳转
  bgtz  $s2, next_sec
  nop

done:
  #jump to kernel
  jr $s3
  nop
  # 这里b跳转到"."不清楚什么意思
  b .
  nop

bad:
  b bad 
  nop

