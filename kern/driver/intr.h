#ifndef __KERN_DRIVER_INTR_H__
#define __KERN_DRIVER_INTR_H__

#include <thumips.h>
#include <asm/mipsregs.h>

void intr_enable(void);
void intr_disable(void);

//关闭本地中断，并将原来的中断标志保存在flags变量中
static inline bool
__intr_save(void) {
  //如果此时禁止中断，那么直接return 0
  if (!(read_c0_status() & ST0_IE)) {
    return 0;
  }
  // 否则将中断disable
  intr_disable();
  return 1;
}

//恢复保存在flags变量中的中断状态
static inline void
__intr_restore(bool flag) {
    if (flag) {
        intr_enable();
    }
}

#define local_intr_save(x)      do { x = __intr_save(); } while (0)
#define local_intr_restore(x)   __intr_restore(x);  

#endif /* !__KERN_DRIVER_INTR_H__ */

