#ifndef __KERN_SYNC_SYNC_H__
#define __KERN_SYNC_SYNC_H__

#include <thumips.h>
#include <intr.h>
#include <mmu.h>
#include <asm/mipsregs.h>

#include <atomic.h>
#include <sched.h>


typedef volatile bool lock_t;


static inline void
lock_init(lock_t *lock) {
    *lock = 0;
}

// 尝试获取锁一次
static inline bool
try_lock(lock_t *lock) {
    return !test_and_set_bit(0, lock);
}

// 反复尝试获取锁，获取失败一次则进入调度
static inline void
lock(lock_t *lock) {
    while (!try_lock(lock)) {
        schedule();
    }
}

// 释放锁
static inline void
unlock(lock_t *lock) {
    if (!test_and_clear_bit(0, lock)) {
        panic("Unlock failed.\n");
    }
}

#endif /* !__KERN_SYNC_SYNC_H__ */

