// 定义了核心线程的入口指令

.text
.globl kernel_thread_entry
/* $a0 = arg, $a1 = func
 * see proc.c:kernel_thread
 */
kernel_thread_entry:        # void kernel_thread(void)
  addiu $sp, $sp, -16
  jal $a1
  nop
  move $a0, $v0
  la  $t0, do_exit
  jal $t0 
  nop
  /* never here */

