// 信号量的定义与方法声明

#ifndef __KERN_SYNC_SEM_H__
#define __KERN_SYNC_SEM_H__

#include <defs.h>
#include <atomic.h>
#include <wait.h>

// 信号量的定义
typedef struct {
    // 值
    int value;
    // 等待这个信号量的队列，其实现在 wait.h、wait.c 中
    wait_queue_t wait_queue;
} semaphore_t;

void sem_init(semaphore_t *sem, int value);
void up(semaphore_t *sem);
void down(semaphore_t *sem);
bool try_down(semaphore_t *sem);

#endif /* !__KERN_SYNC_SEM_H__ */

