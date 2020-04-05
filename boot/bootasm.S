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

/* LOAD_WORD_I宏定义，执行下面所有指令
 * 自FLASH_START的偏移off处读取一个字长存放于dst
 * sll rd,rt,sa  --> (rd) = (rt) << sa
 * lw  rt , offset(base)  --> (rt) = MEM((base)+offset) 取个字节
 */
#define LOAD_WORD_I(dst, off) \
  la $t7, ((off) << 1); \
  addu $t7, $s0, $t7; \
  lw dst, 0($t7); \
  lw $t7, 4($t7); \
  sll $t7, $t7, 16; \
  or dst, dst, $t7

/* LOAD_WORD_R宏定义，执行下面所有指令
 * 个人感觉是从proghdr中取一个字节的内容
*/
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
  #感觉第一部分去掉也没关系
  #dirty hack 这里为什么要 -8？
  la $t0, (FLASH_START+FLASH_SIZE - 8)
  # byte类型转换为int类型，希望保证低8比特数据一致，前24比特为0时要与上0xff
  la $t1, 0x00FF
  #将源寄存器中的值存入指定的地址 MEM($t0+0)=$t1
  sw $t1, 0($t0)

  #addr of elfheader, s0
  la $s0, FLASH_START
  #e_magic -->魔数,位于elf文件头，用于检测elf是否有效
  LOAD_WORD_I($t1, 0)
  la $t0, ELF_MAGIC
  # branch to 1 if  $t0 = $t1 此时代表elf是有效的
  beq $t0, $t1, 1f
  # 如果上一步没有完成则陷入死循环
  nop
  b bad
  nop
1:
  # 这部分对应源ucore中bootmain.c中bootmain函数 load each program segment 

  #e_phoff -->程序头表的文件偏移（以字节为单位）。
  #如果文件没有程序头表，则此成员值为零。
  # 28 -> 第七个字节，也就是在对应偏移上
  LOAD_WORD_I($s1, 28)
  #e_phnum -->程序头表中的项数。
  #如果文件没有程序头表，则 e_phnum 值为零
  LOAD_WORD_I($s2, 44)
  andi $s2, $s2, 0xFFFF

  #e_entry 系统首先将控制权转移到的虚拟地址，从而启动进程。
  #如果文件没有关联的入口点，则此成员值为零。
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

