#define FLASH_START 0xBE000000
#define FLASH_SIZE  0x01000000
#define ELF_MAGIC 0x464C457F


/* off = offset from s0 
 * load a 32bit word from Flash
 * off is byte-addressed 
 * LOAD_WORD_I宏定义
 * 自 FLASH_START 的偏移 off 处读取一个字长(32bits)的数据存放于 dst
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


/* LOAD_WORD_R宏定
 * 自 FLASH_START 的偏移 base+off 处读取一个字长的数据存放于 dst
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
  /* 空转指令(NOP),使得CPU的PIPELINE不会错误的执行一个预取(PRE_FETCH)得指令,放一个NOP是最简单和安全的 */
  nop
  /* 条件分支跳转指令，如果确定跳转目标地址在分支指令前后的128KB范围内，编译器就可以编码只生成一条简单的条件分支指令 */
  b load_elf
  nop


load_elf:
  /* dirty hack 这里为什么要 -8？ */
  la $t0, (FLASH_START+FLASH_SIZE - 8)
  /* byte类型转换为int类型，希望保证低8比特数据一致，前24比特为0时要与上0xff */
  la $t1, 0x00FF
  /* 将源寄存器中的值存入指定的地址 MEM($t0+0)=$t1 */
  sw $t1, 0($t0)

  /* 校验kernel的elf文件的elf魔数，成功则载入kernel，失败则进入死循环 */
  #addr of elfheader, s0
  la $s0, FLASH_START
  /* e_magic -->魔数,位于elf文件头，用于检测elf是否有效 */
  LOAD_WORD_I($t1, 0)
  la $t0, ELF_MAGIC
  /* branch to 1 if  $t0 = $t1 此时代表elf是有效的 */
  beq $t0, $t1, 1f
  /* elf魔数校验失败则陷入死循环 */
  nop
  b bad
  nop


  /* 将kernel的elf文件的关键控制信息填入各寄存器，为载入kernel的elf文件并转移控制权做准备 */
1:  
  /* e_phoff (elf program header offset) --> elf头表的偏移 */
  LOAD_WORD_I($s1, 28)

  /* e_phnum (elf program header number) --> elf头表中的项数。 */
  LOAD_WORD_I($s2, 44)
  andi $s2, $s2, 0xFFFF

  /* e_entry (elf entry) --> elf程序的入口点虚拟地址。系统首先将控制权转移到该虚拟地址，从而启动进程。 */
  LOAD_WORD_I($s3, 24)


/* 以下部分为将elf文件读入虚拟地址空间 */
/* 每次子循环（copy_sec）读入一个section中的4字节，每次大循环（next_sec、3）读入完成一个section */
next_sec:   
  # s1, addr proghdr

  /* 以下三项为初始化相应寄存器的值 */
  # s4, p_va /* 当前写入的section的虚拟内存地址 */
  LOAD_WORD_R($s4, 8, $s1)

  # s5, p_filesz /* 剩余待写入的section大小 */
  LOAD_WORD_R($s5, 16, $s1)

  # s6, p_offset /* 当前已写入部分的偏移 */
  LOAD_WORD_R($s6, 4, $s1)

  
  beq  $s4, $zero, 3f
  nop
  /* 剩余待写入的section大小为0，跳转至3，读取下一section */
  beq  $s5, $zero, 3f
  nop

# copy from file_base+p_offset to p_va
/* copy_sec执行一次写入一个字长的elf文件数据至内存中 */
copy_sec:
  LOAD_WORD_R($t0, 0, $s6)
  sw $t0, 0($s4)
  /* 已读取的文件偏移+4 */
  addiu $s6, $s6, 4 
  /* 当前写入虚拟内存指针+4 */
  addiu $s4, $s4, 4 
  /* 剩余待写入的section大小-4 */
  addiu $s5, $s5, -4
  /* s5 > 0, 还有剩余的文件数据未读入完毕，循环读取 */
  bgtz  $s5, copy_sec
  nop

3:
  /* $s1 += 32 指向下一个表项 */
  addiu $s1, $s1, 32
  /* 待读取section数量-1 */
  addiu $s2, $s2, -1
  /* $s2 > 0 ，还有未读取的section，跳转值next_sec继续读取 */
  bgtz  $s2, next_sec
  nop
/* 以上部分为将elf文件读入虚拟地址空间 */

done:
  # jump to kernel
  /* 循环结束表示读取完毕，跳转到ucore kernel */
  jr $s3
  nop
  /* b . == b done (不太确定) */
  b .
  nop

bad:
  /* 出错后进入的死循环 */
  b bad 
  nop

