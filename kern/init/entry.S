#include <asm/mipsregs.h>

.global kernel_entry
.extern kern_init

/* bss段起始地址 */
.extern edata
.extern end
/* bss段结束地址 */

.set noreorder
.set noat

.section .startup

/* 核心栈大小8KB */
# define KSTACKSIZE (4096*2)

/* kernel elf 可执行文件的入口点 */
kernel_entry:
  b reset
  nop

	.align 4
reset:
  /* from u-boot */
	/* Clear watch registers */
	mtc0	$zero, CP0_WATCHLO
	mtc0	$zero, CP0_WATCHHI

	/* WP(Watch Pending), SW0/1 should be cleared */
	mtc0	$zero, CP0_CAUSE
  /* clear SR(ERL), which is 1 after reset */
	mtc0	$zero, CP0_STATUS

  jal 1f
  nop
  .word _gp
1:
  lw $gp, 0($ra)
  la $sp, bootstacktop
#setup ram exception
  la   $t0, __exception_vector
#TODO 
  mtc0 $t0, $15, 1 

  mfc0 $t0, CP0_STATUS
  li   $t1, ~ST0_BEV
  and  $t0, $t0, $t1
  mtc0 $t0, CP0_STATUS

/* 开始清零bss */
#zero bss
  la $t0, edata
  la $t1, end
  
2:
  sw $zero, 0($t0)
  addiu $t0, $t0, 4
  slt $t3, $t0, $t1
  bgtz $t3, 2b
  nop 
/* 清零bss 完成 */

  addiu $sp, $sp, -16
  la $t9, kern_init 
/* 由此跳转到 /kern/init.c -> kern_init() */
  jal $t9
  nop

# never here
  bgez $zero, .
  nop


# bfc00280: R4000 xtlbmiss vector
romExcHandle_280:
  b romExcHandle_280
  nop

# bfc00300: R4000 cache vector
romExcHandle_300:
  b romExcHandle_300
  nop


/* 初始化.data section，8KB bootstack + 32B bootstacktop */
.section .data
    .global bootstack
bootstack:
    .space KSTACKSIZE
    .global bootstacktop
bootstacktop:
    .space 32


